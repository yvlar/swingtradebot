// ─── Tests d'intégration : faisabilité petit compte 1 000 $ (Sprint 23) ──────
// Verrouille les chiffres de la SECTION 23 du harnais `validate` sur les CSV
// réels du dépôt (QQQ.csv total-return, config prod) : verdicts opérationnels
// par niveau de risque, compteurs de rejet, frais, benchmark net, stress et
// Monte-Carlo par blocs. Valeurs consignées dans le changelog de ROADMAP.md —
// ne les re-baseliner QUE pour un changement de comportement volontaire et
// documenté (décision utilisateur explicite).
//
// RAPPEL DE PÉRIMÈTRE (décision d'ouverture du Sprint 23) : tout est offline/
// simulation/paper-only ; la faisabilité opérationnelle ne dit RIEN de la
// rentabilité — l'edge reste NON démontré et le verrou
// LiveTradingStaysDisapprovedUntilEdgeDoD (test_backtester_integration.cpp)
// reste intact.
#include <gtest/gtest.h>
#include <cstdio>
#include <string>
#include "backtest/BlockBootstrap.hpp"
#include "backtest/MonteCarlo.hpp"
#include "backtest/SmallAccountFeasibility.hpp"
#include "backtest/SmallAccountStress.hpp"
#include "strategies/ProdConfig.hpp"

using namespace trading;

namespace {

SmallAccountFeasibilityReport runScenario(double riskPct,
                                          double capital = 1'000.0) {
    SmallAccountScenario sc;
    sc.initialCapital  = capital;
    sc.riskPerTradePct = riskPct;
    sc.couts           = ExecutionCostConfig::historiqueConservateur();
    return runSmallAccountScenario(prodSwingConfig(), SWINGBOT_QQQ_CSV, sc);
}

// Durée calendaire (années) entre deux dates « YYYY-MM-DD » — même helper
// que main_validate.cpp.
long daysFromCivil(const std::string& date) {
    int y = 0, m = 0, d = 0;
    if (std::sscanf(date.c_str(), "%d-%d-%d", &y, &m, &d) != 3) return 0;
    y -= m <= 2;
    const long     era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153u * (m + (m > 2 ? -3 : 9)) + 2u) / 5u + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<long>(doe) - 719468;
}

} // namespace

// ─── Risque 0,5 % : INEXPLOITABLE — le compte ne trade JAMAIS ────────────────
// Budget de risque 5 $ contre un risque par action ≥ 7 $ (prix ≥ 143 $ sur
// toute la série) : 861 signaux BUY, 861 rejets quantité zéro, zéro ordre.
TEST(SmallAccountFeasibilityIntegration, Risk05VerdictIsLockedInexploitable) {
    const auto r = runScenario(0.005);

    EXPECT_EQ(r.verdict, FeasibilityVerdict::InexploitableActionsEntieres);
    EXPECT_EQ(r.stats.buySignalsGenerated,          861);
    EXPECT_EQ(r.stats.entriesAttempted,             861);
    EXPECT_EQ(r.stats.entriesExecuted,                0);
    EXPECT_EQ(r.stats.entriesRejectedZeroQuantity,  861);
    EXPECT_EQ(r.stats.entriesRejectedInsufficientCash, 0);
    // Tous bloqués UNIQUEMENT par la contrainte entière (fractionnaire > 0)
    EXPECT_EQ(r.fractional.signalsBlockedOnlyByIntegerConstraint, 861);
    // Capital intact : aucun ordre, aucun frais, aucun drawdown
    EXPECT_NEAR(r.finalValue, 1'000.0, 1e-6);
    EXPECT_NEAR(r.totalFees,      0.0, 1e-9);
    EXPECT_NEAR(r.maxDrawdownPct, 0.0, 1e-9);
    EXPECT_FALSE(r.edgeNetDemontre);
}

// ─── Risque 1 % : FRACTIONS POTENTIELLEMENT NÉCESSAIRES ──────────────────────
// 3 entrées exécutées sur 764 tentatives (99,6 % de blocage entier).
TEST(SmallAccountFeasibilityIntegration, Risk1VerdictIsLockedFractionsNeeded) {
    const auto r = runScenario(0.01);

    EXPECT_EQ(r.verdict, FeasibilityVerdict::FractionsPotentiellementNecessaires);
    EXPECT_EQ(r.stats.entriesAttempted,             764);
    EXPECT_EQ(r.stats.entriesExecuted,                3);
    EXPECT_EQ(r.stats.entriesRejectedZeroQuantity,  761);
    EXPECT_EQ(r.fractional.signalsBlockedOnlyByIntegerConstraint, 761);
    EXPECT_NEAR(r.finalValue,     1'040.760233, 0.01);
    EXPECT_NEAR(r.totalFees,          1.211543, 0.01);
    EXPECT_NEAR(r.maxDrawdownPct,     1.454508, 1e-3);
    EXPECT_FALSE(r.edgeNetDemontre);
}

// ─── Risque 2 % : EXPLOITABLE en actions entières… mais AUCUN edge ───────────
// Les 22 signaux d'entrée du golden passent tous (l'historique 2019-2021
// cotait 150-400 $), déploiement moyen ~29 % contre 40 % théorique.
// FAISABILITÉ ≠ RENTABILITÉ : alpha net −192 pt vs B&H net.
TEST(SmallAccountFeasibilityIntegration, Risk2VerdictIsLockedExploitableNoEdge) {
    const auto r = runScenario(0.02);

    EXPECT_EQ(r.verdict, FeasibilityVerdict::ExploitableActionsEntieres);
    EXPECT_EQ(r.stats.buySignalsGenerated,          861);
    EXPECT_EQ(r.stats.entriesAttempted,              22);
    EXPECT_EQ(r.stats.entriesExecuted,               22);
    EXPECT_EQ(r.stats.entriesRejectedZeroQuantity,    0);
    EXPECT_EQ(r.stats.entriesRejectedInsufficientCash, 0);

    EXPECT_NEAR(r.finalValue,           1'155.602642, 0.01);
    EXPECT_NEAR(r.totalReturnPct,          15.560264, 1e-3);
    EXPECT_NEAR(r.maxDrawdownPct,           5.283968, 1e-3);
    EXPECT_NEAR(r.totalFees,               14.483624, 0.01);
    EXPECT_NEAR(r.averagePositionValue,   325.307672, 0.01);

    // Benchmark : brut (historique) et net (coûts + entier + cash résiduel)
    EXPECT_NEAR(r.buyHoldGrossReturnPct,  226.123473, 1e-3);
    EXPECT_NEAR(r.buyHoldNetReturnPct,    207.693996, 1e-3);

    // Contre-factuel fractionnaire (théorique, offline)
    EXPECT_NEAR(r.fractional.avgIntegerQuantity,               1.181818, 1e-4);
    EXPECT_NEAR(r.fractional.avgTheoreticalFractionalQuantity, 1.639296, 1e-4);
    EXPECT_NEAR(r.fractional.lostDeploymentPctDueToIntegerConstraint,
                11.338476, 1e-3);

    // Le verdict d'edge est NÉGATIF : exploitable n'est PAS rentable
    EXPECT_NEAR(r.alphaNetPct, -192.133732, 1e-2);
    EXPECT_FALSE(r.edgeNetDemontre);
}

// ─── 23.0 : le capital initial influence RÉELLEMENT actions et capital ───────
// Même config, mêmes coûts (scénario A) : à 10 000 $ le moteur reproduit le
// golden historique AU CENTIME (11 932,57 $ — preuve que le chemin « modèle
// de coûts » n'a rien changé) ; à 1 000 $ la troncature entière ampute le
// rendement (15,56 % contre 19,33 %) et la taille moyenne (1,18 contre 16,09).
TEST(SmallAccountFeasibilityIntegration, InitialCapitalDrivesSharesAndReturn) {
    const auto petit = runScenario(0.02, 1'000.0);
    const auto grand = runScenario(0.02, 10'000.0);

    EXPECT_NEAR(grand.finalValue, 11'932.570159, 0.01);   // golden historique
    EXPECT_NEAR(grand.totalReturnPct, 19.325702, 1e-3);
    EXPECT_EQ(grand.stats.entriesExecuted, 22);

    EXPECT_LT(petit.totalReturnPct, grand.totalReturnPct);
    EXPECT_NEAR(grand.fractional.avgIntegerQuantity, 16.090909, 1e-4);
    EXPECT_LT(petit.fractional.avgIntegerQuantity,
              grand.fractional.avgIntegerQuantity);
}

// ─── 23.3 : sensibilité au modèle de coûts (mêmes 22 trades) ─────────────────
// B (faible commission) > A (historique) > C (stress minimum par ordre) —
// au stress C, 44 ordres × 1 $ = 44 $ = 4,4 % du capital : le minimum DOMINE.
TEST(SmallAccountFeasibilityIntegration, CostScenariosOrderingIsLocked) {
    SwingConfig cfg = prodSwingConfig();
    SmallAccountScenario b, c;
    b.riskPerTradePct = 0.02; b.couts = ExecutionCostConfig::faibleCommission();
    c.riskPerTradePct = 0.02; c.couts = ExecutionCostConfig::stressPetitCompte();
    const auto rA = runScenario(0.02);
    const auto rB = runSmallAccountScenario(cfg, SWINGBOT_QQQ_CSV, b);
    const auto rC = runSmallAccountScenario(cfg, SWINGBOT_QQQ_CSV, c);

    EXPECT_EQ(rB.stats.entriesExecuted, 22);
    EXPECT_EQ(rC.stats.entriesExecuted, 22);
    EXPECT_NEAR(rB.finalValue, 1'169.826265, 0.01);
    EXPECT_NEAR(rC.finalValue, 1'122.465349, 0.01);
    EXPECT_NEAR(rC.totalFees,     44.000000, 0.01);
    EXPECT_GT(rB.finalValue, rA.finalValue);
    EXPECT_GT(rA.finalValue, rC.finalValue);
}

// ─── 23.6 : stress séquences de pertes et gap, chiffres figés ────────────────
// Composition réelle à risque 2 % (prix 100 $) : 5 pertes → −8,35 %,
// 10 pertes → −16,18 % (sous le naïf 20 %). Au DERNIER close QQQ (~600 $),
// aucune action entière n'est finançable : la séquence est bloquée.
TEST(SmallAccountFeasibilityIntegration, LossSequencesAreLocked) {
    const auto couts = ExecutionCostConfig::historiqueConservateur();
    const auto s5  = simulateLossSequence(1'000.0, 0.02, 0.05,  5, 100.0, couts);
    const auto s10 = simulateLossSequence(1'000.0, 0.02, 0.05, 10, 100.0, couts);

    EXPECT_NEAR(s5.capitalRemaining,  916.479505, 0.01);
    EXPECT_NEAR(s5.capitalLossPct,      8.352049, 1e-3);
    EXPECT_NEAR(s10.capitalRemaining, 838.179041, 0.01);
    EXPECT_NEAR(s10.capitalLossPct,    16.182096, 1e-3);
    EXPECT_LT(s10.capitalLossPct, 20.0);   // composition < naïf linéaire

    // Réalité du compte qui commencerait AUJOURD'HUI : dernier close QQQ
    CsvDataFeed csv(SWINGBOT_QQQ_CSV);
    const double dernier = csv.allBars().back().close;
    ASSERT_GT(dernier, 400.0);   // garde de cohérence des données
    const auto sBloque = simulateLossSequence(1'000.0, 0.02, 0.05, 10,
                                              dernier, couts);
    EXPECT_EQ(sBloque.tradesExecuted, 0);
    EXPECT_EQ(sBloque.tradesBlocked, 10);
    EXPECT_NEAR(sBloque.capitalRemaining, 1'000.0, 1e-9);
}

// Gap à −10 % au lieu du stop −5 % : la perte réelle (40,86 $) DOUBLE la
// perte planifiée (20 $) — le stop ne borne pas un gap d'ouverture.
TEST(SmallAccountFeasibilityIntegration, GapStressIsLocked) {
    const auto g = simulateGapExit(1'000.0, 0.02, 0.05, 0.10, 100.0,
                                   ExecutionCostConfig::historiqueConservateur());
    ASSERT_TRUE(g.executed);
    EXPECT_NEAR(g.lossDollars,        40.860122, 0.01);
    EXPECT_NEAR(g.plannedLossDollars, 20.0,      1e-9);
    EXPECT_GT(g.lossDollars, 2.0 * g.plannedLossDollars - 1.0);
}

// ─── 23.7 : Monte-Carlo IID vs blocs sur les trades du compte 1 000 $ ────────
// Graine 42, 2 000 chemins, 22 trades (risque 2 %). Le drawdown p95 par
// blocs de 3 (6,68 %) dépasse l'IID (5,27 %) : les grappes de pertes
// survivent au bootstrap par blocs. L'IID historique n'a PAS bougé (ses
// goldens dans test_monte_carlo_integration.cpp restent la référence).
TEST(SmallAccountFeasibilityIntegration, MonteCarloIidVsBlocksIsLocked) {
    const auto r = runScenario(0.02);
    ASSERT_EQ(r.backtest.trades.size(), 22u);
    double annees = 0.0;
    ASSERT_GE(r.backtest.equityDates.size(), 2u);
    annees = (daysFromCivil(r.backtest.equityDates.back()) -
              daysFromCivil(r.backtest.equityDates.front())) / 365.25;

    const auto iid = MonteCarlo(1'000.0, 42, 2000).run(r.backtest.trades, annees);
    EXPECT_NEAR(iid.ddP95, 5.265648, 1e-3);

    const auto b3 = BlockBootstrapMonteCarlo(1'000.0, 3, 42, 2000)
                        .run(r.backtest.trades, annees);
    const auto b10 = BlockBootstrapMonteCarlo(1'000.0, 10, 42, 2000)
                         .run(r.backtest.trades, annees);
    EXPECT_NEAR(b3.ddP95,  6.680462, 1e-3);
    EXPECT_NEAR(b10.ddP95, 4.390785, 1e-3);
    EXPECT_GT(b3.ddP95, iid.ddP95);
}
