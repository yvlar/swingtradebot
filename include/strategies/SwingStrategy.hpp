#pragma once
#include "core/Interfaces.hpp"
#include "indicators/Indicators.hpp"
#include "indicators/DayIndicators.hpp"
#include <memory>
#include <algorithm>
#include <chrono>
#include <ctime>

namespace trading {

// ─── Famille de stratégie (Sprint 10) ─────────────────────────────────────────
// Le moteur porte DEUX familles de signal, sélectionnées par `SwingConfig::mode`.
// TrendFollow = comportement historique (croisement EMA + RSI + régime, Sprints
// 1→8-octies). MeanReversion = famille CONTRARIAN (Sprint 10) : acheter la
// faiblesse (survente) et sortir au retour à la moyenne — la prémisse INVERSE du
// trend-following, dont aucun edge OOS n'a été démontré (les deux axes mono-actif
// et rotation sont soldés). Défaut = TrendFollow → les goldens restent
// byte-identiques (le mode MeanReversion n'est jamais atteint par la config prod).
    enum class StrategyMode { TrendFollow, MeanReversion };

// ─── Configuration de la stratégie ────────────────────────────────────────────
    struct SwingConfig {
        std::string symbol        = "QQQ";
        // Famille de signal (Sprint 10) : voir StrategyMode. Défaut TrendFollow =
        // comportement historique ; les champs mr* ci-dessous ne sont consultés
        // QUE si mode == MeanReversion (donc sans effet sur les goldens prod).
        StrategyMode mode         = StrategyMode::TrendFollow;
        int    emaFast            = 9;
        int    emaSlow            = 21;
        int    rsiPeriod          = 14;
        double rsiBuyMax          = 100.0; // ≥ 100 = plafond désactivé (item 8.3, D26 : entrer sur la force, pas la faiblesse)
        double rsiSellMin         = 70.0;  // Vend si RSI > 70
        double stopLossPct        = 0.05;  // -5%
        double takeProfitPct      = 0.0;   // ≤ 0 = désactivé (item 8.2, D26 : laisser courir les gagnants, sortie au trailing)
        double trailingStopPct    = 0.03;  // -3% depuis le pic
        // Item 8q.1 (ré-ouverture 8b.4) : > 0 = trailing en multiples d'ATR(14)
        // qui REMPLACE le trailing % (la « bonne » distance dépend de la
        // volatilité — trailingStopPct est l'axe le plus fragile depuis B2) ;
        // ≤ 0 = désactivé. Axe de recherche 8q.2 — pas de défaut actif tant
        // que l'amélioration OOS n'est pas démontrée.
        double trailingAtrMult    = 0.0;
        // Item 8s.1 (Sprint 8-quinquies) : > 0 = sortie STRUCTURELLE — en
        // position, clôture sous le plus bas des N barres précédentes (barre
        // courante exclue) → sortie (cassure d'un support récent). Gatée par
        // minHoldDays, priorité SL > TP > structure > trailing. ≤ 0 =
        // désactivé. Axe de recherche 8s.3 — pas de défaut actif tant que
        // l'amélioration OOS n'est pas démontrée.
        int    exitOnLowestLowN   = 0;
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
        // Item 8s.2 (Sprint 8-quinquies) : > 0 = entrée BREAKOUT — à plat,
        // régime up et clôture au-dessus du plus haut des M barres
        // précédentes (barre courante exclue) → BUY sans attendre un
        // croisement (hypothèse T3 prolongée : entrer sur la force
        // DÉMONTRÉE, pas le seul état « régime up + prix > EMAs »).
        // Évaluée APRÈS les ventes ; flag indépendant de regimeReentry.
        // ≤ 0 = désactivé. Axe de recherche 8s.3 — pas de défaut actif
        // tant que l'amélioration OOS n'est pas démontrée.
        int    entryBreakoutM          = 0;
        // Item 8y.1 (Sprint 8-sexies) : > 0 = entrée PULLBACK — à plat,
        // régime up et RSI ≤ seuil → BUY : acheter la FAIBLESSE dans la
        // tendance (le creux), l'inverse exact du breakout 8s.2. Évaluée
        // APRÈS les ventes ; flag indépendant de regimeReentry (masquage
        // partiel anticipé, D41 : la re-entrée couvre « prix > EMAs », le
        // pullback peut tirer SOUS les EMAs — les deux hypothèses
        // « remplace / s'ajoute » sont jugées en 8y.3). ≤ 0 = désactivé.
        // Axe de recherche 8y.3 — pas de défaut actif tant que
        // l'amélioration OOS n'est pas démontrée.
        double entryPullbackRsiMax     = 0.0;
        // Item 8y.2 (Sprint 8-sexies) : > 0 = filtre de VOLATILITÉ sur les
        // entrées — TOUTE entrée (croisement, breakout, pullback,
        // re-entrée) est bloquée si ATR(14)/clôture > seuil, strictement
        // (hypothèse : les whipsaws coûteux arrivent en haute volatilité ;
        // n'entrer que dans un marché calme). Les VENTES ne sont jamais
        // bloquées. ATR incalculable → filtre INOPÉRANT (fail-open,
        // décision utilisateur 2026-07-03). ≤ 0 = désactivé. Axe de
        // recherche 8y.3 — pas de défaut actif tant que l'amélioration
        // OOS n'est pas démontrée.
        double entryMaxAtrPct          = 0.0;
        // Item 8o.2 (Sprint 8-octies, D44) : > 0 = référence de volatilité
        // relative (ATR(14)/clôture) du SIZING MODULÉ par la volatilité — la
        // taille de position est réduite en régime plus volatil que ce seuil
        // (couper le risque de queue des achats de creux volatils sans les
        // supprimer). ≤ 0 = désactivé (sizing par le risque seul). Axe de
        // recherche 8o.3 — pas de défaut actif tant que l'amélioration OOS
        // n'est pas démontrée.
        double volSizingAtrRef         = 0.0;
        // ── Paramètres de la famille MEAN-REVERSION (Sprint 10, mode ─────────
        // MeanReversion uniquement) ──────────────────────────────────────────
        // Entrée contrarian : à plat, régime haussier confirmé (réutilise
        // smaTrendPeriod — « acheter le creux DANS une tendance ») et RSI ≤ ce
        // seuil (survente) → BUY. ≤ 0 = désactivé (aucune entrée MR). Défaut 30
        // = survente classique. Sans effet en mode TrendFollow.
        double mrRsiEntryMax      = 30.0;
        // Sortie mean-reversion : en position, RSI ≥ ce seuil (retour à la
        // moyenne) → SELL. Les sorties ne sont JAMAIS gatées par le régime (la
        // SMA ne gate que les achats — convention verrouillée) ; les stops et le
        // trailing du RiskManager restent la couche de sortie de sécurité. ≤ 0 =
        // désactivé (sortie laissée aux seuls stops). Défaut 55. Sans effet en
        // mode TrendFollow.
        double mrRsiExitMin       = 55.0;
        // ── Variante z-score / Bollinger de l'entrée MR (Sprint 11, item ──────
        // 11.1, décision utilisateur 10.4 = a) ────────────────────────────────
        // Le 1er jet MR (RSI ≤ 30 EN régime, Sprint 10) ne tradait quasiment pas
        // (un creux assez profond pour RSI ≤ 30 casse souvent le régime SMA200 →
        // 1 seul trade OOS, verdict = cash drag, D47). L'entrée z-score achète la
        // faiblesse en termes de PRIX (clôture sous la bande basse de Bollinger),
        // moins couplée au RSI → un échantillon de trades RÉEL pour juger la
        // FAMILLE. Active UNIQUEMENT en mode MeanReversion ET si mrBandPeriod > 0
        // ET mrBandEntryK > 0 ; sinon repli exact sur l'entrée RSI ci-dessus
        // (Sprint 10). Sans effet en mode TrendFollow.
        //
        // Période de la bande (SMA + écart-type glissant), ex. 20. ≤ 0 = variante
        // z-score désactivée.
        int    mrBandPeriod       = 0;
        // Entrée : BUY (en régime haussier confirmé) quand
        // z = (clôture − SMA) / σ ≤ −mrBandEntryK — clôture sous la bande basse
        // de k écarts-types. ≤ 0 = désactivé.
        double mrBandEntryK       = 0.0;
        // Sortie : SELL quand z ≥ mrBandExitZ (retour à/au-dessus de la moyenne).
        // Seuil SIGNÉ, défaut 0.0 = sortie exactement au retour à la moyenne.
        // Consulté seulement quand la variante z-score est active (sinon la
        // sortie reste RSI ≥ mrRsiExitMin + les stops du RiskManager).
        double mrBandExitZ        = 0.0;
        // Garde-fous de coupure (item 18, externalisés en C1/passe 3) : les
        // seuils du kill-switch font partie de la config VALIDÉE par le golden
        // (config/prod.json) — plus de dérive prod ≠ backtest sur le risque.
        KillSwitchConfig killSwitch;

        // Conversion vers la config de risque du bot (item 12) : les composition
        // roots passent une SwingConfig à TradingBot::setConfig sans que le bot
        // ne dépende de la stratégie
        operator RiskConfig() const {
            RiskConfig r;
            r.symbol          = symbol;
            r.stopLossPct     = stopLossPct;
            r.takeProfitPct   = takeProfitPct;
            r.trailingStopPct = trailingStopPct;
            r.trailingAtrMult = trailingAtrMult;
            r.exitOnLowestLowN = exitOnLowestLowN;
            r.riskPerTradePct = riskPerTradePct;
            r.volSizingAtrRef = volSizingAtrRef;
            r.minHoldDays     = minHoldDays;
            // La fenêtre de données doit couvrir la SMA de régime (item 8.1) :
            // sinon le bot ne reçoit pas assez de barres pour la calculer.
            r.lookback        = std::max(60, smaTrendPeriod + 30);
            r.killSwitch      = killSwitch;
            return r;
        }
    };

// ─── SwingStrategy ────────────────────────────────────────────────────────────
// Implémente IStrategy — dépend uniquement des abstractions IIndicator
    class SwingStrategy final : public IStrategy {
    public:
        // Le 5e indicateur (ATR, item 8y.2) a un défaut pour préserver tous
        // les sites d'appel historiques : nullptr → ATR(14) vrai true-range
        // (même période que le trailing ATR 8q.1).
        // Les 6e/7e indicateurs (bande z-score MR, item 11.1) ont un défaut
        // nullptr pour préserver tous les sites d'appel historiques ; ils ne
        // sont construits que si mrBandPeriod > 0 (variante z-score active).
        explicit SwingStrategy(
                SwingConfig                       config,
                std::unique_ptr<IIndicator<double>> emaFast,
                std::unique_ptr<IIndicator<double>> emaSlow,
                std::unique_ptr<IIndicator<double>> rsi,
                std::unique_ptr<IIndicator<double>> smaTrend,
                std::unique_ptr<IIndicator<double>> atr = nullptr,
                std::unique_ptr<IIndicator<double>> mrBandMa = nullptr,
                std::unique_ptr<IIndicator<double>> mrBandSd = nullptr
        )
                : config_(std::move(config))
                , emaFast_(std::move(emaFast))
                , emaSlow_(std::move(emaSlow))
                , rsi_(std::move(rsi))
                , smaTrend_(std::move(smaTrend))
                , atr_(std::move(atr))
                , mrBandMa_(std::move(mrBandMa))
                , mrBandSd_(std::move(mrBandSd))
        {
            if (!atr_) atr_ = std::make_unique<ATR>(14);
        }

        // Factory method pour créer une instance avec les paramètres par défaut
        static std::unique_ptr<SwingStrategy> create(SwingConfig cfg = {}) {
            // Bande z-score (item 11.1) : construite seulement si demandée, pour
            // ne rien changer aux configs qui ne l'utilisent pas.
            std::unique_ptr<IIndicator<double>> bandMa, bandSd;
            if (cfg.mrBandPeriod > 0) {
                bandMa = std::make_unique<SMA>(cfg.mrBandPeriod);
                bandSd = std::make_unique<RollingStdDev>(cfg.mrBandPeriod);
            }
            return std::make_unique<SwingStrategy>(
                    cfg,
                    std::make_unique<EMA>(cfg.emaFast),
                    std::make_unique<EMA>(cfg.emaSlow),
                    std::make_unique<RSI>(cfg.rsiPeriod),
                    std::make_unique<SMA>(std::max(1, cfg.smaTrendPeriod)),
                    nullptr,                 // atr → défaut ATR(14) dans le ctor
                    std::move(bandMa),
                    std::move(bandSd)
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

            // ── Famille MEAN-REVERSION (Sprint 10) ────────────────────────────
            // Prémisse INVERSE du trend-following : acheter la FAIBLESSE
            // (survente) et sortir au RETOUR À LA MOYENNE. Bloc entièrement
            // séparé et à retour immédiat : en mode MeanReversion on ne retombe
            // JAMAIS dans la logique de croisement EMA ci-dessous (le mode
            // TrendFollow, défaut, saute ce bloc → goldens byte-identiques).
            // Réutilise le RSI et le filtre de régime déjà calculés ; les stops
            // et le trailing (RiskManager) restent la couche de sortie de
            // sécurité, inchangés.
            if (config_.mode == StrategyMode::MeanReversion) {
                // ── Variante z-score / Bollinger (item 11.1, décision 10.4=a) ──
                // Active si mrBandPeriod > 0 ET mrBandEntryK > 0 : l'entrée
                // achète la faiblesse de PRIX (clôture sous la bande basse de
                // Bollinger), moins couplée au régime que « RSI ≤ 30 » — le 1er
                // jet MR (Sprint 10) ne tradait quasiment pas (D47). Bloc séparé
                // à retour immédiat. Bande incalculable (série trop courte ou σ
                // nul) → on RETOMBE sur l'entrée RSI ci-dessous (fail-safe,
                // jamais de division par zéro).
                if (config_.mrBandPeriod > 0 && config_.mrBandEntryK > 0.0
                    && mrBandMa_ && mrBandSd_) {
                    const auto maVals = mrBandMa_->compute(closes);
                    const auto sdVals = mrBandSd_->compute(closes);
                    if (!maVals.empty() && !sdVals.empty() && sdVals[n-1] > 0.0) {
                        const double z = (lastClose - maVals[n-1]) / sdVals[n-1];
                        // Sortie d'abord (jamais gatée par le régime) : retour à
                        // la moyenne (z ≥ seuil de sortie, défaut 0 = la moyenne).
                        if (z >= config_.mrBandExitZ)
                            return makeSignal(SignalType::SELL, bars,
                                              "Mean-reversion z-score : z=" +
                                              std::to_string(z) + " >= sortie " +
                                              std::to_string(config_.mrBandExitZ) +
                                              " (retour à la moyenne)");
                        // Entrée contrarian : régime haussier confirmé et clôture
                        // sous la bande basse de k écarts-types (z ≤ −k).
                        if (regimeUp && z <= -config_.mrBandEntryK)
                            return makeSignal(SignalType::BUY, bars,
                                              "Mean-reversion z-score : z=" +
                                              std::to_string(z) + " <= -" +
                                              std::to_string(config_.mrBandEntryK) +
                                              " | regime up (achat sous la bande basse)");
                        if (historiqueInsuffisant)
                            return makeSignal(SignalType::HOLD, bars,
                                              "Régime inconnu : historique insuffisant (" +
                                              std::to_string(n) + "/" +
                                              std::to_string(config_.smaTrendPeriod) +
                                              " barres) — aucune entrée possible");
                        return makeSignal(SignalType::HOLD, bars,
                                          "Mean-reversion z-score : z=" +
                                          std::to_string(z) + " neutre");
                    }
                    // bande incalculable → repli sur l'entrée RSI (fail-safe)
                }

                // Sortie d'abord (les sorties gardent la priorité et ne sont
                // jamais gatées par le régime) : RSI revenu au niveau de sortie
                // → retour à la moyenne. Mutuellement exclusif de l'entrée tant
                // que mrRsiExitMin > mrRsiEntryMax (RSI ne peut pas être à la
                // fois ≤ entrée et ≥ sortie).
                if (config_.mrRsiExitMin > 0.0 && lastRsi >= config_.mrRsiExitMin)
                    return makeSignal(SignalType::SELL, bars,
                                      "Mean-reversion : RSI " +
                                      std::to_string(static_cast<int>(lastRsi)) +
                                      " >= sortie " +
                                      std::to_string(static_cast<int>(config_.mrRsiExitMin)) +
                                      " (retour à la moyenne)");
                // Entrée contrarian : régime haussier confirmé et RSI en survente
                // → acheter le creux DANS la tendance (smaTrendPeriod ≤ 1 =
                // filtre désactivé, base de comparaison OOS du filtre).
                if (config_.mrRsiEntryMax > 0.0 && regimeUp
                    && lastRsi <= config_.mrRsiEntryMax)
                    return makeSignal(SignalType::BUY, bars,
                                      "Mean-reversion : RSI " +
                                      std::to_string(static_cast<int>(lastRsi)) +
                                      " <= entrée " +
                                      std::to_string(static_cast<int>(config_.mrRsiEntryMax)) +
                                      " | regime up (achat du creux)");
                if (historiqueInsuffisant)
                    return makeSignal(SignalType::HOLD, bars,
                                      "Régime inconnu : historique insuffisant (" +
                                      std::to_string(n) + "/" +
                                      std::to_string(config_.smaTrendPeriod) +
                                      " barres) — aucune entrée possible");
                return makeSignal(SignalType::HOLD, bars,
                                  "Mean-reversion : RSI " +
                                  std::to_string(static_cast<int>(lastRsi)) +
                                  " neutre");
            }

            // Détection du croisement
            // warmup=emaSlow : ignore les croisements pendant la convergence des EMA
            auto cross = CrossoverDetector::detect(emaFastVals, emaSlowVals,
                                                   static_cast<size_t>(config_.emaSlow));

            // Filtre de volatilité sur les entrées (item 8y.2) : calculé UNE
            // fois, consommé par les QUATRE familles d'entrée (croisement,
            // breakout, pullback, re-entrée). ATR(14) vrai true-range
            // (computeBars) rapporté à la clôture. ATR incalculable (série
            // vide ou nulle) → filtre INOPÉRANT (fail-open, décision
            // utilisateur 2026-07-03) : le comportement chaîne est préservé.
            double atrPct = 0.0;
            bool entreeBloqueeVol = false;
            if (config_.entryMaxAtrPct > 0.0 && lastClose > 0.0) {
                auto atrVals = atr_->computeBars(bars);
                if (!atrVals.empty() && atrVals[n-1] > 0.0) {
                    atrPct = atrVals[n-1] / lastClose;
                    entreeBloqueeVol = atrPct > config_.entryMaxAtrPct;
                }
            }

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
            if (!entreeBloqueeVol
                && regimeUp
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

            // ── Filtre de volatilité : blocage des entrées (item 8y.2) ──────
            // APRÈS le bloc VENTE (les sorties ne sont JAMAIS bloquées — on
            // peut toujours se mettre à l'abri) et AVANT toutes les entrées
            // restantes (breakout, pullback, re-entrée) ; le croisement
            // d'achat, évalué plus haut, porte sa propre garde. HOLD à
            // raison explicite : l'activation du filtre doit se VOIR (D41).
            if (entreeBloqueeVol)
                return makeSignal(SignalType::HOLD, bars,
                                  "Entrée bloquée : volatilité ATR " +
                                  std::to_string(atrPct * 100.0) +
                                  " % > seuil " +
                                  std::to_string(config_.entryMaxAtrPct * 100.0) +
                                  " %");

            // ── Entrée breakout « plus haut de M jours » (item 8s.2) ─────────
            // APRÈS le bloc VENTE (les sorties gardent la priorité) et AVANT
            // la re-entrée (raison plus spécifique quand les deux sont
            // qualifiantes). À plat, régime haussier confirmé et clôture
            // AU-DESSUS (strictement) du plus haut des M barres précédentes
            // — barre courante exclue — → BUY : la force est DÉMONTRÉE par la
            // cassure d'une résistance récente (hypothèse T3 prolongée), pas
            // seulement décrite par l'état « prix > EMAs ». Fenêtre < M+1
            // barres → pas de jugement de breakout.
            if (config_.entryBreakoutM > 0
                && regimeUp
                && n >= static_cast<size_t>(config_.entryBreakoutM) + 1)
            {
                double plusHaut = bars[n-2].high;
                for (size_t i = n - 1 - config_.entryBreakoutM; i + 1 < n; ++i)
                    plusHaut = std::max(plusHaut, bars[i].high);
                if (lastClose > plusHaut)
                    return makeSignal(SignalType::BUY, bars,
                                      "Breakout : clôture " +
                                      std::to_string(static_cast<int>(lastClose)) +
                                      " > plus haut " +
                                      std::to_string(config_.entryBreakoutM) +
                                      " jours | regime up");
            }

            // ── Entrée pullback en tendance (item 8y.1) ──────────────────────
            // APRÈS le bloc VENTE (les sorties gardent la priorité) et AVANT
            // la re-entrée (raison plus spécifique quand les deux sont
            // qualifiantes). À plat, régime haussier confirmé et RSI ≤ seuil
            // → BUY : acheter la FAIBLESSE dans la tendance (le creux),
            // l'inverse exact du breakout 8s.2. Aucune condition sur les EMA :
            // le creux passe typiquement SOUS elles — c'est précisément l'état
            // que la re-entrée 8.5 ne couvre pas (masquage partiel D41).
            if (config_.entryPullbackRsiMax > 0.0
                && regimeUp
                && lastRsi <= config_.entryPullbackRsiMax)
            {
                return makeSignal(SignalType::BUY, bars,
                                  "Pullback : RSI " +
                                  std::to_string(static_cast<int>(lastRsi)) +
                                  " <= " +
                                  std::to_string(static_cast<int>(config_.entryPullbackRsiMax)) +
                                  " | regime up");
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
        std::unique_ptr<IIndicator<double>> atr_;   // filtre de volatilité (item 8y.2)
        std::unique_ptr<IIndicator<double>> mrBandMa_; // SMA de la bande z-score MR (item 11.1)
        std::unique_ptr<IIndicator<double>> mrBandSd_; // écart-type de la bande z-score MR (item 11.1)
    };

} // namespace trading