// ============================================================
//  test_risk_manager_unit.cpp  —  Tests UNITAIRES
//  Cible : RiskManager — sizing, autorisation, conditions de sortie
// ============================================================
#include <gtest/gtest.h>
#include "bot/RiskManager.hpp"

using namespace trading;

namespace {

    Account account(double cash, const std::string& status = "ACTIVE") {
        return {cash, cash, status};
    }

    Position position(int shares = 10, double avg = 420.0) {
        Position p;
        p.symbol   = "QQQ";
        p.shares   = shares;
        p.avgPrice = avg;
        return p;
    }

} // namespace

// ════════════════════════════════════════════════════════════
//  positionSize — sizing par risque dollar
// ════════════════════════════════════════════════════════════

// Exemple documenté : capital 10 000 $, risque 2 %, prix 420 $, stop 5 %
// → 200 $ de risque / 21 $ par action = 9 actions
TEST(RiskManagerUnit, PositionSizeNominalCase) {
    RiskManager rm;
    EXPECT_EQ(rm.positionSize(10'000.0, 420.0, 0.05, 0.02), 9);
}

// BUG Sprint 1 item 5 : max(1, shares) forçait l'achat d'1 action
// même quand le cash ne couvre pas le prix d'une seule action.
TEST(RiskManagerUnit, PositionSizeReturnsZeroWhenCashCannotAffordOneShare) {
    RiskManager rm;
    // 100 $ de cash, action à 420 $ → impossible d'acheter quoi que ce soit
    EXPECT_EQ(rm.positionSize(100.0, 420.0, 0.05, 0.02), 0);
}

// BUG Sprint 1 item 5 : budget de risque < 1 action → doit retourner 0, pas 1
TEST(RiskManagerUnit, PositionSizeReturnsZeroWhenRiskBudgetBelowOneShare) {
    RiskManager rm;
    // 500 $ de capital, risque 1 % = 5 $ ; risque/action = 100 × 10 % = 10 $ → 0 action
    EXPECT_EQ(rm.positionSize(500.0, 100.0, 0.10, 0.01), 0);
}

TEST(RiskManagerUnit, PositionSizeZeroOnInvalidInputs) {
    RiskManager rm;
    EXPECT_EQ(rm.positionSize(-1000.0, 420.0, 0.05, 0.02), 0);
    EXPECT_EQ(rm.positionSize(10'000.0,  -1.0, 0.05, 0.02), 0);
    EXPECT_EQ(rm.positionSize(10'000.0, 420.0,  0.0, 0.02), 0);
    EXPECT_EQ(rm.positionSize(10'000.0, 420.0, 0.05,  0.0), 0);
}

// Le sizing par risque ne doit jamais dépasser le capital utilisable
TEST(RiskManagerUnit, PositionSizeCappedByAvailableCapital) {
    RiskManager rm;
    // Risque énorme : 500 $ / 1 $ par action = 500 actions en théorie,
    // mais 95 % de 1 000 $ ne paie que 9 actions à 100 $
    EXPECT_EQ(rm.positionSize(1'000.0, 100.0, 0.01, 0.50), 9);
}

// ════════════════════════════════════════════════════════════
//  isTradeAllowed — autorisation (dont coût total ≤ cash)
// ════════════════════════════════════════════════════════════

TEST(RiskManagerUnit, IsTradeAllowedAcceptsAffordableTrade) {
    RiskManager rm;
    EXPECT_TRUE(rm.isTradeAllowed(account(10'000.0), std::nullopt, 420.0, 9));
}

TEST(RiskManagerUnit, IsTradeAllowedRejectsWhenAlreadyInPosition) {
    RiskManager rm;
    EXPECT_FALSE(rm.isTradeAllowed(account(10'000.0), position(), 420.0, 9));
}

TEST(RiskManagerUnit, IsTradeAllowedRejectsInactiveAccount) {
    RiskManager rm;
    EXPECT_FALSE(rm.isTradeAllowed(account(10'000.0, "SUSPENDED"), std::nullopt, 420.0, 9));
}

// Sprint 1 item 5 : le coût total de l'ordre doit tenir dans le cash
TEST(RiskManagerUnit, IsTradeAllowedRejectsWhenTotalCostExceedsCash) {
    RiskManager rm;
    // 5 × 420 $ = 2 100 $ > 1 000 $ de cash
    EXPECT_FALSE(rm.isTradeAllowed(account(1'000.0), std::nullopt, 420.0, 5));
}

TEST(RiskManagerUnit, IsTradeAllowedRejectsZeroOrNegativeQty) {
    RiskManager rm;
    EXPECT_FALSE(rm.isTradeAllowed(account(10'000.0), std::nullopt, 420.0, 0));
    EXPECT_FALSE(rm.isTradeAllowed(account(10'000.0), std::nullopt, 420.0, -3));
}

// ════════════════════════════════════════════════════════════
//  checkExitConditions — priorités de sortie (régression)
// ════════════════════════════════════════════════════════════

TEST(RiskManagerUnit, ExitOnStopLoss) {
    RiskManager rm;
    auto r = rm.checkExitConditions(370.0, 400.0, 1, 405.0, 0.05, 0.10, 0.03, 3);
    ASSERT_TRUE(r.has_value());
    EXPECT_NE(r->find("stop-loss"), std::string::npos);
}

TEST(RiskManagerUnit, ExitOnTakeProfit) {
    RiskManager rm;
    auto r = rm.checkExitConditions(444.0, 400.0, 1, 444.0, 0.05, 0.10, 0.03, 3);
    ASSERT_TRUE(r.has_value());
    EXPECT_NE(r->find("take-profit"), std::string::npos);
}

TEST(RiskManagerUnit, TrailingStopOnlyAfterMinHoldDays) {
    RiskManager rm;
    // -4 % depuis le pic mais P&L entre stop et TP → seul le trailing peut sortir
    auto before = rm.checkExitConditions(403.0, 400.0, 2, 420.0, 0.05, 0.10, 0.03, 3);
    EXPECT_FALSE(before.has_value());
    auto after  = rm.checkExitConditions(403.0, 400.0, 3, 420.0, 0.05, 0.10, 0.03, 3);
    ASSERT_TRUE(after.has_value());
    EXPECT_NE(after->find("trailing"), std::string::npos);
}

TEST(RiskManagerUnit, NoExitWhenWithinBands) {
    RiskManager rm;
    EXPECT_FALSE(rm.checkExitConditions(404.0, 400.0, 1, 404.0,
                                        0.05, 0.10, 0.03, 3).has_value());
}

TEST(RiskManagerUnit, NoExitWhenBuyPriceInvalid) {
    RiskManager rm;
    EXPECT_FALSE(rm.checkExitConditions(404.0, 0.0, 1, 404.0,
                                        0.05, 0.10, 0.03, 3).has_value());
}

// Borne complémentaire : un buyPrice négatif (état corrompu) ne doit pas
// produire de P&L absurde ni de sortie (RiskManager.hpp:72, buyPrice<=0)
TEST(RiskManagerUnit, NoExitWhenBuyPriceNegative) {
    RiskManager rm;
    EXPECT_FALSE(rm.checkExitConditions(404.0, -10.0, 5, 410.0,
                                        0.05, 0.10, 0.03, 3).has_value());
}

// ── Priorité : stop-loss > take-profit > trailing ──
// L'ordre des `if` (RiskManager.hpp:77-89) est une garantie : quand plusieurs
// conditions sont vraies simultanément, la plus prioritaire l'emporte. Un
// réordonnancement (régression) ferait remonter la mauvaise raison.

// -7,5 % sous le prix d'achat (stop-loss) ET -17,8 % sous le pic (trailing) :
// les deux conditions sont vraies → le stop-loss, vérifié en premier, gagne.
TEST(RiskManagerUnit, StopLossTakesPriorityOverTrailing) {
    RiskManager rm;
    auto r = rm.checkExitConditions(370.0, 400.0, 5, 450.0, 0.05, 0.10, 0.03, 3);
    ASSERT_TRUE(r.has_value());
    EXPECT_NE(r->find("stop-loss"), std::string::npos);
    EXPECT_EQ(r->find("trailing"), std::string::npos);
}

// +6,25 % depuis l'achat (take-profit à 5 %) ET -5,6 % depuis le pic
// (trailing) : les deux conditions sont vraies → le take-profit gagne.
TEST(RiskManagerUnit, TakeProfitTakesPriorityOverTrailing) {
    RiskManager rm;
    auto r = rm.checkExitConditions(425.0, 400.0, 5, 450.0, 0.05, 0.05, 0.03, 3);
    ASSERT_TRUE(r.has_value());
    EXPECT_NE(r->find("take-profit"), std::string::npos);
    EXPECT_EQ(r->find("trailing"), std::string::npos);
}

// Les stops protègent dès le jour 0 : minHoldDays ne gate QUE le trailing
// (cf. TrailingStopOnlyAfterMinHoldDays), jamais le stop-loss.
TEST(RiskManagerUnit, StopLossFiresRegardlessOfMinHoldDays) {
    RiskManager rm;
    auto r = rm.checkExitConditions(370.0, 400.0, 0, 400.0, 0.05, 0.10, 0.03, 3);
    ASSERT_TRUE(r.has_value());
    EXPECT_NE(r->find("stop-loss"), std::string::npos);
}

// peakPrice=0 (pic jamais renseigné) : la garde `peakPrice > 0`
// (RiskManager.hpp:85) écarte le trailing — pas de drawdown calculé sur un
// pic nul, donc aucune sortie ici (P&L dans les bandes, minHold atteint).
TEST(RiskManagerUnit, TrailingSkippedWhenPeakPriceZero) {
    RiskManager rm;
    EXPECT_FALSE(rm.checkExitConditions(395.0, 400.0, 5, 0.0,
                                        0.05, 0.10, 0.03, 3).has_value());
}

// ════════════════════════════════════════════════════════════
//  Take-profit désactivable (item 8.2, D26)
// ════════════════════════════════════════════════════════════
// Une stratégie de tendance gagne sur les QUEUES : plafonner les gagnants à
// +10 % ampute le ratio gain/risque. Convention : takeProfitPct ≤ 0 désactive
// le take-profit (même convention que les seuils du kill-switch), la sortie
// des gagnants est pilotée par le trailing/structure.

// +15 % depuis l'achat, TP=0 (désactivé) : AUCUNE sortie — la position court.
// Sans la garde, `pnlPct >= 0.0` sortirait en « take-profit » dès +0 %.
TEST(RiskManagerUnit, TakeProfitDisabledWhenZero) {
    RiskManager rm;
    EXPECT_FALSE(rm.checkExitConditions(460.0, 400.0, 1, 460.0,
                                        0.05, 0.0, 0.03, 3).has_value());
}

// Même contrat avec un seuil négatif (config défensive).
TEST(RiskManagerUnit, TakeProfitDisabledWhenNegative) {
    RiskManager rm;
    EXPECT_FALSE(rm.checkExitConditions(460.0, 400.0, 1, 460.0,
                                        0.05, -1.0, 0.03, 3).has_value());
}

// TP désactivé mais -4 % depuis le pic (trailing, minHold atteint) : le
// trailing reste la sortie des gagnants — seul le PLAFOND disparaît.
TEST(RiskManagerUnit, TrailingStillFiresWhenTakeProfitDisabled) {
    RiskManager rm;
    auto r = rm.checkExitConditions(432.0, 400.0, 5, 450.0,
                                    0.05, 0.0, 0.03, 3);
    ASSERT_TRUE(r.has_value());
    EXPECT_NE(r->find("trailing"), std::string::npos);
}

// TP désactivé et -7,5 % sous l'achat : le stop-loss protège toujours.
TEST(RiskManagerUnit, StopLossStillFiresWhenTakeProfitDisabled) {
    RiskManager rm;
    auto r = rm.checkExitConditions(370.0, 400.0, 0, 400.0,
                                    0.05, 0.0, 0.03, 3);
    ASSERT_TRUE(r.has_value());
    EXPECT_NE(r->find("stop-loss"), std::string::npos);
}

// ════════════════════════════════════════════════════════════
//  checkKillSwitch — garde-fous de coupure d'entrée (item 18)
// ════════════════════════════════════════════════════════════

// Sous tous les seuils → aucune coupure
TEST(RiskManagerUnit, KillSwitchAllowsEntryWhenAllUnderThresholds) {
    RiskManager rm;
    KillSwitchConfig cfg;  // 5% DD, 4 pertes, 10 ordres
    EXPECT_FALSE(rm.checkKillSwitch(cfg, 10'000.0, 9'900.0, 3, 9).has_value());
}

// Pertes consécutives au seuil → coupure (≥, pas >)
TEST(RiskManagerUnit, KillSwitchHaltsOnConsecutiveLosses) {
    RiskManager rm;
    KillSwitchConfig cfg;
    auto r = rm.checkKillSwitch(cfg, 10'000.0, 10'000.0, 4, 0);
    ASSERT_TRUE(r.has_value());
    EXPECT_NE(r->find("pertes consécutives"), std::string::npos);
    // Juste en dessous → pas de coupure
    EXPECT_FALSE(rm.checkKillSwitch(cfg, 10'000.0, 10'000.0, 3, 0).has_value());
}

// Plafond d'ordres journalier atteint → coupure
TEST(RiskManagerUnit, KillSwitchHaltsOnDailyOrderCap) {
    RiskManager rm;
    KillSwitchConfig cfg;
    auto r = rm.checkKillSwitch(cfg, 10'000.0, 10'000.0, 0, 10);
    ASSERT_TRUE(r.has_value());
    EXPECT_NE(r->find("plafond d'ordres"), std::string::npos);
    EXPECT_FALSE(rm.checkKillSwitch(cfg, 10'000.0, 10'000.0, 0, 9).has_value());
}

// Drawdown journalier au seuil (-5%) → coupure
TEST(RiskManagerUnit, KillSwitchHaltsOnDailyDrawdown) {
    RiskManager rm;
    KillSwitchConfig cfg;
    // 10 000 → 9 500 = -5% pile
    auto r = rm.checkKillSwitch(cfg, 10'000.0, 9'500.0, 0, 0);
    ASSERT_TRUE(r.has_value());
    EXPECT_NE(r->find("drawdown journalier"), std::string::npos);
    // -4,9% → pas encore
    EXPECT_FALSE(rm.checkKillSwitch(cfg, 10'000.0, 9'510.0, 0, 0).has_value());
}

// Un gain (équité en hausse) ne déclenche jamais le drawdown
TEST(RiskManagerUnit, KillSwitchIgnoresPositiveDailyReturn) {
    RiskManager rm;
    KillSwitchConfig cfg;
    EXPECT_FALSE(rm.checkKillSwitch(cfg, 10'000.0, 11'000.0, 0, 0).has_value());
}

// Priorité : pertes consécutives > plafond d'ordres > drawdown (ordre des if)
TEST(RiskManagerUnit, KillSwitchConsecutiveLossesReportedFirst) {
    RiskManager rm;
    KillSwitchConfig cfg;
    // Les trois conditions sont vraies simultanément
    auto r = rm.checkKillSwitch(cfg, 10'000.0, 9'000.0, 4, 10);
    ASSERT_TRUE(r.has_value());
    EXPECT_NE(r->find("pertes consécutives"), std::string::npos);
}

// Seuil ≤ 0 → garde-fou désactivé
TEST(RiskManagerUnit, KillSwitchThresholdsDisabledWhenNonPositive) {
    RiskManager rm;
    KillSwitchConfig cfg;
    cfg.maxConsecutiveLosses = 0;
    cfg.maxOrdersPerDay      = 0;
    cfg.maxDailyDrawdownPct  = 0.0;
    EXPECT_FALSE(rm.checkKillSwitch(cfg, 10'000.0, 1.0, 999, 999).has_value());
}

// dayStartEquity invalide (≤0, ex. panne getAccount) : le drawdown n'est pas
// calculé (pas de division par zéro / résultat absurde)
TEST(RiskManagerUnit, KillSwitchSkipsDrawdownWhenDayStartEquityInvalid) {
    RiskManager rm;
    KillSwitchConfig cfg;
    EXPECT_FALSE(rm.checkKillSwitch(cfg, 0.0, -500.0, 0, 0).has_value());
}
