// ============================================================
//  test_vol_regime_backtester_unit.cpp  —  Tests UNITAIRES
//  Cible : VolRegimeBacktester (backtest/VolRegimeBacktester.hpp)
//  5e famille d'alpha : VOL-REGIME / volatility-managed (Sprint 13, item 13.1).
//  Aucun I/O : on exerce les fonctions pures (closeReturns, realizedVol,
//  trailingMedian) et des runs synthétiques via le seam fromSeries, avec des
//  scénarios DÉTERMINISTES calculés à la main.
//
//  Astuce de conception : avec volLookback = 2, l'écart-type glissant sur une
//  fenêtre de 2 rendements [a,b] vaut |a−b|/2. Avec refLookback = 2, la médiane de
//  référence est la moyenne des deux dernières vols : med[i] = (rv[i−1]+rv[i])/2.
//  Le régime CALME (rv[i] ≤ med[i]) se réduit alors à rv[i] ≤ rv[i−1] — « la vol
//  n'augmente pas ». Entrées/sorties parfaitement contrôlables ; à coûts nuls, les
//  P&L tombent juste. Le warmup vaut w = volLookback + refLookback − 2 = 2.
// ============================================================
#include <gtest/gtest.h>
#include <cmath>
#include <string>
#include <vector>
#include "backtest/VolRegimeBacktester.hpp"

using namespace trading;

namespace {

// Dates ISO séquentielles simples (l'ordre suffit ; les valeurs exactes
// n'importent pas pour les métriques non liées au calendrier).
std::vector<std::string> dates(size_t n) {
    std::vector<std::string> d;
    for (size_t i = 0; i < n; ++i) {
        char buf[24];
        std::snprintf(buf, sizeof(buf), "2020-01-%02d", static_cast<int>(i + 1));
        d.emplace_back(buf);
    }
    return d;
}

// Config à COÛTS NULS (pour des P&L calculables à la main), volLookback = 2,
// refLookback = 2, seuil = 1,0.
VolRegimeConfig cfg0() {
    VolRegimeConfig c;
    c.volLookback = 2; c.refLookback = 2; c.thresholdMult = 1.0;
    c.initialCapital = 10'000.0;
    c.commissionPct = 0.0; c.slippageBps = 0.0; c.halfSpreadBps = 0.0;
    return c;
}

} // namespace

// ════════════════════════════════════════════════════════════
//  Fonction pure : rendements simples
// ════════════════════════════════════════════════════════════
TEST(VolRegimeBacktesterUnit, ClosesReturnsAreSimpleReturns) {
    const auto ret = closeReturns({100.0, 110.0, 99.0});
    ASSERT_EQ(ret.size(), 3u);
    EXPECT_DOUBLE_EQ(ret[0], 0.0);            // ret[0] conventionnellement nul
    EXPECT_NEAR(ret[1], 0.10, 1e-12);         // 110/100 − 1
    EXPECT_NEAR(ret[2], 99.0 / 110.0 - 1.0, 1e-12);
}

// ════════════════════════════════════════════════════════════
//  Fonction pure : volatilité réalisée (σ glissant des rendements, volLookback=2)
//  σ de 2 valeurs [a,b] = |a−b|/2. rv[i] = |ret[i] − ret[i−1]| / 2.
// ════════════════════════════════════════════════════════════
TEST(VolRegimeBacktesterUnit, RealizedVolMatchesHandComputed) {
    // px = [100,100,110,99] → ret = [0, 0, 0.1, -0.1].
    const auto rv = realizedVol({100.0, 100.0, 110.0, 99.0}, 2);
    ASSERT_EQ(rv.size(), 4u);
    EXPECT_NEAR(rv[0], 0.0,  1e-12);          // warmup
    EXPECT_NEAR(rv[1], 0.0,  1e-12);          // |0−0|/2
    EXPECT_NEAR(rv[2], 0.05, 1e-12);          // |0.1−0|/2
    EXPECT_NEAR(rv[3], 0.10, 1e-12);          // |−0.1−0.1|/2
}

// ════════════════════════════════════════════════════════════
//  Fonction pure : médiane glissante CAUSALE (aucun usage du futur)
// ════════════════════════════════════════════════════════════
TEST(VolRegimeBacktesterUnit, TrailingMedianIsCausal) {
    const std::vector<double> v = {1.0, 2.0, 3.0, 4.0, 5.0};
    const auto m3 = trailingMedian(v, 3);       // fenêtre impaire → valeur centrale
    ASSERT_EQ(m3.size(), 5u);
    EXPECT_DOUBLE_EQ(m3[0], 0.0);               // fenêtre incomplète
    EXPECT_DOUBLE_EQ(m3[1], 0.0);
    EXPECT_DOUBLE_EQ(m3[2], 2.0);               // médiane(1,2,3)
    EXPECT_DOUBLE_EQ(m3[3], 3.0);               // médiane(2,3,4) — n'utilise pas v[4]
    EXPECT_DOUBLE_EQ(m3[4], 4.0);               // médiane(3,4,5)

    const auto m2 = trailingMedian(v, 2);       // fenêtre paire → moyenne des 2 centraux
    EXPECT_DOUBLE_EQ(m2[1], 1.5);
    EXPECT_DOUBLE_EQ(m2[4], 4.5);
}

// ════════════════════════════════════════════════════════════
//  Série parfaitement plate → vol nulle → régime CALME → LONG, mais P&L nul
//  (convention documentée : σ = 0 = calme extrême ⇒ investi ; prix plats ⇒ 0 %).
// ════════════════════════════════════════════════════════════
TEST(VolRegimeBacktesterUnit, FlatSeriesZeroVolIsCalmButFlatPnl) {
    const std::vector<double> px = {100, 100, 100, 100, 100, 100};
    const auto r = VolRegimeBacktester::fromSeries(cfg0(), dates(px.size()), px).run();

    ASSERT_EQ(r.trades.size(), 1u);             // un stint long ouvert dès i=2
    EXPECT_DOUBLE_EQ(r.totalReturnPct, 0.0);
    EXPECT_DOUBLE_EQ(r.trades[0].pnlPct, 0.0);
    EXPECT_DOUBLE_EQ(r.finalValue, 10'000.0);
    EXPECT_DOUBLE_EQ(r.maxDrawdownPct, 0.0);
    EXPECT_EQ(r.trades[0].exitReason, std::string("fin"));
}

// ════════════════════════════════════════════════════════════
//  Régime CALME → LONG avec P&L exact.
//  px = [100,100,101,102,103,104] (coûts nuls).
//   i=2 : rv=0.005 > med=0.0025 → CASH (la vol vient de bondir).
//   i=3 : rv≈0.0000495 ≤ med≈0.00252 → LONG (entrée). Gagne 103/102 puis 104/103.
//   i=5 : dernière barre → liquidation « fin ». gross = 104/102.
// ════════════════════════════════════════════════════════════
TEST(VolRegimeBacktesterUnit, CalmRegimeGoesLongWithExactPnl) {
    const std::vector<double> px = {100, 100, 101, 102, 103, 104};
    const auto r = VolRegimeBacktester::fromSeries(cfg0(), dates(px.size()), px).run();

    ASSERT_EQ(r.trades.size(), 1u);
    EXPECT_EQ(r.switchCount, 1);                        // une seule bascule cash→long (i=3)
    EXPECT_NEAR(r.finalValue, 10'000.0 * 104.0 / 102.0, 1e-6);
    EXPECT_NEAR(r.totalReturnPct, (104.0 / 102.0 - 1.0) * 100.0, 1e-9);
    EXPECT_NEAR(r.trades[0].pnlPct, (104.0 / 102.0 - 1.0) * 100.0, 1e-9);
    EXPECT_EQ(r.trades[0].holdDays, 2);                // entrée i=3, sortie i=5
    EXPECT_EQ(r.trades[0].buyDate, dates(px.size())[3]);
    EXPECT_EQ(r.trades[0].exitReason, std::string("fin"));
    EXPECT_DOUBLE_EQ(r.trades[0].deployedFraction, 1.0);
    EXPECT_TRUE(r.trades[0].isWin);
    // 2 séances investies (i3→i4, i4→i5) sur M−1 = 5 → 40 %.
    EXPECT_NEAR(r.pctTimeInvested, 40.0, 1e-9);
}

// ════════════════════════════════════════════════════════════
//  Régime AGITÉ persistant → JAMAIS long → équité plate, aucun trade.
//  Vol réalisée strictement croissante (rendements à écarts croissants) ⇒ jamais
//  rv[i] ≤ rv[i−1] ⇒ jamais calme.
// ════════════════════════════════════════════════════════════
TEST(VolRegimeBacktesterUnit, HighVolRegimeStaysInCashFlatEquity) {
    // ret cibles = [0, 0.01, 0.03, 0.06, 0.10, 0.15] → |Δret| = 0.01,0.02,…,0.05
    // ⇒ rv = 0,0.005,0.01,0.015,0.02,0.025 (strictement croissant).
    const std::vector<double> px = {100.0, 101.0, 104.03, 110.2718, 121.29898, 139.4938270};
    const auto r = VolRegimeBacktester::fromSeries(cfg0(), dates(px.size()), px).run();

    EXPECT_TRUE(r.trades.empty());
    EXPECT_EQ(r.switchCount, 0);
    EXPECT_DOUBLE_EQ(r.finalValue, 10'000.0);
    EXPECT_DOUBLE_EQ(r.totalReturnPct, 0.0);
    EXPECT_DOUBLE_EQ(r.pctTimeInvested, 0.0);
}

// ════════════════════════════════════════════════════════════
//  Deux stints + garde anti-cash-drag (D47) : entre la sortie et la ré-entrée,
//  l'équité est PLATE (cash). Vérifie trades.size() > 1 et la platitude.
//  px = [100,100,100,105,105,105,115.5,103.95,103.95].
//   Stint 1 : entrée i=2, sortie i=3 (+5 %) ; cash i=3→i=4 (plat) ;
//   Stint 2 : entrée i=4, sortie i=6 (+10 %) ; cash ensuite.
// ════════════════════════════════════════════════════════════
TEST(VolRegimeBacktesterUnit, TwoStintsAndCashIsFlatBetween) {
    const std::vector<double> px = {100, 100, 100, 105, 105, 105, 115.5, 103.95, 103.95};
    const auto r = VolRegimeBacktester::fromSeries(cfg0(), dates(px.size()), px).run();

    ASSERT_EQ(r.trades.size(), 2u);
    EXPECT_GT(r.trades.size(), 1u);                    // D47 : la famille TRADE
    EXPECT_EQ(r.switchCount, 4);                       // cash→long→cash→long→cash
    EXPECT_NEAR(r.trades[0].pnlPct, 5.0, 1e-9);
    EXPECT_NEAR(r.trades[1].pnlPct, 10.0, 1e-9);
    EXPECT_EQ(r.trades[0].exitReason, std::string("regime"));
    EXPECT_EQ(r.trades[1].exitReason, std::string("regime"));
    EXPECT_NEAR(r.totalReturnPct, 15.5, 1e-9);         // 1.05·1.10 − 1
    EXPECT_NEAR(r.finalValue, 11'550.0, 1e-6);
    // Équité plate entre les deux stints : mark au début de i=3 == mark au début de i=4.
    ASSERT_GE(r.equityCurve.size(), 5u);
    EXPECT_NEAR(r.equityCurve[3], r.equityCurve[4], 1e-9);
    EXPECT_NEAR(r.equityCurve[3], 10'500.0, 1e-6);
}

// ════════════════════════════════════════════════════════════
//  Sous-fenêtre : runRange ré-amorce vol/médiane sur les seules barres de la
//  fenêtre (anti-leak). Une fenêtre plus courte que le warmup ne trade jamais.
// ════════════════════════════════════════════════════════════
TEST(VolRegimeBacktesterUnit, RangeShorterThanWarmupNeverTrades) {
    const std::vector<double> px = {100, 100, 100, 105, 105, 105, 115.5, 103.95, 103.95};
    VolRegimeConfig c = cfg0();
    c.volLookback = 5; c.refLookback = 10;             // warmup w = 13 > longueur de la sous-fenêtre
    const auto r = VolRegimeBacktester::fromSeries(c, dates(px.size()), px).runRange(0, 5);
    EXPECT_TRUE(r.trades.empty());
    EXPECT_DOUBLE_EQ(r.totalReturnPct, 0.0);
}
