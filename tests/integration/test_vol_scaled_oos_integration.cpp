// ============================================================
//  test_vol_scaled_oos_integration.cpp  —  Tests d'INTÉGRATION
//  Cible : VolScaledBacktester / VolScaledWalkForward
//  Sprint 18 (item 18.3) — VERDICT OUT-OF-SAMPLE VERROUILLÉ.
//
//  Réouverture de la recherche d'edge (décision utilisateur (r), piste §5.2 de
//  CONCLUSION_RECHERCHE_EDGE.md) : SCALING CONTINU Moreira-Muir — poids investi
//  w* = min(1, cible / vol annualisée réalisée), bande anti-churn 0,05, coût
//  proportionnel au notionnel tradé |Δw|. Attaque frontale du cash drag qui a
//  coulé le filtre BINAIRE long/cash (Sprint 13/D50 : Sharpe OOS positif mais
//  SOUS le B&H).
//
//  Question : le scaling continu dégage-t-il un ALPHA vs B&H net de coûts en
//  OOS, robuste aux 3 pavages (critère primaire D23) — ou au moins la clause de
//  repli Sprint 8 (Sharpe ≥ B&H ET DD réduit ≥ 50 %) ?
//
//  Discipline (D33/D34/D36/D47) : config explicite, trades OOS poolés, garde
//  d'activation, 3 pavages, verdicts figés (sentinelle → mesure → figée).
//  Données longues total-return (*_max.csv).
//
//  AVERTISSEMENT WARMUP (D35) : warmup = volLookback − 1 = 19 barres seulement
//  (pas de médiane de référence) — les fenêtres OOS ≥ 400 le dominent largement.
//
//  Valeurs MESURÉES le 2026-07-09 sur les *_max.csv et figées.
// ============================================================
#include <gtest/gtest.h>
#include <cmath>
#include <string>
#include <vector>
#include <iostream>
#include <iomanip>
#include "backtest/VolScaledBacktester.hpp"
#include "backtest/MonteCarlo.hpp"

using namespace trading;

namespace {

// Pavages : canonique, fin (D34), décalé (offset, D36) — dims Sprint 13/14.
constexpr size_t kIsCan = 750, kOosCan = 500, kStepCan = 500;
constexpr size_t kIsFin = 550, kOosFin = 400, kStepFin = 400;
constexpr size_t kIsShift = 750, kOosShift = 500, kStepShift = 500, kOffShift = 90;

// Config EXPLICITE du scaling continu (D33). Vol réalisée 20 j, cible 15 % de
// vol annualisée (Moreira & Muir), pas de levier, bande 0,05. Coûts D22.
VolScaledConfig cfgVs() {
    VolScaledConfig c;
    c.volLookback     = 20;
    c.targetVolAnnPct = 15.0;
    c.maxWeight       = 1.0;
    c.rebalanceBand   = 0.05;
    c.initialCapital  = 10'000.0;
    c.commissionPct   = 0.001;
    c.slippageBps     = 2.0;
    c.halfSpreadBps   = 0.5;
    return c;
}

// « YYYY-MM-DD » → jours civils (années observées du Monte-Carlo).
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

// Agrégats OOS d'un pavage.
struct AggOos {
    size_t windows = 0;
    double meanTotalReturn = 0.0, meanSharpe = 0.0, meanBhSharpe = 0.0;
    double meanMaxDd = 0.0, meanBhDd = 0.0, meanAlphaVsBh = 0.0;
    double meanAvgWeight = 0.0, meanTurnover = 0.0;
    std::vector<TradeRecord> pool;
    double years = 0.0;
    double sharpeDelta() const { return meanSharpe - meanBhSharpe; }
};

AggOos agregeOos(const std::vector<VolScaledWindow>& ws) {
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
        a.meanTotalReturn /= n; a.meanSharpe /= n; a.meanBhSharpe /= n;
        a.meanMaxDd /= n; a.meanBhDd /= n; a.meanAlphaVsBh /= n;
        a.meanAvgWeight /= n; a.meanTurnover /= n;
    }
    return a;
}

void imprime(const char* nom, const AggOos& a) {
    std::cout << std::fixed << std::setprecision(4)
              << "  VOLSCALED " << nom << " : fen=" << a.windows
              << " ret=" << a.meanTotalReturn
              << " sharpe=" << a.meanSharpe
              << " bhSharpe=" << a.meanBhSharpe
              << " dSharpe=" << a.sharpeDelta()
              << " DD=" << a.meanMaxDd
              << " bhDD=" << a.meanBhDd
              << " alphaBH=" << a.meanAlphaVsBh
              << " poids=" << a.meanAvgWeight
              << " churn=" << a.meanTurnover
              << " trades=" << a.pool.size()
              << " annees=" << a.years << "\n";
}

std::vector<VolScaledWindow> paving(const std::string& csv,
                                    size_t is, size_t oos, size_t step, size_t off = 0) {
    return VolScaledWalkForward(cfgVs(), csv, is, oos, step, off).run();
}

} // namespace

// ════════════════════════════════════════════════════════════
//  Série QQQ_max verrouillée (bornes de l'axe mono-actif).
// ════════════════════════════════════════════════════════════
TEST(VolScaledOosIntegration, VolScaledSeriesIsLocked) {
    VolScaledBacktester v(cfgVs(), SWINGBOT_QQQ_MAX_CSV);
    ASSERT_GT(v.size(), 0u);
    EXPECT_EQ(v.dates().front(), "1999-03-10");
    EXPECT_EQ(v.dates().back(),  "2026-07-01");
    EXPECT_EQ(v.size(), 6870u);
}

// ════════════════════════════════════════════════════════════
//  Pavage CANONIQUE
// ════════════════════════════════════════════════════════════
TEST(VolScaledOosIntegration, VolScaledCanonicalPavingVerdictIsLocked) {
    const auto a = agregeOos(paving(SWINGBOT_QQQ_MAX_CSV, kIsCan, kOosCan, kStepCan));
    imprime("CANON", a);

    ASSERT_GE(a.windows, 2u);
    EXPECT_TRUE(std::isfinite(a.meanSharpe));
    EXPECT_GT(a.pool.size(), 1u);          // D47 : la famille TRADE réellement

    // Verdict : AUCUN edge absolu. Le scaling continu COMBLE l'écart de Sharpe
    // vs B&H (dSharpe ≈ +0,06 ici, contre −0,56 pour le binaire Sprint 13 —
    // progrès réel sur le cash drag) mais l'ALPHA absolu reste négatif et le DD
    // n'est réduit que d'environ un tiers (< 50 %) → DoD NON atteinte.
    EXPECT_LT(a.meanAlphaVsBh, 0.0);           // pas d'alpha absolu vs B&H
    EXPECT_LT(a.meanMaxDd, a.meanBhDd);        // DD réduit…
    EXPECT_GT(a.meanMaxDd, 0.5 * a.meanBhDd);  // …mais < 50 % (clause repli NON atteinte)

    EXPECT_EQ(a.windows, 12u);
    EXPECT_NEAR(a.meanTotalReturn, 24.2159, 1e-2);
    EXPECT_NEAR(a.meanSharpe,       0.8646, 1e-2);
    EXPECT_NEAR(a.meanBhSharpe,     0.8077, 1e-2);
    EXPECT_NEAR(a.meanMaxDd,       15.1509, 1e-2);
    EXPECT_NEAR(a.meanBhDd,        23.4739, 1e-2);
    EXPECT_NEAR(a.meanAlphaVsBh,   -4.5203, 1e-2);
    EXPECT_NEAR(a.meanAvgWeight,    0.7772, 1e-2);
    EXPECT_EQ(a.pool.size(), 669u);
}

// ════════════════════════════════════════════════════════════
//  Pavage FIN
// ════════════════════════════════════════════════════════════
TEST(VolScaledOosIntegration, VolScaledFinePavingVerdictIsLocked) {
    const auto a = agregeOos(paving(SWINGBOT_QQQ_MAX_CSV, kIsFin, kOosFin, kStepFin));
    imprime("FIN", a);

    ASSERT_GE(a.windows, 2u);
    EXPECT_GT(a.pool.size(), 1u);
    EXPECT_LT(a.meanAlphaVsBh, 0.0);           // pas d'alpha absolu
    // Sur CE pavage le dSharpe repasse même négatif → le « quasi-comblement »
    // du Sharpe n'est PAS robuste au choix des fenêtres (leçon 8t.1).
    EXPECT_LT(a.sharpeDelta(), 0.0);

    EXPECT_EQ(a.windows, 15u);
    EXPECT_NEAR(a.meanTotalReturn, 15.9556, 1e-2);
    EXPECT_NEAR(a.meanSharpe,       0.7220, 1e-2);
    EXPECT_NEAR(a.meanBhSharpe,     0.7776, 1e-2);
    EXPECT_NEAR(a.meanAlphaVsBh,   -3.3080, 1e-2);
    EXPECT_EQ(a.pool.size(), 644u);
}

// ════════════════════════════════════════════════════════════
//  Pavage DÉCALÉ (offset 90)
// ════════════════════════════════════════════════════════════
TEST(VolScaledOosIntegration, VolScaledShiftedPavingVerdictIsLocked) {
    const auto a = agregeOos(paving(SWINGBOT_QQQ_MAX_CSV, kIsShift, kOosShift, kStepShift, kOffShift));
    imprime("SHIFT", a);

    ASSERT_GE(a.windows, 2u);
    EXPECT_GT(a.pool.size(), 1u);
    EXPECT_LT(a.meanAlphaVsBh, 0.0);

    EXPECT_EQ(a.windows, 12u);
    EXPECT_NEAR(a.meanTotalReturn, 27.9432, 1e-2);
    EXPECT_NEAR(a.meanSharpe,       0.9630, 1e-2);
    EXPECT_NEAR(a.meanBhSharpe,     0.9312, 1e-2);
    EXPECT_NEAR(a.meanAlphaVsBh,   -8.0785, 1e-2);
    EXPECT_EQ(a.pool.size(), 670u);
}

// ════════════════════════════════════════════════════════════
//  Monte-Carlo size-aware (D45) sur les stints OOS poolés (canonique).
// ════════════════════════════════════════════════════════════
TEST(VolScaledOosIntegration, VolScaledMonteCarloSizeAwareIsLocked) {
    const auto a = agregeOos(paving(SWINGBOT_QQQ_MAX_CSV, kIsCan, kOosCan, kStepCan));
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

    std::cout << std::fixed << std::setprecision(4)
              << "  VOLSCALED MC : cagrP50=" << r.cagrP50 << " ddP95=" << r.ddP95 << "\n";
    // Le MC size-aware est positif (le scaling capte une bonne part de la hausse
    // de QQQ) — mais ce n'est PAS le critère : l'alpha vs B&H reste négatif sur
    // les 3 pavages. Le MC confirme seulement « gagne de l'argent, moins que le B&H ».
    EXPECT_NEAR(r.cagrP50, 11.1903, 1e-3);
    EXPECT_NEAR(r.ddP95,   39.5136, 1e-3);
}

// ════════════════════════════════════════════════════════════
//  Cohérence multi-univers (pavage fin) : QQQ, SPY, IWM, MDY.
//  L'absence d'alpha n'est pas un artefact d'un seul actif.
// ════════════════════════════════════════════════════════════
TEST(VolScaledOosIntegration, VolScaledMultiUniverseVerdictIsLocked) {
    struct U { const char* nom; const char* csv;
               double alphaAttendu; size_t tradesAttendus; };
    const U univ[] = {
        {"QQQ", SWINGBOT_QQQ_MAX_CSV, -3.3080, 644u},
        {"SPY", SWINGBOT_SPY_MAX_CSV, -3.7394, 633u},
        {"IWM", SWINGBOT_IWM_MAX_CSV, -7.4003, 767u},
        {"MDY", SWINGBOT_MDY_MAX_CSV, -5.4255, 761u},
    };
    std::cout << std::fixed << std::setprecision(4) << "  VOLSCALED multi-univers (fin)\n";
    for (const auto& u : univ) {
        const auto a = agregeOos(paving(u.csv, kIsFin, kOosFin, kStepFin));
        std::cout << "    " << u.nom << " : alphaBH=" << a.meanAlphaVsBh
                  << " dSharpe=" << a.sharpeDelta()
                  << " trades=" << a.pool.size() << "\n";
        EXPECT_GT(a.pool.size(), 1u);
        EXPECT_LT(a.meanAlphaVsBh, 0.0);           // aucun alpha sur aucun actif
        EXPECT_NEAR(a.meanAlphaVsBh, u.alphaAttendu, 1e-2);
        EXPECT_EQ(a.pool.size(), u.tradesAttendus);
    }
}

// ════════════════════════════════════════════════════════════
//  Balayage cible × bande (candidat éventuel, GATÉ) : critère = alpha OOS
//  moyen vs B&H (pavage fin). Un candidat n'existe que si un réglage donne un
//  alpha > 0.
// ════════════════════════════════════════════════════════════
TEST(VolScaledOosIntegration, VolScaledTargetBandSweepBestOosIsLocked) {
    const double cibles[] = {10.0, 15.0, 20.0};
    const double bandes[] = {0.02, 0.05, 0.10};
    double best = -1e9; double bestT = 0.0, bestB = 0.0;
    std::cout << std::fixed << std::setprecision(4)
              << "  VOLSCALED balayage cible x bande (fin QQQ, critere = alpha OOS vs B&H)\n";
    for (double tg : cibles) for (double bd : bandes) {
        VolScaledConfig c = cfgVs(); c.targetVolAnnPct = tg; c.rebalanceBand = bd;
        const auto a = agregeOos(VolScaledWalkForward(c, SWINGBOT_QQQ_MAX_CSV,
                                                      kIsFin, kOosFin, kStepFin).run());
        std::cout << "    cible=" << tg << " bande=" << bd
                  << " -> alphaBH=" << a.meanAlphaVsBh << "\n";
        if (a.meanAlphaVsBh > best) { best = a.meanAlphaVsBh; bestT = tg; bestB = bd; }
    }
    std::cout << "    MEILLEUR : cible=" << bestT << " bande=" << bestB
              << " -> alpha OOS " << best
              << (best > 0.0 ? "  (CANDIDAT > 0)" : "  (aucun candidat)") << "\n";

    // VERDICT 18.3 (figé) : AUCUN réglage ne dégage d'alpha positif — le
    // meilleur (cible 20 / bande 0,10) frôle zéro par en-dessous. Gate FERMÉ.
    EXPECT_NEAR(best, -0.5219, 1e-2);
    EXPECT_DOUBLE_EQ(bestT, 20.0);
    EXPECT_DOUBLE_EQ(bestB, 0.10);
    EXPECT_FALSE(best > 0.0);   // pas de candidat -> pas de confirmation
}
