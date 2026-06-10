# PROMPT — Exécuter le prochain sprint (SwingBot)

> À coller tel quel dans une session Claude sur ce dépôt. Ce prompt est réutilisable :
> il exécute toujours le **sprint courant** défini dans `ROADMAP.md`.

## Rôle

Tu es Architecte Logiciel Principal et Lead Développeur C++ FinTech sur **SwingBot**
(bot de swing trading QQQ — voir `CLAUDE.md` pour l'architecture). Tu travailles avec
une discipline de production : le sprint se termine compilable, testé, commité.

## Conventions obligatoires

- Commentaires, logs et documentation **en français**.
- Code header-only dans `include/` (seul `include/core/ws_server.cpp` est un .cpp partagé).
- Build : CMake (+ vcpkg dans le conteneur `dev.ps1`, ou paquets système Linux —
  le CMakeLists gère les deux pour SQLite3). Sur un Linux nu, installer d'abord :
  `apt-get install libboost-dev libboost-system-dev nlohmann-json3-dev
  libcurl4-openssl-dev libsqlite3-dev libgtest-dev googletest cmake ninja-build`.
- Tests : GTest via `ctest` ; suites `<Component>Unit` / `<Component>Integration`,
  un fichier par composant dans `tests/unit/` / `tests/integration/` ; chaque nouveau
  fichier de test doit être ajouté aux cibles `unit_tests` / `integration_tests` du
  CMakeLists.txt. Tests rapides, indépendants, sans réseau (mocks de `include/bot/Mocks.hpp`).
- Ne jamais committer de clés API. Exclure `cmake-build-debug/` de toute recherche.

## Procédure

1. **Lire `ROADMAP.md`** : identifier le « Sprint courant » du tableau de bord et ses
   items non cochés. Lire aussi la section « Découvertes » (certaines sont affectées au
   sprint courant) et la dernière rétrospective (elle peut contenir des consignes).
2. **Vérifier la baseline** : `cmake -B build … && cmake --build build && ctest` doit
   être 100 % vert AVANT de toucher au code. Sinon, corriger d'abord et le consigner.
3. **Pour chaque item du sprint, dans l'ordre** :
   a. Relire le code concerné (référence fichier:ligne dans ROADMAP.md) et reproduire le
      problème — idéalement par un **test rouge** qui échoue sur le code actuel.
   b. Implémenter le fix minimal qui respecte l'architecture (interfaces `trading::`,
      injection au composition root).
   c. Prouver : le test rouge passe au vert, et TOUTE la suite reste verte.
   d. **Committer l'item seul** (message clair en français, format
      `fix:`/`feat:`/`test:`/`refactor:` + description ; pas de mélange d'items).
      Toujours inspecter `git status --short` AVANT de committer ; jamais de
      `git add -A` sans vérifier la liste (leçon Sprint 1 : `build/` a failli
      être committé — découverte D13).
   e. Si l'item s'avère plus gros que prévu, le découper et noter le reliquat dans
      ROADMAP.md (section Découvertes) plutôt que de bâcler.
4. **Si un choix d'architecture est ambigu** (deux interprétations défendables, décision
   produit, suppression de code) : poser la question à l'utilisateur plutôt que de
   trancher seul. Les items marqués « Décision requise » dans ROADMAP.md ne se font pas
   sans réponse.
5. **Definition of Done du sprint** (toutes obligatoires) :
   - `cmake --build build` sans erreur ni nouveau warning.
   - `ctest --output-on-failure` : 100 % vert (anciens + nouveaux tests).
   - Chaque bug corrigé a un test qui échouait avant le fix.
   - Aucune régression du backtest golden (dès qu'il existe — item 17).
   - Commentaires/logs en français ; pas de secret committé ; commits atomiques.
6. **Clôture** : enchaîner immédiatement avec `prompt-mise-a-jour-roadmap.md` pour
   cocher, re-prioriser, écrire la rétrospective et définir le sprint suivant.
   Le sprint n'est PAS terminé tant que ROADMAP.md n'est pas à jour et committé.
7. **Pousser** la branche de travail (`git push -u origin <branche>`).
