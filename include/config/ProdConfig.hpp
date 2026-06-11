#pragma once
#include "strategies/SwingStrategy.hpp"

namespace trading {

// ─── Config de production IBKR — source unique (item 6.1 / D21) ──────────────
// Cette fonction est la SEULE définition des paramètres tradés par
// main_ibkr.cpp, et elle est validée par le golden de non-régression
// « prod » (test_backtester_integration.cpp). Avant ce partage, la config de
// prod était dupliquée dans main_ibkr.cpp et n'était couverte par AUCUN
// backtest : le live tradait des paramètres jamais validés (découverte D21).
//
// Toute modification ici fait échouer le golden prod : re-figer les valeurs
// uniquement après avoir relancé le backtest et documenté le delta dans
// ROADMAP.md. L'externalisation complète (fichier JSON validé au démarrage)
// est l'item 9.1.
inline SwingConfig ibkrProdConfig() {
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

// ─── Config V2 « suivi de tendance » (Sprint 8 — refonte des sorties) ─────────
// Même déclencheur d'entrée que la prod, mais SORTIES refondues : pas de
// take-profit fixe (8.2) et pas de vente sur RSI suracheté (8.4). La synergie de
// ces deux changements laisse les gagnants courir jusqu'au trailing au lieu de
// les écrêter — au banc, l'alpha OOS passe de −13,5 à +14,9 pts et 4/4 segments
// battent le B&H (voir research/prototype_exits.py).
//
// ⚠️ riskPerTradePct reste à 2 % : à cette exposition (~28 %/position) l'edge de
// TIMING ne se traduit PAS encore en dollars battant le B&H — le passage à
// l'échelle (sizing) est une décision délibérée du Sprint 9. La validation OOS
// du timing (test_strategy_v2_integration) s'évalue donc à pleine exposition.
inline SwingConfig swingTrendConfig() {
    SwingConfig cfg = ibkrProdConfig();
    cfg.takeProfitPct      = 0.0;    // 8.2 : pas de take-profit fixe
    cfg.sellOnRsiOverbought = false; // 8.4 : ne pas vendre sur RSI seul
    return cfg;
}

} // namespace trading
