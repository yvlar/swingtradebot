// ============================================================
//  test_vvix_regime_oos_integration.cpp  —  Tests d'INTÉGRATION
//  Cible : VixRegimeBacktester / VixRegimeWalkForward (RÉUTILISÉS tels quels)
//  Sprint 20 (item 20.2) — VERDICT OUT-OF-SAMPLE VERROUILLÉ.
//
//  Famille SURFACE D'OPTIONS (piste §5.4, variante indices gratuits — décision
//  utilisateur 2026-07-09), signal 2/2 : ^VVIX (vol-de-vol implicite, dérivée
//  des options SUR le VIX). On TRADE QQQ mais on gate l'exposition sur le
//  niveau du VVIX vs sa propre médiane glissante : long QQQ quand l'incertitude
//  SUR LA VOLATILITÉ elle-même est basse, cash quand elle est haute. Sémantique
//  sans ambiguïté d'inversion (même lecture que le VIX : haut = stress).
//
//  RÉUTILISATION DE MOTEUR : identique à l'item 20.1 — le VixRegimeBacktester
//  (Sprint 14, D51) est agnostique au signal ; seule la série change
//  (VVIX_max.csv). Aucun code de prod nouveau, aucun test unitaire nouveau.
//
//  LIMITE (D43) : seule la direction CANONIQUE du gate est jugée (long ssi
//  signal ≤ mult × médiane) ; le balayage déplace la frontière, ne l'inverse pas.
//
//  Couverture d'axe : bornée par le VVIX (~2007+) — 2008 inclus, dot-com EXCLU,
//  comparable à l'axe term-structure VIX/VIX3M du Sprint 19 (D57). Moins de
//  fenêtres que l'axe SKEW/VIX (1999+) : pavages plus courts, à mesurer.
//
//  Question : ce filtre bat-il le Buy & Hold de QQQ sur le rendement AJUSTÉ DU
//  RISQUE (Sharpe) NET DE COÛTS en OOS ? Critère PRIMAIRE = delta de Sharpe vs B&H.
//  Sanity D23 = alpha vs B&H. Clause DoD « DD réduit ≥ 50 % » = DD strat vs DD B&H.
//
//  Discipline (D33/D34) : config explicite, trades OOS poolés (comptes verrouillés),
//  garde anti-cash-drag (D47), sentinelle → mesure → figée. Données longues
//  total-return (*_max.csv) + VVIX_max.csv, 3 pavages (canonique/fin/décalé, D36).
//  Warmup = refLookback − 1 = 125 → OOS ≥ 400 (D35).
//
//  Valeurs MESURÉES le 2026-07-09 sur QQQ_max ∩ VVIX_max et figées.
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
// VVIX sur 126 j (~6 mois), seuil 1,0.
VixRegimeConfig cfgVvix() {
    VixRegimeConfig c;
    c.refLookback    = 126;
    c.thresholdMult  = 1.0;
    c.initialCapital = 10'000.0;
    c.commissionPct  = 0.001;
    c.slippageBps    = 2.0;
    c.halfSpreadBps  = 0.5;
    return c;
}

std::vector<std::string> pairQqqVvix() { return { SWINGBOT_QQQ_MAX_CSV, SWINGBOT_VVIX_MAX_CSV }; }

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
              << "  VVIXREG " << nom << " : fen=" << a.windows
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
    return agregeOos(VixRegimeWalkForward(cfgVvix(), csvs, is, oos, step, off).run());
}

} // namespace

// ════════════════════════════════════════════════════════════
//  Axe commun QQQ ∩ VVIX verrouillé (le VVIX ~2007 borne l'axe — pas de dot-com).
// ════════════════════════════════════════════════════════════
TEST(VvixRegimeOosIntegration, VvixRegimeAxisIsLocked) {
    VixRegimeBacktester v(cfgVvix(), pairQqqVvix());
    const auto& ax = v.axis();
    ASSERT_EQ(ax.assets(), 2u);
    EXPECT_EQ(ax.dates.front(), "2007-01-03");
    EXPECT_EQ(ax.dates.back(),  "2026-07-01");
    EXPECT_EQ(ax.size(), 4895u);           // toutes les barres VVIX tombent dans QQQ
    for (const auto& col : ax.close) EXPECT_EQ(col.size(), ax.size());
}

// ════════════════════════════════════════════════════════════
//  Pavage CANONIQUE
// ════════════════════════════════════════════════════════════
TEST(VvixRegimeOosIntegration, VvixRegimeCanonicalPavingVerdictIsLocked) {
    const auto a = paving(pairQqqVvix(), kIsCan, kOosCan, kStepCan);
    imprime("CANON", a);

    ASSERT_GE(a.windows, 2u);
    EXPECT_TRUE(std::isfinite(a.meanSharpe));
    EXPECT_GT(a.pool.size(), 1u);          // D47 : la famille TRADE réellement

    // Verdict : AUCUN edge ajusté du risque — même profil que le VIX (D51) :
    // Sharpe stratégie POSITIF (0,58) mais SOUS le B&H (1,20) → dSharpe −0,61.
    // La vol-de-vol (2e dérivée du prix des options) n'apporte rien de plus que
    // le niveau du VIX. DD réduit (12,3 vs 16,0) mais < 50 %, alpha négatif
    // (cash drag T4/D48).
    EXPECT_LT(a.meanSharpe, a.meanBhSharpe);   // pas d'edge Sharpe vs B&H
    EXPECT_LT(a.meanAlphaVsBh, 0.0);           // sanity D23 : sous-performe le B&H
    EXPECT_LT(a.meanMaxDd, a.meanBhDd);        // DD réduit…
    EXPECT_GT(a.meanMaxDd, 0.5 * a.meanBhDd);  // …mais < 50 % (clause DoD NON atteinte)

    EXPECT_EQ(a.windows, 8u);
    EXPECT_NEAR(a.meanTotalReturn,  11.0913, 1e-2);
    EXPECT_NEAR(a.meanSharpe,        0.5842, 1e-2);
    EXPECT_NEAR(a.meanSortino,       0.9105, 1e-2);
    EXPECT_NEAR(a.meanMaxDd,        12.3210, 1e-2);
    EXPECT_NEAR(a.meanBhDd,         15.9578, 1e-2);
    EXPECT_NEAR(a.meanBhSharpe,      1.1962, 1e-2);
    EXPECT_NEAR(a.meanAlphaVsBh,   -22.5146, 1e-2);
    EXPECT_EQ(a.pool.size(), 194u);
}

// ════════════════════════════════════════════════════════════
//  Pavage FIN
// ════════════════════════════════════════════════════════════
TEST(VvixRegimeOosIntegration, VvixRegimeFinePavingVerdictIsLocked) {
    const auto a = paving(pairQqqVvix(), kIsFin, kOosFin, kStepFin);
    imprime("FIN", a);

    ASSERT_GE(a.windows, 2u);
    EXPECT_GT(a.pool.size(), 1u);

    EXPECT_LT(a.meanSharpe, a.meanBhSharpe);   // pas d'edge Sharpe vs B&H
    EXPECT_LT(a.meanAlphaVsBh, 0.0);

    EXPECT_EQ(a.windows, 10u);
    EXPECT_NEAR(a.meanTotalReturn,   7.6654, 1e-2);
    EXPECT_NEAR(a.meanSharpe,        0.7593, 1e-2);
    EXPECT_NEAR(a.meanSortino,       1.2811, 1e-2);
    EXPECT_NEAR(a.meanMaxDd,         8.3019, 1e-2);
    EXPECT_NEAR(a.meanBhDd,         13.8806, 1e-2);
    EXPECT_NEAR(a.meanBhSharpe,      1.3775, 1e-2);
    EXPECT_NEAR(a.meanAlphaVsBh,   -19.6990, 1e-2);
    EXPECT_EQ(a.pool.size(), 180u);
}

// ════════════════════════════════════════════════════════════
//  Pavage DÉCALÉ (offset 90)
// ════════════════════════════════════════════════════════════
TEST(VvixRegimeOosIntegration, VvixRegimeShiftedPavingVerdictIsLocked) {
    const auto a = paving(pairQqqVvix(), kIsShift, kOosShift, kStepShift, kOffShift);
    imprime("SHIFT", a);

    ASSERT_GE(a.windows, 2u);
    EXPECT_GT(a.pool.size(), 1u);

    EXPECT_LT(a.meanSharpe, a.meanBhSharpe);   // pas d'edge (pavage non aligné)
    EXPECT_LT(a.meanAlphaVsBh, 0.0);

    EXPECT_EQ(a.windows, 8u);
    EXPECT_NEAR(a.meanTotalReturn,   7.2325, 1e-2);
    EXPECT_NEAR(a.meanSharpe,        0.4197, 1e-2);
    EXPECT_NEAR(a.meanSortino,       0.6694, 1e-2);
    EXPECT_NEAR(a.meanMaxDd,        11.6226, 1e-2);
    EXPECT_NEAR(a.meanBhDd,         16.7316, 1e-2);
    EXPECT_NEAR(a.meanBhSharpe,      1.0764, 1e-2);
    EXPECT_NEAR(a.meanAlphaVsBh,   -24.0922, 1e-2);
    EXPECT_EQ(a.pool.size(), 208u);
}

// ════════════════════════════════════════════════════════════
//  Monte-Carlo size-aware (D45) sur les trades OOS poolés (canonique).
// ════════════════════════════════════════════════════════════
TEST(VvixRegimeOosIntegration, VvixRegimeMonteCarloSizeAwareIsLocked) {
    const auto a = paving(pairQqqVvix(), kIsCan, kOosCan, kStepCan);
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
              << "  VVIXREG MC : cagrP50=" << r.cagrP50 << " ddP95=" << r.ddP95 << "\n";
    // Positif (cagrP50 ~5 %, proche du VIX D51) : le filtre capte une partie de
    // la hausse de QQQ, mais ce n'est PAS le critère (le B&H fait mieux en Sharpe).
    EXPECT_NEAR(r.cagrP50, 4.9709, 1e-3);
    EXPECT_NEAR(r.ddP95,  35.6628, 1e-3);
}

// ════════════════════════════════════════════════════════════
//  Multi-univers (pavage fin) : QQQ/SPY/IWM/MDY TRADÉS, tous gatés sur le MÊME VVIX
//  (vol-de-vol du marché). Le régime VVIX aide-t-il un actif quelconque ?
// ════════════════════════════════════════════════════════════
TEST(VvixRegimeOosIntegration, VvixRegimeMultiUniverseVerdictIsLocked) {
    struct U { const char* nom; const char* csv;
               double sharpeAttendu; double retAttendu; size_t tradesAttendus; };
    // NB : 180 trades PARTOUT — l'axe 2007+ est identique pour les 4 actifs (toutes
    // les barres VVIX tombent dans chaque *_max), donc le gate (pur signal) bascule
    // aux mêmes dates quel que soit l'actif tradé.
    const U univ[] = {
        {"QQQ", SWINGBOT_QQQ_MAX_CSV, 0.7593, 7.6654, 180u},
        {"SPY", SWINGBOT_SPY_MAX_CSV, 0.6370, 5.5611, 180u},
        {"IWM", SWINGBOT_IWM_MAX_CSV, 0.2751, 3.7359, 180u},
        {"MDY", SWINGBOT_MDY_MAX_CSV, 0.4032, 5.1791, 180u},
    };
    std::cout << std::fixed << std::setprecision(4) << "  VVIXREG multi-univers (fin)\n";
    for (const auto& u : univ) {
        const auto a = paving({ u.csv, SWINGBOT_VVIX_MAX_CSV }, kIsFin, kOosFin, kStepFin);
        std::cout << "    " << u.nom << " : sharpe=" << a.meanSharpe
                  << " bhSharpe=" << a.meanBhSharpe
                  << " dSharpe=" << a.sharpeDelta()
                  << " ret=" << a.meanTotalReturn
                  << " trades=" << a.pool.size() << "\n";
        EXPECT_GT(a.pool.size(), 1u);
        EXPECT_NEAR(a.meanSharpe,      u.sharpeAttendu, 1e-2);
        EXPECT_NEAR(a.meanTotalReturn, u.retAttendu,    1e-2);
        EXPECT_EQ(a.pool.size(), u.tradesAttendus);
        EXPECT_LT(a.meanSharpe, a.meanBhSharpe);   // aucun edge sur aucun actif gaté VVIX
    }
}

// ════════════════════════════════════════════════════════════
//  Balayage refLookback × thresholdMult : critère = delta de Sharpe OOS vs B&H.
// ════════════════════════════════════════════════════════════
TEST(VvixRegimeOosIntegration, VvixRegimeThresholdSweepBestOosIsLocked) {
    const int    refs[]  = {63, 126, 252};
    const double mults[] = {0.8, 1.0, 1.2};
    double best = -1e9; int bestR = 0; double bestM = 0.0;
    std::cout << std::fixed << std::setprecision(4)
              << "  VVIXREG balayage refLookback x thresholdMult (fin QQQ/VVIX, critere = dSharpe OOS)\n";
    for (int rl : refs) for (double mm : mults) {
        VixRegimeConfig c = cfgVvix(); c.refLookback = rl; c.thresholdMult = mm;
        const auto a = agregeOos(VixRegimeWalkForward(c, pairQqqVvix(),
                                                      kIsFin, kOosFin, kStepFin).run());
        std::cout << "    rl=" << rl << " m=" << mm
                  << " -> dSharpe=" << a.sharpeDelta() << "\n";
        if (a.sharpeDelta() > best) { best = a.sharpeDelta(); bestR = rl; bestM = mm; }
    }
    std::cout << "    MEILLEUR : rl=" << bestR << " m=" << bestM
              << " -> dSharpe OOS " << best
              << (best > 0.0 ? "  (CANDIDAT > 0)" : "  (aucun candidat)") << "\n";

    // VERDICT 20.2 (figé) : AUCUN réglage ne bat le B&H sur le Sharpe — le meilleur
    // (refLookback=63 / seuil=1,2) reste à dSharpe −0,25 : PAS de candidat > 0 →
    // pas de grille resserrée. Gate de confirmation FERMÉ.
    EXPECT_NEAR(best, -0.2545, 1e-2);
    EXPECT_EQ(bestR, 63);
    EXPECT_DOUBLE_EQ(bestM, 1.2);
    EXPECT_FALSE(best > 0.0);   // pas de candidat -> pas de confirmation
}
