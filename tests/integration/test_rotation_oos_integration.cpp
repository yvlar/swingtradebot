// ============================================================
//  test_rotation_oos_integration.cpp  —  Tests d'INTÉGRATION
//  Cible : RotationBacktester / RotationWalkForward
//  Sprint 8-nonies (item 8n.2) — VERDICT OUT-OF-SAMPLE VERROUILLÉ.
//
//  Question : la rotation multi-actifs (détenir le régime le plus fort parmi
//  QQQ/SPY/IWM/MDY, cash sinon) bat-elle, NET DE COÛTS, (a) le MEILLEUR B&H
//  mono-actif (B&L) et (b) le panier équipondéré ? Jugé sur données longues
//  total-return (*_max.csv, ~2000+, dot-com/2008 inclus), 3 pavages
//  (canonique / fin / décalé, anti biais de sélection D36).
//
//  Discipline (D33/D34) : config construite explicitement, trades OOS poolés
//  (comptes verrouillés), verdicts figés (sentinelle → mesure → figée).
//  Toutes les valeurs ci-dessous ont été MESURÉES le 2026-07-05 sur les
//  *_max.csv (période gelée à 2026-07-01) et figées.
// ============================================================
#include <gtest/gtest.h>
#include <cmath>
#include <string>
#include <vector>
#include <iostream>
#include <iomanip>
#include "backtest/RotationBacktester.hpp"
#include "backtest/MonteCarlo.hpp"

using namespace trading;

namespace {

// Pavages (constexpr) : canonique 2+ fenêtres, fin (D34), décalé (offset, D36).
constexpr size_t kIsCan = 700, kOosCan = 400, kStepCan = 400;
constexpr size_t kIsFin = 500, kOosFin = 300, kStepFin = 300;
constexpr size_t kIsShift = 700, kOosShift = 400, kStepShift = 400, kOffShift = 90;

// Config EXPLICITE de la rotation (D33 : jamais depuis d'éventuels défauts qui
// pourraient évoluer). SMA200 de régime, coûts = défauts Backtester (D22).
RotationConfig cfgRotation() {
    RotationConfig c;
    c.smaPeriod      = 200;
    c.initialCapital = 10'000.0;
    c.commissionPct  = 0.001;
    c.slippageBps    = 2.0;
    c.halfSpreadBps  = 0.5;
    return c;
}

std::vector<std::string> csvs4() {
    return { SWINGBOT_QQQ_MAX_CSV, SWINGBOT_SPY_MAX_CSV,
             SWINGBOT_IWM_MAX_CSV, SWINGBOT_MDY_MAX_CSV };
}

// « YYYY-MM-DD » → jours civils (pour les années observées du Monte-Carlo).
long joursCivils(const std::string& d) {
    int y = 0, m = 0, dd = 0;
    std::sscanf(d.c_str(), "%d-%d-%d", &y, &m, &dd);
    y -= m <= 2;
    const long     era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153u * (m + (m > 2 ? -3 : 9)) + 2u) / 5u + dd - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<long>(doe) - 719468;
}

// Agrégats OOS d'un pavage (moyennes d'alpha, trades poolés, bascules, années).
struct AggOos {
    size_t windows = 0;
    double meanAlphaVsBest = 0.0, meanAlphaVsBasket = 0.0;
    int    switchSum = 0;
    std::vector<TradeRecord> pool;
    double years = 0.0;
};

AggOos agregeOos(const std::vector<RotWindow>& ws) {
    AggOos a;
    a.windows = ws.size();
    for (const auto& w : ws) {
        a.meanAlphaVsBest   += w.oos.alphaVsBest;
        a.meanAlphaVsBasket += w.oos.alphaVsBasket;
        a.switchSum += w.oos.switchCount;
        for (const auto& t : w.oos.trades) a.pool.push_back(t);
        if (w.oos.equityDates.size() >= 2) {
            const long d0 = joursCivils(w.oos.equityDates.front());
            const long d1 = joursCivils(w.oos.equityDates.back());
            if (d1 > d0) a.years += static_cast<double>(d1 - d0) / 365.25;
        }
    }
    if (a.windows > 0) {
        a.meanAlphaVsBest   /= static_cast<double>(a.windows);
        a.meanAlphaVsBasket /= static_cast<double>(a.windows);
    }
    return a;
}

void imprime(const char* nom, const AggOos& a) {
    std::cout << std::fixed << std::setprecision(4)
              << "  ROTATION " << nom << " : fenetres=" << a.windows
              << " alphaVsBest=" << a.meanAlphaVsBest
              << " alphaVsBasket=" << a.meanAlphaVsBasket
              << " trades=" << a.pool.size()
              << " bascules=" << a.switchSum
              << " annees=" << a.years << "\n";
}

} // namespace

// ════════════════════════════════════════════════════════════
//  Axe commun aligné (2000-05-26 → 2026-07-01, borné par IWM)
// ════════════════════════════════════════════════════════════
TEST(RotationOosIntegration, RotationCommonAxisIsLocked) {
    RotationBacktester rb(cfgRotation(), csvs4());
    const auto& ax = rb.axis();
    ASSERT_EQ(ax.assets(), 4u);
    EXPECT_EQ(ax.dates.front(), "2000-05-26");
    EXPECT_EQ(ax.dates.back(),  "2026-07-01");
    EXPECT_EQ(ax.size(), 6562u);
    for (const auto& col : ax.close) EXPECT_EQ(col.size(), ax.size());
}

// ════════════════════════════════════════════════════════════
//  Pavage CANONIQUE : la rotation NE BAT NI le meilleur B&H NI le panier
// ════════════════════════════════════════════════════════════
TEST(RotationOosIntegration, RotationCanonicalPavingVsBestBuyHoldAndBasketIsLocked) {
    const auto ws = RotationWalkForward(cfgRotation(), csvs4(),
                                        kIsCan, kOosCan, kStepCan).run();
    const auto a = agregeOos(ws);
    imprime("CANON", a);

    ASSERT_GE(a.windows, 2u);
    EXPECT_TRUE(std::isfinite(a.meanAlphaVsBest));
    EXPECT_TRUE(std::isfinite(a.meanAlphaVsBasket));

    // Verdict : alpha OOS négatif vs les DEUX références → aucun edge
    EXPECT_LT(a.meanAlphaVsBest, 0.0);
    EXPECT_LT(a.meanAlphaVsBasket, 0.0);

    EXPECT_EQ(a.windows, 14u);
    EXPECT_NEAR(a.meanAlphaVsBest,   -11.6102, 1e-2);
    EXPECT_NEAR(a.meanAlphaVsBasket,  -2.7325, 1e-2);
    EXPECT_EQ(a.pool.size(), 164u);        // D34 : compte de trades poolés verrouillé
    EXPECT_EQ(a.switchSum, 196);
}

// ════════════════════════════════════════════════════════════
//  Pavage FIN
// ════════════════════════════════════════════════════════════
TEST(RotationOosIntegration, RotationFinePavingVerdictIsLocked) {
    const auto ws = RotationWalkForward(cfgRotation(), csvs4(),
                                        kIsFin, kOosFin, kStepFin).run();
    const auto a = agregeOos(ws);
    imprime("FIN", a);

    ASSERT_GE(a.windows, 4u);
    EXPECT_LT(a.meanAlphaVsBest, 0.0);
    EXPECT_LT(a.meanAlphaVsBasket, 0.0);

    EXPECT_EQ(a.windows, 20u);
    EXPECT_NEAR(a.meanAlphaVsBest,   -10.0664, 1e-2);
    EXPECT_NEAR(a.meanAlphaVsBasket,  -5.9035, 1e-2);
    EXPECT_EQ(a.pool.size(), 157u);
    EXPECT_EQ(a.switchSum, 186);
}

// ════════════════════════════════════════════════════════════
//  Pavage DÉCALÉ (offset 90) : fenêtres jamais alignées sur le canonique
// ════════════════════════════════════════════════════════════
TEST(RotationOosIntegration, RotationShiftedPavingVerdictIsLocked) {
    const auto ws = RotationWalkForward(cfgRotation(), csvs4(),
                                        kIsShift, kOosShift, kStepShift, kOffShift).run();
    const auto a = agregeOos(ws);
    imprime("SHIFT", a);

    ASSERT_GE(a.windows, 2u);
    EXPECT_LT(a.meanAlphaVsBest, 0.0);
    EXPECT_LT(a.meanAlphaVsBasket, 0.0);

    EXPECT_EQ(a.windows, 14u);
    EXPECT_NEAR(a.meanAlphaVsBest,   -13.3255, 1e-2);
    EXPECT_NEAR(a.meanAlphaVsBasket,  -7.1458, 1e-2);
    EXPECT_EQ(a.pool.size(), 189u);
    EXPECT_EQ(a.switchSum, 222);
}

// ════════════════════════════════════════════════════════════
//  Monte-Carlo size-aware (D45) sur les trades OOS poolés (canonique) :
//  drawdown de queue MASSIF (ddP95 ≈ 55 %) — le risque de la rotation
// ════════════════════════════════════════════════════════════
TEST(RotationOosIntegration, RotationMonteCarloSizeAwareDdIsLocked) {
    const auto ws = RotationWalkForward(cfgRotation(), csvs4(),
                                        kIsCan, kOosCan, kStepCan).run();
    const auto a = agregeOos(ws);
    ASSERT_FALSE(a.pool.empty());          // D34 : pas de verdict sans trades

    MonteCarlo mc(10'000.0, /*graine=*/42, /*chemins=*/2000);
    const auto r = mc.run(a.pool, a.years);

    EXPECT_EQ(r.paths, 2000u);
    EXPECT_TRUE(std::isfinite(r.cagrP50));
    EXPECT_TRUE(std::isfinite(r.ddP95));
    EXPECT_LE(r.cagrP5,  r.cagrP50);
    EXPECT_LE(r.cagrP50, r.cagrP95);
    EXPECT_LE(r.ddP5,  r.ddP50);
    EXPECT_LE(r.ddP50, r.ddP95);

    EXPECT_NEAR(r.cagrP50, 2.5744, 1e-3);
    EXPECT_NEAR(r.ddP95,  55.0777, 1e-3);
}

// ════════════════════════════════════════════════════════════
//  Run complet (2000-2026) : la rotation sous-performe LOURDEMENT le
//  meilleur B&H et le panier, Calmar figé
// ════════════════════════════════════════════════════════════
TEST(RotationOosIntegration, RotationFullRunAndCalmarIsLocked) {
    RotationBacktester rb(cfgRotation(), csvs4());
    const auto r = rb.run();

    std::cout << std::fixed << std::setprecision(4)
              << "  ROTATION FULLRUN : total=" << r.totalReturnPct
              << " best=" << r.bestBuyHoldPct << " basket=" << r.basketReturnPct
              << " alphaBest=" << r.alphaVsBest << " alphaBasket=" << r.alphaVsBasket
              << " DD=" << r.maxDrawdownPct << " CAGR=" << r.cagrPct
              << " Calmar=" << r.calmarRatio << " bascules=" << r.switchCount << "\n";

    for (double v : { r.totalReturnPct, r.bestBuyHoldPct, r.basketReturnPct,
                      r.maxDrawdownPct, r.cagrPct, r.calmarRatio })
        EXPECT_TRUE(std::isfinite(v));

    // Verdict : sous-performe les deux références sur tout l'historique
    EXPECT_LT(r.alphaVsBest,   0.0);
    EXPECT_LT(r.alphaVsBasket, 0.0);

    EXPECT_NEAR(r.totalReturnPct,   186.3845, 1e-2);
    EXPECT_NEAR(r.bestBuyHoldPct,  1836.1031, 1e-2);
    EXPECT_NEAR(r.basketReturnPct, 1119.8145, 1e-2);
    EXPECT_NEAR(r.alphaVsBest,    -1649.7186, 1e-2);
    EXPECT_NEAR(r.alphaVsBasket,   -933.4300, 1e-2);
    EXPECT_NEAR(r.maxDrawdownPct,    44.6823, 1e-2);
    EXPECT_NEAR(r.cagrPct,            4.1141, 1e-2);
    EXPECT_NEAR(r.calmarRatio,       0.0921, 1e-3);
    EXPECT_EQ(r.switchCount, 472);
    EXPECT_EQ(r.trades.size(), 396u);
}
