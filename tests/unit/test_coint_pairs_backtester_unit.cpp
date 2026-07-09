// ============================================================
//  test_coint_pairs_backtester_unit.cpp  —  Tests UNITAIRES
//  Cible : CointPairsBacktester (backtest/CointPairsBacktester.hpp)
//  Réouverture Sprint 18 (piste §5.1) : pairs-trading Engle-Granger — hedge
//  ratio OLS roulant + GATE de cointégration (test DF sur le résidu) au lieu du
//  spread naïf β = 1 réfuté en D49. Aucun I/O : seam fromAxis, scénarios
//  DÉTERMINISTES construits à la main.
//
//  Astuce de conception : une paire SYNTHÉTIQUEMENT cointégrée est construite
//  par log P0 = 2·log P1 + e avec e alternance ±a (réversion parfaite de
//  période 2) et log P1 une rampe régulière. L'OLS roulante retrouve β ≈ 2, le
//  résidu ≈ e passe le test DF (réversion forte), et le z-score du résidu
//  alterne ±1 → allers-retours déterministes d'une barre. Pour le gate FERMÉ,
//  une jambe tendancielle face à une jambe stationnaire donne un résidu
//  tendanciel (jamais stationnaire) → AUCUNE entrée autorisée.
// ============================================================
#include <gtest/gtest.h>
#include <cmath>
#include <string>
#include <vector>
#include "backtest/CointPairsBacktester.hpp"

using namespace trading;

namespace {

AlignedAxis axisPair(const std::vector<std::string>& dates,
                     const std::vector<double>& p0,
                     const std::vector<double>& p1) {
    AlignedAxis ax;
    ax.dates = dates;
    ax.close = { p0, p1 };
    return ax;
}

std::vector<std::string> dates(size_t n) {
    std::vector<std::string> d;
    for (size_t i = 0; i < n; ++i) {
        char buf[24];
        std::snprintf(buf, sizeof(buf), "2020-%02d-%02d",
                      static_cast<int>(i / 28) + 1, static_cast<int>(i % 28) + 1);
        d.emplace_back(buf);
    }
    return d;
}

// Config à COÛTS NULS, petites fenêtres (betaWindow = adfWindow = 8, z = 4),
// entrée souple (K = 0,8 : le z de l'alternance vaut ±1 à un chouia près).
CointPairsConfig cfg0() {
    CointPairsConfig c;
    c.betaWindow = 8; c.adfWindow = 8; c.zWindow = 4;
    c.adfCritical = -3.34; c.entryK = 0.8; c.exitZ = 0.0;
    c.initialCapital = 10'000.0;
    c.commissionPct = 0.0; c.slippageBps = 0.0; c.halfSpreadBps = 0.0;
    return c;
}

// Paire cointégrée : log P1 = rampe de pas `step`, log P0 = 2·log P1 + e,
// e = alternance ±a (période 2, réversion parfaite).
void buildCointegratedPair(size_t n, double step, double a,
                           std::vector<double>& p0, std::vector<double>& p1) {
    p0.clear(); p1.clear();
    for (size_t t = 0; t < n; ++t) {
        const double lx = step * static_cast<double>(t);
        const double e  = (t % 2 == 0) ? a : -a;
        p1.push_back(std::exp(lx));
        p0.push_back(std::exp(2.0 * lx + e));
    }
}

} // namespace

// ════════════════════════════════════════════════════════════
//  Arité : le moteur exige EXACTEMENT 2 séries.
// ════════════════════════════════════════════════════════════
TEST(CointPairsBacktesterUnit, WrongArityYieldsNeutralResult) {
    AlignedAxis ax;
    ax.dates = dates(30);
    ax.close = { std::vector<double>(30, 100.0),
                 std::vector<double>(30, 50.0),
                 std::vector<double>(30, 25.0) };   // 3 séries
    const auto r = CointPairsBacktester::fromAxis(cfg0(), ax).run();
    EXPECT_TRUE(r.trades.empty());
    EXPECT_DOUBLE_EQ(r.totalReturnPct, 0.0);
}

// ════════════════════════════════════════════════════════════
//  Fenêtres dégénérées : zWindow > adfWindow (le z se prend sur la QUEUE du
//  résidu) → résultat neutre, jamais de trade.
// ════════════════════════════════════════════════════════════
TEST(CointPairsBacktesterUnit, ZWindowLargerThanAdfWindowIsNeutral) {
    std::vector<double> p0, p1;
    buildCointegratedPair(30, 0.01, 0.002, p0, p1);
    CointPairsConfig c = cfg0();
    c.zWindow = 12;                       // > adfWindow = 8
    const auto r = CointPairsBacktester::fromAxis(c, axisPair(dates(30), p0, p1)).run();
    EXPECT_TRUE(r.trades.empty());
    EXPECT_DOUBLE_EQ(r.finalValue, 10'000.0);
}

// ════════════════════════════════════════════════════════════
//  GATE FERMÉ : jambe 0 tendancielle vs jambe 1 stationnaire → le résidu garde
//  la tendance (jamais stationnaire au sens DF) → AUCUNE entrée, équité plate,
//  pctBarsCointegrated = 0. C'est la correction structurelle de D49 : quand la
//  paire n'est pas cointégrée on ne trade PAS (le naïf tradait quand même).
// ════════════════════════════════════════════════════════════
TEST(CointPairsBacktesterUnit, GateClosedOnNonCointegratedPairNeverTrades) {
    std::vector<double> p0, p1;
    for (size_t t = 0; t < 40; ++t) {
        p0.push_back(std::exp(0.01 * static_cast<double>(t)));      // tendance pure
        p1.push_back(std::exp((t % 2 == 0) ? 0.0 : 0.02));          // stationnaire
    }
    const auto r = CointPairsBacktester::fromAxis(cfg0(), axisPair(dates(40), p0, p1)).run();

    EXPECT_TRUE(r.trades.empty());
    EXPECT_EQ(r.roundTripCount, 0);
    EXPECT_DOUBLE_EQ(r.finalValue, 10'000.0);
    EXPECT_DOUBLE_EQ(r.pctBarsCointegrated, 0.0);
    EXPECT_DOUBLE_EQ(r.pctTimeInvested, 0.0);
}

// ════════════════════════════════════════════════════════════
//  GATE OUVERT sur une paire cointégrée construite : l'OLS retrouve β ≈ 2, le
//  résidu alternant passe le DF, le z alterne ±1 → allers-retours réguliers.
//  À coûts nuls, CHAQUE aller-retour gagne (entrée à e = −a, sortie à e = +a :
//  le portefeuille hedgé encaisse ~Δe/(1+β) > 0) et la garde d'activation D47
//  confirme que le gate est resté LARGEMENT ouvert.
// ════════════════════════════════════════════════════════════
TEST(CointPairsBacktesterUnit, GateOpenCointegratedPairTradesAndWins) {
    std::vector<double> p0, p1;
    buildCointegratedPair(32, 0.01, 0.002, p0, p1);
    const auto r = CointPairsBacktester::fromAxis(cfg0(), axisPair(dates(32), p0, p1)).run();

    ASSERT_GE(r.roundTripCount, 3);                    // la famille TRADE (D47)
    EXPECT_GT(r.pctBarsCointegrated, 50.0);            // gate largement ouvert
    for (const auto& t : r.trades) {
        EXPECT_TRUE(t.isWin);                          // réversion encaissée
        EXPECT_DOUBLE_EQ(t.deployedFraction, 1.0);
    }
    EXPECT_GT(r.totalReturnPct, 0.0);
    EXPECT_GT(r.sharpeRatio, 0.0);
}

// ════════════════════════════════════════════════════════════
//  INVARIANT COMPTABLE : la composition des pnl des stints reconstruit l'équité
//  finale (chaque coût appartient à exactement un stint ; les barres à plat ne
//  changent pas l'équité) — vrai aussi à coûts NON nuls.
// ════════════════════════════════════════════════════════════
TEST(CointPairsBacktesterUnit, TradePnlsComposeToFinalEquityWithCosts) {
    std::vector<double> p0, p1;
    buildCointegratedPair(32, 0.01, 0.002, p0, p1);
    CointPairsConfig c = cfg0();
    c.commissionPct = 0.001; c.slippageBps = 2.0; c.halfSpreadBps = 0.5;
    const auto r = CointPairsBacktester::fromAxis(c, axisPair(dates(32), p0, p1)).run();

    ASSERT_GE(r.trades.size(), 2u);
    double composed = 10'000.0;
    for (const auto& t : r.trades) composed *= (1.0 + t.pnlPct / 100.0);
    EXPECT_NEAR(composed, r.finalValue, 1e-6);
}

// ════════════════════════════════════════════════════════════
//  Warmup (D35) : une fenêtre plus courte que max(betaWindow, adfWindow) ne
//  trade jamais (aux défauts 126/126, 30 barres sont très insuffisantes).
// ════════════════════════════════════════════════════════════
TEST(CointPairsBacktesterUnit, RangeShorterThanWarmupNeverTrades) {
    std::vector<double> p0, p1;
    buildCointegratedPair(30, 0.01, 0.002, p0, p1);
    CointPairsConfig c;                   // défauts : betaWindow = adfWindow = 126
    c.commissionPct = 0.0; c.slippageBps = 0.0; c.halfSpreadBps = 0.0;
    const auto r = CointPairsBacktester::fromAxis(c, axisPair(dates(30), p0, p1)).run();
    EXPECT_TRUE(r.trades.empty());
    EXPECT_DOUBLE_EQ(r.totalReturnPct, 0.0);
    EXPECT_DOUBLE_EQ(r.pctBarsCointegrated, 0.0);
}
