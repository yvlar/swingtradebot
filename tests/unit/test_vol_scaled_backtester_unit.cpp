// ============================================================
//  test_vol_scaled_backtester_unit.cpp  —  Tests UNITAIRES
//  Cible : VolScaledBacktester (backtest/VolScaledBacktester.hpp)
//  Réouverture Sprint 18 (piste §5.2) : SCALING CONTINU Moreira-Muir,
//  w* = min(maxWeight, cible / vol annualisée), bande anti-churn, coût
//  proportionnel au notionnel tradé |Δw|. Aucun I/O : fonctions pures
//  (mmTargetWeight, mmShouldRebalance, mmAnnualizedVolPct) + runs synthétiques
//  via le seam fromSeries, scénarios DÉTERMINISTES calculés à la main.
//
//  Astuce de conception : avec volLookback = 2, l'écart-type glissant sur une
//  fenêtre de 2 rendements [a,b] vaut |a−b|/2 (cf. tests VolRegime). Avec une
//  cible ÉNORME, w* sature à maxWeight dès que la vol est > 0 → poids constant
//  contrôlé, P&L exact = capital · Π(1 + w·r). Le warmup vaut volLookback − 1.
// ============================================================
#include <gtest/gtest.h>
#include <cmath>
#include <string>
#include <vector>
#include "backtest/VolScaledBacktester.hpp"

using namespace trading;

namespace {

std::vector<std::string> dates(size_t n) {
    std::vector<std::string> d;
    for (size_t i = 0; i < n; ++i) {
        char buf[24];
        std::snprintf(buf, sizeof(buf), "2020-01-%02d", static_cast<int>(i + 1));
        d.emplace_back(buf);
    }
    return d;
}

// Config à COÛTS NULS, volLookback = 2, cible énorme → w* = maxWeight dès que
// la vol est > 0 (poids parfaitement contrôlable).
VolScaledConfig cfg0() {
    VolScaledConfig c;
    c.volLookback = 2; c.targetVolAnnPct = 1e9; c.maxWeight = 0.5;
    c.rebalanceBand = 0.05;
    c.initialCapital = 10'000.0;
    c.commissionPct = 0.0; c.slippageBps = 0.0; c.halfSpreadBps = 0.0;
    return c;
}

} // namespace

// ════════════════════════════════════════════════════════════
//  Fonction pure : poids cible Moreira-Muir
// ════════════════════════════════════════════════════════════
TEST(VolScaledBacktesterUnit, TargetWeightFormulaAndCap) {
    EXPECT_DOUBLE_EQ(mmTargetWeight(30.0, 15.0, 1.0), 0.5);   // cible/vol = 15/30
    EXPECT_DOUBLE_EQ(mmTargetWeight(10.0, 15.0, 1.0), 1.0);   // 1.5 plafonné à 1,0
    EXPECT_DOUBLE_EQ(mmTargetWeight(10.0, 15.0, 0.8), 0.8);   // plafond maxWeight
    EXPECT_DOUBLE_EQ(mmTargetWeight(0.0, 15.0, 1.0), 0.0);    // vol nulle → 0 (inconnu ≠ calme)
    EXPECT_DOUBLE_EQ(mmTargetWeight(-1.0, 15.0, 1.0), 0.0);   // vol dégénérée → 0
    EXPECT_DOUBLE_EQ(mmTargetWeight(30.0, 0.0, 1.0), 0.0);    // cible nulle → 0
    EXPECT_DOUBLE_EQ(mmTargetWeight(30.0, 15.0, 0.0), 0.0);   // plafond nul → 0
}

// ════════════════════════════════════════════════════════════
//  Fonction pure : bande anti-churn (rebalancer ssi |Δw| > bande)
// ════════════════════════════════════════════════════════════
TEST(VolScaledBacktesterUnit, RebalanceBandIsStrictHysteresis) {
    EXPECT_TRUE(mmShouldRebalance(0.50, 0.60, 0.05));    // |Δ| = 0,10 > 0,05
    EXPECT_FALSE(mmShouldRebalance(0.50, 0.54, 0.05));   // |Δ| = 0,04 ≤ 0,05
    // |Δ| = bande exactement → PAS de trade (comparaison stricte ; valeurs
    // binaires exactes 0,5 / 0,625 / 0,125 pour éviter l'artefact flottant).
    EXPECT_FALSE(mmShouldRebalance(0.5, 0.625, 0.125));
    EXPECT_TRUE(mmShouldRebalance(0.00, 0.50, 0.05));    // l'ENTRÉE passe aussi par la bande
    EXPECT_FALSE(mmShouldRebalance(0.00, 0.04, 0.05));   // poids cible minuscule → on n'entre pas
    EXPECT_TRUE(mmShouldRebalance(0.50, 0.00, 0.05));    // retour au cash
}

// ════════════════════════════════════════════════════════════
//  Fonction pure : annualisation de la vol journalière (√252, en %)
// ════════════════════════════════════════════════════════════
TEST(VolScaledBacktesterUnit, AnnualizedVolMatchesHandComputed) {
    EXPECT_NEAR(mmAnnualizedVolPct(0.01), std::sqrt(252.0), 1e-12);   // 1 %/j → √252 %
    EXPECT_DOUBLE_EQ(mmAnnualizedVolPct(0.0), 0.0);
}

// ════════════════════════════════════════════════════════════
//  Série parfaitement plate → vol nulle → poids 0 → JAMAIS investi.
//  CONTRASTE DOCUMENTÉ avec le binaire VolRegime (vol nulle = calme extrême =
//  long) : ici la cible absolue est indivisible par une vol nulle → 0.
// ════════════════════════════════════════════════════════════
TEST(VolScaledBacktesterUnit, FlatSeriesZeroVolNeverInvests) {
    const std::vector<double> px = {100, 100, 100, 100, 100, 100};
    const auto r = VolScaledBacktester::fromSeries(cfg0(), dates(px.size()), px).run();

    EXPECT_TRUE(r.trades.empty());
    EXPECT_EQ(r.rebalanceCount, 0);
    EXPECT_DOUBLE_EQ(r.finalValue, 10'000.0);
    EXPECT_DOUBLE_EQ(r.totalReturnPct, 0.0);
    EXPECT_DOUBLE_EQ(r.pctTimeInvested, 0.0);
    EXPECT_DOUBLE_EQ(r.avgWeight, 0.0);
    EXPECT_DOUBLE_EQ(r.turnover, 0.0);
}

// ════════════════════════════════════════════════════════════
//  P&L EXACT à poids constant (coûts nuls) : cible énorme → w = maxWeight = 0,5
//  dès i = 1 (warmup volLookback−1 = 1, rv[1] > 0 car px[1] ≠ px[0]).
//  px = [100,101,102,103,104] → eq = 10 000 · Π_{i=1..3}(1 + 0,5·(px[i+1]/px[i]−1)).
// ════════════════════════════════════════════════════════════
TEST(VolScaledBacktesterUnit, ConstantWeightExactPnlZeroCosts) {
    const std::vector<double> px = {100, 101, 102, 103, 104};
    const auto r = VolScaledBacktester::fromSeries(cfg0(), dates(px.size()), px).run();

    double expected = 10'000.0;
    for (size_t i = 1; i + 1 < px.size(); ++i)
        expected *= (1.0 + 0.5 * (px[i + 1] / px[i] - 1.0));

    ASSERT_EQ(r.trades.size(), 1u);
    EXPECT_EQ(r.rebalanceCount, 1);                    // une seule entrée cash→0,5
    EXPECT_NEAR(r.finalValue, expected, 1e-9);
    EXPECT_NEAR(r.trades[0].pnlPct, (expected / 10'000.0 - 1.0) * 100.0, 1e-9);
    EXPECT_EQ(r.trades[0].exitReason, std::string("fin"));
    EXPECT_EQ(r.trades[0].buyDate, dates(px.size())[1]);
    // pnlPct = rendement PORTEFEUILLE (le poids est déjà composé dedans) → 1,0.
    EXPECT_DOUBLE_EQ(r.trades[0].deployedFraction, 1.0);
    // turnover = 0,5 (entrée) + 0,5 (liquidation forcée) = 1,0.
    EXPECT_NEAR(r.turnover, 1.0, 1e-12);
    // 3 séances investies sur M−1 = 4 → 75 % ; poids moyen = 3·0,5/4 = 0,375.
    EXPECT_NEAR(r.pctTimeInvested, 75.0, 1e-9);
    EXPECT_NEAR(r.avgWeight, 0.375, 1e-12);
}

// ════════════════════════════════════════════════════════════
//  COÛTS PROPORTIONNELS au notionnel tradé : entrée à w = 0,5 coûte exactement
//  c·0,5 (pas c·1,0), idem la liquidation forcée.
//  eq = 10 000 · (1 − 0,5c) · Π(1 + 0,5·r) · (1 − 0,5c).
// ════════════════════════════════════════════════════════════
TEST(VolScaledBacktesterUnit, CostsAreProportionalToTradedNotional) {
    VolScaledConfig c = cfg0();
    c.commissionPct = 0.001; c.slippageBps = 2.0; c.halfSpreadBps = 0.5;
    const double perSide = 0.001 + (2.0 + 0.5) / 10'000.0;

    const std::vector<double> px = {100, 101, 102, 103, 104};
    const auto r = VolScaledBacktester::fromSeries(c, dates(px.size()), px).run();

    double expected = 10'000.0 * (1.0 - 0.5 * perSide);
    for (size_t i = 1; i + 1 < px.size(); ++i)
        expected *= (1.0 + 0.5 * (px[i + 1] / px[i] - 1.0));
    expected *= (1.0 - 0.5 * perSide);

    ASSERT_EQ(r.trades.size(), 1u);
    EXPECT_NEAR(r.finalValue, expected, 1e-9);
    // Le pnl du stint porte les DEUX coûts (entrée + sortie) : Σ pnl = équité.
    EXPECT_NEAR(r.trades[0].pnlPct, (expected / 10'000.0 - 1.0) * 100.0, 1e-9);
}

// ════════════════════════════════════════════════════════════
//  Bande PLUS LARGE que le poids cible → l'entrée elle-même est bloquée :
//  jamais investi, équité plate (l'entrée passe par la même hystérésis).
// ════════════════════════════════════════════════════════════
TEST(VolScaledBacktesterUnit, BandWiderThanTargetWeightBlocksEntry) {
    VolScaledConfig c = cfg0();          // maxWeight = 0,5
    c.rebalanceBand = 0.75;              // 0,5 ≤ 0,75 → jamais de franchissement
    const std::vector<double> px = {100, 101, 102, 103, 104};
    const auto r = VolScaledBacktester::fromSeries(c, dates(px.size()), px).run();

    EXPECT_TRUE(r.trades.empty());
    EXPECT_EQ(r.rebalanceCount, 0);
    EXPECT_DOUBLE_EQ(r.finalValue, 10'000.0);
    EXPECT_DOUBLE_EQ(r.turnover, 0.0);
}

// ════════════════════════════════════════════════════════════
//  Hystérésis en vol : la vol varie mais |Δw| reste ≤ bande → AUCUN
//  rebalancement après l'entrée (un seul stint, poids d'entrée conservé).
//  Rendements alternés ±r puis ±s (volLookback = 2, rv = |Δret|/2) :
//   i=1 : rv = r/2 (rampe depuis ret[0]=0) → volAnn = 15 → w ENTRE à 1,0 ;
//   i≥2 : rv = r → volAnn = 30 → w* = 0,5 ; jonction → ~0,545 ; puis s → 0,6.
//  À bande 0,6 aucun |Δw| (0,5 / 0,455 / 0,4) ne franchit → poids 1,0 conservé.
// ════════════════════════════════════════════════════════════
TEST(VolScaledBacktesterUnit, VolDriftWithinBandKeepsEntryWeight) {
    // r tel que volAnn = 30 % ; s tel que volAnn = 25 % (cible 15 %).
    const double rr = 30.0 / (std::sqrt(252.0) * 100.0);
    const double ss = 25.0 / (std::sqrt(252.0) * 100.0);
    std::vector<double> rets = {0.0, rr, -rr, rr, -rr, ss, -ss, ss, -ss};
    std::vector<double> px(1, 100.0);
    for (size_t i = 1; i < rets.size(); ++i) px.push_back(px.back() * (1.0 + rets[i]));

    VolScaledConfig c = cfg0();
    c.targetVolAnnPct = 15.0; c.maxWeight = 1.0; c.rebalanceBand = 0.6;
    const auto r = VolScaledBacktester::fromSeries(c, dates(px.size()), px).run();

    // Une seule entrée (cash → 1,0), aucun ajustement ensuite, liquidation « fin ».
    ASSERT_EQ(r.trades.size(), 1u);
    EXPECT_EQ(r.rebalanceCount, 1);
    EXPECT_EQ(r.trades[0].exitReason, std::string("fin"));

    // Équité de référence : poids CONSTANT 1,0 depuis la première barre décidable
    // = B&H de px[1] à px[8] (coûts nuls).
    EXPECT_NEAR(r.finalValue, 10'000.0 * px.back() / px[1], 1e-6);
}

// ════════════════════════════════════════════════════════════
//  Bande FINE → le même scénario rebalance (1,0 → 0,5 → ~0,545 → 0,6) :
//  plusieurs stints, chaque coût appartient à exactement un stint
//  (la COMPOSITION des pnl des stints reconstruit l'équité finale).
// ════════════════════════════════════════════════════════════
TEST(VolScaledBacktesterUnit, TightBandRebalancesAndTradePnlsCompose) {
    const double rr = 30.0 / (std::sqrt(252.0) * 100.0);
    const double ss = 25.0 / (std::sqrt(252.0) * 100.0);
    std::vector<double> rets = {0.0, rr, -rr, rr, -rr, ss, -ss, ss, -ss};
    std::vector<double> px(1, 100.0);
    for (size_t i = 1; i < rets.size(); ++i) px.push_back(px.back() * (1.0 + rets[i]));

    VolScaledConfig c = cfg0();
    c.targetVolAnnPct = 15.0; c.maxWeight = 1.0; c.rebalanceBand = 0.02;
    c.commissionPct = 0.001;   // coûts NON nuls : la composition doit rester exacte
    const auto r = VolScaledBacktester::fromSeries(c, dates(px.size()), px).run();

    ASSERT_GE(r.trades.size(), 2u);
    EXPECT_GE(r.rebalanceCount, 2);
    // Composition : Π(1 + pnlPct_k/100) sur les stints CONTIGUS == équité finale
    // (chaque coût est porté par exactement un stint, aucun trou de cash ici).
    double composed = 10'000.0;
    for (const auto& t : r.trades) composed *= (1.0 + t.pnlPct / 100.0);
    EXPECT_NEAR(composed, r.finalValue, 1e-6);
}

// ════════════════════════════════════════════════════════════
//  Sous-fenêtre : runRange ré-amorce la vol sur les seules barres de la fenêtre
//  (anti-leak). Une fenêtre plus courte que le warmup ne trade jamais.
// ════════════════════════════════════════════════════════════
TEST(VolScaledBacktesterUnit, RangeShorterThanWarmupNeverTrades) {
    const std::vector<double> px = {100, 101, 102, 103, 104, 105, 106, 107};
    VolScaledConfig c = cfg0();
    c.volLookback = 10;                  // warmup = 9 > longueur de la sous-fenêtre
    const auto r = VolScaledBacktester::fromSeries(c, dates(px.size()), px).runRange(0, 5);
    EXPECT_TRUE(r.trades.empty());
    EXPECT_DOUBLE_EQ(r.totalReturnPct, 0.0);
}
