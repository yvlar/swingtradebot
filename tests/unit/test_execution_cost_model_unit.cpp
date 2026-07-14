// ============================================================
//  test_execution_cost_model_unit.cpp  —  Tests UNITAIRES
//  Cible : ExecutionCostConfig (Sprint 23, item 23.3) + chemin
//  « modèle de coûts » du PaperBroker.
//  Chaque attendu est un CALCUL MANUEL au centime — pas de valeur
//  recopiée depuis une exécution.
// ============================================================
#include <gtest/gtest.h>
#include "backtest/ExecutionCostModel.hpp"
#include "brokers/PaperBroker.hpp"

using namespace trading;

// ════════════════════════════════════════════════════════════
//  commissionForOrder — composition de la commission
// ════════════════════════════════════════════════════════════

// Commission proportionnelle pure : 0,1 % de 10 × 100 $ = 1,00 $
TEST(ExecutionCostModelUnit, PctCommissionMatchesManualComputation) {
    ExecutionCostConfig c;
    c.commissionPct = 0.001;
    EXPECT_NEAR(c.commissionForOrder(100.0, 10), 1.00, 1e-9);
}

// Commission par action : 100 actions × 0,005 $ = 0,50 $
TEST(ExecutionCostModelUnit, PerShareCommissionMatchesManualComputation) {
    ExecutionCostConfig c;
    c.commissionPerShare = 0.005;
    EXPECT_NEAR(c.commissionForOrder(50.0, 100), 0.50, 1e-9);
}

// Le MINIMUM par ordre domine une petite transaction (cœur du 23.3) :
// 1 action × 200 $ → commission brute 0,005 $, minimum 1,00 $ → 1,00 $
// = 0,5 % du notionnel sur UN SEUL côté.
TEST(ExecutionCostModelUnit, MinimumCommissionDominatesSmallOrder) {
    ExecutionCostConfig c;
    c.commissionPerShare        = 0.005;
    c.minimumCommissionPerOrder = 1.00;
    EXPECT_NEAR(c.commissionForOrder(200.0, 1), 1.00, 1e-9);
    // Gros ordre : le minimum ne joue plus — 500 actions × 0,005 = 2,50 $
    EXPECT_NEAR(c.commissionForOrder(200.0, 500), 2.50, 1e-9);
}

// Le PLAFOND par ordre borne la commission proportionnelle :
// 0,1 % de 10 000 $ = 10 $ → plafonné à 0,50 $
TEST(ExecutionCostModelUnit, MaximumCommissionCapsLargeOrder) {
    ExecutionCostConfig c;
    c.commissionPct             = 0.001;
    c.maximumCommissionPerOrder = 0.50;
    EXPECT_NEAR(c.commissionForOrder(1'000.0, 10), 0.50, 1e-9);
}

// Frais réglementaires HORS clamp : min 1,00 $ + fixe 0,02 $ + 0,01 bp
// de 100 $ = 1,00 + 0,02 + 0,0001 = 1,0201 $ (le minimum ne les absorbe pas)
TEST(ExecutionCostModelUnit, RegulatoryFeesAreAddedOutsideClamp) {
    ExecutionCostConfig c;
    c.commissionPerShare        = 0.005;
    c.minimumCommissionPerOrder = 1.00;
    c.regulatoryFeeFixed        = 0.02;
    c.regulatoryFeePct          = 0.000001;   // 0,0001 % de la valeur
    EXPECT_NEAR(c.commissionForOrder(100.0, 1), 1.00 + 0.02 + 0.0001, 1e-9);
}

// Quantité ou prix non positifs → 0 (aucun frais fantôme)
TEST(ExecutionCostModelUnit, ZeroForNonPositiveInputs) {
    auto c = ExecutionCostConfig::stressPetitCompte();
    EXPECT_DOUBLE_EQ(c.commissionForOrder(100.0, 0.0), 0.0);
    EXPECT_DOUBLE_EQ(c.commissionForOrder(0.0, 10.0), 0.0);
    EXPECT_DOUBLE_EQ(c.commissionForOrder(-5.0, 10.0), 0.0);
}

// Pénalité de fill : 2 bps + 0,5 bp = 2,5 bps = 0,00025 — même définition
// que le fillPenaltyPct_ historique du PaperBroker
TEST(ExecutionCostModelUnit, FillPenaltyFractionMatchesHistoricalDefinition) {
    ExecutionCostConfig c;
    c.slippageBps   = 2.0;
    c.halfSpreadBps = 0.5;
    EXPECT_NEAR(c.fillPenaltyFraction(), 0.00025, 1e-12);
}

// Le scénario A (historique conservateur) porte EXACTEMENT les valeurs du
// modèle historique (Sprint 6.2/D22) : 0,1 %, 2 bps, 0,5 bp, aucun minimum
TEST(ExecutionCostModelUnit, ScenarioAHasExactHistoricalValues) {
    const auto a = ExecutionCostConfig::historiqueConservateur();
    EXPECT_DOUBLE_EQ(a.commissionPct, 0.001);
    EXPECT_DOUBLE_EQ(a.slippageBps,   2.0);
    EXPECT_DOUBLE_EQ(a.halfSpreadBps, 0.5);
    EXPECT_DOUBLE_EQ(a.commissionPerShare,        0.0);
    EXPECT_DOUBLE_EQ(a.minimumCommissionPerOrder, 0.0);
    EXPECT_DOUBLE_EQ(a.maximumCommissionPerOrder, 0.0);
    EXPECT_DOUBLE_EQ(a.regulatoryFeeFixed,        0.0);
    EXPECT_DOUBLE_EQ(a.regulatoryFeePct,          0.0);
}

// Le scénario B n'a PAS de minimum ; le scénario C en a un (hypothèse de
// stress documentée, pas un tarif réel)
TEST(ExecutionCostModelUnit, ScenarioBHasNoMinimumScenarioCHasOne) {
    EXPECT_DOUBLE_EQ(ExecutionCostConfig::faibleCommission()
                         .minimumCommissionPerOrder, 0.0);
    EXPECT_GT(ExecutionCostConfig::stressPetitCompte()
                  .minimumCommissionPerOrder, 0.0);
}

// ════════════════════════════════════════════════════════════
//  PaperBroker — chemin « modèle de coûts »
// ════════════════════════════════════════════════════════════

namespace {

// Broker sur le chemin modèle de coûts, sans slippage pour des calculs propres
PaperBroker brokerAvecCouts(double capital, ExecutionCostConfig c,
                            double prix = 100.0) {
    c.slippageBps = 0.0;
    c.halfSpreadBps = 0.0;
    PaperBroker b(capital, c);
    b.setCurrentPrice(prix);
    b.setCurrentDate("2024-01-02");
    return b;
}

} // namespace

// Achat ET vente paient chacun leur commission (minimum 1 $ des deux côtés) :
//   achat  : 2 × 100 $ + 1 $ = 201 $ → cash 799 $
//   vente  : 2 × 110 $ − 1 $ = 219 $ → cash 1 018 $
//   P&L    : (110 − 100) × 2 − 1 − 1 = 18 $ (net des DEUX côtés)
//   frais  : 2,00 $
TEST(ExecutionCostModelUnit, BuyAndSellBothPayTheirCommission) {
    ExecutionCostConfig c;
    c.minimumCommissionPerOrder = 1.00;
    auto b = brokerAvecCouts(1'000.0, c);

    ASSERT_TRUE(b.submitBuy("QQQ", 2).has_value());
    EXPECT_NEAR(b.cash(), 799.0, 1e-9);

    b.setCurrentPrice(110.0);
    ASSERT_TRUE(b.submitSell("QQQ", 2).has_value());
    EXPECT_NEAR(b.cash(), 1'018.0, 1e-9);

    ASSERT_EQ(b.trades().size(), 1u);
    EXPECT_NEAR(b.trades().front().pnl, 18.0, 1e-9);
    EXPECT_NEAR(b.totalFees(), 2.0, 1e-9);
}

// La commission minimale peut transformer un aller-retour gagnant en perte :
// 1 action, +1 $ de mouvement, 2 × 1 $ de minimum → P&L = 1 − 2 = −1 $
TEST(ExecutionCostModelUnit, MinimumCommissionTurnsSmallWinIntoLoss) {
    ExecutionCostConfig c;
    c.minimumCommissionPerOrder = 1.00;
    auto b = brokerAvecCouts(1'000.0, c, 200.0);

    ASSERT_TRUE(b.submitBuy("QQQ", 1).has_value());
    b.setCurrentPrice(201.0);
    ASSERT_TRUE(b.submitSell("QQQ", 1).has_value());

    ASSERT_EQ(b.trades().size(), 1u);
    EXPECT_NEAR(b.trades().front().pnl, -1.0, 1e-9);
    EXPECT_FALSE(b.trades().front().isWin);
}

// Cash insuffisant : la réduction de quantité tient compte du coût TOTAL
// avec minimum — 10 × 100 + 1 = 1 001 > 1 000 → 9 × 100 + 1 = 901 ≤ 1 000
TEST(ExecutionCostModelUnit, QtyShrinkAccountsForMinimumCommission) {
    ExecutionCostConfig c;
    c.minimumCommissionPerOrder = 1.00;
    auto b = brokerAvecCouts(1'000.0, c);

    auto o = b.submitBuy("QQQ", 15);
    ASSERT_TRUE(o.has_value());
    EXPECT_EQ(o->quantity, 9);
    EXPECT_NEAR(b.cash(), 1'000.0 - 901.0, 1e-9);
}

// Impossible de payer une seule action + minimum → aucun ordre, cash intact
TEST(ExecutionCostModelUnit, NulloptWhenMinimumMakesOneShareUnaffordable) {
    ExecutionCostConfig c;
    c.minimumCommissionPerOrder = 1.00;
    auto b = brokerAvecCouts(100.5, c);      // 1 × 100 + 1 = 101 > 100,50

    EXPECT_FALSE(b.submitBuy("QQQ", 1).has_value());
    EXPECT_DOUBLE_EQ(b.cash(), 100.5);
    EXPECT_FALSE(b.inPosition());
}

// Le chemin HISTORIQUE cumule aussi les frais (compteur seul, aucune
// modification de l'arithmétique) : achat 10 @ 100 + 0,1 % → 1,00 $ ;
// vente 10 @ 110 → 1,10 $ ; total 2,10 $ — et le cash reste celui des
// tests historiques (10 097,90 $)
TEST(ExecutionCostModelUnit, HistoricalPathAccumulatesFeesWithoutChangingCash) {
    PaperBroker b(10'000.0, 0.001);
    b.setCurrentPrice(100.0);
    b.setCurrentDate("2024-01-02");
    b.submitBuy("QQQ", 10);
    b.setCurrentPrice(110.0);
    b.submitSell("QQQ", 10);

    EXPECT_NEAR(b.cash(), 10'097.9, 1e-9);
    EXPECT_NEAR(b.totalFees(), 2.10, 1e-9);
}

// Équivalence scénario A ↔ chemin historique : mêmes fills, même cash au
// centime près (tolérance flottante 1e-6 — les formules diffèrent d'un
// facteur d'associativité, pas de comportement)
TEST(ExecutionCostModelUnit, ScenarioAMatchesHistoricalPathOnRoundTrip) {
    PaperBroker historique(10'000.0, 0.001, 2.0, 0.5);
    PaperBroker scenarioA(10'000.0, ExecutionCostConfig::historiqueConservateur());

    for (PaperBroker* b : {&historique, &scenarioA}) {
        b->setCurrentPrice(432.10);
        b->setCurrentDate("2024-01-02");
        b->submitBuy("QQQ", 9);
        b->setCurrentPrice(447.65);
        b->setCurrentDate("2024-01-09");
        b->submitSell("QQQ", 9);
    }

    ASSERT_EQ(historique.trades().size(), 1u);
    ASSERT_EQ(scenarioA.trades().size(), 1u);
    EXPECT_NEAR(scenarioA.cash(), historique.cash(), 1e-6);
    EXPECT_NEAR(scenarioA.trades().front().buyPrice,
                historique.trades().front().buyPrice, 1e-9);
    EXPECT_NEAR(scenarioA.trades().front().sellPrice,
                historique.trades().front().sellPrice, 1e-9);
}
