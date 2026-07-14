// ============================================================
//  test_net_buy_hold_unit.cpp  —  Tests UNITAIRES
//  Cible : computeNetBuyHold (Sprint 23, item 23.4)
//  Benchmark buy-and-hold BRUT vs NET : coûts des deux côtés,
//  contrainte d'action entière, cash résiduel — chaque attendu
//  est vérifiable à la main.
// ============================================================
#include <gtest/gtest.h>
#include "backtest/NetBuyHold.hpp"

using namespace trading;

namespace {

// Série synthétique minimale : bars[warmup] = close d'entrée, back() = sortie
std::vector<Bar> serie(double entryClose, double exitClose, int warmup = 1) {
    std::vector<Bar> bars;
    for (int i = 0; i <= warmup + 1; ++i) {
        Bar b;
        b.date  = "2024-01-0" + std::to_string(i + 1);
        b.close = (i == warmup) ? entryClose
                : (i == warmup + 1) ? exitClose
                : entryClose;
        b.open = b.high = b.low = b.close;
        b.volume = 1'000;
        bars.push_back(b);
    }
    return bars;
}

} // namespace

// SANS coûts, quantité fractionnaire : net == brut (contrôle de cohérence).
// 100 → 110 : brut +10 %, net +10 % (tout le capital travaille).
TEST(NetBuyHoldUnit, WithoutCostsFractionalNetEqualsGross) {
    const auto r = computeNetBuyHold(serie(100.0, 110.0), 1, 1'000.0,
                                     ExecutionCostConfig{}, /*whole=*/false);
    EXPECT_NEAR(r.grossReturnPct, 10.0, 1e-9);
    EXPECT_NEAR(r.netReturnPct,   10.0, 1e-6);
    EXPECT_NEAR(r.totalFees,       0.0, 1e-9);
}

// ACTIONS ENTIÈRES sans coûts : 1 000 $ à 300 $ → 3 actions, 100 $ de cash
// résiduel. 300 → 330 (+10 % brut) : valeur finale 100 + 3 × 330 = 1 090 $
// → net +9 % — le cash résiduel DILUE le rendement, c'est le point du 23.4.
TEST(NetBuyHoldUnit, ResidualCashDilutesWholeShareReturn) {
    const auto r = computeNetBuyHold(serie(300.0, 330.0), 1, 1'000.0,
                                     ExecutionCostConfig{}, /*whole=*/true);
    EXPECT_NEAR(r.grossReturnPct, 10.0, 1e-9);
    EXPECT_DOUBLE_EQ(r.sharesBought, 3.0);
    EXPECT_NEAR(r.residualCash, 100.0, 1e-9);
    EXPECT_NEAR(r.finalValue, 1'090.0, 1e-9);
    EXPECT_NEAR(r.netReturnPct, 9.0, 1e-9);
}

// COÛTS COMPLETS au centime (actions entières) :
//   modèle : commission 0,1 %, minimum 1 $, pénalité 2,5 bps
//   achat  : close 200 → fill 200,05 ; 4 actions = 800,20 $,
//            commission max(0,8002 ; 1) = 1 $ → coût 801,20 $, résiduel 198,80 $
//   vente  : close 220 → fill 219,945 ; produit 879,78 − commission 1 $
//            (max(0,87978 ; 1)) = 878,78 $
//   final  : 198,80 + 878,78 = 1 077,58 $ → net +7,758 %
TEST(NetBuyHoldUnit, FullCostModelMatchesManualComputationToTheCent) {
    ExecutionCostConfig c;
    c.commissionPct             = 0.001;
    c.minimumCommissionPerOrder = 1.0;
    c.slippageBps               = 2.0;
    c.halfSpreadBps             = 0.5;
    const auto r = computeNetBuyHold(serie(200.0, 220.0), 1, 1'000.0, c, true);

    EXPECT_DOUBLE_EQ(r.sharesBought, 4.0);
    EXPECT_NEAR(r.residualCash, 1'000.0 - 4 * 200.05 - 1.0, 1e-9);
    EXPECT_NEAR(r.totalFees, 2.0, 1e-9);                 // 1 $ par côté (minimum)
    const double venteNette = 4 * (220.0 * (1.0 - 0.00025)) - 1.0;
    EXPECT_NEAR(r.finalValue, r.residualCash + venteNette, 1e-9);
    EXPECT_NEAR(r.netReturnPct, (r.finalValue - 1'000.0) / 10.0, 1e-9);
    EXPECT_LT(r.netReturnPct, r.grossReturnPct);         // le net est bien NET
}

// Prix au-dessus du capital : AUCUNE action entière finançable → tout reste
// en cash, net = 0 % (et pas un crash) — cas réel du compte 1 000 $ sur un
// titre à 1 200 $.
TEST(NetBuyHoldUnit, UnaffordableShareLeavesEverythingInCash) {
    const auto r = computeNetBuyHold(serie(1'200.0, 1'500.0), 1, 1'000.0,
                                     ExecutionCostConfig{}, /*whole=*/true);
    EXPECT_DOUBLE_EQ(r.sharesBought, 0.0);
    EXPECT_NEAR(r.residualCash, 1'000.0, 1e-9);
    EXPECT_NEAR(r.finalValue,   1'000.0, 1e-9);
    EXPECT_NEAR(r.netReturnPct, 0.0, 1e-9);
    EXPECT_GT(r.grossReturnPct, 0.0);   // le brut, lui, monte de +25 %
}

// La quantité entière respecte le budget quand le minimum de commission
// ferait déborder : 1 000 $ à 199,90 $ → 5 actions = 999,50 $ MAIS
// commission minimum 1 $ → 1 000,50 $ > capital → 4 actions.
TEST(NetBuyHoldUnit, MinimumCommissionReducesAffordableWholeShares) {
    ExecutionCostConfig c;
    c.minimumCommissionPerOrder = 1.0;
    const auto r = computeNetBuyHold(serie(199.90, 210.0), 1, 1'000.0, c, true);
    EXPECT_DOUBLE_EQ(r.sharesBought, 4.0);
}

// Fenêtre invalide ou capital nul → résultat neutre, jamais d'UB
TEST(NetBuyHoldUnit, DegenerateInputsGiveNeutralResult) {
    const auto r1 = computeNetBuyHold({}, 0, 1'000.0, ExecutionCostConfig{}, true);
    EXPECT_DOUBLE_EQ(r1.netReturnPct, 0.0);
    const auto r2 = computeNetBuyHold(serie(100, 110), 1, 0.0,
                                      ExecutionCostConfig{}, true);
    EXPECT_DOUBLE_EQ(r2.netReturnPct, 0.0);
}

// Le même capital initial sert au brut et au net : la comparaison
// stratégie-vs-benchmark reste cohérente (acceptation 23.4)
TEST(NetBuyHoldUnit, NetUsesSameInitialCapitalAsStrategy) {
    const double capital = 1'000.0;
    const auto r = computeNetBuyHold(serie(250.0, 275.0), 1, capital,
                                     ExecutionCostConfig::historiqueConservateur(),
                                     true);
    // 4 actions à 250,0625 (fill) + 1,0003 $ de commission ≈ 1 001,25 $ > 1 000
    // → 3 actions ; vérifie seulement l'invariant de base : valeur finale =
    // résiduel + produit net, rendement rapporté au capital INITIAL.
    EXPECT_NEAR(r.netReturnPct,
                (r.finalValue - capital) / capital * 100.0, 1e-12);
}
