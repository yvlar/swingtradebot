#pragma once
#include "core/Interfaces.hpp"
#include "indicators/Indicators.hpp"
#include "indicators/DayIndicators.hpp"
#include <memory>
#include <sstream>

namespace trading {

// ─── DayTradeConfig ───────────────────────────────────────────────────────────
struct DayTradeConfig {
    std::string symbol          = "QQQ";

    // Indicateurs
    int    emaFast              = 8;      // EMA rapide (barres 5min)
    int    emaSlow              = 21;     // EMA lente  (barres 5min)
    int    rsiPeriod            = 9;      // RSI réactif pour intraday
    int    atrPeriod            = 14;     // ATR pour stop dynamique
    int    volumePeriod         = 20;     // Période moyenne de volume

    // Filtres d'entrée
    double rsiBuyMax            = 60.0;   // N'achète pas si RSI > 60
    double rsiSellMin           = 72.0;   // Vend si RSI > 72
    double volumeMultiplier     = 1.3;    // Volume doit être 1.3× la moyenne
    bool   requireVwapAbove     = true;   // Prix doit être au-dessus du VWAP

    // Gestion du risque (adapté intraday)
    double stopLossPct          = 0.008;  // Stop-loss  : -0.8%
    double takeProfitPct        = 0.020;  // Take-profit: +2.0%
    double trailingStopPct      = 0.005;  // Trailing   : -0.5% depuis le pic
    double atrStopMultiplier    = 1.5;    // Stop = prix - (ATR × 1.5)
    double riskPerTradePct      = 0.01;   // Risque max : 1% du capital par trade

    // Gestion du temps (day trading rules)
    int    minHoldBars          = 2;      // Garde au moins 2 barres de 5min
    int    maxHoldBars          = 48;     // Max 48 barres = 4h (ferme avant la clôture)
    bool   closeBeforeEndOfDay  = true;   // Ferme toutes positions avant 15h30
};

// ─── DayTradeStrategy ─────────────────────────────────────────────────────────
// Stratégie de day trading sur barres 5min.
//
// LOGIQUE D'ENTRÉE (BUY) — 4 conditions simultanées:
//   1. EMA rapide croise au-dessus de l'EMA lente (momentum)
//   2. Prix > VWAP (côté institutionnel)
//   3. RSI < rsiBuyMax (pas suracheté)
//   4. Volume > moyenne × multiplicateur (confirmation)
//
// LOGIQUE DE SORTIE (SELL) — première condition atteinte:
//   1. Stop-loss dynamique (ATR)
//   2. Take-profit
//   3. Trailing stop
//   4. Prix passe sous le VWAP
//   5. RSI suracheté
//   6. Croisement EMA baissier
//   7. Max barres atteint (gestion du temps)

class DayTradeStrategy final : public IStrategy {
public:
    explicit DayTradeStrategy(
        DayTradeConfig                      config,
        std::unique_ptr<IIndicator<double>> emaFast,
        std::unique_ptr<IIndicator<double>> emaSlow,
        std::unique_ptr<IIndicator<double>> rsi,
        std::unique_ptr<IIndicator<double>> atr,
        std::unique_ptr<IIndicator<double>> volOsc
    )
        : config_  (std::move(config))
        , emaFast_ (std::move(emaFast))
        , emaSlow_ (std::move(emaSlow))
        , rsi_     (std::move(rsi))
        , atr_     (std::move(atr))
        , volOsc_  (std::move(volOsc))
    {}

    // Factory method
    static std::unique_ptr<DayTradeStrategy> create(DayTradeConfig cfg = {}) {
        return std::make_unique<DayTradeStrategy>(
            cfg,
            std::make_unique<EMA>(cfg.emaFast),
            std::make_unique<EMA>(cfg.emaSlow),
            std::make_unique<RSI>(cfg.rsiPeriod),
            std::make_unique<ATR>(cfg.atrPeriod),
            std::make_unique<VolumeOscillator>(cfg.volumePeriod)
        );
    }

    Signal evaluate(const std::vector<Bar>& bars) const override {
        const int minBars = config_.emaSlow + config_.volumePeriod + 5;
        if (static_cast<int>(bars.size()) < minBars)
            return hold(bars, "Pas assez de barres");

        // Extraction des séries
        std::vector<double> closes, volumes;
        closes.reserve(bars.size());
        volumes.reserve(bars.size());
        for (const auto& b : bars) {
            closes.push_back(b.close);
            volumes.push_back(static_cast<double>(b.volume));
        }

        // Calcul des indicateurs
        auto emaFastV = emaFast_->compute(closes);
        auto emaSlowV = emaSlow_->compute(closes);
        auto rsiV     = rsi_->compute(closes);
        auto atrV     = atr_->computeBars(bars);   // vrai true-range (item 8.0)
        auto volRatio = volOsc_->compute(volumes);

        // VWAP sur la session courante
        VWAP vwapCalc;
        auto vwapV = vwapCalc.computeWithVolume(bars);

        if (emaFastV.empty() || emaSlowV.empty() || rsiV.empty())
            return hold(bars, "Calcul indicateurs echoue");

        const size_t n = closes.size();
        const double price    = closes[n-1];
        const double ef       = emaFastV[n-1];
        const double es       = emaSlowV[n-1];
        const double ef_prev  = emaFastV[n-2];
        const double es_prev  = emaSlowV[n-2];
        const double rsiLast  = rsiV[n-1];
        const double vwap     = vwapV[n-1];
        const double volR     = volRatio.size() > 1 ? volRatio[n-1] : 1.0;
        const double atrLast  = atrV.size() > 0 ? atrV[n-1] : 0.0;

        // ── Signal d'ACHAT ────────────────────────────────────────────────────
        bool crossUp     = ef > es && ef_prev <= es_prev;
        bool aboveVwap   = !config_.requireVwapAbove || (price > vwap);
        bool rsiOk       = rsiLast > 0 && rsiLast < config_.rsiBuyMax;
        bool volumeOk    = volR >= config_.volumeMultiplier;
        bool priceAboveEMAs = price > ef && price > es;

        if (crossUp && aboveVwap && rsiOk && volumeOk && priceAboveEMAs) {
            std::ostringstream reason;
            reason << "EMA" << config_.emaFast << "x" << config_.emaSlow
                   << " | VWAP=" << static_cast<int>(vwap)
                   << " | RSI=" << static_cast<int>(rsiLast)
                   << " | Vol=" << std::fixed << std::setprecision(1) << volR << "x";
            return buy(bars, reason.str());
        }

        // ── Signal de VENTE ───────────────────────────────────────────────────
        bool crossDown    = ef < es && ef_prev >= es_prev;
        bool belowVwap    = price < vwap;
        bool rsiOverbought = rsiLast > config_.rsiSellMin;

        if (crossDown || rsiOverbought || belowVwap) {
            std::string reason;
            if (crossDown)     reason = "Croisement EMA baissier";
            else if (rsiOverbought) reason = "RSI suracheté (" + std::to_string(static_cast<int>(rsiLast)) + ")";
            else               reason = "Prix sous VWAP (" + std::to_string(static_cast<int>(vwap)) + ")";
            return sell(bars, reason);
        }

        // Info de contexte pour le HOLD
        std::ostringstream info;
        info << "RSI=" << static_cast<int>(rsiLast)
             << " VWAP=" << static_cast<int>(vwap)
             << " Vol=" << std::fixed << std::setprecision(1) << volR << "x";
        return hold(bars, info.str());
    }

    std::string name() const override { return "DayTradeStrategy_VWAP_EMA_RSI"; }
    const DayTradeConfig& config() const { return config_; }

    // Stop dynamique basé sur ATR
    double computeATRStop(const std::vector<Bar>& bars) const {
        auto atrV = atr_->computeBars(bars);   // vrai true-range (item 8.0)
        if (atrV.empty()) return config_.stopLossPct;
        double atr   = atrV.back();
        double price = bars.empty() ? 0.0 : bars.back().close;
        return price > 0 ? (atr * config_.atrStopMultiplier) / price : config_.stopLossPct;
    }

private:
    DayTradeConfig                      config_;
    std::unique_ptr<IIndicator<double>> emaFast_;
    std::unique_ptr<IIndicator<double>> emaSlow_;
    std::unique_ptr<IIndicator<double>> rsi_;
    std::unique_ptr<IIndicator<double>> atr_;
    std::unique_ptr<IIndicator<double>> volOsc_;

    Signal buy (const std::vector<Bar>& b, const std::string& r) const {
        return {SignalType::BUY,  config_.symbol, b.empty() ? 0.0 : b.back().close, r, b.empty() ? "" : b.back().date};
    }
    Signal sell(const std::vector<Bar>& b, const std::string& r) const {
        return {SignalType::SELL, config_.symbol, b.empty() ? 0.0 : b.back().close, r, b.empty() ? "" : b.back().date};
    }
    Signal hold(const std::vector<Bar>& b, const std::string& r) const {
        return {SignalType::HOLD, config_.symbol, b.empty() ? 0.0 : b.back().close, r, b.empty() ? "" : b.back().date};
    }
};

} // namespace trading