// ─── Tests d'intégration : optimiseur de grille en OOS (items 7.2 / 8b.1) ────
// Grilles de SwingConfig évaluées par leur MÉTRIQUE OOS (Sharpe moyen du
// walk-forward, item 7.1) — jamais l'in-sample. Depuis le Sprint 8-bis (8b.1),
// les axes balayés sont ceux qui PILOTENT la chaîne v2 : emaFast/emaSlow,
// smaTrendPeriod, trailingStopPct (21/22 sorties du golden sont des trailing).
// Les grilles restent volontairement PETITES (contrainte de timeout CI) ; le
// balayage exhaustif est un outil hors-ligne (le CLI `validate`).
#include <gtest/gtest.h>
#include "backtest/GridOptimizer.hpp"
#include "backtest/WalkForward.hpp"

using namespace trading;

namespace {
// 2 fenêtres walk-forward sur 1790 barres : IS=900, OOS=400, pas=400
// → s = 0, 400 (s+1300 ≤ 1790).
constexpr size_t IS_BARS  = 900;
constexpr size_t OOS_BARS = 400;
constexpr size_t STEP     = 400;

// Pavage FIN du Sprint 8-bis (item 8b.3, décision d'ordre du sprint : la
// grille 8b.1 est jugée sur le pavage statistiquement plus solide) : 4
// fenêtres OOS contiguës, OOS=300 pour rester tradable après le warmup ~201.
constexpr size_t IS_FIN   = 500;
constexpr size_t OOS_FIN  = 300;
constexpr size_t STEP_FIN = 300;

// ── Config de la chaîne v2, EXPLICITE champ par champ (règle D33) ────────────
// Un verdict est une mesure HISTORIQUE : il ne doit pas bouger quand les
// défauts de SwingConfig évoluent. Jamais `SwingConfig{}` dans un verrou.
// (Duplication volontaire avec test_strategy_v2_integration.cpp — chaque
// fichier de verdict porte son propre snapshot.)
SwingConfig cfgChaineV2() {
    SwingConfig c;
    c.symbol                  = "QQQ";
    c.emaFast                 = 9;
    c.emaSlow                 = 21;
    c.rsiPeriod               = 14;
    c.rsiBuyMax               = 100.0;   // 8.3 : plafond d'achat désactivé
    c.rsiSellMin              = 70.0;
    c.stopLossPct             = 0.05;
    c.takeProfitPct           = 0.0;     // 8.2 : take-profit désactivé
    c.trailingStopPct         = 0.03;
    c.riskPerTradePct         = 0.02;
    c.minHoldDays             = 3;
    c.smaTrendPeriod          = 200;     // 8.1 : filtre de régime
    c.rsiSellOnlyIfRegimeDown = true;    // 8.4 : vente RSI gatée
    c.regimeReentry           = true;    // 8.5 : re-entrée sur régime
    return c;
}

// Objectif générique : Sharpe OOS moyen ; filtre alpha = alpha OOS moyen ;
// départage sur le drawdown OOS moyen. Dépendance explicite 7.2 → 7.1.
GridScore objectifOosAvec(const SwingConfig& c, size_t is, size_t oos, size_t step) {
    WalkForward wf(c, SWINGBOT_QQQ_CSV, is, oos, step);
    const auto windows = wf.run();
    GridScore s;
    if (windows.empty()) return s;
    double sharpe = 0, alpha = 0, dd = 0;
    for (const auto& w : windows) {
        sharpe += w.oos.sharpeRatio;
        alpha  += w.oos.alpha;
        dd     += w.oos.maxDrawdownPct;
    }
    const double k = static_cast<double>(windows.size());
    s.metric   = sharpe / k;
    s.alpha    = alpha  / k;
    s.drawdown = dd     / k;
    return s;
}

GridScore objectifOos(const SwingConfig& c) {
    return objectifOosAvec(c, IS_BARS, OOS_BARS, STEP);
}

GridScore objectifOosFin(const SwingConfig& c) {
    return objectifOosAvec(c, IS_FIN, OOS_FIN, STEP_FIN);
}
} // namespace

// Mécanisme (item 7.2, ré-axé 8b.1) : une petite grille sur les axes v2
// produit une carte de sensibilité et le point retenu est bien le plateau.
TEST(GridOptimizerIntegration, SmallGridProducesSensitivityMapInOos) {
    GridOptimizer opt(
        /*emaFast   */ {9, 13},
        /*emaSlow   */ {21},
        /*rsiBuyMax */ {100},
        /*rsiSellMin*/ {70},
        /*stopLoss  */ {0.05},
        /*takeProfit*/ {0.0},
        objectifOos, cfgChaineV2(),
        /*smaTrend  */ {150, 250},
        /*trailing  */ {0.03, 0.08});

    const auto pts = opt.evaluate();
    ASSERT_EQ(pts.size(), 8u);   // 2 × 1 × 1 × 1 × 1 × 1 × 2 × 2

    // Le point retenu est le plateau : sa moyenne de voisinage est maximale.
    const auto sel = GridOptimizer::selectRobustPlateau(pts);
    for (const auto& p : pts)
        EXPECT_LE(p.neighborhoodAvg, sel.point.neighborhoodAvg + 1e-9);

    // Artefact d'acceptation : la carte de sensibilité. Sur la stratégie
    // actuelle (sans edge), le filtre alpha > 0 ne passe probablement pas —
    // c'est un résultat honnête, pas une erreur du test.
    opt.printSensitivityMap(pts);
}

// ─── Verdict verrouillé du Sprint 8-bis (item 8b.1) ──────────────────────────
// Grille étendue sur la chaîne v2 (base explicite D33), 3 axes balayés
// (emaFast × smaTrendPeriod × trailingStopPct = 18 combos), jugée en OOS sur
// le PAVAGE FIN (4 fenêtres, item 8b.3). Verrous : le verdict d'edge (le
// filtre alpha > 0 passe-t-il ?), le plateau retenu, et le CLASSEMENT DE
// SENSIBILITÉ par axe — c'est lui qui ouvre ou ferme l'item 8b.4 (trailing
// adaptatif ATR : ne s'ouvre que si trailingStopPct est l'axe le plus
// sensible).
//
// VERDICT MESURÉ (re-figé le 2026-07-02, B2 — exécution à l'open i+1) :
//   - le CANDIDAT D'EDGE SURVIT à la correction du look-ahead : le filtre
//     alpha > 0 PASSE toujours, et le plateau se renforce même — (emaFast=9,
//     smaT=250, trail=0,03), Sharpe OOS moyen 1,267, alpha OOS moyen
//     +0,231 pt (vs +0,097 avant correction). Toujours MARGINAL et entaché
//     du même biais de sélection (meilleur de 18 combos jugés sur les MÊMES
//     fenêtres OOS) : « retenir » = décision utilisateur + re-déroulement de
//     la DoD complète — PAS une adoption automatique.
//   - INVERSION vs l'ancien verdict : le trailing retenu passe de 0,05 à
//     0,03, et l'axe le plus sensible devient trailingStopPct (0,266) devant
//     smaTrendPeriod (0,194) et emaFast (0,105) → le GATE 8b.4 (trailing
//     adaptatif ATR) s'OUVRE désormais à la lettre. Historique pré-B2 :
//     plateau (9, 250, 0,05), metric 1,219, alpha +0,097, axe smaT (0,244).
TEST(GridOptimizerIntegration, V2ChainExtendedGridOosVerdictIsLocked) {
    GridOptimizer opt(
        /*emaFast   */ {9, 13},
        /*emaSlow   */ {21},
        /*rsiBuyMax */ {100},
        /*rsiSellMin*/ {70},
        /*stopLoss  */ {0.05},
        /*takeProfit*/ {0.0},
        objectifOosFin, cfgChaineV2(),
        /*smaTrend  */ {150, 200, 250},
        /*trailing  */ {0.03, 0.05, 0.08});

    const auto pts = opt.evaluate();
    ASSERT_EQ(pts.size(), 18u);   // 2 × 3 × 3

    const auto sel  = GridOptimizer::selectRobustPlateau(pts);
    const auto sens = opt.axisSensitivities(pts);
    ASSERT_EQ(sens.size(), 8u);
    size_t plusSensible = 0;
    for (size_t ax = 1; ax < sens.size(); ++ax)
        if (sens[ax] > sens[plusSensible]) plusSensible = ax;

    opt.printSensitivityMap(pts);
    std::cout << std::fixed << std::setprecision(4)
              << "  VERDICT 8b.1 — plateau : emaFast=" << sel.point.cfg.emaFast
              << " smaT=" << sel.point.cfg.smaTrendPeriod
              << " trail=" << sel.point.cfg.trailingStopPct
              << " (metric=" << sel.point.score.metric
              << ", alpha=" << sel.point.score.alpha
              << ", filtre alpha>0 : " << (sel.passedAlphaFilter ? "PASSE" : "ne passe pas")
              << ")\n  axe le plus sensible : " << GridOptimizer::axisName(plusSensible)
              << " (emaFast=" << sens[0] << ", smaT=" << sens[6]
              << ", trail=" << sens[7] << ")\n";

    // Candidat d'edge : le filtre alpha > 0 passe pour la première fois.
    // Si ce verrou casse un jour (retour à « pas d'edge »), le candidat
    // n'a pas survécu à un changement de données/moteur — re-juger avant
    // toute décision.
    EXPECT_TRUE(sel.passedAlphaFilter);
    EXPECT_EQ(sel.point.cfg.emaFast, 9);
    EXPECT_EQ(sel.point.cfg.smaTrendPeriod, 250);
    EXPECT_NEAR(sel.point.cfg.trailingStopPct, 0.03, 1e-9);
    EXPECT_NEAR(sel.point.score.metric, 1.2674, 1e-2);   // Sharpe OOS moyen
    EXPECT_NEAR(sel.point.score.alpha,  0.2312, 1e-2);   // alpha OOS moyen
    EXPECT_EQ(plusSensible, 7u);   // trailingStopPct → gate 8b.4 OUVERT (B2)
}

// ─── Verdict verrouillé du Sprint 8-ter (item 8t.3) ──────────────────────────
// Grille de CONFIRMATION resserrée AUTOUR du candidat post-B2 (9/250/0,03) :
// emaFast {7,9,11} × smaT {225,250,275} × trail {0,02/0,03/0,04} = 27 combos,
// candidat au CENTRE exact du cube (voisinage ±1 complet sur les 3 axes).
// Mêmes objectif et pavage que le verrou 8b.1 (Sharpe OOS moyen du pavage fin,
// base chaîne v2 explicite D33). Le principe (D36) : un VRAI plateau doit
// rester alpha > 0 quand on resserre les crans autour de lui — l'ancienne
// grille {0,04/0,05/0,08} n'offrait même pas trail=0,03 à la sélection.
// Acceptation ROADMAP 8t.3 : plateau resserré alpha > 0 ET contenant smaT=250,
// sinon « artefact de grille ».
// VERDICT MESURÉ (figé le 2026-07-03) : ARTEFACT DE GRILLE — le filtre
// alpha > 0 passe, mais le plateau resserré est (emaFast=9, smaT=275,
// trail=0,04) : il NE CONTIENT PAS smaT=250. À crans resserrés, même l'axe
// réputé stable (smaT, D36) dérive de 250 → 275 et le trailing de 0,03 → 0,04.
// Le « meilleur » se déplace avec la grille qui le mesure : c'est la signature
// d'un artefact de sélection, pas d'un plateau d'edge. Cohérent avec 8t.1
// (le candidat s'effondre sur les fenêtres canoniques) et 8t.2 (CAGR médian
// inférieur à la chaîne).
TEST(GridOptimizerIntegration, CandidateConfirmationTightGridOosVerdictIsLocked) {
    GridOptimizer opt(
        /*emaFast   */ {7, 9, 11},
        /*emaSlow   */ {21},
        /*rsiBuyMax */ {100},
        /*rsiSellMin*/ {70},
        /*stopLoss  */ {0.05},
        /*takeProfit*/ {0.0},
        objectifOosFin, cfgChaineV2(),
        /*smaTrend  */ {225, 250, 275},
        /*trailing  */ {0.02, 0.03, 0.04});

    const auto pts = opt.evaluate();
    ASSERT_EQ(pts.size(), 27u);   // 3 × 3 × 3

    const auto sel = GridOptimizer::selectRobustPlateau(pts);
    opt.printSensitivityMap(pts);
    std::cout << std::fixed << std::setprecision(4)
              << "  VERDICT 8t.3 — plateau resserre : emaFast=" << sel.point.cfg.emaFast
              << " smaT=" << sel.point.cfg.smaTrendPeriod
              << " trail=" << sel.point.cfg.trailingStopPct
              << " (metric=" << sel.point.score.metric
              << ", alpha=" << sel.point.score.alpha
              << ", filtre alpha>0 : " << (sel.passedAlphaFilter ? "PASSE" : "ne passe pas")
              << ")\n";

    // Verdict figé : alpha > 0 quelque part dans le voisinage… mais PAS au
    // point candidat — le plateau a dérivé (250 → 275, 0,03 → 0,04).
    // L'acceptation « contient smaT=250 » est NON satisfaite : artefact.
    EXPECT_TRUE(sel.passedAlphaFilter);
    EXPECT_EQ(sel.point.cfg.emaFast, 9);
    EXPECT_EQ(sel.point.cfg.smaTrendPeriod, 275);
    EXPECT_NE(sel.point.cfg.smaTrendPeriod, 250);   // ← le cœur du verdict 8t.3
    EXPECT_NEAR(sel.point.cfg.trailingStopPct, 0.04, 1e-9);
    EXPECT_NEAR(sel.point.score.metric, 1.7851, 1e-2);  // Sharpe OOS moyen
    EXPECT_NEAR(sel.point.score.alpha,  0.5141, 1e-2);  // alpha OOS moyen
}
