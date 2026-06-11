// ─── Tests d'intégration : WalkForward — témoin D30 archivé (item 7.1) ───────
// Fige le comportement hors-échantillon de la config HÉRITÉE V1 (legacyProdConfig)
// sur QQQ.csv (commission 0,1 % + slippage 5 bps). C'était le verdict fondateur
// D30 : « l'edge in-sample ne tient pas en OOS » — RENVERSÉ par la V2 au Sprint 8
// (le verdict de la prod actuelle est figé dans test_strategy_v2_integration :
// +46,5 % vs B&H +39,4 % en OOS). Conservé comme témoin : si ces chiffres
// bougent, c'est le harnais lui-même (pas la stratégie) qui a dérivé.
//
// ⚠️ Rappel D28 : QQQ.csv n'est pas total-return (Close == Adj Close) — le B&H
// réel serait encore plus haut, donc l'écart OOS est SOUS-estimé. Le verdict
// « la stratégie perd en OOS » est donc conservateur.
#include <gtest/gtest.h>
#include "backtest/WalkForward.hpp"
#include "config/ProdConfig.hpp"

using namespace trading;

namespace {
WalkForward legacyWalkForward() {
    return WalkForward(legacyProdConfig(), SWINGBOT_QQQ_CSV, 10'000.0, 0.001, 0.0005);
}
} // namespace

// Split ancré 70/30 : la config V1 sous-performe massivement le Buy & Hold
// sur la période RÉCENTE qu'elle n'a pas servi à se calibrer.
TEST(WalkForwardIntegration, AnchoredSplitLegacyConfigLosesOutOfSample) {
    auto r = legacyWalkForward().anchoredSplit(0.7);
    ASSERT_EQ(r.folds.size(), 2u);

    const auto& is  = r.folds[0].result;
    const auto& oos = r.folds[1].result;

    EXPECT_EQ(r.folds[0].label, "IS");
    EXPECT_EQ(r.folds[1].label, "OOS");

    // In-sample : déjà sous le B&H (28,13 % vs 126,73 %)
    EXPECT_NEAR(is.totalReturnPct,   28.13, 0.05);
    EXPECT_NEAR(is.buyHoldReturnPct, 126.73, 0.05);
    EXPECT_FALSE(is.beatsBuyHold);

    // Out-of-sample : le verdict qui compte — +6,56 % vs +39,39 % de B&H
    EXPECT_NEAR(oos.totalReturnPct,   6.56,  0.05);
    EXPECT_NEAR(oos.buyHoldReturnPct, 39.39, 0.05);
    EXPECT_EQ  (oos.totalTrades, 3);
    EXPECT_FALSE(oos.beatsBuyHold);
    EXPECT_EQ(r.oosBeatBuyHold, 0);
}

// Walk-forward roulant 4 segments : un seul segment bat le B&H, et c'est le
// segment BAISSIER (2020-10 → 2022-07, B&H négatif) — la stratégie « gagne » en
// étant hors marché, pas en captant de la hausse. Aucun edge directionnel.
TEST(WalkForwardIntegration, RollingLegacyConfigBeatsBuyHoldOnlyInBearSegment) {
    auto r = legacyWalkForward().rolling(4);
    ASSERT_EQ(r.folds.size(), 4u);
    EXPECT_EQ(r.oosCount, 4);

    // Exactement 1 segment sur 4 bat le B&H
    EXPECT_EQ(r.oosBeatBuyHold, 1);

    // Et c'est le 2e (le seul où le B&H est négatif)
    EXPECT_TRUE (r.folds[1].result.beatsBuyHold);
    EXPECT_LT   (r.folds[1].result.buyHoldReturnPct, 0.0);
    EXPECT_FALSE(r.folds[0].result.beatsBuyHold);
    EXPECT_FALSE(r.folds[2].result.beatsBuyHold);
    EXPECT_FALSE(r.folds[3].result.beatsBuyHold);

    // Dans les 3 segments haussiers, la stratégie laisse énormément sur la table
    EXPECT_GT(r.folds[2].result.buyHoldReturnPct, 50.0);   // WF#3 : B&H +81 %
    EXPECT_LT(r.folds[2].result.totalReturnPct,   15.0);   // stratégie < +15 %
}
