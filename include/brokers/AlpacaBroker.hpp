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
#pragma once
#include "core/Interfaces.hpp"
#include "core/HttpClient.hpp"
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <sstream>

using json = nlohmann::json;

namespace trading {

class AlpacaBroker final : public IBroker {
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
            {"symbol",        symbol},
            {"qty",           std::to_string(qty)},
            {"side",          "buy"},
            {"type",          "market"},
            {"time_in_force", "day"},   // valide pour la journée
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
            {"symbol",        symbol},
            {"qty",           std::to_string(qty)},
            {"side",          "sell"},
            {"type",          "market"},
            {"time_in_force", "day"},
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
    std::optional<Position> getPosition(const std::string& symbol) override {
        try {
            auto resp = get("/v2/positions/" + symbol);
            auto j    = json::parse(resp);

            Position p;
            p.symbol        = j.value("symbol",        symbol);
            p.shares        = static_cast<int>(std::stod(j.value("qty", "0")));
            p.avgPrice      = std::stod(j.value("avg_entry_price", "0"));
            p.marketValue   = std::stod(j.value("market_value",    "0"));
            p.unrealizedPnl = std::stod(j.value("unrealized_pl",   "0"));
            return p.shares > 0 ? std::optional<Position>(p) : std::nullopt;
        } catch (...) {
            // 404 = pas de position ouverte pour ce symbole
            return std::nullopt;
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
        std::string response = http_.request(method, baseUrl_ + path, body, {
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
};

} // namespace trading
