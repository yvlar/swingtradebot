// ============================================================
//  test_pairs_oos_integration.cpp  —  Tests d'INTÉGRATION
//  Cible : PairsBacktester / PairsWalkForward
//  Sprint 12 (item 12.2) — VERDICT OUT-OF-SAMPLE VERROUILLÉ.
//
//  4e famille d'alpha : PAIRS-TRADING market-neutral (z-score du spread log
//  log(P0)-log(P1), position dollar-neutral 0,5/0,5). Seule famille ORTHOGONALE
//  aux trois précédentes (toutes directionnelles, soldées sans edge).
//
//  Question : la stratégie market-neutral produit-elle un rendement ajusté du
//  risque ABSOLU positif (Sharpe > 0, DD faible) NET DE COÛTS en OOS ? Une
//  famille market-neutral (bêta ~0) ne bat PAS un indice long ; on ne fige donc
//  l'alpha vs B&H jambe 0 que par CONTINUITÉ (attendu très négatif), le vrai
//  critère est le Sharpe. On mappe le résultat sur la clause DoD EXISTANTE
//  « drawdown réduit >= 50 % » en figeant le DD stratégie ET le DD B&H jambe 0.
//
//  Discipline (D33/D34) : config construite explicitement, trades OOS poolés
//  (comptes verrouillés), garde anti-cash-drag (D47), verdicts figés
//  (sentinelle -> mesure -> figée). Données longues total-return (*_max.csv),
//  3 pavages (canonique / fin / décalé, anti biais de sélection D36).
//  Valeurs MESURÉES le 2026-07-06 sur les *_max.csv et figées.
//
//  VERDICT 12.2 : AUCUN EDGE. La famille TRADE massivement (~209-212 A/R OOS —
//  D47 largement satisfait, la famille est VRAIMENT jugée) mais le Sharpe OOS est
//  NÉGATIF sur les 3 pavages, les 4 paires et tous les réglages du balayage. La
//  clause DoD « DD réduit >= 50 % » est techniquement atteinte (DD stratégie ~7 %
//  vs DD B&H jambe 0 ~22 %) MAIS sans intérêt : le rendement est négatif. Le
//  spread log(QQQ)-log(SPY) sur fenêtre courte est du bruit — pas de retour à la
//  moyenne exploitable net de coûts. Gate de confirmation FERMÉ. Prod paper.
// ============================================================
#include <gtest/gtest.h>
#include <cmath>
#include <string>
#include <vector>
#include <iostream>
#include <iomanip>
#include "backtest/PairsBacktester.hpp"
#include "backtest/MonteCarlo.hpp"

using namespace trading;

namespace {

// Pavages (constexpr) : canonique, fin (D34), décalé (offset, D36).
constexpr size_t kIsCan = 700, kOosCan = 400, kStepCan = 400;
constexpr size_t kIsFin = 500, kOosFin = 300, kStepFin = 300;
constexpr size_t kIsShift = 700, kOosShift = 400, kStepShift = 400, kOffShift = 90;

// Config EXPLICITE du pairs-trading (D33). z-score Bollinger(20), entrée +/-2 sigma,
// sortie au retour à la moyenne. Coûts = défauts Backtester (D22).
PairsConfig cfgPairs() {
    PairsConfig c;
    c.zWindow        = 20;
    c.entryK         = 2.0;
    c.exitZ          = 0.0;
    c.initialCapital = 10'000.0;
    c.commissionPct  = 0.001;
    c.slippageBps    = 2.0;
    c.halfSpreadBps  = 0.5;
    return c;
}

// Paire canonique (données longues *_max.csv).
std::vector<std::string> pairQqqSpy() { return { SWINGBOT_QQQ_MAX_CSV, SWINGBOT_SPY_MAX_CSV }; }

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
    double meanTotalReturn = 0.0, meanSharpe = 0.0, meanSortino = 0.0;
    double meanMaxDd = 0.0, meanLeg0Dd = 0.0, meanAlphaVsLeg0 = 0.0;
    int    roundTrips = 0;
    std::vector<TradeRecord> pool;
    double years = 0.0;
};

AggOos agregeOos(const std::vector<PairWindow>& ws) {
    AggOos a;
    a.windows = ws.size();
    for (const auto& w : ws) {
        a.meanTotalReturn += w.oos.totalReturnPct;
        a.meanSharpe      += w.oos.sharpeRatio;
        a.meanSortino     += w.oos.sortinoRatio;
        a.meanMaxDd       += w.oos.maxDrawdownPct;
        a.meanLeg0Dd      += w.oos.leg0BuyHoldDrawdownPct;
        a.meanAlphaVsLeg0 += w.oos.alphaVsLeg0BuyHold;
        a.roundTrips      += w.oos.roundTripCount;
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
        a.meanMaxDd /= n; a.meanLeg0Dd /= n; a.meanAlphaVsLeg0 /= n;
    }
    return a;
}

void imprime(const char* nom, const AggOos& a) {
    std::cout << std::fixed << std::setprecision(4)
              << "  PAIRS " << nom << " : fen=" << a.windows
              << " ret=" << a.meanTotalReturn
              << " sharpe=" << a.meanSharpe
              << " sortino=" << a.meanSortino
              << " DD=" << a.meanMaxDd
              << " leg0DD=" << a.meanLeg0Dd
              << " alphaLeg0=" << a.meanAlphaVsLeg0
              << " trades=" << a.pool.size()
              << " annees=" << a.years << "\n";
}

} // namespace

// ════════════════════════════════════════════════════════════
//  Axe commun aligné QQQ/SPY : borné par le plus court des deux (1999-03-10,
//  début de QQQ) → 6870 barres, PAS les 6562 de la rotation (bornée par IWM).
// ════════════════════════════════════════════════════════════
TEST(PairsOosIntegration, PairsCommonAxisIsLocked) {
    PairsBacktester pb(cfgPairs(), pairQqqSpy());
    const auto& ax = pb.axis();
    ASSERT_EQ(ax.assets(), 2u);
    EXPECT_EQ(ax.dates.front(), "1999-03-10");
    EXPECT_EQ(ax.dates.back(),  "2026-07-01");
    EXPECT_EQ(ax.size(), 6870u);
    for (const auto& col : ax.close) EXPECT_EQ(col.size(), ax.size());
}

// ════════════════════════════════════════════════════════════
//  Pavage CANONIQUE : Sharpe OOS NÉGATIF (aucun edge)
// ════════════════════════════════════════════════════════════
TEST(PairsOosIntegration, PairsCanonicalPavingVerdictIsLocked) {
    const auto ws = PairsWalkForward(cfgPairs(), pairQqqSpy(),
                                     kIsCan, kOosCan, kStepCan).run();
    const auto a = agregeOos(ws);
    imprime("CANON", a);

    ASSERT_GE(a.windows, 2u);
    EXPECT_TRUE(std::isfinite(a.meanSharpe));
    EXPECT_TRUE(std::isfinite(a.meanTotalReturn));

    // Verdict : Sharpe OOS négatif → aucun edge market-neutral.
    EXPECT_LT(a.meanSharpe, 0.0);
    EXPECT_GT(a.pool.size(), 1u);   // D47 : la famille TRADE réellement
    // Clause DoD « DD réduit >= 50 % » : ATTEINTE (mais sans intérêt, ret < 0).
    EXPECT_LT(a.meanMaxDd, 0.5 * a.meanLeg0Dd);

    EXPECT_EQ(a.windows, 15u);
    EXPECT_NEAR(a.meanTotalReturn,  -5.6844, 1e-2);
    EXPECT_NEAR(a.meanSharpe,       -1.1790, 1e-2);
    EXPECT_NEAR(a.meanSortino,      -1.5328, 1e-2);
    EXPECT_NEAR(a.meanMaxDd,         7.2401, 1e-2);
    EXPECT_NEAR(a.meanLeg0Dd,       22.2578, 1e-2);
    EXPECT_NEAR(a.meanAlphaVsLeg0, -29.2829, 1e-2);
    EXPECT_EQ(a.pool.size(), 209u);        // D34 : compte de trades poolés verrouillé
    EXPECT_EQ(a.roundTrips, 209);
}

// ════════════════════════════════════════════════════════════
//  Pavage FIN
// ════════════════════════════════════════════════════════════
TEST(PairsOosIntegration, PairsFinePavingVerdictIsLocked) {
    const auto ws = PairsWalkForward(cfgPairs(), pairQqqSpy(),
                                     kIsFin, kOosFin, kStepFin).run();
    const auto a = agregeOos(ws);
    imprime("FIN", a);

    ASSERT_GE(a.windows, 4u);
    EXPECT_LT(a.meanSharpe, 0.0);
    EXPECT_GT(a.pool.size(), 1u);
    EXPECT_LT(a.meanMaxDd, 0.5 * a.meanLeg0Dd);

    EXPECT_EQ(a.windows, 21u);
    EXPECT_NEAR(a.meanTotalReturn,  -4.5156, 1e-2);
    EXPECT_NEAR(a.meanSharpe,       -1.1954, 1e-2);
    EXPECT_NEAR(a.meanSortino,      -1.5591, 1e-2);
    EXPECT_NEAR(a.meanMaxDd,         6.2459, 1e-2);
    EXPECT_NEAR(a.meanLeg0Dd,       20.0448, 1e-2);
    EXPECT_NEAR(a.meanAlphaVsLeg0, -17.7605, 1e-2);
    EXPECT_EQ(a.pool.size(), 212u);
    EXPECT_EQ(a.roundTrips, 212);
}

// ════════════════════════════════════════════════════════════
//  Pavage DÉCALÉ (offset 90) : fenêtres jamais alignées sur le canonique
// ════════════════════════════════════════════════════════════
TEST(PairsOosIntegration, PairsShiftedPavingVerdictIsLocked) {
    const auto ws = PairsWalkForward(cfgPairs(), pairQqqSpy(),
                                     kIsShift, kOosShift, kStepShift, kOffShift).run();
    const auto a = agregeOos(ws);
    imprime("SHIFT", a);

    ASSERT_GE(a.windows, 2u);
    EXPECT_LT(a.meanSharpe, 0.0);
    EXPECT_GT(a.pool.size(), 1u);

    EXPECT_EQ(a.windows, 15u);
    EXPECT_NEAR(a.meanTotalReturn,  -5.5529, 1e-2);
    EXPECT_NEAR(a.meanSharpe,       -1.1622, 1e-2);
    EXPECT_NEAR(a.meanSortino,      -1.5336, 1e-2);
    EXPECT_NEAR(a.meanMaxDd,         6.9034, 1e-2);
    EXPECT_NEAR(a.meanLeg0Dd,       20.1311, 1e-2);
    EXPECT_NEAR(a.meanAlphaVsLeg0, -30.4462, 1e-2);
    EXPECT_EQ(a.pool.size(), 209u);
    EXPECT_EQ(a.roundTrips, 209);
}

// ════════════════════════════════════════════════════════════
//  Monte-Carlo size-aware (D45) sur les trades OOS poolés (canonique) : CAGR
//  médian NÉGATIF, drawdown de queue élevé — le risque du market-neutral bruité
// ════════════════════════════════════════════════════════════
TEST(PairsOosIntegration, PairsMonteCarloSizeAwareIsLocked) {
    const auto ws = PairsWalkForward(cfgPairs(), pairQqqSpy(),
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

    EXPECT_LT(r.cagrP50, 0.0);   // CAGR médian négatif : pas d'edge
    EXPECT_NEAR(r.cagrP50, -3.6202, 1e-3);
    EXPECT_NEAR(r.ddP95,   68.1360, 1e-3);
}

// ════════════════════════════════════════════════════════════
//  Cohérence multi-paires (pavage fin) : QQQ/SPY, QQQ/IWM, QQQ/MDY, SPY/IWM.
//  Le pairs-trading n'est pas un artefact d'une paire : Sharpe NÉGATIF partout.
// ════════════════════════════════════════════════════════════
TEST(PairsOosIntegration, PairsMultiUniverseVerdictIsLocked) {
    struct P { const char* nom; const char* a; const char* b;
               double sharpeAttendu; double retAttendu; size_t tradesAttendus; };
    const P paires[] = {
        {"QQQ/SPY", SWINGBOT_QQQ_MAX_CSV, SWINGBOT_SPY_MAX_CSV, -1.1954, -4.5156, 212u},
        {"QQQ/IWM", SWINGBOT_QQQ_MAX_CSV, SWINGBOT_IWM_MAX_CSV, -0.6695, -3.5184, 203u},
        {"QQQ/MDY", SWINGBOT_QQQ_MAX_CSV, SWINGBOT_MDY_MAX_CSV, -1.0069, -4.8533, 202u},
        {"SPY/IWM", SWINGBOT_SPY_MAX_CSV, SWINGBOT_IWM_MAX_CSV, -0.8813, -3.5824, 208u},
    };
    std::cout << std::fixed << std::setprecision(4) << "  PAIRS multi-univers (fin)\n";
    for (const auto& p : paires) {
        const auto ws = PairsWalkForward(cfgPairs(), { p.a, p.b },
                                         kIsFin, kOosFin, kStepFin).run();
        const auto agg = agregeOos(ws);
        std::cout << "    " << p.nom << " : sharpe=" << agg.meanSharpe
                  << " ret=" << agg.meanTotalReturn
                  << " trades=" << agg.pool.size() << "\n";
        for (const auto& w : ws) EXPECT_TRUE(std::isfinite(w.oos.sharpeRatio));
        EXPECT_NEAR(agg.meanSharpe,      p.sharpeAttendu, 1e-2);
        EXPECT_NEAR(agg.meanTotalReturn, p.retAttendu,    1e-2);
        EXPECT_EQ(agg.pool.size(), p.tradesAttendus);
        EXPECT_LT(agg.meanSharpe, 0.0);   // aucun edge sur aucune paire
    }
}

// ════════════════════════════════════════════════════════════
//  Balayage entryK x zWindow (candidat éventuel pour la confirmation, GATÉE) :
//  critère = Sharpe OOS. AUCUN réglage ne donne un Sharpe positif → gate FERMÉ.
// ════════════════════════════════════════════════════════════
TEST(PairsOosIntegration, PairsThresholdSweepBestOosIsLocked) {
    const int    zwins[] = {10, 20};
    const double ks[]    = {1.5, 2.0, 2.5};
    double best = -1e9; int bestZ = 0; double bestK = 0.0;
    std::cout << std::fixed << std::setprecision(4)
              << "  PAIRS balayage zWindow x entryK (fin QQQ/SPY, critere = Sharpe OOS)\n";
    for (int z : zwins) for (double k : ks) {
        PairsConfig c = cfgPairs(); c.zWindow = z; c.entryK = k;
        const auto ws = PairsWalkForward(c, pairQqqSpy(), kIsFin, kOosFin, kStepFin).run();
        const auto agg = agregeOos(ws);
        std::cout << "    z=" << z << " k=" << k << " -> sharpe=" << agg.meanSharpe << "\n";
        if (agg.meanSharpe > best) { best = agg.meanSharpe; bestZ = z; bestK = k; }
    }
    std::cout << "    MEILLEUR : z=" << bestZ << " k=" << bestK
              << " -> sharpe OOS " << best
              << (best > 0.0 ? "  (CANDIDAT > 0)" : "  (aucun candidat)") << "\n";

    // VERDICT 12.2 (figé) : AUCUN réglage ne produit de Sharpe OOS positif — le
    // meilleur (z=10 / k=2,5) reste à -0,44. Gate de confirmation FERMÉ.
    EXPECT_NEAR(best, -0.4401, 1e-2);
    EXPECT_EQ(bestZ, 10);
    EXPECT_DOUBLE_EQ(bestK, 2.5);
    EXPECT_FALSE(best > 0.0);   // pas de candidat -> pas de confirmation
}
