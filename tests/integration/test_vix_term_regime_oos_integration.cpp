// ============================================================
//  test_vix_term_regime_oos_integration.cpp  —  Tests d'INTÉGRATION
//  Cible : VixTermRegimeBacktester / VixTermRegimeWalkForward
//  Sprint 19 (item 19.2) — VERDICT OUT-OF-SAMPLE VERROUILLÉ.
//
//  Piste §5.3 (DERNIÈRE piste offline du §5) : la TERM-STRUCTURE de vol
//  implicite comme signal de régime. On TRADE QQQ mais on gate l'exposition
//  sur la FORME de la courbe VIX/VIX3M : contango (ratio lissé ≤ 1) → long,
//  backwardation (inversion de la courbe = stress) → cash. Contraste avec le
//  Sprint 14 (NIVEAU du VIX vs sa médiane) : l'inversion est un signal plus
//  SÉLECTIF — hypothèse « moins de cash drag » (D50).
//
//  Question : ce filtre bat-il le Buy & Hold de QQQ sur le rendement AJUSTÉ DU
//  RISQUE (Sharpe) NET DE COÛTS en OOS ? Critère PRIMAIRE = delta de Sharpe vs B&H.
//  Sanity D23 = alpha vs B&H. Clause DoD « DD réduit ≥ 50 % » = DD strat vs DD B&H.
//
//  Discipline (D33/D34) : config explicite, trades OOS poolés (comptes verrouillés),
//  garde anti-cash-drag (D47), sentinelle → mesure → figée. Données longues
//  total-return (*_max.csv) + VIX_max.csv + VIX3M_max.csv, 3 pavages (canonique/
//  fin/décalé, D36). Warmup = smoothLookback − 1 = 4 seulement → D35 trivial.
//  ATTENTION : l'axe commun est borné par le VIX3M (~2006) — SANS l'épisode
//  dot-com, contrairement aux familles jugées sur 1999+.
//
//  Valeurs MESURÉES le 2026-07-09 sur QQQ_max ∩ VIX_max ∩ VIX3M_max et figées.
// ============================================================
#include <gtest/gtest.h>
#include <cmath>
#include <string>
#include <vector>
#include <iostream>
#include <iomanip>
#include "backtest/VixTermRegimeBacktester.hpp"
#include "backtest/MonteCarlo.hpp"

using namespace trading;

namespace {

constexpr size_t kIsCan = 750, kOosCan = 500, kStepCan = 500;
constexpr size_t kIsFin = 550, kOosFin = 400, kStepFin = 400;
constexpr size_t kIsShift = 750, kOosShift = 500, kStepShift = 500, kOffShift = 90;

// Config EXPLICITE (D33). Seuil 1,0 = frontière naturelle contango/backwardation,
// lissage SMA 5 j du ratio (anti-whipsaw autour de la frontière).
VixTermRegimeConfig cfgTr() {
    VixTermRegimeConfig c;
    c.ratioThreshold = 1.0;
    c.smoothLookback = 5;
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
    double meanPctInvested = 0.0;
    std::vector<TradeRecord> pool;
    double years = 0.0;
    double sharpeDelta() const { return meanSharpe - meanBhSharpe; }
};

AggOos agregeOos(const std::vector<VixTermRegimeWindow>& ws) {
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
        a.meanPctInvested += w.oos.pctTimeInvested;
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
        a.meanPctInvested /= n;
    }
    return a;
}

void imprime(const char* nom, const AggOos& a) {
    std::cout << std::fixed << std::setprecision(4)
              << "  VIXTERM " << nom << " : fen=" << a.windows
              << " ret=" << a.meanTotalReturn
              << " sharpe=" << a.meanSharpe
              << " bhSharpe=" << a.meanBhSharpe
              << " dSharpe=" << a.sharpeDelta()
              << " sortino=" << a.meanSortino
              << " DD=" << a.meanMaxDd
              << " bhDD=" << a.meanBhDd
              << " alphaBH=" << a.meanAlphaVsBh
              << " invest=" << a.meanPctInvested
              << " trades=" << a.pool.size()
              << " annees=" << a.years << "\n";
}

AggOos paving(const std::vector<std::string>& csvs,
              size_t is, size_t oos, size_t step, size_t off = 0) {
    return agregeOos(VixTermRegimeWalkForward(cfgTr(), csvs, is, oos, step, off).run());
}

} // namespace

// ════════════════════════════════════════════════════════════
//  Axe commun QQQ ∩ VIX ∩ VIX3M verrouillé (borné par le VIX3M ~2006 : QQQ
//  ~1999 et VIX ~1990 débordent — l'axe term-structure est SANS le dot-com).
// ════════════════════════════════════════════════════════════
TEST(VixTermRegimeOosIntegration, VixTermRegimeAxisIsLocked) {
    VixTermRegimeBacktester v(cfgTr(), trioQqqVixV3m());
    const auto& ax = v.axis();
    ASSERT_EQ(ax.assets(), 3u);
    EXPECT_EQ(ax.dates.front(), "2006-07-17");
    EXPECT_EQ(ax.dates.back(),  "2026-07-01");
    EXPECT_EQ(ax.size(), 5021u);           // QQQ ∩ VIX ∩ VIX3M, borné par VIX3M (2006)
    for (const auto& col : ax.close) EXPECT_EQ(col.size(), ax.size());
}

// ════════════════════════════════════════════════════════════
//  Pavage CANONIQUE
// ════════════════════════════════════════════════════════════
TEST(VixTermRegimeOosIntegration, VixTermRegimeCanonicalPavingVerdictIsLocked) {
    const auto a = paving(trioQqqVixV3m(), kIsCan, kOosCan, kStepCan);
    imprime("CANON", a);

    ASSERT_GE(a.windows, 2u);
    EXPECT_TRUE(std::isfinite(a.meanSharpe));
    EXPECT_GT(a.pool.size(), 1u);          // D47 : la famille TRADE réellement

    // Verdict : AUCUN edge au seuil NATUREL (1,0). L'hypothèse « moins de cash
    // drag » est CONFIRMÉE (~93 % investi vs ~55-70 % pour les filtres de
    // niveau D50/D51) mais le dSharpe reste négatif (−0,12) et l'alpha aussi.
    // DD réduit (18,1 vs 20,7) mais TRÈS loin de la clause DoD (≥ 50 %).
    EXPECT_LT(a.meanSharpe, a.meanBhSharpe);   // pas d'edge Sharpe vs B&H
    EXPECT_LT(a.meanAlphaVsBh, 0.0);           // sanity D23 : sous-performe le B&H
    EXPECT_LT(a.meanMaxDd, a.meanBhDd);        // DD réduit…
    EXPECT_GT(a.meanMaxDd, 0.5 * a.meanBhDd);  // …mais < 50 % (clause DoD NON atteinte)
    EXPECT_GT(a.meanPctInvested, 85.0);        // l'inversion est RARE : quasi pas de cash drag

    EXPECT_EQ(a.windows, 8u);
    EXPECT_NEAR(a.meanTotalReturn,  30.4903, 1e-2);
    EXPECT_NEAR(a.meanSharpe,        0.8971, 1e-2);
    EXPECT_NEAR(a.meanSortino,       1.2538, 1e-2);
    EXPECT_NEAR(a.meanMaxDd,        18.0517, 1e-2);
    EXPECT_NEAR(a.meanBhDd,         20.6631, 1e-2);
    EXPECT_NEAR(a.meanBhSharpe,      1.0130, 1e-2);
    EXPECT_NEAR(a.meanAlphaVsBh,   -11.4760, 1e-2);
    EXPECT_EQ(a.pool.size(), 42u);
}

// ════════════════════════════════════════════════════════════
//  Pavage FIN
// ════════════════════════════════════════════════════════════
TEST(VixTermRegimeOosIntegration, VixTermRegimeFinePavingVerdictIsLocked) {
    const auto a = paving(trioQqqVixV3m(), kIsFin, kOosFin, kStepFin);
    imprime("FIN", a);

    ASSERT_GE(a.windows, 2u);
    EXPECT_GT(a.pool.size(), 1u);
    // dSharpe −0,01 : quasi nul mais toujours ≤ 0 ; alpha négatif.
    EXPECT_LT(a.meanSharpe, a.meanBhSharpe);
    EXPECT_LT(a.meanAlphaVsBh, 0.0);

    EXPECT_EQ(a.windows, 11u);
    EXPECT_NEAR(a.meanTotalReturn,  25.6957, 1e-2);
    EXPECT_NEAR(a.meanSharpe,        0.9489, 1e-2);
    EXPECT_NEAR(a.meanSortino,       1.3586, 1e-2);
    EXPECT_NEAR(a.meanMaxDd,        16.3604, 1e-2);
    EXPECT_NEAR(a.meanBhDd,         20.2903, 1e-2);
    EXPECT_NEAR(a.meanBhSharpe,      0.9591, 1e-2);
    EXPECT_NEAR(a.meanAlphaVsBh,    -4.3651, 1e-2);
    EXPECT_EQ(a.pool.size(), 47u);
}

// ════════════════════════════════════════════════════════════
//  Pavage DÉCALÉ (offset 90)
// ════════════════════════════════════════════════════════════
TEST(VixTermRegimeOosIntegration, VixTermRegimeShiftedPavingVerdictIsLocked) {
    const auto a = paving(trioQqqVixV3m(), kIsShift, kOosShift, kStepShift, kOffShift);
    imprime("SHIFT", a);

    ASSERT_GE(a.windows, 2u);
    EXPECT_GT(a.pool.size(), 1u);
    EXPECT_LT(a.meanSharpe, a.meanBhSharpe);   // pas d'edge (pavage non aligné)
    EXPECT_LT(a.meanAlphaVsBh, 0.0);

    EXPECT_EQ(a.windows, 8u);
    EXPECT_NEAR(a.meanTotalReturn,  32.2486, 1e-2);
    EXPECT_NEAR(a.meanSharpe,        0.9308, 1e-2);
    EXPECT_NEAR(a.meanSortino,       1.3106, 1e-2);
    EXPECT_NEAR(a.meanMaxDd,        18.5583, 1e-2);
    EXPECT_NEAR(a.meanBhDd,         20.8782, 1e-2);
    EXPECT_NEAR(a.meanBhSharpe,      1.0149, 1e-2);
    EXPECT_NEAR(a.meanAlphaVsBh,   -10.6985, 1e-2);
    EXPECT_EQ(a.pool.size(), 41u);
}

// ════════════════════════════════════════════════════════════
//  Monte-Carlo size-aware (D45) sur les trades OOS poolés (canonique).
// ════════════════════════════════════════════════════════════
TEST(VixTermRegimeOosIntegration, VixTermRegimeMonteCarloSizeAwareIsLocked) {
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
              << "  VIXTERM MC : cagrP50=" << r.cagrP50 << " ddP95=" << r.ddP95 << "\n";
    // Positif (cagrP50 ~13 %) : quasi tout le B&H est capté (le filtre n'est en
    // cash que ~7 % du temps) — mais ce n'est PAS le critère (Sharpe < B&H).
    EXPECT_NEAR(r.cagrP50, 13.3654, 1e-3);
    EXPECT_NEAR(r.ddP95,  34.8182, 1e-3);
}

// ════════════════════════════════════════════════════════════
//  Multi-univers (pavage fin) : QQQ/SPY/IWM/MDY TRADÉS, tous gatés sur la MÊME
//  term-structure VIX/VIX3M (courbe de vol du marché). Aide-t-elle un actif
//  quelconque ?
// ════════════════════════════════════════════════════════════
TEST(VixTermRegimeOosIntegration, VixTermRegimeMultiUniverseVerdictIsLocked) {
    struct U { const char* nom; const char* csv;
               double sharpeAttendu; double retAttendu; size_t tradesAttendus; };
    // Au seuil naturel 1,0 : dSharpe légèrement négatif sur QQQ/SPY/IWM,
    // +0,0015 (≈ 0, bruit) sur MDY — aucun edge exploitable sur aucun actif.
    const U univ[] = {
        {"QQQ", SWINGBOT_QQQ_MAX_CSV, 0.9489, 25.6957, 47u},
        {"SPY", SWINGBOT_SPY_MAX_CSV, 0.8559, 18.2638, 47u},
        {"IWM", SWINGBOT_IWM_MAX_CSV, 0.5237, 16.7380, 47u},
        {"MDY", SWINGBOT_MDY_MAX_CSV, 0.6440, 18.1120, 47u},
    };
    std::cout << std::fixed << std::setprecision(4) << "  VIXTERM multi-univers (fin)\n";
    for (const auto& u : univ) {
        const auto a = paving({ u.csv, SWINGBOT_VIX_MAX_CSV, SWINGBOT_VIX3M_MAX_CSV },
                              kIsFin, kOosFin, kStepFin);
        std::cout << "    " << u.nom << " : sharpe=" << a.meanSharpe
                  << " bhSharpe=" << a.meanBhSharpe
                  << " dSharpe=" << a.sharpeDelta()
                  << " ret=" << a.meanTotalReturn
                  << " trades=" << a.pool.size() << "\n";
        EXPECT_GT(a.pool.size(), 1u);
        EXPECT_NEAR(a.meanSharpe,      u.sharpeAttendu, 1e-2);
        EXPECT_NEAR(a.meanTotalReturn, u.retAttendu,    1e-2);
        EXPECT_EQ(a.pool.size(), u.tradesAttendus);
        EXPECT_LT(a.sharpeDelta(), 0.01);   // au mieux ≈ 0 (MDY +0,0015) : jamais un edge
    }
}

// ════════════════════════════════════════════════════════════
//  Balayage ratioThreshold × smoothLookback : critère = delta de Sharpe OOS vs B&H.
//  smoothLookback = 1 inclut le signal BRUT (lecture littérale du §5.3).
// ════════════════════════════════════════════════════════════
TEST(VixTermRegimeOosIntegration, VixTermRegimeThresholdSweepBestOosIsLocked) {
    const double seuils[]   = {0.95, 1.0, 1.05};
    const int    lissages[] = {1, 5, 10};
    double best = -1e9; double bestS = 0.0; int bestL = 0;
    std::cout << std::fixed << std::setprecision(4)
              << "  VIXTERM balayage seuil x lissage (fin QQQ/VIX/VIX3M, critere = dSharpe OOS)\n";
    for (double s : seuils) for (int l : lissages) {
        VixTermRegimeConfig c = cfgTr(); c.ratioThreshold = s; c.smoothLookback = l;
        const auto a = agregeOos(VixTermRegimeWalkForward(c, trioQqqVixV3m(),
                                                          kIsFin, kOosFin, kStepFin).run());
        std::cout << "    seuil=" << s << " lissage=" << l
                  << " -> dSharpe=" << a.sharpeDelta()
                  << " trades=" << a.pool.size() << "\n";
        if (a.sharpeDelta() > best) { best = a.sharpeDelta(); bestS = s; bestL = l; }
    }
    std::cout << "    MEILLEUR : seuil=" << bestS << " lissage=" << bestL
              << " -> dSharpe OOS " << best
              << (best > 0.0 ? "  (CANDIDAT > 0)" : "  (aucun candidat)") << "\n";

    // VERDICT 19.2 (figé) : le balayage produit un CANDIDAT > 0 (seuil 1,05 /
    // lissage 5 → dSharpe +0,07) — une PREMIÈRE dans le projet. Mais la clause
    // D43 s'applique : voir VixTermRegimeCandidateFragilityIsLocked — la grille
    // resserrée montre que le signe s'inverse sur le pavage canonique à ±0,01
    // de seuil (artefact de sélection, leçon 8t.1/8t.3) et l'alpha absolu
    // canonique reste négatif. AUCUNE adoption.
    EXPECT_NEAR(best, 0.0681, 1e-2);
    EXPECT_DOUBLE_EQ(bestS, 1.05);
    EXPECT_EQ(bestL, 5);
    EXPECT_TRUE(best > 0.0);    // candidat existant → la fragilité DOIT être jugée à part
}

// ════════════════════════════════════════════════════════════
//  Grille RESSERRÉE autour du candidat (leçon 8t.3) : le « plateau positif »
//  du seuil 1,05 est un ARTEFACT — sur le pavage canonique (fenêtres jamais
//  vues par le balayage), le signe du dSharpe s'inverse à ±0,01-0,03 de seuil,
//  et l'alpha absolu canonique reste négatif sur TOUTE la grille. Un avantage
//  qui dépend de la maille n'est pas un edge (8t.1) → gate de confirmation FERMÉ.
// ════════════════════════════════════════════════════════════
TEST(VixTermRegimeOosIntegration, VixTermRegimeCandidateFragilityIsLocked) {
    struct P { double seuil; double dShCanAttendu; double alphaCanAttendu; };
    // Lissage 5 (celui du candidat), pavage CANONIQUE (non choisi par le balayage).
    const P grille[] = {
        {1.04, -0.0523, -7.5907},   // 1 cran sous le candidat : NÉGATIF
        {1.05,  0.0045, -3.6193},   // le candidat : à peine positif (+0,004)
        {1.06, -0.0130, -3.5060},   // 1 cran au-dessus : le signe S'INVERSE
        {1.08,  0.0023, -1.9760},   // et re-bascule : du BRUIT, pas un plateau
    };
    std::cout << std::fixed << std::setprecision(4)
              << "  VIXTERM grille resserree (lissage 5, pavage canonique)\n";
    bool signeStable = true;
    for (const auto& p : grille) {
        VixTermRegimeConfig c = cfgTr(); c.ratioThreshold = p.seuil; c.smoothLookback = 5;
        const auto a = agregeOos(VixTermRegimeWalkForward(c, trioQqqVixV3m(),
                                                          kIsCan, kOosCan, kStepCan).run());
        std::cout << "    seuil=" << p.seuil << " -> dSharpe(canon)=" << a.sharpeDelta()
                  << " alpha(canon)=" << a.meanAlphaVsBh << "\n";
        EXPECT_NEAR(a.sharpeDelta(),  p.dShCanAttendu,  1e-2);
        EXPECT_NEAR(a.meanAlphaVsBh, p.alphaCanAttendu, 1e-2);
        EXPECT_LT(a.meanAlphaVsBh, 0.0);   // alpha absolu canonique négatif PARTOUT
        if (a.sharpeDelta() <= 0.0) signeStable = false;
    }
    EXPECT_FALSE(signeStable);   // le signe n'est PAS stable sur la grille → artefact
}
