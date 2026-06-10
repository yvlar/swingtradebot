#pragma once
#include "core/Interfaces.hpp"
#include <vector>
#include <numeric>
#include <algorithm>
#include <cmath>

namespace trading {

// ─── TradeRecord — historique d'un trade complété ────────────────────────────
struct TradeRecord {
    std::string buyDate;
    std::string sellDate;
    double      buyPrice    = 0.0;
    double      sellPrice   = 0.0;
    int         shares      = 0;
    double      pnl         = 0.0;   // P&L en dollars
    double      pnlPct      = 0.0;   // P&L en %
    int         holdDays    = 0;
    std::string exitReason;          // "stop-loss", "take-profit", "trailing-stop", "signal"
    bool        isWin       = false;
};

// ─── PaperBroker ──────────────────────────────────────────────────────────────
// Implémente IBroker pour le backtesting.
// Simule l'exécution des ordres et enregistre tous les trades pour le rapport.
//
// Modèle de coûts (item 6.2 / D22) :
//   - Exécution au prix de clôture de la barre courante, dégradé par le
//     slippage : achat à close×(1+slippagePct), vente à close×(1−slippagePct).
//     slippagePct regroupe slippage + demi-spread (coût par CÔTÉ, en fraction :
//     0.0005 = 5 bps). Défaut 0 pour préserver les goldens historiques.
//   - Commission proportionnelle par côté, appliquée sur le prix de fill.
//   - La valorisation (mark-to-market) reste au prix coté : seuls les FILLS
//     sont dégradés, comme en réalité.

class PaperBroker final : public IBroker {
public:
    explicit PaperBroker(double initialCapital = 10'000.0,
                         double commissionPct  = 0.0,
                         double slippagePct    = 0.0)
        : initialCapital_(initialCapital)
        , cash_(initialCapital)
        , commissionPct_(commissionPct)
        , slippagePct_(slippagePct)
    {}

    // ── Implémentation IBroker ────────────────────────────────────────────────

    std::optional<Order> submitBuy(const std::string& symbol, int qty) override {
        if (qty <= 0 || !currentPrice_) return std::nullopt;

        // Fill dégradé par le slippage : on paie PLUS que le prix coté
        double price = *currentPrice_ * (1.0 + slippagePct_);
        double cost  = price * qty * (1.0 + commissionPct_);

        if (cost > cash_) {
            // Ajuste la quantité au cash disponible
            qty = static_cast<int>(cash_ / (price * (1.0 + commissionPct_)));
            if (qty <= 0) return std::nullopt;
            cost = price * qty * (1.0 + commissionPct_);
        }

        cash_ -= cost;
        position_ = Position{symbol, qty, price, price * qty, 0.0};
        currentBuyDate_ = currentDate_;

        Order o;
        o.symbol   = symbol;
        o.side     = OrderSide::BUY;
        o.quantity = qty;
        o.price    = price;
        o.status   = OrderStatus::FILLED;
        return o;
    }

    std::optional<Order> submitSell(const std::string& symbol, int qty) override {
        if (!position_.has_value() || qty <= 0 || !currentPrice_) return std::nullopt;

        // Fill dégradé par le slippage : on reçoit MOINS que le prix coté
        double price    = *currentPrice_ * (1.0 - slippagePct_);
        double proceeds = price * qty * (1.0 - commissionPct_);
        cash_ += proceeds;

        // Enregistre le trade complété
        TradeRecord trade;
        trade.buyDate    = currentBuyDate_;
        trade.sellDate   = currentDate_;
        trade.buyPrice   = position_->avgPrice;
        trade.sellPrice  = price;
        trade.shares     = qty;
        // P&L net de la commission de vente (cohérent avec le débit du cash)
        trade.pnl        = (price - position_->avgPrice) * qty
                           - price * qty * commissionPct_;
        trade.pnlPct     = (price - position_->avgPrice) / position_->avgPrice * 100.0;
        trade.holdDays   = holdDays_;
        trade.exitReason = lastExitReason_;
        trade.isWin      = trade.pnl > 0;
        trades_.push_back(trade);

        position_ = std::nullopt;
        holdDays_ = 0;

        Order o;
        o.symbol   = symbol;
        o.side     = OrderSide::SELL;
        o.quantity = qty;
        o.price    = price;
        o.status   = OrderStatus::FILLED;
        return o;
    }

    // Broker simulé : jamais de panne → toujours Ok (item 10)
    Result<std::optional<Position>> getPosition(const std::string& /*symbol*/) override {
        using R = Result<std::optional<Position>>;
        if (!position_.has_value() || !currentPrice_) return R::Ok(std::nullopt);
        position_->marketValue   = position_->shares * *currentPrice_;
        position_->unrealizedPnl = ((*currentPrice_) - position_->avgPrice) * position_->shares;
        return R::Ok(position_);
    }

    Account getAccount() override {
        double posValue = 0.0;
        if (position_.has_value() && currentPrice_)
            posValue = position_->shares * *currentPrice_;
        return {cash_, cash_ + posValue, "ACTIVE"};
    }

    // ── API spécifique au Backtester ──────────────────────────────────────────

    // À appeler par le Backtester avant chaque runOnce()
    void setCurrentPrice(double price) { currentPrice_ = price; }
    void setCurrentDate (const std::string& date) { currentDate_ = date; }
    void setLastExitReason(const std::string& r)  { lastExitReason_ = r; }
    void incrementHoldDays() { if (position_.has_value()) holdDays_++; }

    // Valeur totale du portefeuille à l'instant courant
    double portfolioValue() const {
        double posValue = 0.0;
        if (position_.has_value() && currentPrice_)
            posValue = position_->shares * *currentPrice_;
        return cash_ + posValue;
    }

    // ── Accesseurs pour le rapport ────────────────────────────────────────────
    const std::vector<TradeRecord>& trades()  const { return trades_; }
    double initialCapital()                   const { return initialCapital_; }
    double cash()                             const { return cash_; }
    bool   inPosition()                       const { return position_.has_value(); }

    // Enregistre un point de la courbe d'équité
    void equitySnapshot(double value, const std::string& date) {
        equityCurve_.push_back(value);
        equityDates_.push_back(date);
    }
    const std::vector<double>&      equityCurve() const { return equityCurve_; }
    const std::vector<std::string>& equityDates() const { return equityDates_; }

    // Clôture une position ouverte en fin de backtest
    void closeOpenPosition() {
        if (position_.has_value() && currentPrice_) {
            lastExitReason_ = "fin-backtest";
            submitSell(position_->symbol, position_->shares);
        }
    }

private:
    double                   initialCapital_;
    double                   cash_;
    double                   commissionPct_;
    double                   slippagePct_;
    std::optional<Position>  position_;
    std::optional<double>    currentPrice_;
    std::string              currentDate_;
    std::string              currentBuyDate_;
    std::string              lastExitReason_ = "signal";
    int                      holdDays_ = 0;
    std::vector<TradeRecord> trades_;
    std::vector<double>      equityCurve_;
    std::vector<std::string> equityDates_;
};

} // namespace trading