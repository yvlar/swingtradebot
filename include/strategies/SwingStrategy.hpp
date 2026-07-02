#pragma once
#include "core/Interfaces.hpp"
#include "indicators/Indicators.hpp"
#include <memory>
#include <algorithm>
#include <chrono>
#include <ctime>

namespace trading {

// ─── Configuration de la stratégie ────────────────────────────────────────────
    struct SwingConfig {
        std::string symbol        = "QQQ";
        int    emaFast            = 9;
        int    emaSlow            = 21;
        int    rsiPeriod          = 14;
        double rsiBuyMax          = 100.0; // ≥ 100 = plafond désactivé (item 8.3, D26 : entrer sur la force, pas la faiblesse)
        double rsiSellMin         = 70.0;  // Vend si RSI > 70
        double stopLossPct        = 0.05;  // -5%
        double takeProfitPct      = 0.0;   // ≤ 0 = désactivé (item 8.2, D26 : laisser courir les gagnants, sortie au trailing)
        double trailingStopPct    = 0.03;  // -3% depuis le pic
        double riskPerTradePct    = 0.02;  // risque 2% du capital par trade
        int    minHoldDays        = 3;
        int    smaTrendPeriod     = 200;   // filtre de régime : n'entrer que si prix > SMA200 (item 8.1) ; ≤ 1 = filtre désactivé
        // Item 8.4 (D26/T2) : si vrai, RSI > rsiSellMin ne vend que si le
        // régime de fond n'est PAS haussier (RSI > 70 en tendance = force,
        // pas retournement). Flag INDÉPENDANT de smaTrendPeriod : la base A/B
        // 8.1 (smaTrendPeriod = 1 → régime toujours « up ») garderait sinon
        // ses ventes supprimées à son insu. Faux = comportement historique
        // (vente sur RSI seul quel que soit le régime).
        bool   rsiSellOnlyIfRegimeDown = true;
        // Item 8.5 (T4 — cash drag) : à plat, régime haussier confirmé et prix
        // au-dessus des deux EMA → ré-entrer SANS attendre un nouveau
        // croisement (il n'arrive qu'une fois par cycle de tendance ; sans
        // re-entrée, une sortie sur trailing laissait le bot en cash pendant
        // tout le reste de la tendance). Faux = comportement historique
        // (entrée uniquement sur croisement).
        bool   regimeReentry           = true;

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
            // La fenêtre de données doit couvrir la SMA de régime (item 8.1) :
            // sinon le bot ne reçoit pas assez de barres pour la calculer.
            r.lookback        = std::max(60, smaTrendPeriod + 30);
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
                std::unique_ptr<IIndicator<double>> rsi,
                std::unique_ptr<IIndicator<double>> smaTrend
        )
                : config_(std::move(config))
                , emaFast_(std::move(emaFast))
                , emaSlow_(std::move(emaSlow))
                , rsi_(std::move(rsi))
                , smaTrend_(std::move(smaTrend))
        {}

        // Factory method pour créer une instance avec les paramètres par défaut
        static std::unique_ptr<SwingStrategy> create(SwingConfig cfg = {}) {
            return std::make_unique<SwingStrategy>(
                    cfg,
                    std::make_unique<EMA>(cfg.emaFast),
                    std::make_unique<EMA>(cfg.emaSlow),
                    std::make_unique<RSI>(cfg.rsiPeriod),
                    std::make_unique<SMA>(std::max(1, cfg.smaTrendPeriod))
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
            // Filtre de régime de fond (item 8.1) : SMA longue (ex. SMA200).
            // Vide si l'historique est plus court que smaTrendPeriod → on ne peut
            // pas confirmer le régime, donc AUCUNE entrée (mais on peut toujours
            // sortir : la SMA ne gate que les achats).
            auto smaVals     = smaTrend_->compute(closes);

            if (emaFastVals.empty() || emaSlowVals.empty() || rsiVals.empty())
                return makeSignal(SignalType::HOLD, bars, "Calcul indicateurs échoué");

            const size_t n = closes.size();
            const double lastClose   = closes[n-1];
            const double lastEmaFast = emaFastVals[n-1];
            const double lastEmaSlow = emaSlowVals[n-1];
            const double lastRsi     = rsiVals[n-1];

            // Régime haussier confirmé : prix au-dessus de la SMA de fond.
            // smaTrendPeriod ≤ 1 = filtre désactivé (régime toujours « ouvert »),
            // utile comme base de comparaison out-of-sample du filtre (item 8.1).
            const bool regimeUp = config_.smaTrendPeriod <= 1
                                  || (!smaVals.empty() && lastClose > smaVals[n-1]);

            // Historique < smaTrendPeriod : régime INCONNU (pas « baissier »).
            // Aucune entrée ne partira jamais — le HOLD final doit le dire
            // explicitement (B3.2 : sinon le bot reste muet pour toujours).
            const bool historiqueInsuffisant = config_.smaTrendPeriod > 1
                && n < static_cast<size_t>(config_.smaTrendPeriod);

            // Détection du croisement
            // warmup=emaSlow : ignore les croisements pendant la convergence des EMA
            auto cross = CrossoverDetector::detect(emaFastVals, emaSlowVals,
                                                   static_cast<size_t>(config_.emaSlow));

            // ── Signal d'ACHAT ────────────────────────────────────────────────
            // Conditions: régime de fond haussier (prix > SMA200) + croisement
            // haussier + prix > EMAs. Le filtre de régime (8.1) coupe les
            // entrées à contre-tendance (whipsaws de range/marché baissier).
            // Plafond RSI d'achat — rsiBuyMax ≥ 100 = désactivé (item 8.3,
            // D26/T3) : exiger un croisement haussier (momentum ↑) ET un RSI
            // faible s'auto-annulait (7 entrées en 5 ans) et ratait les
            // breakouts forts. On entre sur la force ; convention ≥ 100 car
            // RSI == 100.0 est atteignable (série de gains purs).
            const bool rsiGateOff = config_.rsiBuyMax >= 100.0;
            if (regimeUp
                && cross == CrossoverDetector::Cross::BULLISH
                && (rsiGateOff || lastRsi < config_.rsiBuyMax)
                && lastClose   > lastEmaFast
                && lastClose   > lastEmaSlow)
            {
                return makeSignal(SignalType::BUY, bars,
                                  "EMA" + std::to_string(config_.emaFast) + " croise EMA" +
                                  std::to_string(config_.emaSlow) + " | RSI=" +
                                  std::to_string(static_cast<int>(lastRsi)) +
                                  " | regime>SMA" + std::to_string(config_.smaTrendPeriod));
            }

            // ── Signal de VENTE ───────────────────────────────────────────────
            // Conditions: croisement baissier OU RSI suracheté. Item 8.4
            // (D26/T2) : si rsiSellOnlyIfRegimeDown, la vente sur RSI SEUL est
            // supprimée en régime haussier confirmé (RSI > 70 en tendance =
            // force, pas retournement) — le croisement baissier vend toujours.
            // SMA incalculable → regimeUp faux → la vente RSI reste active.
            const bool venteRsi = lastRsi > config_.rsiSellMin
                && !(config_.rsiSellOnlyIfRegimeDown && regimeUp);
            if (cross == CrossoverDetector::Cross::BEARISH || venteRsi)
            {
                std::string reason = (cross == CrossoverDetector::Cross::BEARISH)
                                     ? "Croisement baissier EMA"
                                     : "RSI suracheté (" + std::to_string(static_cast<int>(lastRsi)) + ")";
                return makeSignal(SignalType::SELL, bars, reason);
            }

            // ── Re-entrée sur régime (item 8.5, T4) ──────────────────────────
            // APRÈS le bloc VENTE : les signaux de sortie gardent la priorité
            // (un croisement baissier ou une vente RSI active ne sont jamais
            // masqués par la re-entrée). À plat, régime haussier confirmé et
            // prix au-dessus des deux EMA → BUY sans attendre un nouveau
            // croisement. La stratégie est sans état : le BUY est ré-émis à
            // chaque barre qualifiante, TradingBot l'ignore s'il est déjà en
            // position.
            if (config_.regimeReentry
                && regimeUp
                && lastClose > lastEmaFast
                && lastClose > lastEmaSlow)
            {
                return makeSignal(SignalType::BUY, bars,
                                  "Re-entrée régime : prix > SMA" +
                                  std::to_string(config_.smaTrendPeriod) +
                                  " et > EMAs");
            }

            if (historiqueInsuffisant)
                return makeSignal(SignalType::HOLD, bars,
                                  "Régime inconnu : historique insuffisant (" +
                                  std::to_string(n) + "/" +
                                  std::to_string(config_.smaTrendPeriod) +
                                  " barres) — aucune entrée possible");

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
        std::unique_ptr<IIndicator<double>> smaTrend_;
    };

} // namespace trading