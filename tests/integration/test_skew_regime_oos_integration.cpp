// ============================================================
//  test_skew_regime_oos_integration.cpp  —  Tests d'INTÉGRATION
//  Cible : VixRegimeBacktester / VixRegimeWalkForward (RÉUTILISÉS tels quels)
//  Sprint 20 (item 20.1) — VERDICT OUT-OF-SAMPLE VERROUILLÉ.
//
//  Famille SURFACE D'OPTIONS (piste §5.4, variante indices gratuits — décision
//  utilisateur 2026-07-09), signal 1/2 : ^SKEW (CBOE Skew Index, prix de la
//  protection de queue dérivé de la surface d'options OTM du S&P 500). On TRADE
//  QQQ mais on gate l'exposition sur le niveau du SKEW vs sa propre médiane
//  glissante : long QQQ quand la protection de queue est BON MARCHÉ (SKEW bas),
//  cash quand elle est chère (stress pricé par le marché d'options).
//
//  RÉUTILISATION DE MOTEUR : le VixRegimeBacktester (Sprint 14, D51) est
//  agnostique au signal — csvPaths = { actif TRADÉ, indice SIGNAL } ; seule la
//  série change (SKEW_max.csv au lieu de VIX_max.csv). Aucun nouveau code de
//  prod : le moteur reste verrouillé par VixRegimeBacktesterUnit et
//  VixRegimeOosIntegration ; ces verrous-ci ne figent que le VERDICT des
//  nouvelles DONNÉES.
//
//  LIMITE (D43) : seule la direction CANONIQUE du gate est jugée (long ssi
//  signal ≤ mult × médiane). Le balayage de thresholdMult déplace la frontière
//  mais n'INVERSE pas la direction — « long quand SKEW > médiane » est une
//  hypothèse DISTINCTE, non jugée ce sprint.
//
//  Couverture d'axe : QQQ ∩ SKEW ~1999+ (borné par QQQ, comme l'axe VIX du
//  Sprint 14) — dot-com ET 2008 inclus. Ce n'est PAS une première : c'est la
//  même couverture que le vix-regime, contrairement au term-structure D57
//  (2006+, sans dot-com).
//
//  Question : ce filtre bat-il le Buy & Hold de QQQ sur le rendement AJUSTÉ DU
//  RISQUE (Sharpe) NET DE COÛTS en OOS ? Critère PRIMAIRE = delta de Sharpe vs B&H.
//  Sanity D23 = alpha vs B&H. Clause DoD « DD réduit ≥ 50 % » = DD strat vs DD B&H.
//
//  Discipline (D33/D34) : config explicite, trades OOS poolés (comptes verrouillés),
//  garde anti-cash-drag (D47), sentinelle → mesure → figée. Données longues
//  total-return (*_max.csv) + SKEW_max.csv, 3 pavages (canonique/fin/décalé, D36).
//  Warmup = refLookback − 1 = 125 → OOS ≥ 400 (D35).
//
//  Valeurs MESURÉES le 2026-07-09 sur QQQ_max ∩ SKEW_max et figées.
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

// Config EXPLICITE (D33), identique au précédent D51 : médiane de référence du
// SKEW sur 126 j (~6 mois), seuil 1,0.
VixRegimeConfig cfgSkew() {
    VixRegimeConfig c;
    c.refLookback    = 126;
    c.thresholdMult  = 1.0;
    c.initialCapital = 10'000.0;
    c.commissionPct  = 0.001;
    c.slippageBps    = 2.0;
    c.halfSpreadBps  = 0.5;
    return c;
}

std::vector<std::string> pairQqqSkew() { return { SWINGBOT_QQQ_MAX_CSV, SWINGBOT_SKEW_MAX_CSV }; }

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
              << "  SKEWREG " << nom << " : fen=" << a.windows
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
    return agregeOos(VixRegimeWalkForward(cfgSkew(), csvs, is, oos, step, off).run());
}

} // namespace

// ════════════════════════════════════════════════════════════
//  Axe commun QQQ ∩ SKEW verrouillé (le SKEW ~1990 déborde QQQ ~1999 → borné par QQQ).
// ════════════════════════════════════════════════════════════
TEST(SkewRegimeOosIntegration, SkewRegimeAxisIsLocked) {
    VixRegimeBacktester v(cfgSkew(), pairQqqSkew());
    const auto& ax = v.axis();
    ASSERT_EQ(ax.assets(), 2u);
    EXPECT_EQ(ax.dates.front(), "1999-03-10");
    EXPECT_EQ(ax.dates.back(),  "2026-07-01");
    EXPECT_EQ(ax.size(), 6797u);           // QQQ ∩ SKEW, borné par QQQ (1999)
    for (const auto& col : ax.close) EXPECT_EQ(col.size(), ax.size());
}

// ════════════════════════════════════════════════════════════
//  Pavage CANONIQUE
// ════════════════════════════════════════════════════════════
TEST(SkewRegimeOosIntegration, SkewRegimeCanonicalPavingVerdictIsLocked) {
    const auto a = paving(pairQqqSkew(), kIsCan, kOosCan, kStepCan);
    imprime("CANON", a);

    ASSERT_GE(a.windows, 2u);
    EXPECT_TRUE(std::isfinite(a.meanSharpe));
    EXPECT_GT(a.pool.size(), 1u);          // D47 : la famille TRADE réellement

    // Verdict : AUCUN edge ajusté du risque — le PIRE dSharpe des filtres de
    // régime externes (−0,68 vs −0,54 pour le VIX D51) : le SKEW croise sa
    // médiane bien plus souvent que le VIX (343 A/R vs 216) → churn de coûts,
    // sans que la protection de queue chère prédise mieux les baisses.
    // DD réduit (16,6 vs 18,9) mais << 50 %, alpha négatif (cash drag T4/D48).
    EXPECT_LT(a.meanSharpe, a.meanBhSharpe);   // pas d'edge Sharpe vs B&H
    EXPECT_LT(a.meanAlphaVsBh, 0.0);           // sanity D23 : sous-performe le B&H
    EXPECT_LT(a.meanMaxDd, a.meanBhDd);        // DD réduit…
    EXPECT_GT(a.meanMaxDd, 0.5 * a.meanBhDd);  // …mais < 50 % (clause DoD NON atteinte)

    EXPECT_EQ(a.windows, 12u);
    EXPECT_NEAR(a.meanTotalReturn,   4.1160, 1e-2);
    EXPECT_NEAR(a.meanSharpe,        0.2741, 1e-2);
    EXPECT_NEAR(a.meanSortino,       0.4449, 1e-2);
    EXPECT_NEAR(a.meanMaxDd,        16.5740, 1e-2);
    EXPECT_NEAR(a.meanBhDd,         18.8882, 1e-2);
    EXPECT_NEAR(a.meanBhSharpe,      0.9588, 1e-2);
    EXPECT_NEAR(a.meanAlphaVsBh,   -23.2058, 1e-2);
    EXPECT_EQ(a.pool.size(), 343u);
}

// ════════════════════════════════════════════════════════════
//  Pavage FIN
// ════════════════════════════════════════════════════════════
TEST(SkewRegimeOosIntegration, SkewRegimeFinePavingVerdictIsLocked) {
    const auto a = paving(pairQqqSkew(), kIsFin, kOosFin, kStepFin);
    imprime("FIN", a);

    ASSERT_GE(a.windows, 2u);
    EXPECT_GT(a.pool.size(), 1u);

    EXPECT_LT(a.meanSharpe, a.meanBhSharpe);   // pas d'edge Sharpe vs B&H
    EXPECT_LT(a.meanAlphaVsBh, 0.0);

    EXPECT_EQ(a.windows, 15u);
    EXPECT_NEAR(a.meanTotalReturn,   1.0049, 1e-2);
    EXPECT_NEAR(a.meanSharpe,        0.2342, 1e-2);
    EXPECT_NEAR(a.meanSortino,       0.4216, 1e-2);
    EXPECT_NEAR(a.meanMaxDd,        16.8082, 1e-2);
    EXPECT_NEAR(a.meanBhDd,         19.6242, 1e-2);
    EXPECT_NEAR(a.meanBhSharpe,      0.7863, 1e-2);
    EXPECT_NEAR(a.meanAlphaVsBh,   -12.0384, 1e-2);
    EXPECT_EQ(a.pool.size(), 311u);
}

// ════════════════════════════════════════════════════════════
//  Pavage DÉCALÉ (offset 90)
// ════════════════════════════════════════════════════════════
TEST(SkewRegimeOosIntegration, SkewRegimeShiftedPavingVerdictIsLocked) {
    const auto a = paving(pairQqqSkew(), kIsShift, kOosShift, kStepShift, kOffShift);
    imprime("SHIFT", a);

    ASSERT_GE(a.windows, 2u);
    EXPECT_GT(a.pool.size(), 1u);

    EXPECT_LT(a.meanSharpe, a.meanBhSharpe);   // pas d'edge (pavage non aligné)
    EXPECT_LT(a.meanAlphaVsBh, 0.0);

    EXPECT_EQ(a.windows, 11u);
    EXPECT_NEAR(a.meanTotalReturn,   3.5317, 1e-2);
    EXPECT_NEAR(a.meanSharpe,        0.2686, 1e-2);
    EXPECT_NEAR(a.meanSortino,       0.4477, 1e-2);
    EXPECT_NEAR(a.meanMaxDd,        15.2546, 1e-2);
    EXPECT_NEAR(a.meanBhDd,         17.8688, 1e-2);
    EXPECT_NEAR(a.meanBhSharpe,      0.9356, 1e-2);
    EXPECT_NEAR(a.meanAlphaVsBh,   -25.2565, 1e-2);
    EXPECT_EQ(a.pool.size(), 300u);
}

// ════════════════════════════════════════════════════════════
//  Monte-Carlo size-aware (D45) sur les trades OOS poolés (canonique).
// ════════════════════════════════════════════════════════════
TEST(SkewRegimeOosIntegration, SkewRegimeMonteCarloSizeAwareIsLocked) {
    const auto a = paving(pairQqqSkew(), kIsCan, kOosCan, kStepCan);
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
              << "  SKEWREG MC : cagrP50=" << r.cagrP50 << " ddP95=" << r.ddP95 << "\n";
    // Faible (cagrP50 ~1,6 %, contre ~5,2 % pour le VIX D51) et queue de DD
    // lourde (62,5 %) : 343 A/R sur ~24 ans, le churn de coûts pèse.
    EXPECT_NEAR(r.cagrP50, 1.6441, 1e-3);
    EXPECT_NEAR(r.ddP95,  62.5450, 1e-3);
}

// ════════════════════════════════════════════════════════════
//  Multi-univers (pavage fin) : QQQ/SPY/IWM/MDY TRADÉS, tous gatés sur le MÊME SKEW
//  (prix de la protection de queue du marché). Le régime SKEW aide-t-il un actif ?
// ════════════════════════════════════════════════════════════
TEST(SkewRegimeOosIntegration, SkewRegimeMultiUniverseVerdictIsLocked) {
    struct U { const char* nom; const char* csv;
               double sharpeAttendu; double retAttendu; size_t tradesAttendus; };
    const U univ[] = {
        {"QQQ", SWINGBOT_QQQ_MAX_CSV, 0.2342, 1.0049, 311u},
        {"SPY", SWINGBOT_SPY_MAX_CSV, 0.3175, 2.7096, 392u},
        {"IWM", SWINGBOT_IWM_MAX_CSV, 0.3454, 2.1039, 281u},
        {"MDY", SWINGBOT_MDY_MAX_CSV, 0.4549, 5.8098, 376u},
    };
    std::cout << std::fixed << std::setprecision(4) << "  SKEWREG multi-univers (fin)\n";
    for (const auto& u : univ) {
        const auto a = paving({ u.csv, SWINGBOT_SKEW_MAX_CSV }, kIsFin, kOosFin, kStepFin);
        std::cout << "    " << u.nom << " : sharpe=" << a.meanSharpe
                  << " bhSharpe=" << a.meanBhSharpe
                  << " dSharpe=" << a.sharpeDelta()
                  << " ret=" << a.meanTotalReturn
                  << " trades=" << a.pool.size() << "\n";
        EXPECT_GT(a.pool.size(), 1u);
        EXPECT_NEAR(a.meanSharpe,      u.sharpeAttendu, 1e-2);
        EXPECT_NEAR(a.meanTotalReturn, u.retAttendu,    1e-2);
        EXPECT_EQ(a.pool.size(), u.tradesAttendus);
        EXPECT_LT(a.meanSharpe, a.meanBhSharpe);   // aucun edge sur aucun actif gaté SKEW
    }
}

// ════════════════════════════════════════════════════════════
//  Balayage refLookback × thresholdMult : critère = delta de Sharpe OOS vs B&H.
// ════════════════════════════════════════════════════════════
TEST(SkewRegimeOosIntegration, SkewRegimeThresholdSweepBestOosIsLocked) {
    const int    refs[]  = {63, 126, 252};
    const double mults[] = {0.8, 1.0, 1.2};
    double best = -1e9; int bestR = 0; double bestM = 0.0;
    std::cout << std::fixed << std::setprecision(4)
              << "  SKEWREG balayage refLookback x thresholdMult (fin QQQ/SKEW, critere = dSharpe OOS)\n";
    for (int rl : refs) for (double mm : mults) {
        VixRegimeConfig c = cfgSkew(); c.refLookback = rl; c.thresholdMult = mm;
        const auto a = agregeOos(VixRegimeWalkForward(c, pairQqqSkew(),
                                                      kIsFin, kOosFin, kStepFin).run());
        std::cout << "    rl=" << rl << " m=" << mm
                  << " -> dSharpe=" << a.sharpeDelta() << "\n";
        if (a.sharpeDelta() > best) { best = a.sharpeDelta(); bestR = rl; bestM = mm; }
    }
    std::cout << "    MEILLEUR : rl=" << bestR << " m=" << bestM
              << " -> dSharpe OOS " << best
              << (best > 0.0 ? "  (CANDIDAT > 0)" : "  (aucun candidat)") << "\n";

    // VERDICT 20.1 (figé) : AUCUN réglage ne bat le B&H sur le Sharpe — le meilleur
    // (refLookback=63 / seuil=1,2) frôle zéro par en-dessous (dSharpe −0,01) sans le
    // franchir : PAS de candidat > 0 → pas de grille resserrée (contraste D57, où le
    // candidat 1,05 existait puis mourait sur la grille). Gate de confirmation FERMÉ.
    EXPECT_NEAR(best, -0.0141, 1e-2);
    EXPECT_EQ(bestR, 63);
    EXPECT_DOUBLE_EQ(bestM, 1.2);
    EXPECT_FALSE(best > 0.0);   // pas de candidat -> pas de confirmation
}
