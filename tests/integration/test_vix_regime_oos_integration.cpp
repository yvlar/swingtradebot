// ============================================================
//  test_vix_regime_oos_integration.cpp  —  Tests d'INTÉGRATION
//  Cible : VixRegimeBacktester / VixRegimeWalkForward
//  Sprint 14 (item 14.2) — VERDICT OUT-OF-SAMPLE VERROUILLÉ.
//
//  5e famille, variante à VOLATILITÉ IMPLICITE (^VIX). On TRADE QQQ mais on gate
//  l'exposition sur le niveau du VIX (vol implicite, exogène, anticipatrice) vs sa
//  propre médiane glissante : long QQQ en régime CALME (VIX bas), cash sinon.
//  Contraste avec le Sprint 13 (vol RÉALISÉE, dérivée du prix) : le VIX est un
//  signal EXTERNE, forward-looking.
//
//  Question : ce filtre bat-il le Buy & Hold de QQQ sur le rendement AJUSTÉ DU
//  RISQUE (Sharpe) NET DE COÛTS en OOS ? Critère PRIMAIRE = delta de Sharpe vs B&H.
//  Sanity D23 = alpha vs B&H. Clause DoD « DD réduit ≥ 50 % » = DD strat vs DD B&H.
//
//  Discipline (D33/D34) : config explicite, trades OOS poolés (comptes verrouillés),
//  garde anti-cash-drag (D47), sentinelle → mesure → figée. Données longues
//  total-return (*_max.csv) + VIX_max.csv, 3 pavages (canonique/fin/décalé, D36).
//  Warmup = refLookback − 1 = 125 → OOS ≥ 400 (D35).
//
//  Valeurs MESURÉES le 2026-07-06 sur QQQ_max ∩ VIX_max et figées.
// ============================================================
#include <gtest/gtest.h>
#include <cmath>
#include <string>
#include <vector>
#include <iostream>
#include <iomanip>
#include "backtest/VixRegimeBacktester.hpp"
#include "backtest/MonteCarlo.hpp"

using namespace trading;

namespace {

constexpr size_t kIsCan = 750, kOosCan = 500, kStepCan = 500;
constexpr size_t kIsFin = 550, kOosFin = 400, kStepFin = 400;
constexpr size_t kIsShift = 750, kOosShift = 500, kStepShift = 500, kOffShift = 90;

// Config EXPLICITE (D33). Médiane de référence du VIX sur 126 j (~6 mois), seuil 1,0.
VixRegimeConfig cfgVix() {
    VixRegimeConfig c;
    c.refLookback    = 126;
    c.thresholdMult  = 1.0;
    c.initialCapital = 10'000.0;
    c.commissionPct  = 0.001;
    c.slippageBps    = 2.0;
    c.halfSpreadBps  = 0.5;
    return c;
}

std::vector<std::string> pairQqqVix() { return { SWINGBOT_QQQ_MAX_CSV, SWINGBOT_VIX_MAX_CSV }; }

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
    std::vector<TradeRecord> pool;
    double years = 0.0;
    double sharpeDelta() const { return meanSharpe - meanBhSharpe; }
};

AggOos agregeOos(const std::vector<VixWindow>& ws) {
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
    }
    return a;
}

void imprime(const char* nom, const AggOos& a) {
    std::cout << std::fixed << std::setprecision(4)
              << "  VIXREG " << nom << " : fen=" << a.windows
              << " ret=" << a.meanTotalReturn
              << " sharpe=" << a.meanSharpe
              << " bhSharpe=" << a.meanBhSharpe
              << " dSharpe=" << a.sharpeDelta()
              << " sortino=" << a.meanSortino
              << " DD=" << a.meanMaxDd
              << " bhDD=" << a.meanBhDd
              << " alphaBH=" << a.meanAlphaVsBh
              << " trades=" << a.pool.size()
              << " annees=" << a.years << "\n";
}

AggOos paving(const std::vector<std::string>& csvs,
              size_t is, size_t oos, size_t step, size_t off = 0) {
    return agregeOos(VixRegimeWalkForward(cfgVix(), csvs, is, oos, step, off).run());
}

} // namespace

// ════════════════════════════════════════════════════════════
//  Axe commun QQQ ∩ VIX verrouillé (le VIX ~1990 déborde QQQ ~1999 → borné par QQQ).
// ════════════════════════════════════════════════════════════
TEST(VixRegimeOosIntegration, VixRegimeAxisIsLocked) {
    VixRegimeBacktester v(cfgVix(), pairQqqVix());
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
TEST(VixRegimeOosIntegration, VixRegimeCanonicalPavingVerdictIsLocked) {
    const auto a = paving(pairQqqVix(), kIsCan, kOosCan, kStepCan);
    imprime("CANON", a);

    ASSERT_GE(a.windows, 2u);
    EXPECT_TRUE(std::isfinite(a.meanSharpe));
    EXPECT_GT(a.pool.size(), 1u);          // D47 : la famille TRADE réellement

    // Verdict : AUCUN edge ajusté du risque, comme la vol RÉALISÉE (Sprint 13). Le
    // Sharpe stratégie (0,55) est POSITIF mais SOUS le Sharpe B&H (1,06) → dSharpe < 0.
    // La vol IMPLICITE (VIX, anticipatrice) ne fait pas mieux que la vol réalisée.
    // DD réduit (13,4 vs 18,6) mais < 50 %, alpha négatif (cash drag T4/D48).
    EXPECT_LT(a.meanSharpe, a.meanBhSharpe);   // pas d'edge Sharpe vs B&H
    EXPECT_LT(a.meanAlphaVsBh, 0.0);           // sanity D23 : sous-performe le B&H
    EXPECT_LT(a.meanMaxDd, a.meanBhDd);        // DD réduit…
    EXPECT_GT(a.meanMaxDd, 0.5 * a.meanBhDd);  // …mais < 50 % (clause DoD NON atteinte)

    EXPECT_EQ(a.windows, 12u);
    EXPECT_NEAR(a.meanTotalReturn,  11.0692, 1e-2);
    EXPECT_NEAR(a.meanSharpe,        0.5457, 1e-2);
    EXPECT_NEAR(a.meanSortino,       0.8051, 1e-2);
    EXPECT_NEAR(a.meanMaxDd,        13.3594, 1e-2);
    EXPECT_NEAR(a.meanBhDd,         18.6350, 1e-2);
    EXPECT_NEAR(a.meanBhSharpe,      1.0638, 1e-2);
    EXPECT_NEAR(a.meanAlphaVsBh,   -18.8798, 1e-2);
    EXPECT_EQ(a.pool.size(), 216u);
}

// ════════════════════════════════════════════════════════════
//  Pavage FIN
// ════════════════════════════════════════════════════════════
TEST(VixRegimeOosIntegration, VixRegimeFinePavingVerdictIsLocked) {
    const auto a = paving(pairQqqVix(), kIsFin, kOosFin, kStepFin);
    imprime("FIN", a);

    ASSERT_GE(a.windows, 2u);
    EXPECT_GT(a.pool.size(), 1u);
    EXPECT_LT(a.meanSharpe, a.meanBhSharpe);   // pas d'edge Sharpe vs B&H
    EXPECT_LT(a.meanAlphaVsBh, 0.0);

    EXPECT_EQ(a.windows, 15u);
    EXPECT_NEAR(a.meanTotalReturn,   3.3925, 1e-2);
    EXPECT_NEAR(a.meanSharpe,        0.4160, 1e-2);
    EXPECT_NEAR(a.meanSortino,       0.6104, 1e-2);
    EXPECT_NEAR(a.meanMaxDd,        13.7719, 1e-2);
    EXPECT_NEAR(a.meanBhDd,         18.6328, 1e-2);
    EXPECT_NEAR(a.meanBhSharpe,      0.8749, 1e-2);
    EXPECT_NEAR(a.meanAlphaVsBh,   -10.8850, 1e-2);
    EXPECT_EQ(a.pool.size(), 195u);
}

// ════════════════════════════════════════════════════════════
//  Pavage DÉCALÉ (offset 90)
// ════════════════════════════════════════════════════════════
TEST(VixRegimeOosIntegration, VixRegimeShiftedPavingVerdictIsLocked) {
    const auto a = paving(pairQqqVix(), kIsShift, kOosShift, kStepShift, kOffShift);
    imprime("SHIFT", a);

    ASSERT_GE(a.windows, 2u);
    EXPECT_GT(a.pool.size(), 1u);
    EXPECT_LT(a.meanSharpe, a.meanBhSharpe);   // pas d'edge (pavage non aligné)
    EXPECT_LT(a.meanAlphaVsBh, 0.0);

    EXPECT_EQ(a.windows, 12u);
    EXPECT_NEAR(a.meanTotalReturn,   8.4412, 1e-2);
    EXPECT_NEAR(a.meanSharpe,        0.4545, 1e-2);
    EXPECT_NEAR(a.meanSortino,       0.6659, 1e-2);
    EXPECT_NEAR(a.meanMaxDd,        12.2235, 1e-2);
    EXPECT_NEAR(a.meanBhDd,         17.5178, 1e-2);
    EXPECT_NEAR(a.meanBhSharpe,      0.9801, 1e-2);
    EXPECT_NEAR(a.meanAlphaVsBh,   -21.5595, 1e-2);
    EXPECT_EQ(a.pool.size(), 227u);
}

// ════════════════════════════════════════════════════════════
//  Monte-Carlo size-aware (D45) sur les trades OOS poolés (canonique).
// ════════════════════════════════════════════════════════════
TEST(VixRegimeOosIntegration, VixRegimeMonteCarloSizeAwareIsLocked) {
    const auto a = paving(pairQqqVix(), kIsCan, kOosCan, kStepCan);
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
              << "  VIXREG MC : cagrP50=" << r.cagrP50 << " ddP95=" << r.ddP95 << "\n";
    // Positif (cagrP50 ~5 %) : le filtre capte une partie de la hausse de QQQ, mais
    // ce n'est PAS le critère (le B&H fait mieux en Sharpe). Cohérent avec VolRegime.
    EXPECT_NEAR(r.cagrP50, 5.1546, 1e-3);
    EXPECT_NEAR(r.ddP95,  40.3497, 1e-3);
}

// ════════════════════════════════════════════════════════════
//  Multi-univers (pavage fin) : QQQ/SPY/IWM/MDY TRADÉS, tous gatés sur le MÊME VIX
//  (indice de vol du marché). Le régime VIX aide-t-il un actif quelconque ?
// ════════════════════════════════════════════════════════════
TEST(VixRegimeOosIntegration, VixRegimeMultiUniverseVerdictIsLocked) {
    struct U { const char* nom; const char* csv;
               double sharpeAttendu; double retAttendu; size_t tradesAttendus; };
    const U univ[] = {
        {"QQQ", SWINGBOT_QQQ_MAX_CSV,  0.4160,  3.3925, 195u},
        {"SPY", SWINGBOT_SPY_MAX_CSV, -0.0116, -0.8772, 264u},
        {"IWM", SWINGBOT_IWM_MAX_CSV,  0.3809,  6.0049, 213u},
        {"MDY", SWINGBOT_MDY_MAX_CSV,  0.2784,  5.9062, 263u},
    };
    std::cout << std::fixed << std::setprecision(4) << "  VIXREG multi-univers (fin)\n";
    for (const auto& u : univ) {
        const auto a = paving({ u.csv, SWINGBOT_VIX_MAX_CSV }, kIsFin, kOosFin, kStepFin);
        std::cout << "    " << u.nom << " : sharpe=" << a.meanSharpe
                  << " bhSharpe=" << a.meanBhSharpe
                  << " ret=" << a.meanTotalReturn
                  << " trades=" << a.pool.size() << "\n";
        EXPECT_GT(a.pool.size(), 1u);
        EXPECT_NEAR(a.meanSharpe,      u.sharpeAttendu, 1e-2);
        EXPECT_NEAR(a.meanTotalReturn, u.retAttendu,    1e-2);
        EXPECT_EQ(a.pool.size(), u.tradesAttendus);
        EXPECT_LT(a.meanSharpe, a.meanBhSharpe);   // aucun edge sur aucun actif gaté VIX
    }
}

// ════════════════════════════════════════════════════════════
//  Balayage refLookback × thresholdMult : critère = delta de Sharpe OOS vs B&H.
// ════════════════════════════════════════════════════════════
TEST(VixRegimeOosIntegration, VixRegimeThresholdSweepBestOosIsLocked) {
    const int    refs[]  = {63, 126, 252};
    const double mults[] = {0.8, 1.0, 1.2};
    double best = -1e9; int bestR = 0; double bestM = 0.0;
    std::cout << std::fixed << std::setprecision(4)
              << "  VIXREG balayage refLookback x thresholdMult (fin QQQ/VIX, critere = dSharpe OOS)\n";
    for (int rl : refs) for (double mm : mults) {
        VixRegimeConfig c = cfgVix(); c.refLookback = rl; c.thresholdMult = mm;
        const auto a = agregeOos(VixRegimeWalkForward(c, pairQqqVix(),
                                                      kIsFin, kOosFin, kStepFin).run());
        std::cout << "    rl=" << rl << " m=" << mm
                  << " -> dSharpe=" << a.sharpeDelta() << "\n";
        if (a.sharpeDelta() > best) { best = a.sharpeDelta(); bestR = rl; bestM = mm; }
    }
    std::cout << "    MEILLEUR : rl=" << bestR << " m=" << bestM
              << " -> dSharpe OOS " << best
              << (best > 0.0 ? "  (CANDIDAT > 0)" : "  (aucun candidat)") << "\n";

    // VERDICT 14.2 (figé) : AUCUN réglage ne bat le B&H sur le Sharpe — le meilleur
    // (refLookback=126 / seuil=1,2) reste à dSharpe -0,22. Gate de confirmation FERMÉ.
    EXPECT_NEAR(best, -0.2171, 1e-2);
    EXPECT_EQ(bestR, 126);
    EXPECT_DOUBLE_EQ(bestM, 1.2);
    EXPECT_FALSE(best > 0.0);   // pas de candidat -> pas de confirmation
}
