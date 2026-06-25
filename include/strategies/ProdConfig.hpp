#pragma once
#include "strategies/SwingStrategy.hpp"

namespace trading {

// ─── Config de production (Sprint 6.1, D21 ; révisée Sprint 7, item 7.4) ─────
// Source UNIQUE de la SwingConfig câblée en production : main_ibkr.cpp la
// consomme, et le golden « config prod » (test_backtester_integration.cpp)
// backteste exactement cette fonction. Conséquence voulue : toute modification
// ici casse le golden tant que les nouvelles valeurs n'ont pas été
// re-backtestées et re-figées — aucune config non validée ne peut partir en
// live silencieusement. L'externalisation complète (fichier JSON validé au
// démarrage) reste l'item 9.1.
//
// DÉCISION Sprint 7 (item 7.4, D29) : après le ré-export de QQQ.csv en série
// total-return RÉELLE (dividendes réinvestis), la config prod historique
// (EMA 13/21, RSI 65/80, SL 7 %, TP 15 %, minHold 2) — jamais validée hors
// d'un dataset sans dividende — s'est révélée INFÉRIEURE à la config par
// défaut (retour +4,45 % vs +4,85 %, Sharpe 0,30 vs 0,39, et 23 trades contre
// 6, donc plus de coûts). Le test « côte à côte » avait été écrit précisément
// pour exiger une décision dans ce cas. Décision retenue : aligner la prod sur
// les paramètres par DÉFAUT, désormais la meilleure config mesurée honnêtement.
// La refonte de la stratégie (capter la tendance, battre le B&H) reste l'objet
// du Sprint 8 ; ici on ne fait que ne plus trader des paramètres dominés.
inline SwingConfig prodSwingConfig() {
    // La config prod EST la config par défaut validée (symbol "QQQ" par défaut).
    return SwingConfig{};
}

} // namespace trading
