#pragma once
#include "strategies/SwingStrategy.hpp"

namespace trading {

// ─── Config héritée (V1, archivée) — témoin du verdict D30 ───────────────────
// L'ancienne config de production (avant le Sprint 8) : take-profit fixe 15 %,
// vente sur RSI > 80, sizing risk-based 2 %. Conservée comme TÉMOIN : les goldens
// d'archive (walk-forward D30, grille 7.2, Monte-Carlo 7.3) la rejouent pour que
// les leçons des Sprints 6-7 (« aucun edge OOS », « tuner ne sauve pas une
// structure cassée », « cash drag décisif ») restent vérifiables. Ne plus la
// trader : elle perd contre le Buy & Hold en OOS (D30).
inline SwingConfig legacyProdConfig() {
    SwingConfig cfg;
    cfg.symbol          = "QQQ";
    cfg.emaFast         = 13;
    cfg.emaSlow         = 21;
    cfg.rsiPeriod       = 14;
    cfg.rsiBuyMax       = 65.0;
    cfg.rsiSellMin      = 80.0;
    cfg.stopLossPct     = 0.07;
    cfg.takeProfitPct   = 0.15;
    cfg.trailingStopPct = 0.03;
    cfg.riskPerTradePct = 0.02;
    cfg.minHoldDays     = 2;
    return cfg;
}

// ─── Config V2 « suivi de tendance » (Sprints 8-9) ────────────────────────────
// Même déclencheur d'entrée que la V1, mais SORTIES refondues : pas de
// take-profit fixe (8.2) et pas de vente sur RSI suracheté (8.4). La synergie de
// ces deux changements laisse les gagnants courir jusqu'au trailing au lieu de
// les écrêter — au banc, l'alpha OOS passe de −13,5 à +14,9 pts et 4/4 segments
// battent le B&H (voir research/prototype_exits.py).
//
// Sizing (item 9.0b) : fraction fixe 90 % de l'exposition. Le risk-based bridait
// l'exposition à ~28 %/position (frein distinct du timing, D34) ; le timing — pas
// le stop — porte l'edge, donc on vise une exposition fixe. Niveau 90 % arbitré au
// banc (research/prototype_sizing.py) : bat le B&H en OOS avec marge (~+10 pts, 4/4)
// pour un maxDD backtest ~4,5 %, en gardant un coussin de cash de 10 %. Vol-targeting
// et Kelly écartés (sans gain mesuré / strictement pires). Décision utilisateur.
inline SwingConfig swingTrendConfig() {
    SwingConfig cfg = legacyProdConfig();
    cfg.takeProfitPct       = 0.0;    // 8.2 : pas de take-profit fixe
    cfg.sellOnRsiOverbought = false;  // 8.4 : ne pas vendre sur RSI seul
    cfg.targetExposurePct   = 0.90;   // 9.0b : exposition fixe 90 %
    return cfg;
}

// ─── Config de production IBKR — source unique (item 6.1 / D21) ──────────────
// Cette fonction est la SEULE définition des paramètres tradés par
// main_ibkr.cpp, et elle est validée par les goldens « prod »
// (test_backtester_integration.cpp) et OOS (test_strategy_v2_integration.cpp).
//
// Depuis l'item 9.0 (Sprint 9), la prod EST la V2 : première config qui bat le
// Buy & Hold en out-of-sample (OOS +46,5 % vs +39,4 %, alpha +7,2 pts, maxDD
// 4,6 % — verdict figé dans test_strategy_v2_integration). Toute modification ici
// fait échouer les goldens : re-figer uniquement après avoir relancé le backtest
// et documenté le delta dans ROADMAP.md. L'externalisation complète (fichier
// JSON validé au démarrage) est l'item 9.1.
inline SwingConfig ibkrProdConfig() {
    return swingTrendConfig();  // item 9.0 : la prod EST la V2
}

} // namespace trading
