// ============================================================
//  test_vix_term_scaled_oos_integration.cpp  —  Tests d'INTÉGRATION
//  Cible : VixTermScaledBacktester / VixTermScaledWalkForward
//  Sprint 19 (item 19.4) — VERDICT OUT-OF-SAMPLE VERROUILLÉ.
//
//  Piste §5.3, variante CONTINUE : le poids d'exposition suit la forme de la
//  courbe VIX/VIX3M — w = min(1, (VIX3M/VIX)^steepness). Leçon D55 : le cash
//  drag est le vrai coût du binaire ; ici le contango (≈ 85 % des séances)
//  donne w = 1 (zéro drag en régime normal), seule l'inversion réduit le poids.
//
//  Question : ce scaling bat-il le Buy & Hold de QQQ sur le rendement AJUSTÉ DU
//  RISQUE (Sharpe) NET DE COÛTS en OOS ? Critère PRIMAIRE = delta de Sharpe vs
//  B&H. Sanity D23 = alpha vs B&H. Clause DoD « DD réduit ≥ 50 % ».
//
//  Discipline (D33/D34) : config explicite, stints OOS poolés (comptes
//  verrouillés), garde d'activation (D47), sentinelle → mesure → figée.
//  Données longues (*_max.csv) + VIX_max.csv + VIX3M_max.csv, 3 pavages.
//  Aucun warmup (ratio instantané) → D35 sans objet. Axe borné par le VIX3M
//  (~2006) : SANS l'épisode dot-com.
//
//  Valeurs MESURÉES le 2026-07-09 sur QQQ_max ∩ VIX_max ∩ VIX3M_max et figées.
// ============================================================
#include <gtest/gtest.h>
#include <cmath>
#include <string>
#include <vector>
#include <iostream>
#include <iomanip>
#include "backtest/VixTermScaledBacktester.hpp"
#include "backtest/MonteCarlo.hpp"

using namespace trading;

namespace {

constexpr size_t kIsCan = 750, kOosCan = 500, kStepCan = 500;
constexpr size_t kIsFin = 550, kOosFin = 400, kStepFin = 400;
constexpr size_t kIsShift = 750, kOosShift = 500, kStepShift = 500, kOffShift = 90;

// Config EXPLICITE (D33). Pente 1 (ratio brut), pas de levier, bande 0,05.
VixTermScaledConfig cfgTs() {
    VixTermScaledConfig c;
    c.steepness      = 1.0;
    c.maxWeight      = 1.0;
    c.rebalanceBand  = 0.05;
    c.initialCapital = 10'000.0;
    c.commissionPct  = 0.001;
    c.slippageBps    = 2.0;
    c.halfSpreadBps  = 0.5;
    return c;
}

std::vector<std::string> trioQqqVixV3m() {
    return { SWINGBOT_QQQ_MAX_CSV, SWINGBOT_VIX_MAX_CSV, SWINGBOT_VIX3M_MAX_CSV };
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
    double meanTotalReturn = 0.0, meanSharpe = 0.0, meanSortino = 0.0;
    double meanMaxDd = 0.0, meanBhDd = 0.0, meanBhSharpe = 0.0, meanAlphaVsBh = 0.0;
    double meanAvgWeight = 0.0, meanTurnover = 0.0;
    std::vector<TradeRecord> pool;
    double years = 0.0;
    double sharpeDelta() const { return meanSharpe - meanBhSharpe; }
};

AggOos agregeOos(const std::vector<VixTermScaledWindow>& ws) {
    AggOos a;
    a.windows = ws.size();
    for (const auto& w : ws) {
        a.meanTotalReturn += w.oos.totalReturnPct;
        a.meanSharpe      += w.oos.sharpeRatio;
        a.meanSortino     += w.oos.sortinoRatio;
        a.meanMaxDd       += w.oos.maxDrawdownPct;
        a.meanBhDd        += w.oos.buyHoldMaxDrawdownPct;
        a.meanBhSharpe    += w.oos.buyHoldSharpe;
        a.meanAlphaVsBh   += w.oos.alphaVsBuyHold;
        a.meanAvgWeight   += w.oos.avgWeight;
        a.meanTurnover    += w.oos.turnover;
        for (const auto& t : w.oos.trades) a.pool.push_back(t);
        if (w.oos.equityDates.size() >= 2) {
            const long d0 = joursCivils(w.oos.equityDates.front());
            const long d1 = joursCivils(w.oos.equityDates.back());
            if (d1 > d0) a.years += static_cast<double>(d1 - d0) / 365.25;
        }
    }
    if (a.windows > 0) {
        const double n = static_cast<double>(a.windows);
        a.meanTotalReturn /= n; a.meanSharpe /= n; a.meanSortino /= n;
        a.meanMaxDd /= n; a.meanBhDd /= n; a.meanBhSharpe /= n; a.meanAlphaVsBh /= n;
        a.meanAvgWeight /= n; a.meanTurnover /= n;
    }
    return a;
}

void imprime(const char* nom, const AggOos& a) {
    std::cout << std::fixed << std::setprecision(4)
              << "  VIXTERMSC " << nom << " : fen=" << a.windows
              << " ret=" << a.meanTotalReturn
              << " sharpe=" << a.meanSharpe
              << " bhSharpe=" << a.meanBhSharpe
              << " dSharpe=" << a.sharpeDelta()
              << " DD=" << a.meanMaxDd
              << " bhDD=" << a.meanBhDd
              << " alphaBH=" << a.meanAlphaVsBh
              << " poids=" << a.meanAvgWeight
              << " churn=" << a.meanTurnover
              << " stints=" << a.pool.size()
              << " annees=" << a.years << "\n";
}

AggOos paving(const std::vector<std::string>& csvs,
              size_t is, size_t oos, size_t step, size_t off = 0) {
    return agregeOos(VixTermScaledWalkForward(cfgTs(), csvs, is, oos, step, off).run());
}

} // namespace

// ════════════════════════════════════════════════════════════
//  Axe commun QQQ ∩ VIX ∩ VIX3M (le même que le binaire 19.2 : borné VIX3M 2006).
// ════════════════════════════════════════════════════════════
TEST(VixTermScaledOosIntegration, VixTermScaledAxisIsLocked) {
    VixTermScaledBacktester v(cfgTs(), trioQqqVixV3m());
    const auto& ax = v.axis();
    ASSERT_EQ(ax.assets(), 3u);
    EXPECT_EQ(ax.dates.front(), "2006-07-17");
    EXPECT_EQ(ax.dates.back(),  "2026-07-01");
    EXPECT_EQ(ax.size(), 5021u);
    for (const auto& col : ax.close) EXPECT_EQ(col.size(), ax.size());
}

// ════════════════════════════════════════════════════════════
//  Pavage CANONIQUE
// ════════════════════════════════════════════════════════════
TEST(VixTermScaledOosIntegration, VixTermScaledCanonicalPavingVerdictIsLocked) {
    const auto a = paving(trioQqqVixV3m(), kIsCan, kOosCan, kStepCan);
    imprime("CANON", a);

    ASSERT_GE(a.windows, 2u);
    EXPECT_TRUE(std::isfinite(a.meanSharpe));
    EXPECT_GT(a.pool.size(), 1u);          // D47 : le scaling TRADE réellement

    // Verdict : AUCUN edge au sens de la DoD — mais le profil le plus « propre »
    // du projet : dSharpe ≥ 0 sur les 3 pavages (+0,002 ici), du jamais-vu,
    // SAUF que l'ampleur (≤ 0,02) est du bruit, l'alpha absolu reste NÉGATIF
    // (−1,8) et le DD n'est réduit que de ~3 % (clause ≥ 50 % très loin).
    // Poids moyen 0,98 : le moteur est un quasi-B&H à micro-écrêtage de stress.
    EXPECT_GT(a.sharpeDelta(), 0.0);           // ≥ 0 mais…
    EXPECT_LT(a.sharpeDelta(), 0.05);          // …ordre de grandeur du bruit
    EXPECT_LT(a.meanAlphaVsBh, 0.0);           // sanity D23 : sous le B&H en rendement
    EXPECT_GT(a.meanMaxDd, 0.5 * a.meanBhDd);  // clause DoD « DD réduit ≥ 50 % » NON atteinte
    EXPECT_GT(a.meanAvgWeight, 0.9);           // quasi-B&H : zéro cash drag en contango

    EXPECT_EQ(a.windows, 8u);
    EXPECT_NEAR(a.meanTotalReturn,  42.6080, 1e-2);
    EXPECT_NEAR(a.meanSharpe,        1.0457, 1e-2);
    EXPECT_NEAR(a.meanBhSharpe,      1.0437, 1e-2);
    EXPECT_NEAR(a.meanMaxDd,        19.9631, 1e-2);
    EXPECT_NEAR(a.meanBhDd,         20.6631, 1e-2);
    EXPECT_NEAR(a.meanAlphaVsBh,    -1.8084, 1e-2);
    EXPECT_NEAR(a.meanAvgWeight,     0.9779, 1e-3);
    EXPECT_NEAR(a.meanTurnover,      2.6121, 1e-2);
    EXPECT_EQ(a.pool.size(), 67u);
}

// ════════════════════════════════════════════════════════════
//  Pavage FIN
// ════════════════════════════════════════════════════════════
TEST(VixTermScaledOosIntegration, VixTermScaledFinePavingVerdictIsLocked) {
    const auto a = paving(trioQqqVixV3m(), kIsFin, kOosFin, kStepFin);
    imprime("FIN", a);

    ASSERT_GE(a.windows, 2u);
    EXPECT_GT(a.pool.size(), 1u);
    EXPECT_GT(a.sharpeDelta(), 0.0);
    EXPECT_LT(a.sharpeDelta(), 0.05);
    EXPECT_LT(a.meanAlphaVsBh, 0.0);

    EXPECT_EQ(a.windows, 11u);
    EXPECT_NEAR(a.meanTotalReturn,  30.7099, 1e-2);
    EXPECT_NEAR(a.meanSharpe,        0.9925, 1e-2);
    EXPECT_NEAR(a.meanBhSharpe,      0.9888, 1e-2);
    EXPECT_NEAR(a.meanMaxDd,        19.5815, 1e-2);
    EXPECT_NEAR(a.meanBhDd,         20.3476, 1e-2);
    EXPECT_NEAR(a.meanAlphaVsBh,    -0.8687, 1e-2);
    EXPECT_NEAR(a.meanAvgWeight,     0.9807, 1e-3);
    EXPECT_EQ(a.pool.size(), 87u);
}

// ════════════════════════════════════════════════════════════
//  Pavage DÉCALÉ (offset 90)
// ════════════════════════════════════════════════════════════
TEST(VixTermScaledOosIntegration, VixTermScaledShiftedPavingVerdictIsLocked) {
    const auto a = paving(trioQqqVixV3m(), kIsShift, kOosShift, kStepShift, kOffShift);
    imprime("SHIFT", a);

    ASSERT_GE(a.windows, 2u);
    EXPECT_GT(a.pool.size(), 1u);
    EXPECT_GT(a.sharpeDelta(), 0.0);
    EXPECT_LT(a.sharpeDelta(), 0.05);
    EXPECT_LT(a.meanAlphaVsBh, 0.0);

    EXPECT_EQ(a.windows, 8u);
    EXPECT_NEAR(a.meanTotalReturn,  41.8612, 1e-2);
    EXPECT_NEAR(a.meanSharpe,        1.0162, 1e-2);
    EXPECT_NEAR(a.meanBhSharpe,      1.0124, 1e-2);
    EXPECT_NEAR(a.meanMaxDd,        20.2186, 1e-2);
    EXPECT_NEAR(a.meanBhDd,         20.8782, 1e-2);
    EXPECT_NEAR(a.meanAlphaVsBh,    -1.7061, 1e-2);
    EXPECT_NEAR(a.meanAvgWeight,     0.9852, 1e-3);
    EXPECT_EQ(a.pool.size(), 71u);
}

// ════════════════════════════════════════════════════════════
//  Monte-Carlo size-aware (D45) sur les stints OOS poolés (canonique).
// ════════════════════════════════════════════════════════════
TEST(VixTermScaledOosIntegration, VixTermScaledMonteCarloSizeAwareIsLocked) {
    const auto a = paving(trioQqqVixV3m(), kIsCan, kOosCan, kStepCan);
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
              << "  VIXTERMSC MC : cagrP50=" << r.cagrP50 << " ddP95=" << r.ddP95 << "\n";
    // cagrP50 ~18,6 % : quasi tout le B&H est capté (poids moyen 0,98) — mais
    // l'alpha vs B&H reste négatif, ce n'est pas le critère.
    EXPECT_NEAR(r.cagrP50, 18.5755, 1e-3);
    EXPECT_NEAR(r.ddP95,  37.9145, 1e-3);
}

// ════════════════════════════════════════════════════════════
//  Balayage steepness × bande : critère = delta de Sharpe OOS vs B&H.
// ════════════════════════════════════════════════════════════
TEST(VixTermScaledOosIntegration, VixTermScaledSteepnessSweepBestOosIsLocked) {
    const double pentes[] = {1.0, 2.0, 3.0};
    const double bandes[] = {0.02, 0.05, 0.10};
    double best = -1e9; double bestP = 0.0, bestB = 0.0;
    std::cout << std::fixed << std::setprecision(4)
              << "  VIXTERMSC balayage pente x bande (fin QQQ/VIX/VIX3M, critere = dSharpe OOS)\n";
    for (double p : pentes) for (double b : bandes) {
        VixTermScaledConfig c = cfgTs(); c.steepness = p; c.rebalanceBand = b;
        const auto a = agregeOos(VixTermScaledWalkForward(c, trioQqqVixV3m(),
                                                          kIsFin, kOosFin, kStepFin).run());
        std::cout << "    pente=" << p << " bande=" << b
                  << " -> dSharpe=" << a.sharpeDelta()
                  << " alpha=" << a.meanAlphaVsBh
                  << " stints=" << a.pool.size() << "\n";
        if (a.sharpeDelta() > best) { best = a.sharpeDelta(); bestP = p; bestB = b; }
    }
    std::cout << "    MEILLEUR : pente=" << bestP << " bande=" << bestB
              << " -> dSharpe OOS " << best
              << (best > 0.0 ? "  (CANDIDAT > 0)" : "  (aucun candidat)") << "\n";

    // VERDICT 19.4 (figé) : les 9 réglages donnent un dSharpe ≥ 0 (cohérence
    // inédite dans le projet) mais TOUS ≤ 0,03 (bruit) et l'ALPHA absolu est
    // négatif sur les 9 (sanity D23). Le meilleur (pente 3, bande 0,05,
    // +0,021) reste positif sur les pavages non choisis (+0,006 canon /
    // +0,010 décalé — sondé le 2026-07-09) mais son alpha canonique est −2,6 :
    // la DoD d'edge (alpha ABSOLU positif, robuste) n'est PAS atteinte →
    // AUCUNE adoption (D43).
    EXPECT_NEAR(best, 0.0214, 1e-2);
    EXPECT_DOUBLE_EQ(bestP, 3.0);
    EXPECT_DOUBLE_EQ(bestB, 0.05);
    EXPECT_FALSE(best > 0.05);   // jamais au-dessus du bruit → pas de candidat d'edge
}
