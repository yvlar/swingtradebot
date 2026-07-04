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

// ════════════════════════════════════════════════════════════
//  Trailing adaptatif ATR (item 8q.1, ré-ouverture 8b.4)
// ════════════════════════════════════════════════════════════
// Hypothèse : un trailing FIXE en % est fragile parce que la « bonne »
// distance dépend de la volatilité (l'axe le plus sensible depuis B2).
// Convention arbitrée à l'ouverture (décision utilisateur 2026-07-03) :
// quand trailingAtrMult > 0, le seuil devient peak − mult × ATR(14) et
// REMPLACE le trailing % (pas de cumul) ; repli DÉFENSIF sur le % si l'ATR
// est incalculable (fenêtre < 15 barres, série vide, barres plates) — une
// position ouverte ne reste JAMAIS sans filet.

namespace {

    // Fenêtre à true range CONSTANT : open=close=100, high=101, low=99
    // → TR_i = max(H−L, |H−Cprev|, |L−Cprev|) = max(2, 1, 1) = 2 pour toute
    // barre, seed SMA = 2, le lissage de Wilder conserve 2 → ATR(14) = 2,0
    // EXACTEMENT (représentable en double) dès l'indice 14.
    std::vector<Bar> barresTrConstant(int n, double close = 100.0,
                                      double demiAmplitude = 1.0) {
        std::vector<Bar> bars;
        for (int i = 0; i < n; ++i)
            bars.push_back({"2024-01-01", close, close + demiAmplitude,
                            close - demiAmplitude, close, 1'000});
        return bars;
    }

} // namespace

// ════════════════════════════════════════════════════════════
//  positionSize modulé par la volatilité (item 8o.2, D44)
//  barresTrConstant(16,100,1) → ATR(14)=2, prix=100 → volatilité
//  relative volRel = ATR/prix = 0,02. Base : positionSize(10000,100,
//  0.05,0.02) = 40 actions (dollarRisk 200 / riskPerShare 5).
// ════════════════════════════════════════════════════════════

// volSizingAtrRef > volRel → facteur min(1, 0,03/0,02) = 1 → PLEINE taille.
TEST(RiskManagerUnit, VolSizingFullSizeBelowReference) {
    RiskManager rm;
    auto bars = barresTrConstant(16);
    EXPECT_EQ(rm.positionSize(10'000.0, 100.0, 0.05, 0.02, bars, 0.03), 40);
}

// volSizingAtrRef < volRel → facteur min(1, 0,01/0,02) = 0,5 → 40 × 0,5 = 20.
TEST(RiskManagerUnit, VolSizingReducesAboveReference) {
    RiskManager rm;
    auto bars = barresTrConstant(16);
    EXPECT_EQ(rm.positionSize(10'000.0, 100.0, 0.05, 0.02, bars, 0.01), 20);
}

// Le facteur est BORNÉ à 1 : marché très calme (réf ≫ volRel) ne gonfle
// jamais la taille au-dessus du sizing par le risque.
TEST(RiskManagerUnit, VolSizingNeverExceedsBaseSize) {
    RiskManager rm;
    auto bars = barresTrConstant(16);
    EXPECT_EQ(rm.positionSize(10'000.0, 100.0, 0.05, 0.02, bars, 1.0), 40);
}

// Fail-open : fenêtre trop courte pour ATR(14) (< 15 barres) → série vide →
// PLEINE taille (jamais de blocage silencieux du sizing).
TEST(RiskManagerUnit, VolSizingFailOpenWhenAtrUnavailable) {
    RiskManager rm;
    auto bars = barresTrConstant(10);   // < 15 → ATR incalculable
    EXPECT_EQ(rm.positionSize(10'000.0, 100.0, 0.05, 0.02, bars, 0.01), 40);
}

// volSizingAtrRef ≤ 0 → identité stricte avec le sizing historique.
TEST(RiskManagerUnit, VolSizingDisabledIsIdentity) {
    RiskManager rm;
    auto bars = barresTrConstant(16);
    const int base = rm.positionSize(10'000.0, 100.0, 0.05, 0.02);
    EXPECT_EQ(rm.positionSize(10'000.0, 100.0, 0.05, 0.02, bars, 0.0), base);
    EXPECT_EQ(rm.positionSize(10'000.0, 100.0, 0.05, 0.02, {}, 0.0),  base);
}

// Cas central calculé à la main : buy=100, peak=110, mult=3, ATR=2
// → seuil = 110 − 3×2 = 104 (exact en double). Sortie à 104,0 (borne <=
// verrouillée), pas de sortie à 104,1.
TEST(RiskManagerUnit, AtrTrailingExitAtHandComputedThreshold) {
    RiskManager rm;
    auto bars = barresTrConstant(16);
    auto atSeuil = rm.checkExitConditions(104.0, 100.0, 5, 110.0,
                                          0.05, 0.0, 0.03, 3, bars, 3.0);
    ASSERT_TRUE(atSeuil.has_value());
    EXPECT_NE(atSeuil->find("trailing"), std::string::npos);
    EXPECT_NE(atSeuil->find("ATR"), std::string::npos);

    auto auDessus = rm.checkExitConditions(104.1, 100.0, 5, 110.0,
                                           0.05, 0.0, 0.03, 3, bars, 3.0);
    EXPECT_FALSE(auDessus.has_value());
}

// Verrouille le LISSAGE de Wilder (pas seulement le seed) : TR 1..14 = 2,
// dernière barre high=102/low=98 → TR15 = 4 → ATR@15 = (2×13 + 4)/14 = 30/14
// ≈ 2,142857 → seuil = 110 − 3×30/14 ≈ 103,5714. Encadrement strict
// (103,5 sort / 103,7 non), pas d'égalité flottante.
TEST(RiskManagerUnit, AtrTrailingWilderSmoothingHandComputed) {
    RiskManager rm;
    auto bars = barresTrConstant(16);
    bars.back().high = 102.0;
    bars.back().low  =  98.0;

    auto sous = rm.checkExitConditions(103.5, 100.0, 5, 110.0,
                                       0.05, 0.0, 0.03, 3, bars, 3.0);
    ASSERT_TRUE(sous.has_value());
    EXPECT_NE(sous->find("ATR"), std::string::npos);

    auto dessus = rm.checkExitConditions(103.7, 100.0, 5, 110.0,
                                         0.05, 0.0, 0.03, 3, bars, 3.0);
    EXPECT_FALSE(dessus.has_value());
}

// La convention « remplace » : à 105, le trailing % sortirait (−4,5 % depuis
// le pic ≤ −3 %) mais le seuil ATR (104) ne sort PAS → mult > 0 remplace le
// %, il ne s'y ajoute pas (pas de max des deux).
TEST(RiskManagerUnit, AtrTrailingReplacesPercentTrailing) {
    RiskManager rm;
    auto bars = barresTrConstant(16);
    EXPECT_FALSE(rm.checkExitConditions(105.0, 100.0, 5, 110.0,
                                        0.05, 0.0, 0.03, 3, bars, 3.0)
                     .has_value());
}

// mult = 0 → chemin % historique, à l'identique de la surcharge 8 arguments
// (délégation) : même décision, même raison, sans mention d'ATR.
TEST(RiskManagerUnit, AtrTrailingDisabledWhenMultZero) {
    RiskManager rm;
    auto bars = barresTrConstant(16);
    auto r10 = rm.checkExitConditions(105.0, 100.0, 5, 110.0,
                                      0.05, 0.0, 0.03, 3, bars, 0.0);
    auto r8  = rm.checkExitConditions(105.0, 100.0, 5, 110.0,
                                      0.05, 0.0, 0.03, 3);
    ASSERT_TRUE(r10.has_value());
    ASSERT_TRUE(r8.has_value());
    EXPECT_EQ(*r10, *r8);
    EXPECT_EQ(r10->find("ATR"), std::string::npos);
}

// Repli défensif : fenêtre trop courte pour ATR(14) (< 15 barres, ou vide)
// → le trailing % reste actif — jamais de position sans filet.
TEST(RiskManagerUnit, AtrTrailingFallsBackToPercentWhenWindowTooShort) {
    RiskManager rm;
    auto courte = barresTrConstant(10);
    auto r = rm.checkExitConditions(105.0, 100.0, 5, 110.0,
                                    0.05, 0.0, 0.03, 3, courte, 3.0);
    ASSERT_TRUE(r.has_value());
    EXPECT_NE(r->find("trailing"), std::string::npos);
    EXPECT_EQ(r->find("ATR"), std::string::npos);

    auto vide = rm.checkExitConditions(105.0, 100.0, 5, 110.0,
                                       0.05, 0.0, 0.03, 3, {}, 3.0);
    ASSERT_TRUE(vide.has_value());
    EXPECT_NE(vide->find("trailing"), std::string::npos);
}

// Repli défensif : barres plates (high = low = close) → ATR = 0, un seuil
// peak − 0 sortirait à CHAQUE barre sous le pic — on retombe sur le %.
TEST(RiskManagerUnit, AtrTrailingFallsBackWhenAtrZero) {
    RiskManager rm;
    auto plates = barresTrConstant(16, 100.0, 0.0);
    auto r = rm.checkExitConditions(105.0, 100.0, 5, 110.0,
                                    0.05, 0.0, 0.03, 3, plates, 3.0);
    ASSERT_TRUE(r.has_value());
    EXPECT_NE(r->find("trailing"), std::string::npos);
    EXPECT_EQ(r->find("ATR"), std::string::npos);
}

// Le gating minHoldDays s'applique au trailing ATR comme au trailing %
// (miroir de TrailingStopOnlyAfterMinHoldDays).
TEST(RiskManagerUnit, AtrTrailingOnlyAfterMinHoldDays) {
    RiskManager rm;
    auto bars = barresTrConstant(16);
    auto before = rm.checkExitConditions(104.0, 100.0, 2, 110.0,
                                         0.05, 0.0, 0.03, 3, bars, 3.0);
    EXPECT_FALSE(before.has_value());
    auto after = rm.checkExitConditions(104.0, 100.0, 3, 110.0,
                                        0.05, 0.0, 0.03, 3, bars, 3.0);
    ASSERT_TRUE(after.has_value());
    EXPECT_NE(after->find("ATR"), std::string::npos);
}

// Priorité inchangée : stop-loss (−6 % ≤ −5 %) ET seuil ATR (94 ≤ 104) vrais
// simultanément → le stop-loss, vérifié en premier, l'emporte.
TEST(RiskManagerUnit, StopLossTakesPriorityOverAtrTrailing) {
    RiskManager rm;
    auto bars = barresTrConstant(16);
    auto r = rm.checkExitConditions(94.0, 100.0, 5, 110.0,
                                    0.05, 0.0, 0.03, 3, bars, 3.0);
    ASSERT_TRUE(r.has_value());
    EXPECT_NE(r->find("stop-loss"), std::string::npos);
    EXPECT_EQ(r->find("trailing"), std::string::npos);
}

// Priorité inchangée : take-profit (+9,5 % ≥ +5 %) ET seuil ATR (104 ≤ 104)
// vrais simultanément → le take-profit l'emporte.
TEST(RiskManagerUnit, TakeProfitTakesPriorityOverAtrTrailing) {
    RiskManager rm;
    auto bars = barresTrConstant(16);
    auto r = rm.checkExitConditions(104.0, 95.0, 5, 110.0,
                                    0.05, 0.05, 0.03, 3, bars, 3.0);
    ASSERT_TRUE(r.has_value());
    EXPECT_NE(r->find("take-profit"), std::string::npos);
    EXPECT_EQ(r->find("trailing"), std::string::npos);
}

// peakPrice=0 (pic jamais renseigné) : la garde `peakPrice > 0` écarte le
// trailing ATR comme le trailing % (miroir de TrailingSkippedWhenPeakPriceZero).
TEST(RiskManagerUnit, AtrTrailingSkippedWhenPeakPriceZero) {
    RiskManager rm;
    auto bars = barresTrConstant(16);
    EXPECT_FALSE(rm.checkExitConditions(98.0, 100.0, 5, 0.0,
                                        0.05, 0.0, 0.03, 3, bars, 3.0)
                     .has_value());
}

// ════════════════════════════════════════════════════════════
//  Sortie structurelle « plus bas de N jours » (item 8s.1)
// ════════════════════════════════════════════════════════════
// Hypothèse (Sprint 8-quinquies) : une sortie sur STRUCTURE de prix — clôture
// sous le plus bas des N barres PRÉCÉDENTES (barre courante exclue) — colle
// mieux aux retournements qu'une distance depuis le pic. Décisions
// utilisateur d'ouverture (2026-07-03) : gatée par minHoldDays (comme le
// trailing qu'elle concurrence) ; priorité SL > TP > structure > trailing.
// exitOnLowestLowN ≤ 0 = désactivé (comportement historique inchangé).

namespace {

    // Barres dont seuls les plus bas importent : open = close, high = close+1,
    // low fourni par la liste (une barre par valeur), volume constant.
    std::vector<Bar> barresAvecLows(const std::vector<double>& lows,
                                    double close = 100.0) {
        std::vector<Bar> bars;
        for (double lo : lows)
            bars.push_back({"2024-01-01", close, close + 1.0, lo, close, 1'000});
        return bars;
    }

} // namespace

// Cas central calculé à la main : N=5, plus bas des 5 barres PRÉCÉDENTES
// = 100. Les deux bornes de fenêtre sont verrouillées en même temps : la
// barre plus ancienne (low 90) est HORS fenêtre et la barre COURANTE (low 99)
// est EXCLUE — si l'une ou l'autre comptait, le seuil serait 90 ou 99 et la
// sortie à 100,0 n'aurait pas lieu. Borne <= verrouillée (100,1 ne sort pas).
TEST(RiskManagerUnit, StructureExitAtHandComputedThreshold) {
    RiskManager rm;
    auto bars = barresAvecLows({90.0, 100.0, 101.0, 102.0, 103.0, 104.0, 99.0});
    auto atSeuil = rm.checkExitConditions(100.0, 98.0, 5, 101.0,
                                          0.05, 0.0, 0.03, 3, bars, 0.0, 5);
    ASSERT_TRUE(atSeuil.has_value());
    EXPECT_NE(atSeuil->find("structure"), std::string::npos);

    auto auDessus = rm.checkExitConditions(100.1, 98.0, 5, 101.0,
                                           0.05, 0.0, 0.03, 3, bars, 0.0, 5);
    EXPECT_FALSE(auDessus.has_value());
}

// La barre courante est exclue du plus bas : ici elle fait 94 (sous la
// clôture 95) — si elle comptait, le seuil serait 94 et 95 ne sortirait pas.
// Le plus bas des 5 PRÉCÉDENTES (100) déclenche la sortie.
TEST(RiskManagerUnit, StructureExitExcludesCurrentBarFromLowestLow) {
    RiskManager rm;
    auto bars = barresAvecLows({100.0, 101.0, 102.0, 103.0, 104.0, 94.0});
    auto r = rm.checkExitConditions(95.0, 98.0, 5, 96.0,
                                    0.05, 0.0, 0.03, 3, bars, 0.0, 5);
    ASSERT_TRUE(r.has_value());
    EXPECT_NE(r->find("structure"), std::string::npos);
}

// N=0 → désactivé : le scénario qui sortirait en structure ne sort pas, et
// la délégation est l'identité stricte du chemin historique (même raison de
// trailing que la surcharge 8 arguments).
TEST(RiskManagerUnit, StructureExitDisabledWhenNZero) {
    RiskManager rm;
    auto bars = barresAvecLows({100.0, 101.0, 102.0, 103.0, 104.0, 99.0});
    EXPECT_FALSE(rm.checkExitConditions(100.0, 98.0, 5, 101.0,
                                        0.05, 0.0, 0.03, 3, bars, 0.0, 0)
                     .has_value());

    auto r11 = rm.checkExitConditions(105.0, 100.0, 5, 110.0,
                                      0.05, 0.0, 0.03, 3, bars, 0.0, 0);
    auto r8  = rm.checkExitConditions(105.0, 100.0, 5, 110.0,
                                      0.05, 0.0, 0.03, 3);
    ASSERT_TRUE(r11.has_value());
    ASSERT_TRUE(r8.has_value());
    EXPECT_EQ(*r11, *r8);
}

// Fenêtre trop courte : N=5 exige N+1 barres (les N précédentes + la
// courante) — avec 5 barres, pas de sortie structure (et pas d'accès hors
// bornes).
TEST(RiskManagerUnit, StructureExitNeedsNPlusOneBars) {
    RiskManager rm;
    auto bars = barresAvecLows({100.0, 101.0, 102.0, 103.0, 104.0});
    EXPECT_FALSE(rm.checkExitConditions(100.0, 98.0, 5, 101.0,
                                        0.05, 0.0, 0.03, 3, bars, 0.0, 5)
                     .has_value());
}

// Décision utilisateur (2026-07-03) : la structure est gatée par minHoldDays,
// comme le trailing qu'elle concurrence (miroir de TrailingStopOnlyAfterMinHoldDays).
TEST(RiskManagerUnit, StructureExitOnlyAfterMinHoldDays) {
    RiskManager rm;
    auto bars = barresAvecLows({90.0, 100.0, 101.0, 102.0, 103.0, 104.0, 99.0});
    auto before = rm.checkExitConditions(100.0, 98.0, 2, 101.0,
                                         0.05, 0.0, 0.03, 3, bars, 0.0, 5);
    EXPECT_FALSE(before.has_value());
    auto after = rm.checkExitConditions(100.0, 98.0, 3, 101.0,
                                        0.05, 0.0, 0.03, 3, bars, 0.0, 5);
    ASSERT_TRUE(after.has_value());
    EXPECT_NE(after->find("structure"), std::string::npos);
}

// Priorité : −8 % sous l'achat (stop-loss) ET clôture 92 ≤ plus bas 100
// (structure) vrais simultanément → le stop-loss, vérifié en premier, gagne.
TEST(RiskManagerUnit, StopLossTakesPriorityOverStructureExit) {
    RiskManager rm;
    auto bars = barresAvecLows({100.0, 101.0, 102.0, 103.0, 104.0, 99.0});
    auto r = rm.checkExitConditions(92.0, 100.0, 5, 110.0,
                                    0.05, 0.0, 0.03, 3, bars, 0.0, 5);
    ASSERT_TRUE(r.has_value());
    EXPECT_NE(r->find("stop-loss"), std::string::npos);
    EXPECT_EQ(r->find("structure"), std::string::npos);
}

// Priorité : +11 % depuis l'achat (take-profit 5 %) ET clôture 100 ≤ plus
// bas 100 (structure) vrais simultanément → le take-profit gagne.
TEST(RiskManagerUnit, TakeProfitTakesPriorityOverStructureExit) {
    RiskManager rm;
    auto bars = barresAvecLows({100.0, 101.0, 102.0, 103.0, 104.0, 99.0});
    auto r = rm.checkExitConditions(100.0, 90.0, 5, 101.0,
                                    0.05, 0.05, 0.03, 3, bars, 0.0, 5);
    ASSERT_TRUE(r.has_value());
    EXPECT_NE(r->find("take-profit"), std::string::npos);
    EXPECT_EQ(r->find("structure"), std::string::npos);
}

// Priorité : clôture 100 ≤ plus bas 100 (structure) ET −9,1 % depuis le pic
// (trailing %) vrais simultanément → la structure, vérifiée avant le
// trailing, l'emporte.
TEST(RiskManagerUnit, StructureExitTakesPriorityOverTrailing) {
    RiskManager rm;
    auto bars = barresAvecLows({100.0, 101.0, 102.0, 103.0, 104.0, 99.0});
    auto r = rm.checkExitConditions(100.0, 98.0, 5, 110.0,
                                    0.05, 0.0, 0.03, 3, bars, 0.0, 5);
    ASSERT_TRUE(r.has_value());
    EXPECT_NE(r->find("structure"), std::string::npos);
    EXPECT_EQ(r->find("trailing"), std::string::npos);
}

// La structure passe AVANT le trailing quel que soit son MODE : avec un
// trailing ATR actif (seuil 110 − 3×2 = 104), la clôture 99 ≤ plus bas 99
// sort en « structure », pas en ATR.
TEST(RiskManagerUnit, StructureExitTakesPriorityOverAtrTrailing) {
    RiskManager rm;
    auto bars = barresTrConstant(16);   // plus bas constant à 99, ATR = 2
    auto r = rm.checkExitConditions(99.0, 98.0, 5, 110.0,
                                    0.05, 0.0, 0.03, 3, bars, 3.0, 5);
    ASSERT_TRUE(r.has_value());
    EXPECT_NE(r->find("structure"), std::string::npos);
    EXPECT_EQ(r->find("ATR"), std::string::npos);
}
