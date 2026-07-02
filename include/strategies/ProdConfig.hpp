#pragma once
#include "strategies/SwingStrategy.hpp"
#include "strategies/ConfigLoader.hpp"

// Chemin du JSON de prod, injecté par CMake (target_compile_definitions).
// Compile-time exprès : une cible qui consomme prodSwingConfig() sans
// déclarer quel fichier elle charge est une erreur de build, pas de runtime.
#ifndef SWINGBOT_PROD_CONFIG_JSON
#error "SWINGBOT_PROD_CONFIG_JSON doit être défini par la cible CMake (config/prod.json)"
#endif

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
//
// ITEM 9.1 (clos) : la config prod est désormais EXTERNALISÉE — chargée et
// validée depuis config/prod.json (ConfigLoader.hpp, échec bruyant). Le golden
// « config prod » charge LE MÊME fichier que main_ibkr : aucune config non
// validée ne peut partir en live, et aucune divergence fichier ↔ golden n'est
// possible. Le JSON actuel reproduit la config par défaut validée au Sprint 7.
inline SwingConfig prodSwingConfig() {
    return loadSwingConfigJson(SWINGBOT_PROD_CONFIG_JSON);
}

} // namespace trading
