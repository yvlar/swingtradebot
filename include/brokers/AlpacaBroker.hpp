#pragma once
// ============================================================
//  AlpacaBroker.hpp  —  Broker Alpaca Paper Trading
//
//  Implémente IBroker avec l'API Alpaca Trading v2.
//  Même interface que PaperBroker — TradingBot ne change pas.
//
//  paper=true  → paper-api.alpaca.markets (100k$ fictifs)
//  paper=false → api.alpaca.markets       (ARGENT RÉEL)
//
//  Docs API : https://docs.alpaca.markets/reference/postorder
// ============================================================
#include "core/Interfaces.hpp"
#include "core/HttpClient.hpp"
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <sstream>
#include <ctime>

namespace trading {

// Alias local au namespace (pas de pollution du scope global — D9)
using json = nlohmann::json;

class AlpacaBroker : public IBroker {   // non-final : les tests substituent httpRequest()
public:
    AlpacaBroker(std::string apiKey,
                 std::string apiSecret,
                 bool        paper = true)
        : apiKey_   (std::move(apiKey))
        , apiSecret_(std::move(apiSecret))
        , baseUrl_  (paper
            ? "https://paper-api.alpaca.markets"
            : "https://api.alpaca.markets")
    {
        // L'init globale de libcurl est faite une seule fois au main
        // via CurlGlobal (core/curl_global.h) — jamais ici (item 7)
    }

    // ── IBroker ───────────────────────────────────────────────

    // Soumet un ordre d'achat au marché (market order)
    std::optional<Order> submitBuy(const std::string& symbol, int qty) override {
        if (qty <= 0) return std::nullopt;

        json body = {
            {"symbol",          symbol},
            {"qty",             std::to_string(qty)},
            {"side",            "buy"},
            {"type",            "market"},
            {"time_in_force",   "day"},   // valide pour la journée
            {"client_order_id", makeClientOrderId(symbol, "buy")},  // idempotence (D15)
        };

        try {
            auto resp = post("/v2/orders", body.dump());
            auto j    = json::parse(resp);
            return parseOrder(j);
        } catch (const std::exception& e) {
            lastError_ = e.what();
            return std::nullopt;
        }
    }

    // Soumet un ordre de vente au marché
    std::optional<Order> submitSell(const std::string& symbol, int qty) override {
        if (qty <= 0) return std::nullopt;

        json body = {
            {"symbol",          symbol},
            {"qty",             std::to_string(qty)},
            {"side",            "sell"},
            {"type",            "market"},
            {"time_in_force",   "day"},
            {"client_order_id", makeClientOrderId(symbol, "sell")},  // idempotence (D15)
        };

        try {
            auto resp = post("/v2/orders", body.dump());
            auto j    = json::parse(resp);
            return parseOrder(j);
        } catch (const std::exception& e) {
            lastError_ = e.what();
            return std::nullopt;
        }
    }

    // Récupère la position ouverte pour un symbole
    // Ok(nullopt) = Alpaca confirme l'absence de position (404) ;
    // Err = panne réseau/parsing : la position est INCONNUE (item 10)
    Result<std::optional<Position>> getPosition(const std::string& symbol) override {
        using R = Result<std::optional<Position>>;
        try {
            auto resp = get("/v2/positions/" + symbol);
            auto j    = json::parse(resp);

            Position p;
            p.symbol        = j.value("symbol",        symbol);
            p.shares        = static_cast<int>(std::stod(j.value("qty", "0")));
            p.avgPrice      = std::stod(j.value("avg_entry_price", "0"));
            p.marketValue   = std::stod(j.value("market_value",    "0"));
            p.unrealizedPnl = std::stod(j.value("unrealized_pl",   "0"));
            if (p.shares > 0) return R::Ok(p);
            return R::Ok(std::nullopt);
        } catch (const HttpError& e) {
            // 404 = réponse certaine : pas de position ouverte pour ce symbole
            if (e.status() == 404) return R::Ok(std::nullopt);
            lastError_ = e.what();
            return R::Err(std::string("Alpaca getPosition: ") + e.what());
        } catch (const std::exception& e) {
            lastError_ = e.what();
            return R::Err(std::string("Alpaca getPosition: ") + e.what());
        }
    }

    // Récupère le solde du compte paper
    Account getAccount() override {
        try {
            auto resp = get("/v2/account");
            auto j    = json::parse(resp);

            Account a;
            a.cash   = std::stod(j.value("cash",   "0"));
            a.equity = std::stod(j.value("equity", "0"));
            a.status = j.value("status", "INACTIVE");
            return a;
        } catch (...) {
            return {0.0, 0.0, "INACTIVE"};
        }
    }

    // ── Utilitaires ───────────────────────────────────────────

    // Annule tous les ordres en attente (utile au démarrage)
    void cancelAllOrders() {
        try { del("/v2/orders"); } catch (...) {}
    }

    // Clôture toutes les positions ouvertes
    void closeAllPositions() {
        try { del("/v2/positions"); } catch (...) {}
    }

    // Dernier message d'erreur
    const std::string& lastError() const { return lastError_; }

private:
    std::string apiKey_;
    std::string apiSecret_;
    std::string baseUrl_;
    std::string lastError_;
    HttpClient  http_;

    // ── Identifiant client idempotent (D15) ───────────────────
    // Le retry du HttpClient (item 8) peut re-poster un POST /v2/orders déjà
    // reçu par Alpaca. Un client_order_id stable par (symbole, side, heure UTC)
    // fait dédupliquer le doublon par Alpaca (422 sur cOID rejoué), comme le
    // cOID d'IBKR (item 4) — granularité alignée sur le cycle de 60 min du bot.
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

    // ── Parsing ───────────────────────────────────────────────
    static Order parseOrder(const json& j) {
        Order o;
        o.symbol    = j.value("symbol",  "");
        o.orderId   = j.value("id",      "");
        o.timestamp = j.value("created_at", "");

        std::string side   = j.value("side",   "buy");
        std::string status = j.value("status", "pending_new");

        o.side = (side == "buy") ? OrderSide::BUY : OrderSide::SELL;

        if      (status == "filled")    o.status = OrderStatus::FILLED;
        else if (status == "canceled")  o.status = OrderStatus::CANCELLED;
        else if (status == "rejected")  o.status = OrderStatus::REJECTED;
        else                            o.status = OrderStatus::PENDING;

        // filled_avg_price disponible après exécution
        std::string fp = j.value("filled_avg_price", "");
        o.price = fp.empty() ? 0.0 : std::stod(fp);

        std::string fq = j.value("filled_qty", "");
        o.quantity = fq.empty() ? 0 : static_cast<int>(std::stod(fq));

        return o;
    }

    // ── HTTP helpers ──────────────────────────────────────────
    std::string get(const std::string& path) {
        return request("GET", path, "");
    }

    std::string post(const std::string& path, const std::string& body) {
        return request("POST", path, body);
    }

    void del(const std::string& path) {
        request("DELETE", path, "");
    }

    // HTTP via le client commun : code HTTP vérifié, retry + backoff,
    // 429 géré (item 8)
    std::string request(const std::string& method,
                        const std::string& path,
                        const std::string& body) {
        std::string response = httpRequest(method, baseUrl_ + path, body, {
            "APCA-API-KEY-ID: "     + apiKey_,
            "APCA-API-SECRET-KEY: " + apiSecret_,
            "Content-Type: application/json",
        });

        // Erreur applicative Alpaca dans un corps 2xx
        auto j = json::parse(response, nullptr, false);
        if (!j.is_discarded() && j.contains("code") && j.contains("message"))
            throw std::runtime_error("Alpaca error "
                + std::to_string(j["code"].get<int>())
                + ": " + j["message"].get<std::string>());

        return response;
    }

protected:
    // HTTP bas niveau — virtuel pour la substitution dans les tests
    // unitaires (aucun réseau), même pattern qu'IBKRBroker
    virtual std::string httpRequest(const std::string& method,
                                    const std::string& url,
                                    const std::string& body,
                                    const std::vector<std::string>& headers) {
        return http_.request(method, url, body, headers);
    }
};

} // namespace trading
