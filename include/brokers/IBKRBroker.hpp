#pragma once
// ============================================================
//  IBKRBroker.hpp  —  Broker IBKR via CP Gateway
//
//  Envoie les ordres au CP Gateway local (localhost:5000)
//  Compatible paper trading ET live trading.
//
//  IMPORTANT : Les ordres IBKR nécessitent une confirmation
//  pour les nouveaux utilisateurs → on gère ça automatiquement.
// ============================================================
#include "core/Interfaces.hpp"
#include "brokers/IBKRDataFeed.hpp"  // pour KNOWN_CONIDS
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <sstream>
#include <ctime>

using json = nlohmann::json;

namespace trading {

class IBKRBroker : public IBroker {   // non-final : les tests substituent request()
public:
    explicit IBKRBroker(std::string accountId,
                        std::string gatewayUrl = "https://localhost:5000")
        : accountId_ (std::move(accountId))
        , gatewayUrl_(std::move(gatewayUrl))
    {}

    // ── IBroker ───────────────────────────────────────────────

    std::optional<Order> submitBuy(const std::string& symbol, int qty) override {
        if (qty <= 0) return std::nullopt;
        return submitOrder(symbol, qty, "BUY");
    }

    std::optional<Order> submitSell(const std::string& symbol, int qty) override {
        if (qty <= 0) return std::nullopt;
        return submitOrder(symbol, qty, "SELL");
    }

    std::optional<Position> getPosition(const std::string& symbol) override {
        std::string conid = resolveConid(symbol);
        std::string url   = gatewayUrl_
            + "/v1/api/portfolio/" + accountId_
            + "/position/" + conid;
        try {
            auto resp = get(url);
            auto j    = json::parse(resp);
            if (!j.is_array() || j.empty()) return std::nullopt;

            const auto& p = j[0];
            int shares = static_cast<int>(p.value("position", 0.0));
            if (shares <= 0) return std::nullopt;

            Position pos;
            pos.symbol        = symbol;
            pos.shares        = shares;
            pos.avgPrice      = p.value("avgCost",       0.0);
            pos.marketValue   = p.value("mktValue",      0.0);
            pos.unrealizedPnl = p.value("unrealizedPnl", 0.0);
            return pos;
        } catch (...) {
            return std::nullopt;
        }
    }

    Account getAccount() override {
        try {
            auto resp = get(gatewayUrl_ + "/v1/api/portfolio/" + accountId_ + "/summary");
            auto j    = json::parse(resp);

            Account a;
            // IBKR retourne les valeurs dans des objets imbriqués
            if (j.contains("availablefunds"))
                a.cash = j["availablefunds"].value("amount", 0.0);
            if (j.contains("netliquidation"))
                a.equity = j["netliquidation"].value("amount", 0.0);
            a.status = "ACTIVE";
            return a;
        } catch (...) {
            return {0.0, 0.0, "INACTIVE"};
        }
    }

    // ── Utilitaires ───────────────────────────────────────────

    // Récupère le premier accountId disponible (utile si tu ne connais pas ton ID)
    static std::string fetchFirstAccountId(
            const std::string& gatewayUrl = "https://localhost:5000") {
        CURL* curl = curl_easy_init();
        if (!curl) throw std::runtime_error("curl_easy_init failed");

        std::string response;
        curl_easy_setopt(curl, CURLOPT_URL,
            (gatewayUrl + "/v1/api/portfolio/accounts").c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA,      &response);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_perform(curl);
        curl_easy_cleanup(curl);

        auto j = json::parse(response);
        if (j.is_array() && !j.empty())
            return j[0].value("accountId", "");
        throw std::runtime_error("Aucun compte trouvé dans le CP Gateway");
    }

private:
    std::string accountId_;
    std::string gatewayUrl_;

    // ── Soumission d'ordre ────────────────────────────────────
    // Flux de confirmation IBKR : quand le Gateway pose une question
    // (suppression de message), la réponse de /iserver/reply CONTIENT le
    // résultat de l'ordre initial. Il ne faut JAMAIS re-poster l'ordre
    // (l'ancien code le faisait → risque de double exécution réelle).
    static constexpr int kMaxConfirmRounds = 5;

    std::optional<Order> submitOrder(const std::string& symbol,
                                      int qty, const std::string& side) {
        std::string conid = resolveConid(symbol);

        json orderBody = json::array();
        orderBody.push_back({
            {"conid",       std::stoi(conid)},
            {"secType",     conid + ":STK"},
            {"cOID",        makeClientOrderId(symbol, side)},  // idempotence
            {"orderType",   "MKT"},           // market order
            {"side",        side},
            {"quantity",    qty},
            {"tif",         "DAY"},
            {"listingExchange", "SMART"},      // routage intelligent IBKR
        });

        json body = {{"orders", orderBody}};

        try {
            auto resp = post(
                "/v1/api/iserver/account/" + accountId_ + "/orders",
                body.dump()
            );
            auto j = json::parse(resp);

            // Consomme les éventuelles questions de confirmation en chaîne :
            // chaque acquittement renvoie soit une nouvelle question, soit
            // le résultat final de l'ordre.
            for (int round = 0; round < kMaxConfirmRounds; ++round) {
                if (!j.is_array() || j.empty()) break;
                std::string replyId = extractReplyId(j[0]);
                if (replyId.empty()) break;   // plus de question → résultat final

                json confirmBody = {{"confirmed", true}};
                auto replyResp = post("/v1/api/iserver/reply/" + replyId,
                                      confirmBody.dump());
                j = json::parse(replyResp);
            }

            if (j.is_array() && !j.empty()
                && extractReplyId(j[0]).empty()      // toutes les questions résolues
                && !j[0].contains("error")) {
                return parseOrderResponse(j[0], symbol, qty, side);
            }
            lastError_ = "Confirmations IBKR non résolues ou réponse en erreur";
        } catch (const std::exception& e) {
            lastError_ = e.what();
        }
        return std::nullopt;
    }

    // ── Extraction de l'identifiant de confirmation ───────────
    // Le Gateway signale une question via {"id", "message":[…]} (format
    // courant) ou {"messageIds":[…]} (format historique). "" si aucune.
    static std::string extractReplyId(const json& j0) {
        if (j0.contains("messageIds") && j0["messageIds"].is_array()
            && !j0["messageIds"].empty())
            return j0["messageIds"][0].get<std::string>();
        if (j0.contains("id") && j0.contains("message"))
            return j0.value("id", "");
        return "";
    }

    // ── Identifiant client idempotent ─────────────────────────
    // Un seul ordre par (symbole, side, heure UTC) : IBKR rejette les cOID
    // dupliqués, ce qui bloque les doublons en cas de retry — granularité
    // alignée sur le cycle de 60 min du bot.
    static std::string makeClientOrderId(const std::string& symbol,
                                         const std::string& side) {
        std::time_t t = std::time(nullptr);
        std::tm tm{};
#ifdef _WIN32
        gmtime_s(&tm, &t);
#else
        gmtime_r(&t, &tm);
#endif
        char buf[16];
        std::strftime(buf, sizeof(buf), "%Y%m%d%H", &tm);
        return "swingbot-" + symbol + "-" + side + "-" + buf;
    }

    // ── Parsing de la réponse d'ordre ─────────────────────────
    static Order parseOrderResponse(const json& j, const std::string& symbol,
                                     int qty, const std::string& side) {
        Order o;
        o.symbol    = symbol;
        o.orderId   = j.value("order_id", "");
        o.quantity  = qty;
        o.side      = (side == "BUY") ? OrderSide::BUY : OrderSide::SELL;
        o.timestamp = j.value("order_time", "");

        std::string status = j.value("order_status", "");
        if      (status == "Filled")    o.status = OrderStatus::FILLED;
        else if (status == "Cancelled") o.status = OrderStatus::CANCELLED;
        else if (status == "Rejected")  o.status = OrderStatus::REJECTED;
        else                            o.status = OrderStatus::PENDING;

        o.price = j.value("avg_price", 0.0);
        return o;
    }

    // ── Résolution de conid ───────────────────────────────────
    static std::string resolveConid(const std::string& symbol) {
        auto it = KNOWN_CONIDS.find(symbol);
        if (it != KNOWN_CONIDS.end()) return it->second;
        throw std::runtime_error("Conid inconnu pour " + symbol
            + " — ajoute-le dans KNOWN_CONIDS dans IBKRDataFeed.hpp");
    }

    // ── HTTP helpers ──────────────────────────────────────────
    std::string lastError_;

    std::string get(const std::string& url) {
        return request("GET", url, "");
    }

    std::string post(const std::string& path, const std::string& body) {
        return request("POST", gatewayUrl_ + path, body);
    }

protected:
    // HTTP bas niveau — virtuel pour permettre la substitution dans les
    // tests unitaires (aucun accès réseau)
    virtual std::string request(const std::string& method,
                                const std::string& url,
                                const std::string& body) {
        CURL* curl = curl_easy_init();
        if (!curl) throw std::runtime_error("curl_easy_init failed");

        std::string response;
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");

        curl_easy_setopt(curl, CURLOPT_URL,            url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER,     headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA,      &response);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT,        15L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L); // cert auto-signé
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

        if (method == "POST") {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        }

        CURLcode res = curl_easy_perform(curl);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK)
            throw std::runtime_error(std::string("HTTP ") + method
                                     + ": " + curl_easy_strerror(res));
        return response;
    }

    static size_t writeCallback(void* data, size_t size,
                                 size_t nmemb, std::string* out) {
        out->append(static_cast<char*>(data), size * nmemb);
        return size * nmemb;
    }
};

} // namespace trading
