#pragma once
#include "core/Interfaces.hpp"
#include "indicators/Indicators.hpp"
#include <memory>
#include <chrono>
#include <ctime>

namespace trading {

// ─── Configuration de la stratégie ────────────────────────────────────────────
    struct SwingConfig {
        std::string symbol        = "QQQ";
        int    emaFast            = 9;
        int    emaSlow            = 21;
        int    rsiPeriod          = 14;
        double rsiBuyMax          = 55.0;  // N'achète pas si RSI > 55
        double rsiSellMin         = 70.0;  // Vend si RSI > 70
        double stopLossPct        = 0.05;  // -5%
        double takeProfitPct      = 0.10;  // +10%
        double trailingStopPct    = 0.03;  // -3% depuis le pic
        double riskPerTradePct    = 0.02;  // risque 2% du capital par trade
        int    minHoldDays        = 3;
        // Item 8.4 : si false, ne PAS vendre sur RSI suracheté seul (rester dans
        // la tendance ; seul un croisement baissier sortira). Défaut true =
        // comportement historique.
        bool   sellOnRsiOverbought = true;

        // Conversion vers la config de risque du bot (item 12) : les composition
        // roots passent une SwingConfig à TradingBot::setConfig sans que le bot
        // ne dépende de la stratégie
        operator RiskConfig() const {
            RiskConfig r;
            r.symbol          = symbol;
            r.stopLossPct     = stopLossPct;
            r.takeProfitPct   = takeProfitPct;
            r.trailingStopPct = trailingStopPct;
            r.riskPerTradePct = riskPerTradePct;
            r.minHoldDays     = minHoldDays;
            return r;
        }
    };

// ─── SwingStrategy ────────────────────────────────────────────────────────────
// Implémente IStrategy — dépend uniquement des abstractions IIndicator
    class SwingStrategy final : public IStrategy {
    public:
        explicit SwingStrategy(
                SwingConfig                       config,
                std::unique_ptr<IIndicator<double>> emaFast,
                std::unique_ptr<IIndicator<double>> emaSlow,
                std::unique_ptr<IIndicator<double>> rsi
        )
                : config_(std::move(config))
                , emaFast_(std::move(emaFast))
                , emaSlow_(std::move(emaSlow))
                , rsi_(std::move(rsi))
        {}

        // Factory method pour créer une instance avec les paramètres par défaut
        static std::unique_ptr<SwingStrategy> create(SwingConfig cfg = {}) {
            return std::make_unique<SwingStrategy>(
                    cfg,
                    std::make_unique<EMA>(cfg.emaFast),
                    std::make_unique<EMA>(cfg.emaSlow),
                    std::make_unique<RSI>(cfg.rsiPeriod)
            );
        }

        Signal evaluate(const std::vector<Bar>& bars) const override {
            if (bars.size() < static_cast<size_t>(config_.emaSlow + 5))
                return makeSignal(SignalType::HOLD, bars, "Pas assez de données");

            // Extraction des prix de clôture
            std::vector<double> closes;
            closes.reserve(bars.size());
            for (const auto& b : bars)
                closes.push_back(b.close);

            // Calcul des indicateurs
            auto emaFastVals = emaFast_->compute(closes);
            auto emaSlowVals = emaSlow_->compute(closes);
            auto rsiVals     = rsi_->compute(closes);

            if (emaFastVals.empty() || emaSlowVals.empty() || rsiVals.empty())
                return makeSignal(SignalType::HOLD, bars, "Calcul indicateurs échoué");

            const size_t n = closes.size();
            const double lastClose   = closes[n-1];
            const double lastEmaFast = emaFastVals[n-1];
            const double lastEmaSlow = emaSlowVals[n-1];
            const double lastRsi     = rsiVals[n-1];

            // Détection du croisement
            // warmup=emaSlow : ignore les croisements pendant la convergence des EMA
            auto cross = CrossoverDetector::detect(emaFastVals, emaSlowVals,
                                                   static_cast<size_t>(config_.emaSlow));

            // ── Signal d'ACHAT ────────────────────────────────────────────────
            // Conditions: croisement haussier + RSI < seuil + prix > EMAs
            if (cross == CrossoverDetector::Cross::BULLISH
                && lastRsi     < config_.rsiBuyMax
                && lastClose   > lastEmaFast
                && lastClose   > lastEmaSlow)
            {
                return makeSignal(SignalType::BUY, bars,
                                  "EMA" + std::to_string(config_.emaFast) + " croise EMA" +
                                  std::to_string(config_.emaSlow) + " | RSI=" +
                                  std::to_string(static_cast<int>(lastRsi)));
            }

            // ── Signal de VENTE ───────────────────────────────────────────────
            // Conditions: croisement baissier OU RSI suracheté. Le terme RSI est
            // gaté par sellOnRsiOverbought (item 8.4) : en tendance haussière,
            // vendre sur RSI seul coupe les gagnants trop tôt — on laisse alors
            // le croisement baissier piloter la sortie.
            const bool rsiOverboughtExit = config_.sellOnRsiOverbought
                                           && lastRsi > config_.rsiSellMin;
            if (cross == CrossoverDetector::Cross::BEARISH || rsiOverboughtExit)
            {
                std::string reason = (cross == CrossoverDetector::Cross::BEARISH)
                                     ? "Croisement baissier EMA"
                                     : "RSI suracheté (" + std::to_string(static_cast<int>(lastRsi)) + ")";
                return makeSignal(SignalType::SELL, bars, reason);
            }

            return makeSignal(SignalType::HOLD, bars,
                              "RSI=" + std::to_string(static_cast<int>(lastRsi)) +
                              " | EMA" + std::to_string(config_.emaFast) + "=" +
                              std::to_string(static_cast<int>(lastEmaFast)));
        }

        std::string name() const override { return "SwingStrategy_EMA_RSI"; }
        const SwingConfig& config() const { return config_; }

    private:
        Signal makeSignal(SignalType type, const std::vector<Bar>& bars, const std::string& reason) const {
            Signal s;
            s.type      = type;
            s.symbol    = config_.symbol;
            s.price     = bars.empty() ? 0.0 : bars.back().close;
            s.reason    = reason;
            s.timestamp = bars.empty() ? "" : bars.back().date;
            return s;
        }

        SwingConfig                         config_;
        std::unique_ptr<IIndicator<double>> emaFast_;
        std::unique_ptr<IIndicator<double>> emaSlow_;
        std::unique_ptr<IIndicator<double>> rsi_;
    };

} // namespace trading