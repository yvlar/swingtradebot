#pragma once
#include "backtest/BackTester.hpp"
#include "backtest/ExecutionCostModel.hpp"
#include "backtest/ExecutionStats.hpp"
#include "backtest/FractionalSizingAnalysis.hpp"
#include "backtest/NetBuyHold.hpp"
#include <algorithm>
#include <memory>
#include <string>

namespace trading {

// ─── Faisabilité d'un petit compte (Sprint 23, items 23.0 / 23.2) ────────────
// Scénario de capital/risque OFFLINE : ces valeurs alimentent le harnais de
// validation et n'écrivent JAMAIS dans config/prod.json — la configuration de
// production reste gouvernée et byte-identique pendant le sprint.
struct SmallAccountScenario {
    double              initialCapital  = 1'000.0;
    double              riskPerTradePct = 0.01;   // 0,005 / 0,01 / 0,02 au harnais
    ExecutionCostConfig couts = ExecutionCostConfig::historiqueConservateur();
};

// ─── Verdict de faisabilité OPÉRATIONNELLE ────────────────────────────────────
// « Exploitable » signifie : le compte PEUT exécuter les signaux en actions
// entières — cela ne dit RIEN de la rentabilité (un scénario peut être
// parfaitement exécutable ET sans edge ; l'edge est jugé séparément, net de
// coûts, contre le benchmark). Règles DÉTERMINISTES, seuils documentés :
//   • INEXPLOITABLE_ACTIONS_ENTIERES : aucune entrée exécutée sur tout le
//     backtest (entriesExecuted == 0) — le compte ne trade pas du tout ;
//   • FRACTIONS_POTENTIELLEMENT_NECESSAIRES : au moins 50 % des tentatives
//     d'entrée rejetées pour quantité zéro ou cash insuffisant
//     (kSeuilBlocageFractions) — la contrainte entière domine le compte ;
//   • EXPLOITABLE_MAIS_CONTRAINT : au moins un rejet quantité-zéro/cash, OU
//     déploiement perdu par la troncature entière ≥ 15 points de %
//     (kSeuilDeploiementPerdu) — le compte trade mais la contrainte pèse ;
//   • EXPLOITABLE_ACTIONS_ENTIERES : aucun rejet lié à la taille et perte de
//     déploiement < 15 points — la contrainte entière est négligeable.
enum class FeasibilityVerdict {
    InexploitableActionsEntieres,
    ExploitableMaisContraint,
    ExploitableActionsEntieres,
    FractionsPotentiellementNecessaires
};

inline constexpr double kSeuilBlocageFractions  = 0.50;  // fraction des tentatives
inline constexpr double kSeuilDeploiementPerdu  = 15.0;  // points de % de déploiement

inline FeasibilityVerdict feasibilityVerdict(
    const ExecutionStats& stats, const FractionalSizingAnalysis& frac) {
    if (stats.entriesExecuted == 0)
        return FeasibilityVerdict::InexploitableActionsEntieres;
    const long bloquees = stats.entriesRejectedZeroQuantity
                        + stats.entriesRejectedInsufficientCash;
    const double taux = stats.entriesAttempted > 0
        ? static_cast<double>(bloquees) / stats.entriesAttempted : 0.0;
    if (taux >= kSeuilBlocageFractions)
        return FeasibilityVerdict::FractionsPotentiellementNecessaires;
    if (bloquees > 0
        || frac.lostDeploymentPctDueToIntegerConstraint >= kSeuilDeploiementPerdu)
        return FeasibilityVerdict::ExploitableMaisContraint;
    return FeasibilityVerdict::ExploitableActionsEntieres;
}

inline const char* feasibilityVerdictLabel(FeasibilityVerdict v) {
    switch (v) {
        case FeasibilityVerdict::InexploitableActionsEntieres:
            return "INEXPLOITABLE_ACTIONS_ENTIERES";
        case FeasibilityVerdict::ExploitableMaisContraint:
            return "EXPLOITABLE_MAIS_CONTRAINT";
        case FeasibilityVerdict::ExploitableActionsEntieres:
            return "EXPLOITABLE_ACTIONS_ENTIERES";
        case FeasibilityVerdict::FractionsPotentiellementNecessaires:
            return "FRACTIONS_POTENTIELLEMENT_NECESSAIRES";
    }
    return "?";
}

// ─── Rapport de faisabilité (item 23.2) ───────────────────────────────────────
// Sépare explicitement la FAISABILITÉ OPÉRATIONNELLE (le compte peut-il
// exécuter ?) de la RENTABILITÉ (edge net vs benchmark) — les deux volets ont
// chacun leur verdict.
struct SmallAccountFeasibilityReport {
    SmallAccountScenario scenario;

    // Volet performance (backtest complet, moteur de production)
    double initialCapital  = 0.0;
    double finalValue      = 0.0;
    double totalReturnPct  = 0.0;
    double maxDrawdownPct  = 0.0;
    double buyHoldGrossReturnPct = 0.0;  // métrique historique (prix pur)
    double buyHoldNetReturnPct   = 0.0;  // net de coûts + entier + cash résiduel
    double alphaGrossPct   = 0.0;        // retour − B&H brut
    double alphaNetPct     = 0.0;        // retour − B&H net (le juge d'edge)
    bool   edgeNetDemontre = false;      // alphaNetPct > 0 sur CE scénario

    // Volet exécution (instrumentation 23.1)
    ExecutionStats stats;
    double averagePositionValue = 0.0;   // moyenne de (prix d'achat × actions)

    // Volet coûts (modèle 23.3)
    double totalFees                 = 0.0;
    double feesPctOfInitialCapital   = 0.0;
    double avgFeesPerRoundTrip       = 0.0;

    // Volet fractions (contre-factuel offline 23.5)
    FractionalSizingAnalysis fractional;

    FeasibilityVerdict verdict = FeasibilityVerdict::InexploitableActionsEntieres;

    // Résultat complet conservé pour les analyses en aval (Monte-Carlo 23.7)
    BacktestResult backtest;
};

// Exécute UN scénario petit compte sur un CSV du dépôt : moteur de production
// réel (TradingBot + PaperBroker + RiskManager) via le Backtester, coûts du
// scénario, instrumentation branchée. Déterministe — mêmes entrées, mêmes
// chiffres (verrouillés par les tests d'intégration).
inline SmallAccountFeasibilityReport runSmallAccountScenario(
    SwingConfig cfg, const std::string& csvPath,
    const SmallAccountScenario& scenario) {

    cfg.riskPerTradePct = scenario.riskPerTradePct;

    Backtester bt(cfg, csvPath, scenario.initialCapital, scenario.couts);
    SmallAccountFeasibilityReport r;
    r.scenario = scenario;
    bt.setStatsCollector(&r.stats);
    r.backtest = bt.run();

    r.initialCapital = r.backtest.initialCapital;
    r.finalValue     = r.backtest.finalValue;
    r.totalReturnPct = r.backtest.totalReturnPct;
    r.maxDrawdownPct = r.backtest.maxDrawdownPct;

    // Benchmark : brut = métrique historique du backtest ; net = même modèle
    // de coûts, actions entières, cash résiduel (item 23.4). Le warmup DOIT
    // refléter celui du Backtester (même formule que runRange).
    r.buyHoldGrossReturnPct = r.backtest.buyHoldReturnPct;
    {
        CsvDataFeed csv(csvPath);
        const int warmup = std::max(cfg.emaSlow + cfg.rsiPeriod + 2,
                                    cfg.smaTrendPeriod + 1);
        r.buyHoldNetReturnPct = computeNetBuyHold(
            csv.allBars(), warmup, scenario.initialCapital, scenario.couts,
            /*wholeShares=*/true).netReturnPct;
    }
    r.alphaGrossPct   = r.totalReturnPct - r.buyHoldGrossReturnPct;
    r.alphaNetPct     = r.totalReturnPct - r.buyHoldNetReturnPct;
    r.edgeNetDemontre = r.alphaNetPct > 0.0;

    // Valeur moyenne d'une position (sur les trades clôturés)
    if (!r.backtest.trades.empty()) {
        double somme = 0.0;
        for (const auto& t : r.backtest.trades)
            somme += t.buyPrice * t.shares;
        r.averagePositionValue = somme / static_cast<double>(r.backtest.trades.size());
    }

    // Frais
    r.totalFees = r.backtest.totalFees;
    r.feesPctOfInitialCapital = scenario.initialCapital > 0
        ? r.totalFees / scenario.initialCapital * 100.0 : 0.0;
    r.avgFeesPerRoundTrip = !r.backtest.trades.empty()
        ? r.totalFees / static_cast<double>(r.backtest.trades.size()) : 0.0;

    // Contre-factuel fractionnaire (théorique, offline)
    r.fractional = analyzeFractionalSizing(r.stats.decisions,
                                           cfg.riskPerTradePct, cfg.stopLossPct);

    r.verdict = feasibilityVerdict(r.stats, r.fractional);
    return r;
}

} // namespace trading
