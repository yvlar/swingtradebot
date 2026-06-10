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

} // namespace trading
