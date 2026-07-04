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
    double      pnlPct      = 0.0;   // P&L en % (rendement de la POSITION, indépendant de la taille)
    int         holdDays    = 0;
    std::string exitReason;          // "stop-loss", "take-profit", "trailing-stop", "signal"
    bool        isWin       = false;
    // Fraction du capital réellement DÉPLOYÉE à l'entrée (item 8o.1, D45) :
    // (actions × prix d'entrée) / équité totale à l'entrée. Permet au
    // Monte-Carlo de refléter la TAILLE de position : le rendement porté au
    // portefeuille est deployedFraction × pnlPct, pas pnlPct (qui suppose
    // 100 % de déploiement). Défaut 1.0 = plein déploiement (rétro-compatible
    // avec les TradeRecords synthétiques des tests unitaires).
    double      deployedFraction = 1.0;
};

// ─── PaperBroker ──────────────────────────────────────────────────────────────
// Implémente IBroker pour le backtesting.
// Enregistre tous les trades pour générer le rapport de performance.
//
// Modèle d'exécution (Sprint 6.2, D22) :
//   - Référence : prix de clôture de la barre courante (le « mid » simulé)
//   - Slippage + demi-spread paramétrables en points de base, appliqués
//     DÉFAVORABLEMENT : achat au-dessus du close, vente en dessous —
//     comme un ordre au marché réel (IBKR). Défaut 0 = ancien comportement.
//   - Commission en %, en plus du fill dégradé.

class PaperBroker final : public IBroker {
public:
    explicit PaperBroker(double initialCapital = 10'000.0,
                         double commissionPct  = 0.0,
                         double slippageBps    = 0.0,
                         double halfSpreadBps  = 0.0)
        : initialCapital_(initialCapital)
        , cash_(initialCapital)
        , commissionPct_(commissionPct)
        , fillPenaltyPct_((slippageBps + halfSpreadBps) / 10'000.0)
    {}

    // ── Implémentation IBroker ────────────────────────────────────────────────

    std::optional<Order> submitBuy(const std::string& symbol, int qty) override {
        if (qty <= 0 || !currentPrice_) return std::nullopt;

        // Fill dégradé : un achat au marché paie slippage + demi-spread (D22),
        // appliqués sur le prix d'EXÉCUTION (open i+1 en backtest — B2)
        double price = fillPrice_.value_or(*currentPrice_) * (1.0 + fillPenaltyPct_);
        double cost  = price * qty * (1.0 + commissionPct_);

        if (cost > cash_) {
            // Ajuste la quantité au cash disponible
            qty = static_cast<int>(cash_ / (price * (1.0 + commissionPct_)));
            if (qty <= 0) return std::nullopt;
            cost = price * qty * (1.0 + commissionPct_);
        }

        // Équité totale à l'entrée : à plat (avant achat), équité = cash pur.
        // Sert à calculer la fraction déployée du trade (D45, sizing MC).
        entryEquity_ = cash_;
        cash_ -= cost;
        position_ = Position{symbol, qty, price, price * qty, 0.0};
        currentBuyDate_ = fillDate_.value_or(currentDate_);

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

        // Fill dégradé : une vente au marché concède slippage + demi-spread (D22),
        // sur le prix d'EXÉCUTION (open i+1 en backtest — B2)
        double price    = fillPrice_.value_or(*currentPrice_) * (1.0 - fillPenaltyPct_);
        double proceeds = price * qty * (1.0 - commissionPct_);
        cash_ += proceeds;

        // Enregistre le trade complété
        TradeRecord trade;
        trade.buyDate    = currentBuyDate_;
        trade.sellDate   = fillDate_.value_or(currentDate_);
        trade.buyPrice   = position_->avgPrice;
        trade.sellPrice  = price;
        trade.shares     = qty;
        // P&L net de la commission de vente (cohérent avec le débit du cash)
        trade.pnl        = (price - position_->avgPrice) * qty
                           - price * qty * commissionPct_;
        trade.pnlPct     = (price - position_->avgPrice) / position_->avgPrice * 100.0;
        // Fraction déployée (D45) : capital investi à l'entrée / équité d'entrée.
        // Garde entryEquity_ > 0 → sinon 1.0 (plein déploiement par défaut).
        trade.deployedFraction = entryEquity_ > 0.0
            ? (position_->avgPrice * position_->shares) / entryEquity_
            : 1.0;
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
    // Prix/date d'EXÉCUTION des ordres (B2 : open de la barre suivante en
    // backtest — la décision au close i ne peut pas se remplir au close i).
    // nullopt = repli sur currentPrice_/currentDate_ (valorisation et
    // clôture de fin de backtest). La valorisation (getPosition/getAccount/
    // portfolioValue) reste TOUJOURS au close courant.
    void setNextFillPrice(std::optional<double> p)      { fillPrice_ = p; }
    void setNextFillDate (std::optional<std::string> d) { fillDate_  = std::move(d); }
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
    double                   fillPenaltyPct_; // (slippage + demi-spread) en fraction
    std::optional<Position>  position_;
    std::optional<double>    currentPrice_;
    std::optional<double>    fillPrice_;      // prix d'exécution (B2), sinon close courant
    std::optional<std::string> fillDate_;     // date d'exécution (B2), sinon date courante
    std::string              currentDate_;
    std::string              currentBuyDate_;
    double                   entryEquity_ = 0.0;  // équité totale à l'entrée (D45, sizing MC)
    std::string              lastExitReason_ = "signal";
    int                      holdDays_ = 0;
    std::vector<TradeRecord> trades_;
    std::vector<double>      equityCurve_;
    std::vector<std::string> equityDates_;
};

} // namespace trading