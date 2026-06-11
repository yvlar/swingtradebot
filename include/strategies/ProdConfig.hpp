#pragma once
#include "strategies/SwingStrategy.hpp"

namespace trading {

// ─── Config de production (Sprint 6.1, D21) ──────────────────────────────────
// Source UNIQUE de la SwingConfig câblée en production : main_ibkr.cpp la
// consomme, et le golden « config prod » (test_backtester_integration.cpp)
// backteste exactement cette fonction. Conséquence voulue : toute modification
// ici casse le golden tant que les nouvelles valeurs n'ont pas été
// re-backtestées et re-figées — aucune config non validée ne peut partir en
// live silencieusement. L'externalisation complète (fichier JSON validé au
// démarrage) reste l'item 9.1.
inline SwingConfig prodSwingConfig() {
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
