// ============================================================
//  test_vol_regime_oos_integration.cpp  —  Tests d'INTÉGRATION
//  Cible : VolRegimeBacktester / VolRegimeWalkForward
//  Sprint 13 (item 13.2) — VERDICT OUT-OF-SAMPLE VERROUILLÉ.
//
//  5e famille d'alpha : VOL-REGIME / volatility-managed exposure. Filtre binaire
//  long/cash : long l'actif quand la volatilité réalisée est CALME (≤ thresholdMult
//  × sa médiane glissante de référence), sinon cash. Famille NEUVE — non un signal
//  de prix directionnel de plus, mais une modulation d'exposition par le régime de
//  vol (hypothèse Moreira & Muir 2017 : réduire l'exposition en vol haute améliore
//  le rendement ajusté du risque).
//
//  Question : ce filtre bat-il le Buy & Hold de l'actif sur le rendement AJUSTÉ DU
//  RISQUE (Sharpe) NET DE COÛTS en OOS ? Étant directionnel long/cash, il est
//  DIRECTEMENT comparable au B&H sur rendement ET Sharpe. Critère PRIMAIRE : Sharpe
//  stratégie vs Sharpe B&H (delta). Clause DoD « DD réduit ≥ 50 % » : figée via le
//  DD stratégie ET le DD B&H. Sanity D23 (« faire de l'argent ») : alpha vs B&H
//  (attendu négatif sur un QQQ structurellement haussier — cash drag T4/D48).
//
//  Discipline (D33/D34) : config construite explicitement, trades OOS poolés
//  (comptes verrouillés), garde anti-cash-drag (D47), verdicts figés
//  (sentinelle → mesure → figée). Données longues total-return (*_max.csv), 3
//  pavages (canonique / fin / décalé, anti biais de sélection D36).
//
//  AVERTISSEMENT WARMUP (D35) : warmup = volLookback + refLookback − 2 = 144 barres
//  (bien plus que le z-score du pairs-trading). Les fenêtres OOS sont donc
//  dimensionnées LARGEMENT au-dessus (OOS ≥ 400) pour ne pas ne mesurer que du cash
//  drag.
//
//  Valeurs MESURÉES le 2026-07-06 sur les *_max.csv et figées.
// ============================================================
#include <gtest/gtest.h>
#include <cmath>
#include <string>
#include <vector>
#include <iostream>
#include <iomanip>
#include "backtest/VolRegimeBacktester.hpp"
#include "backtest/MonteCarlo.hpp"

using namespace trading;

namespace {

// Pavages (constexpr) : canonique, fin (D34), décalé (offset, D36). OOS large
// (≥ 400) pour dominer le warmup de 144 barres (D35).
constexpr size_t kIsCan = 750, kOosCan = 500, kStepCan = 500;
constexpr size_t kIsFin = 550, kOosFin = 400, kStepFin = 400;
constexpr size_t kIsShift = 750, kOosShift = 500, kStepShift = 500, kOffShift = 90;

// Config EXPLICITE du filtre vol-regime (D33). Vol réalisée 20 j, médiane de
// référence 126 j (~6 mois), seuil 1,0 (calme = vol sous sa médiane). Coûts =
// défauts Backtester (D22).
VolRegimeConfig cfgVol() {
    VolRegimeConfig c;
    c.volLookback    = 20;
    c.refLookback    = 126;
    c.thresholdMult  = 1.0;
    c.initialCapital = 10'000.0;
    c.commissionPct  = 0.001;
    c.slippageBps    = 2.0;
    c.halfSpreadBps  = 0.5;
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
    double meanTotalReturn = 0.0, meanSharpe = 0.0, meanSortino = 0.0;
    double meanMaxDd = 0.0, meanBhDd = 0.0, meanBhSharpe = 0.0, meanAlphaVsBh = 0.0;
    std::vector<TradeRecord> pool;
    double years = 0.0;
    double sharpeDelta() const { return meanSharpe - meanBhSharpe; }
};

AggOos agregeOos(const std::vector<VolWindow>& ws) {
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
              << "  VOLREG " << nom << " : fen=" << a.windows
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

std::vector<VolWindow> paving(const std::string& csv,
                              size_t is, size_t oos, size_t step, size_t off = 0) {
    return VolRegimeWalkForward(cfgVol(), csv, is, oos, step, off).run();
}

} // namespace

// ════════════════════════════════════════════════════════════
//  Série QQQ_max verrouillée (bornes de l'axe mono-actif).
// ════════════════════════════════════════════════════════════
TEST(VolRegimeOosIntegration, VolRegimeSeriesIsLocked) {
    VolRegimeBacktester v(cfgVol(), SWINGBOT_QQQ_MAX_CSV);
    ASSERT_GT(v.size(), 0u);
    EXPECT_EQ(v.dates().front(), "1999-03-10");
    EXPECT_EQ(v.dates().back(),  "2026-07-01");
    EXPECT_EQ(v.size(), 6870u);
}

// ════════════════════════════════════════════════════════════
//  Pavage CANONIQUE
// ════════════════════════════════════════════════════════════
TEST(VolRegimeOosIntegration, VolRegimeCanonicalPavingVerdictIsLocked) {
    const auto a = agregeOos(paving(SWINGBOT_QQQ_MAX_CSV, kIsCan, kOosCan, kStepCan));
    imprime("CANON", a);

    ASSERT_GE(a.windows, 2u);
    EXPECT_TRUE(std::isfinite(a.meanSharpe));
    EXPECT_GT(a.pool.size(), 1u);          // D47 : la famille TRADE réellement

    // Verdict : AUCUN edge ajusté du risque. Le Sharpe stratégie (0,52) est
    // POSITIF mais SOUS le Sharpe B&H (1,09) → dSharpe < 0. Le filtre réduit le DD
    // (14,5 vs 18,1) MAIS modestement (< 50 %, contraste avec le pairs-trading) et
    // au prix d'un alpha négatif (cash drag T4/D48) → non retenu.
    EXPECT_LT(a.meanSharpe, a.meanBhSharpe);   // pas d'edge Sharpe vs B&H
    EXPECT_LT(a.meanAlphaVsBh, 0.0);           // sanity D23 : sous-performe le B&H (cash drag)
    EXPECT_LT(a.meanMaxDd, a.meanBhDd);        // DD réduit…
    EXPECT_GT(a.meanMaxDd, 0.5 * a.meanBhDd);  // …mais < 50 % (clause DoD NON atteinte)

    EXPECT_EQ(a.windows, 12u);
    EXPECT_NEAR(a.meanTotalReturn,  12.2915, 1e-2);
    EXPECT_NEAR(a.meanSharpe,        0.5210, 1e-2);
    EXPECT_NEAR(a.meanSortino,       0.8010, 1e-2);
    EXPECT_NEAR(a.meanMaxDd,        14.5456, 1e-2);
    EXPECT_NEAR(a.meanBhDd,         18.1328, 1e-2);
    EXPECT_NEAR(a.meanBhSharpe,      1.0854, 1e-2);
    EXPECT_NEAR(a.meanAlphaVsBh,   -17.9860, 1e-2);
    EXPECT_EQ(a.pool.size(), 118u);
}

// ════════════════════════════════════════════════════════════
//  Pavage FIN
// ════════════════════════════════════════════════════════════
TEST(VolRegimeOosIntegration, VolRegimeFinePavingVerdictIsLocked) {
    const auto a = agregeOos(paving(SWINGBOT_QQQ_MAX_CSV, kIsFin, kOosFin, kStepFin));
    imprime("FIN", a);

    ASSERT_GE(a.windows, 2u);
    EXPECT_GT(a.pool.size(), 1u);
    EXPECT_LT(a.meanSharpe, a.meanBhSharpe);   // pas d'edge Sharpe vs B&H
    EXPECT_LT(a.meanAlphaVsBh, 0.0);

    EXPECT_EQ(a.windows, 15u);
    EXPECT_NEAR(a.meanTotalReturn,   1.3774, 1e-2);
    EXPECT_NEAR(a.meanSharpe,        0.2325, 1e-2);
    EXPECT_NEAR(a.meanSortino,       0.3769, 1e-2);
    EXPECT_NEAR(a.meanMaxDd,        14.8655, 1e-2);
    EXPECT_NEAR(a.meanBhDd,         18.5305, 1e-2);
    EXPECT_NEAR(a.meanBhSharpe,      0.7681, 1e-2);
    EXPECT_NEAR(a.meanAlphaVsBh,    -9.4971, 1e-2);
    EXPECT_EQ(a.pool.size(), 107u);
}

// ════════════════════════════════════════════════════════════
//  Pavage DÉCALÉ (offset 90)
// ════════════════════════════════════════════════════════════
TEST(VolRegimeOosIntegration, VolRegimeShiftedPavingVerdictIsLocked) {
    const auto a = agregeOos(paving(SWINGBOT_QQQ_MAX_CSV, kIsShift, kOosShift, kStepShift, kOffShift));
    imprime("SHIFT", a);

    ASSERT_GE(a.windows, 2u);
    EXPECT_GT(a.pool.size(), 1u);
    EXPECT_LT(a.meanSharpe, a.meanBhSharpe);   // pas d'edge Sharpe vs B&H (pavage non aligné)
    EXPECT_LT(a.meanAlphaVsBh, 0.0);

    EXPECT_EQ(a.windows, 12u);
    EXPECT_NEAR(a.meanTotalReturn,  13.6830, 1e-2);
    EXPECT_NEAR(a.meanSharpe,        0.6073, 1e-2);
    EXPECT_NEAR(a.meanSortino,       0.9752, 1e-2);
    EXPECT_NEAR(a.meanMaxDd,        12.4169, 1e-2);
    EXPECT_NEAR(a.meanBhDd,         17.4499, 1e-2);
    EXPECT_NEAR(a.meanBhSharpe,      1.0381, 1e-2);
    EXPECT_NEAR(a.meanAlphaVsBh,   -17.2145, 1e-2);
    EXPECT_EQ(a.pool.size(), 119u);
}

// ════════════════════════════════════════════════════════════
//  Monte-Carlo size-aware (D45) sur les trades OOS poolés (canonique).
// ════════════════════════════════════════════════════════════
TEST(VolRegimeOosIntegration, VolRegimeMonteCarloSizeAwareIsLocked) {
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
              << "  VOLREG MC : cagrP50=" << r.cagrP50 << " ddP95=" << r.ddP95 << "\n";
    // Le MC size-aware sur les stints long OOS est POSITIF (cagrP50 ~5 %) car le
    // filtre capte une partie de la hausse de QQQ — mais ce n'est PAS le critère :
    // le B&H fait mieux (Sharpe 1,09 vs 0,52). Le MC ne fait que confirmer que la
    // stratégie gagne de l'argent, moins efficacement que le B&H (dSharpe < 0).
    EXPECT_NEAR(r.cagrP50, 5.0262, 1e-3);
    EXPECT_NEAR(r.ddP95,  45.8750, 1e-3);
}

// ════════════════════════════════════════════════════════════
//  Cohérence multi-univers (pavage fin) : QQQ, SPY, IWM, MDY (mono-actif chacun).
//  Le filtre vol-regime n'est pas un artefact d'un seul actif.
// ════════════════════════════════════════════════════════════
TEST(VolRegimeOosIntegration, VolRegimeMultiUniverseVerdictIsLocked) {
    struct U { const char* nom; const char* csv;
               double sharpeAttendu; double retAttendu; size_t tradesAttendus; };
    const U univ[] = {
        {"QQQ", SWINGBOT_QQQ_MAX_CSV, 0.2325, 1.3774, 107u},
        {"SPY", SWINGBOT_SPY_MAX_CSV, 0.1601, 1.7551, 134u},
        {"IWM", SWINGBOT_IWM_MAX_CSV, 0.5600, 7.7028, 118u},
        {"MDY", SWINGBOT_MDY_MAX_CSV, 0.2971, 5.1548, 135u},
    };
    std::cout << std::fixed << std::setprecision(4) << "  VOLREG multi-univers (fin)\n";
    for (const auto& u : univ) {
        const auto a = agregeOos(paving(u.csv, kIsFin, kOosFin, kStepFin));
        std::cout << "    " << u.nom << " : sharpe=" << a.meanSharpe
                  << " bhSharpe=" << a.meanBhSharpe
                  << " ret=" << a.meanTotalReturn
                  << " trades=" << a.pool.size() << "\n";
        EXPECT_GT(a.pool.size(), 1u);
        EXPECT_NEAR(a.meanSharpe,      u.sharpeAttendu, 1e-2);
        EXPECT_NEAR(a.meanTotalReturn, u.retAttendu,    1e-2);
        EXPECT_EQ(a.pool.size(), u.tradesAttendus);
        EXPECT_LT(a.meanSharpe, a.meanBhSharpe);   // aucun edge sur aucun actif
    }
}

// ════════════════════════════════════════════════════════════
//  Balayage volLookback × thresholdMult (candidat éventuel, GATÉ) : critère =
//  delta de Sharpe OOS vs B&H. Un candidat n'existe que si un réglage donne un
//  delta > 0 (le filtre bat le B&H sur le risque ajusté).
// ════════════════════════════════════════════════════════════
TEST(VolRegimeOosIntegration, VolRegimeThresholdSweepBestOosIsLocked) {
    const int    vols[]  = {10, 20, 42};
    const double mults[] = {0.8, 1.0, 1.2};
    double best = -1e9; int bestV = 0; double bestM = 0.0;
    std::cout << std::fixed << std::setprecision(4)
              << "  VOLREG balayage volLookback x thresholdMult (fin QQQ, critere = dSharpe OOS)\n";
    for (int vl : vols) for (double mm : mults) {
        VolRegimeConfig c = cfgVol(); c.volLookback = vl; c.thresholdMult = mm;
        const auto ws = VolRegimeWalkForward(c, SWINGBOT_QQQ_MAX_CSV,
                                             kIsFin, kOosFin, kStepFin).run();
        const auto a = agregeOos(ws);
        std::cout << "    vl=" << vl << " m=" << mm
                  << " -> dSharpe=" << a.sharpeDelta() << "\n";
        if (a.sharpeDelta() > best) { best = a.sharpeDelta(); bestV = vl; bestM = mm; }
    }
    std::cout << "    MEILLEUR : vl=" << bestV << " m=" << bestM
              << " -> dSharpe OOS " << best
              << (best > 0.0 ? "  (CANDIDAT > 0)" : "  (aucun candidat)") << "\n";

    // VERDICT 13.2 (figé) : AUCUN réglage ne bat le B&H sur le Sharpe — le meilleur
    // (volLookback=42 / seuil=1,2) reste à dSharpe -0,16. Gate de confirmation FERMÉ.
    EXPECT_NEAR(best, -0.1554, 1e-2);
    EXPECT_EQ(bestV, 42);
    EXPECT_DOUBLE_EQ(bestM, 1.2);
    EXPECT_FALSE(best > 0.0);   // pas de candidat -> pas de confirmation
}
