#pragma once
#include "core/Interfaces.hpp"
#include <vector>
#include <optional>
#include <functional>
#include <stdexcept>

namespace trading::mocks {

// ─── MockDataFeed ─────────────────────────────────────────────────────────────
// Simule une source de données pour les tests — pas de réseau requis
class MockDataFeed final : public IDataFeed {
public:
    // Configure les barres à retourner
    void setBars(std::vector<Bar> bars) { bars_ = std::move(bars); }
    void setLatestPrice(double p)       { latestPrice_ = p; }
    void setMarketOpen(bool open)       { marketOpen_ = open; }

    std::vector<Bar> getBars(const std::string&, int) override {
        return bars_;
    }

    std::optional<double> getLatestPrice(const std::string&) override {
        if (latestPrice_ > 0) return latestPrice_;
        return std::nullopt;
    }

    bool isMarketOpen() override { return marketOpen_; }

    // Helpers pour construire des séries de prix réalistes
    static std::vector<Bar> buildTrend(double startPrice, int days, double dailyChange) {
        std::vector<Bar> bars;
        double price = startPrice;
        for (int i = 0; i < days; ++i) {
            price += dailyChange;
            Bar b;
            b.date   = std::string("2024-01-") + (i < 9 ? "0" : "") + std::to_string(i + 1);
            b.open   = price - 0.5;
            b.high   = price + 1.0;
            b.low    = price - 1.0;
            b.close  = price;
            b.volume = 1'000'000;
            bars.push_back(b);
        }
        return bars;
    }

    // Crée un croisement EMA haussier sur les dernières barres
    static std::vector<Bar> buildBullishCrossover(int totalBars = 30) {
        std::vector<Bar> bars;
        // Phase baissière (EMA lente > EMA rapide)
        double price = 400.0;
        for (int i = 0; i < totalBars - 5; ++i) {
            price -= 0.3;
            bars.push_back(makeBar("2024-01-" + std::to_string(i+1), price));
        }
        // Phase haussière (croisement)
        for (int i = 0; i < 5; ++i) {
            price += 3.0;
            bars.push_back(makeBar("2024-02-" + std::to_string(i+1), price));
        }
        return bars;
    }

private:
    std::vector<Bar>      bars_;
    double                latestPrice_ = 0.0;
    bool                  marketOpen_  = true;

    static Bar makeBar(const std::string& date, double price) {
        Bar b;
        b.date   = date;
        b.open   = price - 0.5;
        b.high   = price + 1.0;
        b.low    = price - 1.0;
        b.close  = price;
        b.volume = 1'000'000;
        return b;
    }
};

// ─── MockStrategy ─────────────────────────────────────────────────────────────
// Force un signal donné — permet de tester TradingBot sans dépendre des indicateurs
class MockStrategy final : public IStrategy {
public:
    void setSignal(SignalType type, std::string reason = "mock") {
        type_   = type;
        reason_ = std::move(reason);
    }

    Signal evaluate(const std::vector<Bar>& bars) const override {
        Signal s;
        s.type      = type_;
        s.symbol    = "QQQ";
        s.price     = bars.empty() ? 0.0 : bars.back().close;
        s.reason    = reason_;
        s.timestamp = bars.empty() ? "" : bars.back().date;
        return s;
    }

    std::string name() const override { return "MockStrategy"; }

private:
    SignalType  type_   = SignalType::HOLD;
    std::string reason_ = "mock";
};

// ─── MockBroker ───────────────────────────────────────────────────────────────
// Simule l'exécution des ordres — enregistre tout pour vérification dans les tests
class MockBroker final : public IBroker {
public:
    struct OrderRecord {
        std::string symbol;
        OrderSide   side;
        int         qty;
    };

    void setAccount(Account acct)              { account_  = std::move(acct); }
    void setPosition(std::optional<Position> p){ position_ = std::move(p); }
    void setRejectOrders(bool reject)          { rejectOrders_ = reject; }

    std::optional<Order> submitBuy(const std::string& symbol, int qty) override {
        if (rejectOrders_) return std::nullopt;
        orders_.push_back({symbol, OrderSide::BUY, qty});
        // Simule l'ouverture de position
        Position p;
        p.symbol       = symbol;
        p.shares       = qty;
        p.avgPrice     = 420.0;
        p.marketValue  = qty * 420.0;
        p.unrealizedPnl = 0.0;
        position_ = p;

        Order o;
        o.symbol   = symbol;
        o.side     = OrderSide::BUY;
        o.quantity = qty;
        o.status   = OrderStatus::FILLED;
        return o;
    }

    std::optional<Order> submitSell(const std::string& symbol, int qty) override {
        if (rejectOrders_) return std::nullopt;
        orders_.push_back({symbol, OrderSide::SELL, qty});
        position_ = std::nullopt;  // ferme la position

        Order o;
        o.symbol   = symbol;
        o.side     = OrderSide::SELL;
        o.quantity = qty;
        o.status   = OrderStatus::FILLED;
        return o;
    }

    std::optional<Position> getPosition(const std::string&) override {
        return position_;
    }

    Account getAccount() override { return account_; }

    // Vérifications pour les tests
    int buyCount()  const { int n=0; for(auto& o: orders_) if(o.side==OrderSide::BUY)  n++; return n; }
    int sellCount() const { int n=0; for(auto& o: orders_) if(o.side==OrderSide::SELL) n++; return n; }
    const std::vector<OrderRecord>& orders() const { return orders_; }
    void clearOrders() { orders_.clear(); }

private:
    Account                  account_      = {10000.0, 10000.0, "ACTIVE"};
    std::optional<Position>  position_     = std::nullopt;
    std::vector<OrderRecord> orders_;
    bool                     rejectOrders_ = false;
};

} // namespace trading::mocks
