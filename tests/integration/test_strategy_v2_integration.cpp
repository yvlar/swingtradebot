// ─── Tests d'intégration : stratégie V2 « suivi de tendance » (Sprint 8) ─────
// Valide la refonte des SORTIES (items 8.2 + 8.4, config swingTrendConfig) en
// OUT-OF-SAMPLE sur QQQ.csv. C'est le test qui matérialise la Definition of Done
// du Sprint 8 : à exposition comparable au Buy & Hold, l'edge de TIMING bat le
// B&H net de coûts en OOS.
//
// Deux régimes d'exposition (décision « timing d'abord, sizing au Sprint 9 ») :
//   - pleine exposition (riskPerTradePct=1.0, capé 95 % par le RiskManager) :
//     isole le timing → la V2 DOIT battre le B&H en OOS (DoD).
//   - sizing prod 2 % : l'edge de timing ne se traduit pas encore en dollars
//     battant le B&H (l'exposition ~28 %/position le bride) — documenté, c'est
//     l'objet du Sprint 9. La V2 améliore tout de même nettement la prod.
//
// ⚠️ Rappel D28 : QQQ.csv n'est pas total-return → B&H sous-estimé. L'alpha OOS
// positif (~+9 pts) garde une marge confortable face à ce biais (~0,55 %/an).
#include <gtest/gtest.h>
#include "backtest/WalkForward.hpp"
#include "config/ProdConfig.hpp"

using namespace trading;

namespace {
WalkForward wf(SwingConfig cfg) {
    return WalkForward(cfg, SWINGBOT_QQQ_CSV, 10'000.0, 0.001, 0.0005);
}
} // namespace

// DoD du Sprint 8 : à pleine exposition, la V2 bat strictement le B&H en OOS.
TEST(StrategyV2Integration, TrendConfigBeatsBuyHoldOutOfSampleAtFullExposure) {
    SwingConfig cfg = swingTrendConfig();
    cfg.riskPerTradePct = 1.0;   // pleine exposition (capée 95 %) → timing isolé

    auto split = wf(cfg).anchoredSplit(0.7);
    const auto& oos = split.folds[1].result;

    EXPECT_TRUE(oos.beatsBuyHold);                         // bat le B&H en OOS
    EXPECT_NEAR(oos.totalReturnPct,   48.73, 0.10);
    EXPECT_NEAR(oos.buyHoldReturnPct, 39.39, 0.10);
    EXPECT_GT(oos.totalReturnPct - oos.buyHoldReturnPct, 5.0);  // alpha > +5 pts

    // Robustesse : la majorité des segments roulants battent le B&H (3/4)
    EXPECT_GE(wf(cfg).rolling(4).oosBeatBuyHold, 3);
}

// La refonte des sorties améliore nettement la prod même au sizing 2 %…
TEST(StrategyV2Integration, TrendConfigImprovesOnProdAtConservativeSizing) {
    auto prodOos = wf(ibkrProdConfig()).anchoredSplit(0.7).folds[1].result;
    auto v2Oos   = wf(swingTrendConfig()).anchoredSplit(0.7).folds[1].result;

    EXPECT_GT(v2Oos.totalReturnPct, prodOos.totalReturnPct * 1.5);  // nettement mieux
    EXPECT_NEAR(v2Oos.totalReturnPct, 13.21, 0.10);
}

// …mais à 2 % de risque, l'edge de timing ne bat PAS encore le B&H en dollars :
// c'est l'écart de SIZING, arbitré au Sprint 9 (et non un défaut de la stratégie).
TEST(StrategyV2Integration, TrendConfigStillBelowBuyHoldAtConservativeSizing) {
    auto v2Oos = wf(swingTrendConfig()).anchoredSplit(0.7).folds[1].result;
    EXPECT_FALSE(v2Oos.beatsBuyHold);
    EXPECT_LT(v2Oos.totalReturnPct, v2Oos.buyHoldReturnPct);
}
