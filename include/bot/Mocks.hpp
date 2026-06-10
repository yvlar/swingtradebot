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
    // Simule une panne réseau du feed (item 10) : getBars/getLatestPrice
    // retournent Err au lieu d'une réponse
    void setFeedDown(std::string error) { feedError_ = std::move(error); }
    void setFeedUp()                    { feedError_.reset(); }

    Result<std::vector<Bar>> getBars(const std::string&, int) override {
        if (feedError_) return Result<std::vector<Bar>>::Err(*feedError_);
        return Result<std::vector<Bar>>::Ok(bars_);
    }

    Result<std::optional<double>> getLatestPrice(const std::string&) override {
        using R = Result<std::optional<double>>;
        if (feedError_) return R::Err(*feedError_);
        if (latestPrice_ > 0) return R::Ok(latestPrice_);
        return R::Ok(std::nullopt);
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
    std::vector<Bar>           bars_;
    double                     latestPrice_ = 0.0;
    bool                       marketOpen_  = true;
    std::optional<std::string> feedError_;

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
    // Simule une panne réseau du broker (item 10) : getPosition retourne Err
    void setPositionQueryFails(std::string error) { positionError_ = std::move(error); }
    void setPositionQueryOk()                     { positionError_.reset(); }

    // Résultat des prochains submit* :
    //   nullopt          → échec de soumission (panne réseau simulée)
    //   un OrderStatus   → ordre retourné avec ce statut
    // Seul FILLED modifie la position simulée (comme un vrai broker).
    void setSubmitResult(std::optional<OrderStatus> r) { submitResult_ = std::move(r); }
    void setFillPrice(double p)                        { fillPrice_ = p; }

    std::optional<Order> submitBuy(const std::string& symbol, int qty) override {
        orders_.push_back({symbol, OrderSide::BUY, qty});
        if (!submitResult_.has_value()) return std::nullopt;

        Order o;
        o.symbol = symbol;
        o.side   = OrderSide::BUY;
        o.status = *submitResult_;
        if (o.status == OrderStatus::FILLED) {
            o.quantity = qty;
            o.price    = fillPrice_;
            // Simule l'ouverture de position au prix de fill
            Position p;
            p.symbol        = symbol;
            p.shares        = qty;
            p.avgPrice      = fillPrice_;
            p.marketValue   = qty * fillPrice_;
            p.unrealizedPnl = 0.0;
            position_ = p;
        }
        return o;
    }

    std::optional<Order> submitSell(const std::string& symbol, int qty) override {
        orders_.push_back({symbol, OrderSide::SELL, qty});
        if (!submitResult_.has_value()) return std::nullopt;

        Order o;
        o.symbol = symbol;
        o.side   = OrderSide::SELL;
        o.status = *submitResult_;
        if (o.status == OrderStatus::FILLED) {
            o.quantity = qty;
            o.price    = fillPrice_;
            position_  = std::nullopt;  // ferme la position
        }
        return o;
    }

    Result<std::optional<Position>> getPosition(const std::string&) override {
        using R = Result<std::optional<Position>>;
        if (positionError_) return R::Err(*positionError_);
        return R::Ok(position_);
    }

    Account getAccount() override { return account_; }

    // Vérifications pour les tests
    int buyCount()  const { int n=0; for(auto& o: orders_) if(o.side==OrderSide::BUY)  n++; return n; }
    int sellCount() const { int n=0; for(auto& o: orders_) if(o.side==OrderSide::SELL) n++; return n; }
    const std::vector<OrderRecord>& orders() const { return orders_; }
    void clearOrders() { orders_.clear(); }

private:
    Account                     account_      = {10000.0, 10000.0, "ACTIVE"};
    std::optional<Position>     position_     = std::nullopt;
    std::vector<OrderRecord>    orders_;
    std::optional<OrderStatus>  submitResult_ = OrderStatus::FILLED;
    double                      fillPrice_    = 420.0;
    std::optional<std::string>  positionError_;
};

// ─── MockStateStore ───────────────────────────────────────────────────────────
// Persistance en mémoire — enregistre chaque save pour vérification
class MockStateStore final : public IStateStore {
public:
    void preload(BotState s)   { preloaded_ = std::move(s); }
    void setSaveFails(bool f)  { saveFails_ = f; }

    std::optional<BotState> load(const std::string&) override { return preloaded_; }

    bool save(const std::string&, const BotState& s) override {
        if (saveFails_) return false;
        saved_.push_back(s);
        return true;
    }

    const std::vector<BotState>& saved() const { return saved_; }
    std::optional<BotState> lastSaved() const {
        if (saved_.empty()) return std::nullopt;
        return saved_.back();
    }

private:
    std::optional<BotState> preloaded_;
    std::vector<BotState>   saved_;
    bool                    saveFails_ = false;
};

} // namespace trading::mocks
