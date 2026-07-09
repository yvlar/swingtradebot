// ============================================================
//  test_coint_pairs_oos_integration.cpp  —  Tests d'INTÉGRATION
//  Cible : CointPairsBacktester / CointPairsWalkForward
//  Sprint 18 (item 18.6) — VERDICT OUT-OF-SAMPLE VERROUILLÉ.
//
//  Réouverture (décision utilisateur (r), piste §5.1) : pairs-trading
//  RÉELLEMENT cointégré (Engle-Granger) — hedge ratio OLS ROULANT (β figé à
//  l'entrée) + GATE de cointégration (Dickey-Fuller sur le résidu, critique
//  figée −3,34) — la correction structurelle du spread naïf β = 1 réfuté en
//  D49 (« du bruit », Sharpe OOS −1,16/−1,20 partout).
//
//  Question : le gate transforme-t-il le spread en retour à la moyenne
//  exploitable net de coûts ? Famille market-neutral → critère PRIMAIRE =
//  Sharpe OOS > 0 robuste aux 3 pavages et aux paires. Garde d'ACTIVATION
//  spécifique (D47) : pctBarsCointegrated — si le gate ne s'ouvre jamais, le
//  verdict ne mesurerait que du cash drag.
//
//  AVERTISSEMENT WARMUP (D35) : max(betaWindow, adfWindow) − 1 = 125 barres
//  aux défauts (251 quand betaWindow = 252) → fenêtres OOS de 500 barres.
//
//  Valeurs MESURÉES le 2026-07-09 sur les *_max.csv et figées.
// ============================================================
#include <gtest/gtest.h>
#include <cmath>
#include <string>
#include <vector>
#include <iostream>
#include <iomanip>
#include "backtest/CointPairsBacktester.hpp"
#include "backtest/MonteCarlo.hpp"

using namespace trading;

namespace {

// Pavages : OOS 500 pour dominer largement le warmup 125-251 (D35).
constexpr size_t kIsCan = 750, kOosCan = 500, kStepCan = 500;
constexpr size_t kIsFin = 700, kOosFin = 500, kStepFin = 500;
constexpr size_t kIsShift = 750, kOosShift = 500, kStepShift = 500, kOffShift = 90;

// Config EXPLICITE (D33) : fenêtres Engle-Granger 126 j (~6 mois), critique
// MacKinnon 5 % figée, z-score 20 j, hystérésis 2,0/0,0 (Sprint 12), coûts D22.
CointPairsConfig cfgCp() {
    CointPairsConfig c;
    c.betaWindow     = 126;
    c.adfWindow      = 126;
    c.adfCritical    = -3.34;
    c.zWindow        = 20;
    c.entryK         = 2.0;
    c.exitZ          = 0.0;
    c.initialCapital = 10'000.0;
    c.commissionPct  = 0.001;
    c.slippageBps    = 2.0;
    c.halfSpreadBps  = 0.5;
    return c;
}

std::vector<std::string> qqqSpy() {
    return { SWINGBOT_QQQ_MAX_CSV, SWINGBOT_SPY_MAX_CSV };
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
    double meanTotalReturn = 0.0, meanSharpe = 0.0, meanMaxDd = 0.0;
    double meanLeg0Dd = 0.0, meanCoint = 0.0;
    std::vector<TradeRecord> pool;
    double years = 0.0;
};

AggOos agregeOos(const std::vector<CointPairWindow>& ws) {
    AggOos a;
    a.windows = ws.size();
    for (const auto& w : ws) {
        a.meanTotalReturn += w.oos.totalReturnPct;
        a.meanSharpe      += w.oos.sharpeRatio;
        a.meanMaxDd       += w.oos.maxDrawdownPct;
        a.meanLeg0Dd      += w.oos.leg0BuyHoldDrawdownPct;
        a.meanCoint       += w.oos.pctBarsCointegrated;
        for (const auto& t : w.oos.trades) a.pool.push_back(t);
        if (w.oos.equityDates.size() >= 2) {
            const long d0 = joursCivils(w.oos.equityDates.front());
            const long d1 = joursCivils(w.oos.equityDates.back());
            if (d1 > d0) a.years += static_cast<double>(d1 - d0) / 365.25;
        }
    }
    if (a.windows > 0) {
        const double n = static_cast<double>(a.windows);
        a.meanTotalReturn /= n; a.meanSharpe /= n; a.meanMaxDd /= n;
        a.meanLeg0Dd /= n; a.meanCoint /= n;
    }
    return a;
}

void imprime(const char* nom, const AggOos& a) {
    std::cout << std::fixed << std::setprecision(4)
              << "  COINTPAIRS " << nom << " : fen=" << a.windows
              << " ret=" << a.meanTotalReturn
              << " sharpe=" << a.meanSharpe
              << " DD=" << a.meanMaxDd
              << " leg0DD=" << a.meanLeg0Dd
              << " coint=" << a.meanCoint << "%"
              << " AR=" << a.pool.size()
              << " annees=" << a.years << "\n";
}

std::vector<CointPairWindow> paving(const std::vector<std::string>& csvs,
                                    size_t is, size_t oos, size_t step, size_t off = 0) {
    return CointPairsWalkForward(cfgCp(), csvs, is, oos, step, off).run();
}

} // namespace

// ════════════════════════════════════════════════════════════
//  Axe commun QQQ ∩ SPY verrouillé (identique au pairs naïf Sprint 12).
// ════════════════════════════════════════════════════════════
TEST(CointPairsOosIntegration, CointPairsAxisIsLocked) {
    CointPairsBacktester pb(cfgCp(), qqqSpy());
    const auto& ax = pb.axis();
    ASSERT_EQ(ax.assets(), 2u);
    EXPECT_EQ(ax.dates.front(), "1999-03-10");
    EXPECT_EQ(ax.dates.back(),  "2026-07-01");
    EXPECT_EQ(ax.size(), 6870u);           // borné par QQQ (1999)
    for (const auto& col : ax.close) EXPECT_EQ(col.size(), ax.size());
}

// ════════════════════════════════════════════════════════════
//  Pavage CANONIQUE
// ════════════════════════════════════════════════════════════
TEST(CointPairsOosIntegration, CointPairsCanonicalPavingVerdictIsLocked) {
    const auto a = agregeOos(paving(qqqSpy(), kIsCan, kOosCan, kStepCan));
    imprime("CANON", a);

    ASSERT_GE(a.windows, 2u);
    EXPECT_TRUE(std::isfinite(a.meanSharpe));
    // Garde d'activation D47 (DOUBLE) : la famille trade réellement ET le gate
    // de cointégration s'ouvre sur une part non triviale des barres décidables.
    EXPECT_GT(a.pool.size(), 1u);
    EXPECT_GT(a.meanCoint, 1.0);

    // Verdict : AUCUN edge — mieux que le spread naïf D49 (Sharpe −0,31 contre
    // −1,18 : le gate filtre bien le bruit) mais toujours NÉGATIF net de coûts.
    EXPECT_LT(a.meanSharpe, 0.0);

    EXPECT_EQ(a.windows, 12u);
    EXPECT_NEAR(a.meanTotalReturn, -0.6774, 1e-2);
    EXPECT_NEAR(a.meanSharpe,      -0.3145, 1e-2);
    EXPECT_NEAR(a.meanMaxDd,        1.4843, 1e-2);
    EXPECT_NEAR(a.meanCoint,        9.5588, 1e-2);
    EXPECT_EQ(a.pool.size(), 23u);
}

// ════════════════════════════════════════════════════════════
//  Pavage FIN (IS 700 — le warmup 125 impose des fenêtres larges, D35)
// ════════════════════════════════════════════════════════════
TEST(CointPairsOosIntegration, CointPairsFinePavingVerdictIsLocked) {
    const auto a = agregeOos(paving(qqqSpy(), kIsFin, kOosFin, kStepFin));
    imprime("FIN", a);

    ASSERT_GE(a.windows, 2u);
    EXPECT_GT(a.pool.size(), 1u);
    EXPECT_GT(a.meanCoint, 1.0);
    EXPECT_LT(a.meanSharpe, 0.0);

    EXPECT_EQ(a.windows, 12u);
    EXPECT_NEAR(a.meanTotalReturn, -0.5607, 1e-2);
    EXPECT_NEAR(a.meanSharpe,      -0.2501, 1e-2);
    EXPECT_NEAR(a.meanCoint,        8.1996, 1e-2);
    EXPECT_EQ(a.pool.size(), 19u);
}

// ════════════════════════════════════════════════════════════
//  Pavage DÉCALÉ (offset 90)
// ════════════════════════════════════════════════════════════
TEST(CointPairsOosIntegration, CointPairsShiftedPavingVerdictIsLocked) {
    const auto a = agregeOos(paving(qqqSpy(), kIsShift, kOosShift, kStepShift, kOffShift));
    imprime("SHIFT", a);

    ASSERT_GE(a.windows, 2u);
    EXPECT_GT(a.pool.size(), 1u);
    EXPECT_GT(a.meanCoint, 1.0);
    EXPECT_LT(a.meanSharpe, 0.0);

    EXPECT_EQ(a.windows, 12u);
    EXPECT_NEAR(a.meanTotalReturn, -1.5241, 1e-2);
    EXPECT_NEAR(a.meanSharpe,      -0.4006, 1e-2);
    EXPECT_NEAR(a.meanCoint,        9.8039, 1e-2);
    EXPECT_EQ(a.pool.size(), 24u);
}

// ════════════════════════════════════════════════════════════
//  Cohérence multi-paires (pavage fin) : QQQ/SPY, QQQ/IWM, QQQ/MDY, SPY/IWM.
//  Le verdict n'est pas un artefact d'une seule paire.
// ════════════════════════════════════════════════════════════
TEST(CointPairsOosIntegration, CointPairsMultiPairVerdictIsLocked) {
    struct P { const char* nom; const char* c0; const char* c1;
               double sharpeAttendu; double cointAttendu; };
    const P paires[] = {
        {"QQQ/SPY", SWINGBOT_QQQ_MAX_CSV, SWINGBOT_SPY_MAX_CSV, -0.2501, 8.1996},
        {"QQQ/IWM", SWINGBOT_QQQ_MAX_CSV, SWINGBOT_IWM_MAX_CSV, -0.2154, 6.6845},
        {"QQQ/MDY", SWINGBOT_QQQ_MAX_CSV, SWINGBOT_MDY_MAX_CSV, -0.2490, 8.1105},
        {"SPY/IWM", SWINGBOT_SPY_MAX_CSV, SWINGBOT_IWM_MAX_CSV, -0.1303, 7.7783},
    };
    std::cout << std::fixed << std::setprecision(4) << "  COINTPAIRS multi-paires (fin)\n";
    for (const auto& p : paires) {
        const auto a = agregeOos(paving({p.c0, p.c1}, kIsFin, kOosFin, kStepFin));
        std::cout << "    " << p.nom << " : sharpe=" << a.meanSharpe
                  << " coint=" << a.meanCoint << "%"
                  << " AR=" << a.pool.size() << "\n";
        EXPECT_GT(a.pool.size(), 1u);
        EXPECT_LT(a.meanSharpe, 0.0);              // aucun edge sur aucune paire
        EXPECT_NEAR(a.meanSharpe, p.sharpeAttendu, 1e-2);
        EXPECT_NEAR(a.meanCoint,  p.cointAttendu,  1e-2);
    }
}

// ════════════════════════════════════════════════════════════
//  Balayage betaWindow × entryK (candidat éventuel, GATÉ) : critère = Sharpe
//  OOS moyen (pavage fin). adfWindow suit betaWindow (fenêtres EG cohérentes).
// ════════════════════════════════════════════════════════════
TEST(CointPairsOosIntegration, CointPairsSweepBestOosIsLocked) {
    const int    fenetres[] = {126, 252};
    const double entrees[]  = {1.5, 2.0, 2.5};
    double best = -1e9; int bestW = 0; double bestK = 0.0;
    std::cout << std::fixed << std::setprecision(4)
              << "  COINTPAIRS balayage betaWindow x entryK (fin QQQ/SPY, critere = Sharpe OOS)\n";
    for (int bw : fenetres) for (double k : entrees) {
        CointPairsConfig c = cfgCp(); c.betaWindow = bw; c.adfWindow = bw; c.entryK = k;
        const auto a = agregeOos(CointPairsWalkForward(c, qqqSpy(),
                                                       kIsFin, kOosFin, kStepFin).run());
        std::cout << "    w=" << bw << " k=" << k
                  << " -> sharpe=" << a.meanSharpe << " AR=" << a.pool.size() << "\n";
        if (a.meanSharpe > best) { best = a.meanSharpe; bestW = bw; bestK = k; }
    }
    std::cout << "    MEILLEUR : w=" << bestW << " k=" << bestK
              << " -> Sharpe OOS " << best
              << (best > 0.0 ? "  (CANDIDAT > 0)" : "  (aucun candidat)") << "\n";

    // VERDICT 18.6 (figé) : AUCUN réglage ne donne un Sharpe OOS positif.
    EXPECT_NEAR(best, -0.1663, 1e-2);
    EXPECT_EQ(bestW, 252);
    EXPECT_DOUBLE_EQ(bestK, 2.5);
    EXPECT_FALSE(best > 0.0);   // pas de candidat -> pas de confirmation
}

// ════════════════════════════════════════════════════════════
//  Monte-Carlo size-aware (D45) sur les A/R OOS poolés (canonique).
// ════════════════════════════════════════════════════════════
TEST(CointPairsOosIntegration, CointPairsMonteCarloSizeAwareIsLocked) {
    const auto a = agregeOos(paving(qqqSpy(), kIsCan, kOosCan, kStepCan));
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
              << "  COINTPAIRS MC : cagrP50=" << r.cagrP50 << " ddP95=" << r.ddP95 << "\n";
    // Nettement moins destructeur que le naïf D49 (CAGR −3,62 % / DD p95 68 %)
    // mais toujours ≤ 0 : le gate filtre le bruit sans créer d'edge.
    EXPECT_NEAR(r.cagrP50, -0.3292, 1e-3);
    EXPECT_NEAR(r.ddP95,   17.2841, 1e-3);
}
