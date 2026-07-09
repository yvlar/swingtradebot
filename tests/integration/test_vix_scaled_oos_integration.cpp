// ============================================================
//  test_vix_scaled_oos_integration.cpp  —  Tests d'INTÉGRATION
//  Cible : VixScaledBacktester / VixScaledWalkForward
//  Sprint 18 (item 18.3) — VERDICT OUT-OF-SAMPLE VERROUILLÉ (variante VIX).
//
//  Scaling continu Moreira-Muir piloté par la volatilité IMPLICITE : le poids
//  investi sur QQQ est w* = min(1, cible / VIX) — le VIX est déjà une vol
//  annualisée en points de %, décidable dès la première barre (aucun warmup).
//  Même bande anti-churn et mêmes coûts ∝ |Δw| que la variante réalisée.
//
//  Question : la vol implicite (anticipatrice) fait-elle mieux que la vol
//  réalisée pour le scaling continu — et surtout, dégage-t-elle un ALPHA vs
//  B&H net de coûts OOS robuste aux 3 pavages ?
//
//  Discipline identique (D33/D34/D36/D47), valeurs MESURÉES le 2026-07-09 sur
//  QQQ_max ∩ VIX_max et figées.
// ============================================================
#include <gtest/gtest.h>
#include <cmath>
#include <string>
#include <vector>
#include <iostream>
#include <iomanip>
#include "backtest/VixScaledBacktester.hpp"
#include "backtest/MonteCarlo.hpp"

using namespace trading;

namespace {

constexpr size_t kIsCan = 750, kOosCan = 500, kStepCan = 500;
constexpr size_t kIsFin = 550, kOosFin = 400, kStepFin = 400;
constexpr size_t kIsShift = 750, kOosShift = 500, kStepShift = 500, kOffShift = 90;

// Config EXPLICITE (D33) : cible 15 % de vol annualisée, pas de levier,
// bande 0,05, coûts D22.
VixScaledConfig cfgVx() {
    VixScaledConfig c;
    c.targetVixPct   = 15.0;
    c.maxWeight      = 1.0;
    c.rebalanceBand  = 0.05;
    c.initialCapital = 10'000.0;
    c.commissionPct  = 0.001;
    c.slippageBps    = 2.0;
    c.halfSpreadBps  = 0.5;
    return c;
}

std::vector<std::string> qqqVix() {
    return { SWINGBOT_QQQ_MAX_CSV, SWINGBOT_VIX_MAX_CSV };
}

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

struct AggOos {
    size_t windows = 0;
    double meanTotalReturn = 0.0, meanSharpe = 0.0, meanBhSharpe = 0.0;
    double meanMaxDd = 0.0, meanBhDd = 0.0, meanAlphaVsBh = 0.0, meanAvgWeight = 0.0;
    std::vector<TradeRecord> pool;
    double years = 0.0;
    double sharpeDelta() const { return meanSharpe - meanBhSharpe; }
};

AggOos agregeOos(const std::vector<VixScaledWindow>& ws) {
    AggOos a;
    a.windows = ws.size();
    for (const auto& w : ws) {
        a.meanTotalReturn += w.oos.totalReturnPct;
        a.meanSharpe      += w.oos.sharpeRatio;
        a.meanBhSharpe    += w.oos.buyHoldSharpe;
        a.meanMaxDd       += w.oos.maxDrawdownPct;
        a.meanBhDd        += w.oos.buyHoldMaxDrawdownPct;
        a.meanAlphaVsBh   += w.oos.alphaVsBuyHold;
        a.meanAvgWeight   += w.oos.avgWeight;
        for (const auto& t : w.oos.trades) a.pool.push_back(t);
        if (w.oos.equityDates.size() >= 2) {
            const long d0 = joursCivils(w.oos.equityDates.front());
            const long d1 = joursCivils(w.oos.equityDates.back());
            if (d1 > d0) a.years += static_cast<double>(d1 - d0) / 365.25;
        }
    }
    if (a.windows > 0) {
        const double n = static_cast<double>(a.windows);
        a.meanTotalReturn /= n; a.meanSharpe /= n; a.meanBhSharpe /= n;
        a.meanMaxDd /= n; a.meanBhDd /= n; a.meanAlphaVsBh /= n; a.meanAvgWeight /= n;
    }
    return a;
}

void imprime(const char* nom, const AggOos& a) {
    std::cout << std::fixed << std::setprecision(4)
              << "  VIXSCALED " << nom << " : fen=" << a.windows
              << " ret=" << a.meanTotalReturn
              << " sharpe=" << a.meanSharpe
              << " bhSharpe=" << a.meanBhSharpe
              << " dSharpe=" << a.sharpeDelta()
              << " DD=" << a.meanMaxDd
              << " bhDD=" << a.meanBhDd
              << " alphaBH=" << a.meanAlphaVsBh
              << " poids=" << a.meanAvgWeight
              << " trades=" << a.pool.size()
              << " annees=" << a.years << "\n";
}

std::vector<VixScaledWindow> paving(size_t is, size_t oos, size_t step, size_t off = 0) {
    return VixScaledWalkForward(cfgVx(), qqqVix(), is, oos, step, off).run();
}

} // namespace

// ════════════════════════════════════════════════════════════
//  Axe commun QQQ ∩ VIX verrouillé (identique au VixRegime Sprint 14).
// ════════════════════════════════════════════════════════════
TEST(VixScaledOosIntegration, VixScaledAxisIsLocked) {
    VixScaledBacktester v(cfgVx(), qqqVix());
    const auto& ax = v.axis();
    ASSERT_EQ(ax.assets(), 2u);
    EXPECT_EQ(ax.dates.front(), "1999-03-10");
    EXPECT_EQ(ax.dates.back(),  "2026-07-01");
    EXPECT_EQ(ax.size(), 6870u);           // QQQ ∩ VIX, borné par QQQ (1999)
    for (const auto& col : ax.close) EXPECT_EQ(col.size(), ax.size());
}

// ════════════════════════════════════════════════════════════
//  Pavage CANONIQUE
// ════════════════════════════════════════════════════════════
TEST(VixScaledOosIntegration, VixScaledCanonicalPavingVerdictIsLocked) {
    const auto a = agregeOos(paving(kIsCan, kOosCan, kStepCan));
    imprime("CANON", a);

    ASSERT_GE(a.windows, 2u);
    EXPECT_TRUE(std::isfinite(a.meanSharpe));
    EXPECT_GT(a.pool.size(), 1u);          // D47 : la famille TRADE réellement

    // Verdict : AUCUN edge — la vol IMPLICITE fait MOINS bien que la vol
    // réalisée pour le scaling continu (alpha plus négatif ET dSharpe < 0,
    // là où la variante réalisée comblait presque l'écart de Sharpe).
    EXPECT_LT(a.meanAlphaVsBh, 0.0);
    EXPECT_LT(a.sharpeDelta(), 0.0);

    EXPECT_EQ(a.windows, 12u);
    EXPECT_NEAR(a.meanTotalReturn, 19.0997, 1e-2);
    EXPECT_NEAR(a.meanSharpe,       0.7053, 1e-2);
    EXPECT_NEAR(a.meanBhSharpe,     0.7921, 1e-2);
    EXPECT_NEAR(a.meanAlphaVsBh,  -10.9115, 1e-2);
    EXPECT_NEAR(a.meanAvgWeight,    0.8100, 1e-2);
    EXPECT_EQ(a.pool.size(), 1292u);
}

// ════════════════════════════════════════════════════════════
//  Pavage FIN
// ════════════════════════════════════════════════════════════
TEST(VixScaledOosIntegration, VixScaledFinePavingVerdictIsLocked) {
    const auto a = agregeOos(paving(kIsFin, kOosFin, kStepFin));
    imprime("FIN", a);

    ASSERT_GE(a.windows, 2u);
    EXPECT_GT(a.pool.size(), 1u);
    EXPECT_LT(a.meanAlphaVsBh, 0.0);
    EXPECT_LT(a.sharpeDelta(), 0.0);

    EXPECT_EQ(a.windows, 15u);
    EXPECT_NEAR(a.meanTotalReturn, 13.7933, 1e-2);
    EXPECT_NEAR(a.meanSharpe,       0.6727, 1e-2);
    EXPECT_NEAR(a.meanBhSharpe,     0.8243, 1e-2);
    EXPECT_NEAR(a.meanAlphaVsBh,   -8.5653, 1e-2);
    EXPECT_EQ(a.pool.size(), 1273u);
}

// ════════════════════════════════════════════════════════════
//  Pavage DÉCALÉ (offset 90)
// ════════════════════════════════════════════════════════════
TEST(VixScaledOosIntegration, VixScaledShiftedPavingVerdictIsLocked) {
    const auto a = agregeOos(paving(kIsShift, kOosShift, kStepShift, kOffShift));
    imprime("SHIFT", a);

    ASSERT_GE(a.windows, 2u);
    EXPECT_GT(a.pool.size(), 1u);
    EXPECT_LT(a.meanAlphaVsBh, 0.0);
    EXPECT_LT(a.sharpeDelta(), 0.0);

    EXPECT_EQ(a.windows, 12u);
    EXPECT_NEAR(a.meanTotalReturn, 22.4436, 1e-2);
    EXPECT_NEAR(a.meanSharpe,       0.7731, 1e-2);
    EXPECT_NEAR(a.meanBhSharpe,     0.8557, 1e-2);
    EXPECT_NEAR(a.meanAlphaVsBh,  -11.8296, 1e-2);
    EXPECT_EQ(a.pool.size(), 1309u);
}

// ════════════════════════════════════════════════════════════
//  Monte-Carlo size-aware (D45) sur les stints OOS poolés (canonique).
// ════════════════════════════════════════════════════════════
TEST(VixScaledOosIntegration, VixScaledMonteCarloSizeAwareIsLocked) {
    const auto a = agregeOos(paving(kIsCan, kOosCan, kStepCan));
    ASSERT_FALSE(a.pool.empty());

    MonteCarlo mc(10'000.0, /*graine=*/42, /*chemins=*/2000);
    const auto r = mc.run(a.pool, a.years);

    EXPECT_EQ(r.paths, 2000u);
    EXPECT_TRUE(std::isfinite(r.cagrP50));
    EXPECT_TRUE(std::isfinite(r.ddP95));
    EXPECT_LE(r.cagrP5,  r.cagrP50);
    EXPECT_LE(r.cagrP50, r.cagrP95);
    EXPECT_LE(r.ddP5,  r.ddP50);
    EXPECT_LE(r.ddP50, r.ddP95);

    std::cout << std::fixed << std::setprecision(4)
              << "  VIXSCALED MC : cagrP50=" << r.cagrP50 << " ddP95=" << r.ddP95 << "\n";
    EXPECT_NEAR(r.cagrP50,  8.5129, 1e-3);
    EXPECT_NEAR(r.ddP95,   49.5288, 1e-3);
}
