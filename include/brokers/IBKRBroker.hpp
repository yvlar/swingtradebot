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

using json = nlohmann::json;

namespace trading {

class IBKRBroker final : public IBroker {
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
    std::optional<Order> submitOrder(const std::string& symbol,
                                      int qty, const std::string& side) {
        std::string conid = resolveConid(symbol);

        json orderBody = json::array();
        orderBody.push_back({
            {"conid",       std::stoi(conid)},
            {"secType",     conid + ":STK"},
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

            // IBKR peut retourner une liste d'ordres ou une demande de confirmation
            if (j.is_array() && !j.empty()) {
                // Vérifie si IBKR demande une confirmation supplémentaire
                if (j[0].contains("messageIds")) {
                    confirmOrder(j[0]["messageIds"]);
                    // Re-soumet après confirmation
                    resp = post(
                        "/v1/api/iserver/account/" + accountId_ + "/orders",
                        body.dump()
                    );
                    j = json::parse(resp);
                }

                if (j.is_array() && !j.empty()) {
                    return parseOrderResponse(j[0], symbol, qty, side);
                }
            }
        } catch (const std::exception& e) {
            lastError_ = e.what();
        }
        return std::nullopt;
    }

    // ── Confirmation automatique des ordres IBKR ─────────────
    // IBKR envoie parfois des messages de confirmation à acquitter
    void confirmOrder(const json& messageIds) {
        for (const auto& msgId : messageIds) {
            try {
                json confirmBody = {{"confirmed", true}};
                post("/v1/api/iserver/reply/" + msgId.get<std::string>(),
                     confirmBody.dump());
            } catch (...) {}
        }
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

    std::string request(const std::string& method,
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
