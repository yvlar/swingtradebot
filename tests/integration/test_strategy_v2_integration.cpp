// ─── Tests d'intégration : stratégie V2 « suivi de tendance » (Sprints 8-9) ──
// Valide la config déployable `swingTrendConfig` en OUT-OF-SAMPLE sur QQQ.csv :
// sorties refondues (8.2 + 8.4, edge de TIMING) + sizing à fraction fixe 90 %
// (9.0b, capter l'edge en dollars). C'est le test qui matérialise la Definition
// of Done : la stratégie bat le Buy & Hold net de coûts en OOS, au sizing RÉEL
// de déploiement (pas un proxy de pleine exposition).
//
// Deux leviers, deux assertions :
//   - TIMING (sorties) : isolé au sizing conservateur 2 %, l'edge double la prod
//     mais ne bat pas encore le B&H en dollars (l'exposition ~28 %/position bride).
//   - SIZING (9.0b) : l'exposition fixe 90 % capte l'edge → bat le B&H en OOS,
//     drawdown maîtrisé (~4,6 %, le timing contrôle déjà le risque).
//
// ⚠️ Rappel D28 : QQQ.csv n'est pas total-return → B&H sous-estimé. L'alpha OOS
// positif (~+7 pts) garde une marge face à ce biais (~0,55 %/an).
#include <gtest/gtest.h>
#include "backtest/WalkForward.hpp"
#include "config/ProdConfig.hpp"

using namespace trading;

namespace {
WalkForward wf(SwingConfig cfg) {
    return WalkForward(cfg, SWINGBOT_QQQ_CSV, 10'000.0, 0.001, 0.0005);
}
// V2 (sorties refondues) mais SANS le sizing fixe : isole le timing au risk-based 2 %.
SwingConfig v2TimingOnly() {
    SwingConfig cfg = swingTrendConfig();
    cfg.targetExposurePct = 0.0;   // désactive le sizing fixe → risk-based 2 %
    return cfg;
}
} // namespace

// DoD : la config déployable (V2 @ 90 % d'exposition) bat strictement le B&H en OOS.
TEST(StrategyV2Integration, TrendConfigBeatsBuyHoldOutOfSample) {
    auto split = wf(swingTrendConfig()).anchoredSplit(0.7);
    const auto& oos = split.folds[1].result;

    EXPECT_TRUE(oos.beatsBuyHold);
    EXPECT_NEAR(oos.totalReturnPct,   46.54, 0.10);
    EXPECT_NEAR(oos.buyHoldReturnPct, 39.39, 0.10);
    EXPECT_GT(oos.totalReturnPct - oos.buyHoldReturnPct, 5.0);   // alpha > +5 pts

    // Robustesse : la majorité des segments roulants battent le B&H (3/4)
    EXPECT_GE(wf(swingTrendConfig()).rolling(4).oosBeatBuyHold, 3);
}

// Drawdown maîtrisé au sizing 90 % : le timing (sorties au trailing, ~56 % investi)
// contrôle déjà le risque → maxDD OOS bien inférieur à un B&H tout-investi.
TEST(StrategyV2Integration, TrendConfigDrawdownStaysControlledAt90Pct) {
    auto oos = wf(swingTrendConfig()).anchoredSplit(0.7).folds[1].result;
    EXPECT_LT(oos.maxDrawdownPct, 6.0);
    EXPECT_NEAR(oos.maxDrawdownPct, 4.61, 0.20);
}

// Acceptation de l'item 9.0 : la config de PROD (celle que main_ibkr.cpp injecte
// via ibkrProdConfig) EST la V2 — sorties refondues + sizing 90 %. Ce test scelle
// le déploiement : si quelqu'un fait diverger la prod de la config validée OOS,
// il échoue.
TEST(StrategyV2Integration, ProdConfigIsTheV2) {
    const SwingConfig prod = ibkrProdConfig();
    const SwingConfig v2   = swingTrendConfig();
    EXPECT_DOUBLE_EQ(prod.takeProfitPct,     v2.takeProfitPct);      // 0 (8.2)
    EXPECT_EQ       (prod.sellOnRsiOverbought, v2.sellOnRsiOverbought); // false (8.4)
    EXPECT_DOUBLE_EQ(prod.targetExposurePct, v2.targetExposurePct);  // 0,90 (9.0b)
    EXPECT_EQ       (prod.emaFast,  v2.emaFast);
    EXPECT_EQ       (prod.emaSlow,  v2.emaSlow);
    EXPECT_DOUBLE_EQ(prod.targetExposurePct, 0.90);
    EXPECT_DOUBLE_EQ(prod.takeProfitPct,     0.0);
    EXPECT_FALSE    (prod.sellOnRsiOverbought);
}

// Le TIMING seul (sorties refondues, sizing 2 %) améliore nettement la V1…
TEST(StrategyV2Integration, TimingAloneImprovesOnLegacyAtConservativeSizing) {
    auto legacyOos = wf(legacyProdConfig()).anchoredSplit(0.7).folds[1].result;
    auto v2Oos     = wf(v2TimingOnly()).anchoredSplit(0.7).folds[1].result;

    EXPECT_GT(v2Oos.totalReturnPct, legacyOos.totalReturnPct * 1.5);
    EXPECT_NEAR(v2Oos.totalReturnPct, 13.21, 0.10);
}

// …mais à 2 % de risque, l'edge de timing ne bat PAS encore le B&H en dollars :
// c'est l'écart de SIZING que 9.0b (exposition 90 %) referme — pas un défaut du timing.
TEST(StrategyV2Integration, TimingAloneStillBelowBuyHoldWithoutSizing) {
    auto v2Oos = wf(v2TimingOnly()).anchoredSplit(0.7).folds[1].result;
    EXPECT_FALSE(v2Oos.beatsBuyHold);
    EXPECT_LT(v2Oos.totalReturnPct, v2Oos.buyHoldReturnPct);
}
