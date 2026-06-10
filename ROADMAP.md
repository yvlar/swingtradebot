# ROADMAP — SwingBot C++

> **Source de vérité du workflow.** Ce fichier est lu par `prompt-executer-sprint.md`
> (exécution du sprint courant) et mis à jour via `prompt-mise-a-jour-roadmap.md`
> (clôture de sprint, re-priorisation, rétrospective). Ne pas le modifier à la main
> en dehors de ce cycle, sauf pour ajouter une découverte.

## Tableau de bord

| Dimension    | Note /100 | Baseline (audit 2026-06-10) |
|--------------|-----------|------------------------------|
| Architecture | 74        | 68                           |
| Qualité      | 76        | 60                           |
| FinTech      | 63        | 38                           |
| Production   | 55        | 35                           |

- **Dernière mise à jour** : 2026-06-10 (clôture Sprint 2)
- **Sprint courant** : Sprint 3 — Architecture (+ item 17 remonté du Sprint 4)
- **État des tests** : 179/179 verts (155 après Sprint 1 + 24 ajoutés au Sprint 2)
- **Environnement de référence** : conteneur vcpkg (`dev.ps1`) ; build aussi possible
  sur Linux avec paquets système (validé de bout en bout au Sprint 2 — voir D11 et
  la liste apt dans `prompt-executer-sprint.md`)

## Audit Phase 0 — statut des constats (2026-06-10)

Tous les constats de l'audit initial (items 1 à 21 ci-dessous) ont été **reproduits et
confirmés un par un** sur le code actuel, références fichier:ligne vérifiées. Aucun
constat invalidé. Les découvertes nouvelles sont consignées dans la section
« Découvertes ».

**Inventaire de couverture de tests (Phase 0)** :
- Avec tests (5×2 fichiers) : `::BotState` (dashboard), `DbLogger`, `Watchdog`, `WsServer`, `SwingStrategy`
- **Zéro test** : `TradingBot`, `RiskManager`, indicateurs (`EMA`/`RSI`/`CrossoverDetector`,
  couverts seulement indirectement via SwingStrategy), tous les brokers
  (`IBKRBroker`, `IBKRDataFeed`, `AlpacaBroker`, `AlpacaDataFeed`, `PaperBroker`, `CsvDataFeed`),
  `Backtester`. `Mocks.hpp` n'est inclus par aucun test (code mort — item 15 confirmé).

---

## 🔴 SPRINT 1 — Sécurité financière ✅ (clos le 2026-06-10)

Bugs disqualifiants pour l'argent réel. Chaque item : test rouge → fix → test vert → commit.

- [x] **1. Position orpheline au redémarrage** → `281000d`
  Interface `IStateStore` (Interfaces.hpp) + `SqliteStateStore` (core/state_store.h,
  table `bot_state`, UPSERT, WAL) + réconciliation à chaque cycle dans `runOnce` :
  position broker non suivie → adoption (stops actifs immédiatement) ; position absente
  → réinitialisation (couvre D3). `BotState` déplacé dans models/Models.hpp.
  Câblé dans main_ibkr.cpp (`swingbot_ibkr_state.db`).
  Tests rouges : RestartAdoptsExistingBrokerPosition, AdoptedPositionStopLossFires,
  PositionGoneAtBrokerResetsState + StateStoreUnit (round-trip, réouverture).
- [x] **2. Statut d'ordre jamais vérifié** → `4bb8ac2`
  Seul `OrderStatus::FILLED` change l'état, au prix/quantité réels d'exécution (D5
  inclus) ; PENDING → réconciliation au cycle suivant ; vente non exécutée → position
  conservée + log. Tests rouges : RejectedBuyDoesNotEnterPosition,
  PendingBuyDoesNotEnterPositionYet, BuyUsesFillPriceNotSignalPrice,
  RejectedSellKeepsPosition.
- [x] **3. Sémantique `holdDays` cassée** → `55587bf`
  Incrément seulement au changement de date de barre (`BotState::lastBarDate`,
  initialisé au jour d'entrée). Tests rouges : HoldDaysDoesNotIncrementWithinSameBarDate,
  HoldDaysIncrementsOncePerNewBarDate, SellSignalWaitsForRealTradingDays.
- [x] **4. Risque de double-ordre IBKR** → `9aeef3f`
  Boucle de confirmation (max 5 tours) consommant la réponse de `/iserver/reply`
  (formats `{id, message}` et `{messageIds}`, questions en chaîne) ; plus aucun
  re-POST de l'ordre ; `cOID` idempotent par (symbole, side, heure UTC) ; seam de test
  `request()` protected virtual. Tests rouges : ConfirmationConsumesReplyWithoutReposting,
  ChainedConfirmationsAllConsumed, OrderPayloadContainsIdempotentClientOrderId.
- [x] **5. Sizing forcé à 1** → `1c1451b`
  `positionSize` retourne 0 ; `IRiskManager::isTradeAllowed(account, pos, price, qty)`
  vérifie le coût total ; TradingBot ne soumet rien si qty ≤ 0. Tests rouges :
  PositionSizeReturnsZeroWhenCashCannotAffordOneShare,
  PositionSizeReturnsZeroWhenRiskBudgetBelowOneShare, InsufficientCashSubmitsNoOrder.
- [x] **D1. Watchdog mal calibré** → `eaf70ff` — `max_silence_sec` porté à 3900 s
  (un cycle de 60 min + marge) dans main_ibkr.cpp.
- [x] **Avancé de l'item 14 : `.gitignore`** → `52f9c95` — le fichier `gitignore` sans
  point n'ignorait rien ; renommé après que `build/` a failli être committé (D13).

## 🟠 SPRINT 2 — Fiabilité / concurrence ✅ (clos le 2026-06-10)

- [x] **6. Data races Watchdog + alertes sans timeout** → `9142571`
  `last_heartbeat_` devenu `std::atomic<time_point>` ; `build_alert_msg_` lit
  `BotState` sous `state_.mtx` ; `CURLOPT_TIMEOUT`+`CONNECTTIMEOUT`
  (`AlertConfig::alert_timeout_sec`, 10 s) sur les 3 canaux d'alerte.
  Tests : DefaultAlertTimeoutPositive + stress concurrent (et suite existante verte).
- [x] **7. `curl_global_init` unique (RAII)** → `404a08a`
  Nouveau `core/curl_global.h` (`CurlGlobal`, compteur d'instances) ; plus aucun
  init/cleanup dans les ctors/dtors des 4 classes ; instancié en tête des 3 mains.
  Nouvelle suite CurlGlobalUnit (4 tests).
- [x] **8. `HttpClient` commun (codes HTTP, retry/backoff, 429)** → `73a48d3`
  `core/HttpClient.hpp` remplace les 4 copies request()/writeCallback ; statut HTTP
  vérifié ; retry + backoff exponentiel sur transport/429/5xx ; 4xx définitif sans
  retry ; seam request() d'IBKRBroker préservé (délègue au HttpClient) ; D10 réglé
  (fetchFirstAccountId vérifie les erreurs, lastError() exposé).
  Nouvelle suite HttpClientUnit (9 tests, scriptés sans réseau).
- [x] **9. Codes retour SQLite vérifiés dans DbLogger (+ D2)** → `9008e49`
  Helpers prepare_()/step_done_() sur le modèle de state_store.h ; écritures → bool,
  lectures dégradent en `[]` ; plus d'UB sur stmt non initialisé. Tests rouges sur
  tables supprimées derrière le dos du logger.
- [x] **10. Canal d'erreur `Result<T>`** → `b0f394f`
  `models/Result.hpp` ; `getBars`/`getLatestPrice`/`getPosition` retournent
  Result (Ok vide/nullopt = certitude « rien », Err = panne) ; `runOnce` saute le
  cycle sur Err SANS réconcilier ni trader ; `HttpError` porte le statut (Alpaca
  404 → Ok(nullopt)) ; mocks avec injection de panne. 5 tests TradingBot.
- [x] **D4. File d'écriture WebSocket par session** → `7627801`
  `Session::send` poste sur l'executor ; deque → une seule async_write en vol.
  Test rouge RapidBroadcastsAllDeliveredInOrder (abortait : assertion Beast).

## 🟡 SPRINT 3 — Architecture (sprint courant)

> **Ordre imposé** : l'item 17 (remonté du Sprint 4) se fait AVANT l'item 11 —
> le refactor du backtester doit être validé par le golden. L'item 13 attend la
> **décision utilisateur** ; s'il n'y a pas de réponse au moment de l'exécution,
> le sauter et le reporter.

- [ ] **17.** (remonté du Sprint 4 — dépendance de l'item 11) Test de non-régression
  du backtest : valeurs golden figées sur `QQQ.csv` (total return, nb trades, max DD)
  pour détecter toute dérive. **Acceptation** : test d'intégration qui exécute
  `Backtester::run()` sur `QQQ.csv` et compare aux valeurs figées (tolérance
  serrée) ; valeurs consignées dans le changelog.

- [ ] **11.** Le backtest ne teste pas le code de prod : `Backtester::run()` réimplémente
  sorties + sizing inline (BackTester.hpp:60-195 ; le `RiskManager rm` ligne 78 n'est
  jamais utilisé) → faire tourner le backtest sur `TradingBot::runOnce` + `PaperBroker` +
  `RiskManager` réels, supprimer la logique dupliquée (+ D6 : stratégie recréée à chaque
  barre). **Dépendance** : item 17 d'abord (golden vert avant ET après le refactor).
- [ ] **12.** Découpler `TradingBot` de `SwingConfig` (TradingBot.hpp:3 inclut la
  stratégie concrète juste pour sa config) → extraire un `RiskConfig` injecté dans le
  bot ; les paramètres stratégie restent dans la stratégie.
  **Acceptation** : TradingBot.hpp n'inclut plus strategies/ ; tests verts inchangés.
- [ ] **13.** `DayTradeStrategy.hpp:4` inclut `indicators/DayIndicators.hpp` **qui
  n'existe pas** (ATR, VWAP, VolumeOscillator manquants — le fichier ne compile que parce
  que rien ne l'inclut). **Décision utilisateur (clôture Sprint 2) : CRÉER les
  indicateurs** — implémenter ATR, VWAP, VolumeOscillator (IIndicator<T>, tests
  dans `test_indicators_unit.cpp` ou fichier dédié) et rendre DayTradeStrategy
  compilable (inclus par au moins un TU de test pour ne plus jamais casser en
  silence).
- [ ] **14.** Hygiène : `main.cpp:1` inclut `"backtest/Backtester.hpp"` vs fichier réel
  `BackTester.hpp` (casse Linux — reconfirmé au Sprint 2 par `g++ -fsyntax-only`) ;
  `cmake-build-debug/` et `.idea/` encore commités (le `.gitignore` corrigé au Sprint 1
  n'efface pas l'historique du cache git) ; double `#pragma once` dans
  AlpacaBroker.hpp:1,13 ; `QQQv1.csv` non référencé (D12) ; D8 (`volatile bool
  g_running`, main_ibkr.cpp:34) ; D9 (`using json` global, bot_state.h:18) ;
  D16 (`getLatestPrice` sans consommateur).

## 🟢 SPRINT 4 — Tests du moteur

- [ ] **15.** Compléter `test_trading_bot_unit.cpp` (démarré aux Sprints 1-2) : matrice
  complète runOnce × {achat, vente, rejet, feed vide, marché fermé, désync, restart,
  panne réseau}.
- [ ] **16.** Compléter `test_risk_manager_unit.cpp` (démarré au Sprint 1 : sizing) :
  priorités de sortie, bornes. + `test_indicators_unit.cpp` : EMA (seed SMA),
  RSI ∈ [0,100], cas plat 0/0→50, CrossoverDetector + warmup.

## 🔵 SPRINT 5 — Durcissement production

- [ ] **18.** Kill-switch dans `IRiskManager` : drawdown journalier max, pertes
  consécutives max, plafond d'ordres/jour.
- [ ] **19.** Stops côté broker (ordre stop résident) en complément du stop logiciel
  (réduit aussi le risque de double-vente sur ordre PENDING, cf. note item 2 et D14).
  + D15 : donner un `client_order_id` idempotent aux ordres Alpaca (le retry du
  HttpClient peut re-poster un POST /v2/orders).
- [ ] **20.** Calendrier de marché : `isUsMarketHours` est en UTC-5 fixe
  (IBKRDataFeed.hpp:219), faux 8 mois/an (EDT) ; horodatages unifiés en UTC.
- [ ] **21.** Câbler la persistance des trades en prod : `record_trade`/`close_trade` ne
  sont **jamais appelés** dans `main_ibkr.cpp` (table `trades` vide, dashboard sans
  positions — `botState.positions` jamais alimenté). Unifier les deux systèmes de logging
  (`trading::ILogger` ↔ `DbLogger`).
- [ ] **22.** (ajouté à la rétro Sprint 1) Pipeline CI GitHub Actions : build Linux
  (paquets système, fallback D11) + `ctest` sur chaque push — aurait attrapé le
  CMakeLists vcpkg-only, le gitignore mort et tout test rouge avant merge.

---

## Découvertes (Phase 0, 2026-06-10)

| # | Gravité | Constat | Affectation |
|---|---------|---------|-------------|
| D1 | 🔴 | Watchdog : `max_silence_sec=300` (main_ibkr.cpp:124) pour une boucle qui dort 3600 s (main_ibkr.cpp:184) → fausse alerte « bot silencieux » à chaque cycle dès la 6e minute (spam stderr/logs ; spam email/SMS si alertes activées) | Sprint 1 (fix config trivial) |
| D2 | 🔴 | db_logger.h : `sqlite3_stmt* stmt;` non initialisé — si `sqlite3_prepare_v2` échoue, bind/step/finalize sur pointeur indéterminé = UB/crash (toutes les méthodes) | Sprint 2 (item 9) |
| D3 | 🟠 | Symétrique de l'item 1 : `inPosition=true` mais `getPosition()`→nullopt (position fermée à la main ou erreur réseau) → état bloqué « en position » pour toujours, plus aucune entrée possible | Sprint 1 (item 1, réconciliation) |
| D4 | 🟠 | ws_server.cpp:54-62 : `async_write` concurrent sans queue d'écriture (UB Beast si deux broadcasts se chevauchent) | Sprint 2 |
| D5 | 🟡 | P&L de vente calculé au prix du signal, pas au prix de fill (TradingBot.hpp:85) | Sprint 1 (item 2) |
| D6 | 🟡 | Backtester recrée `SwingStrategy::create()` à chaque barre (BackTester.hpp:112,146) | Sprint 3 (item 11) |
| D7 | 🟡 | CsvDataFeed utilise `Close` et non `Adj Close` (CsvDataFeed.hpp:119) → dividendes QQQ ignorés dans le backtest et la comparaison Buy&Hold | Backlog (décision produit) |
| D8 | 🟢 | `volatile bool g_running` dans les handlers de signaux (main_ibkr.cpp:32, main_v2, main_alpaca) — devrait être `volatile std::sig_atomic_t` | Sprint 3 (item 14) |
| D9 | 🟢 | `using json = nlohmann::json;` au scope global dans bot_state.h:18 (pollution de tous les TU qui l'incluent) | Sprint 3 (item 14) |
| D10 | 🟢 | `IBKRBroker::lastError_` écrit mais jamais exposé ; `fetchFirstAccountId` ignore le code retour curl (IBKRBroker.hpp:101) | Sprint 2 (item 8) |
| D11 | ✅ | CMakeLists exigeait `unofficial-sqlite3` (config vcpkg uniquement) → fallback `find_package(SQLite3)` système ajouté, build/tests possibles hors conteneur vcpkg | Corrigé (commit `build:`) |
| D12 | 🟢 | `QQQv1.csv` non référencé par le code (donnée morte) | Sprint 3 (item 14) |
| D13 | ✅ | (Sprint 1) Le `gitignore` sans point a fait committer `build/` par accident pendant le sprint (commit amendé) — preuve vivante de l'item 14 | Corrigé (`52f9c95`) |
| D14 | 🟡 | (Sprint 1) Risque résiduel : vente PENDING avec position encore visible au cycle suivant → re-tentative de vente possible. Mitigé par le cOID horaire (1 ordre/side/heure) ; les stops côté broker (item 19) réduiront encore ce risque | Sprint 5 (item 19, noté) |
| D15 | 🟠 | (Sprint 2) Le retry du HttpClient (item 8) peut re-poster un ordre déjà reçu par le serveur. IBKR est protégé par le cOID idempotent (Sprint 1, item 4) ; **AlpacaBroker n'envoie aucun `client_order_id`** → double-ordre possible sur retry. Sans impact sur la cible compilée (main_ibkr), mais à corriger avant tout usage Alpaca | Sprint 5 (item 19) |
| D16 | 🟢 | (Sprint 2) `IDataFeed::getLatestPrice` n'a aucun consommateur (aucun appel hors implémentations) — converti à `Result` par cohérence, mais c'est une méthode d'interface morte : la supprimer ou la consommer | Sprint 3 (item 14) |

## Changelog

### Sprint 0 — Initialisation du workflow (2026-06-10)
- Audit Phase 0 : 21/21 constats confirmés, 12 découvertes (D1-D12), inventaire de couverture.
- `build:` fallback SQLite3 système dans CMakeLists.txt (D11).
- `chore:` création de ROADMAP.md, prompt-executer-sprint.md, prompt-mise-a-jour-roadmap.md.
- Baseline tests : 110/110 verts.

### Sprint 1 — Sécurité financière (2026-06-10)

**Commits** (ordre chronologique) :
- `1c1451b` fix(risk) : sizing 0 si cash insuffisant + coût total vérifié (item 5)
- `52f9c95` fix : gitignore → .gitignore (avancé de l'item 14, suite D13)
- `4bb8ac2` fix(bot) : statut d'ordre vérifié, fill au prix réel (item 2)
- `55587bf` fix(bot) : holdDays en jours de bourse réels (item 3)
- `281000d` feat(bot) : persistance d'état + réconciliation broker (item 1)
- `eaf70ff` fix(main) : seuil watchdog aligné sur le cycle 60 min (D1)
- `9aeef3f` fix(ibkr) : confirmation sans re-POST + cOID idempotent (item 4)

**Tests** : 110 → 155 (45 ajoutés, tous rouges-puis-verts ou régression).
Nouvelles suites : `RiskManagerUnit` (15), `TradingBotUnit` (18), `StateStoreUnit` (6),
`IbkrBrokerUnit` (6). `Mocks.hpp` n'est plus du code mort (item 15 bien entamé) :
+ `MockStrategy`, + `MockStateStore`, `MockBroker` refondu (statut/prix de fill
configurables, seuls les FILLED touchent la position simulée).

**Nouveaux fichiers** : `include/core/state_store.h`, 4 fichiers de tests unitaires.
**Interfaces modifiées** : `IRiskManager::isTradeAllowed(+price,+qty)`, `IStateStore`
(nouvelle), `TradingBot` (6e paramètre optionnel `stateStore`), `trading::BotState`
(+`lastBarDate`, déplacé dans Models.hpp). Les composition roots non compilés
(main_alpaca, main_v2) restent compatibles source (paramètre par défaut).

**DoD** : rebuild propre 0 warning ; 155/155 verts ; chaque bug avec test rouge préalable ;
pas de golden backtest encore (item 17 — à figer AVANT le refactor du backtester).

### Sprint 2 — Fiabilité / concurrence (2026-06-10)

**Commits** (ordre chronologique) :
- `9142571` fix(watchdog) : heartbeat atomique, BotState sous lock, timeout curl (item 6)
- `404a08a` fix(core) : init libcurl unique par processus, garde RAII CurlGlobal (item 7)
- `73a48d3` refactor(http) : HttpClient commun, codes vérifiés, retry/backoff/429 (item 8 + D10)
- `9008e49` fix(db) : codes retour SQLite vérifiés, plus d'UB sur prepare échoué (item 9 + D2)
- `b0f394f` feat(core) : canal d'erreur Result<T>, panne ≠ donnée vide (item 10)
- `7627801` fix(ws) : file d'écriture par session WebSocket (D4)

**Tests** : 155 → 179 (24 ajoutés). Nouvelles suites : `CurlGlobalUnit` (4),
`HttpClientUnit` (9). Étendues : `WatchdogUnit` (+2), `DbLoggerUnit` (+3),
`TradingBotUnit` (+5 pannes réseau), `WsServerIntegration` (+1 stress rouge→vert,
abortait sur assertion Beast avant le fix D4).

**Nouveaux fichiers** : `include/core/curl_global.h`, `include/core/HttpClient.hpp`,
`include/models/Result.hpp`, 2 fichiers de tests unitaires.
**Interfaces modifiées** : `IDataFeed::getBars` → `Result<vector<Bar>>`,
`IDataFeed::getLatestPrice` → `Result<optional<double>>`, `IBroker::getPosition` →
`Result<optional<Position>>` ; 5 méthodes d'écriture de `DbLogger` retournent `bool`.
Les composition roots non compilés (main_alpaca, main_v2) restent compatibles
(vérifiés par `g++ -fsyntax-only`) ; main.cpp ne compile toujours pas (item 14, casse).

**DoD** : rebuild propre 0 warning ; 179/179 verts ; sprint entièrement réalisé sur
Linux/paquets système (fallback D11 validé de bout en bout) ; pas de golden encore
(item 17, remonté en tête du Sprint 3).

## Rétrospectives

### Sprint 2 — Fiabilité / concurrence (2026-06-10)

**1. Découpage** : bon calibre (6 items, ordre 6→7→8→9→10→D4 sans dépendance ratée —
l'item 8 devait précéder le 10, et c'est ce qui s'est passé : `HttpError` du
HttpClient a servi au mapping 404→Ok(nullopt) d'Alpaca). L'item 10 était le plus
gros (3 interfaces, 6 implémentations, mocks, bot) mais tenait dans le sprint sans
découpage. Remontée de l'item 17 en tête du Sprint 3 actée : la note « à faire avant
l'item 11 » était enterrée dans le Sprint 4 — un ordre implicite entre sprints est
une dépendance ratée en puissance.

**2. Suffisance des prompts** : une improvisation d'infrastructure — l'environnement
Linux nu a demandé d'identifier la liste apt (boost-system, nlohmann, curl-dev,
gtest) avant la baseline. Corrigé dans ce commit : `prompt-executer-sprint.md` liste
désormais les paquets. À noter aussi : pour deux items (6, 7), le « test rouge »
était impossible au sens strict (data race = UB non déterministe) — le prompt dit
bien « idéalement », l'esprit a été respecté via tests de compilation rouges
(nouvelle API) + stress tests ; pour D4 en revanche, le test rouge a réellement
aborté (assertion Beast), preuve nette.

**3. À détecter plus tôt** : (a) D15 (retry HTTP vs idempotence des ordres Alpaca)
aurait dû être identifié À LA CONCEPTION de l'item 8 — le retry d'un POST d'ordre
est un risque financier, pas un détail technique ; garde-fou ajouté : l'en-tête de
HttpClient.hpp documente le contrat (« l'idempotence est garantie en amont ») et
D15 est affecté au Sprint 5. (b) Le golden backtest (17) manque toujours — tout le
Sprint 2 a modifié des chemins de code sans filet sur le comportement de trading
global ; c'est la première tâche du Sprint 3. (c) La CI (item 22) reste le garde-fou
le moins cher non installé.

**4. Notes** (précédent 71/70/58/42) :
- **Architecture 74** (+3) : `Result<T>` assainit le contrat des interfaces,
  `HttpClient`/`CurlGlobal` suppriment 4 duplications et une responsabilité mal
  placée. Restent les gros écarts du Sprint 3 (backtest ≠ prod, couplage SwingConfig).
- **Qualité 76** (+6) : +24 tests ciblés (dont injection de pannes réseau et stress
  WebSocket), 3 familles d'UB éliminées (data race watchdog, stmt SQLite, écritures
  Beast concurrentes). Manquent : indicateurs purs, golden.
- **FinTech 63** (+5) : une panne réseau ne désactive plus les stops d'une position
  réelle (item 10) et ne gèle plus le watchdog (item 6) ; retry/429 proprement gérés.
  Pour dépasser 70 : kill-switch (18), stops broker (19), calendrier (20).
- **Production 55** (+13) : plus aucun UB connu dans la couche opérationnelle,
  libcurl initialisé une fois, SQLite robuste aux échecs, dashboard stable sous
  rafale. Manquent : CI (22), persistance des trades en prod (21), calendrier (20).

### Sprint 1 — Sécurité financière (2026-06-10)

**1. Découpage** : bon calibre (5 items + 2 hors-plan absorbés sans déborder). L'ordre
5→2→3→1→4 a bien géré la dépendance « PENDING s'appuie sur la réconciliation »
(item 2 avant item 1, sémantique documentée puis implémentée). Erreur de découpage
identifiée : la partie `.gitignore` de l'item 14 aurait dû être au Sprint 1 dès le
départ — son absence a failli polluer l'historique (D13).

**2. Suffisance des prompts** : une improvisation = `git add -A` sans inspection a
committé `build/` (amendé). Correction apportée au workflow dans ce même commit :
`prompt-executer-sprint.md` exige désormais `git status --short` avant chaque commit
et interdit `git add -A` sans inspection. Le reste (rouge→fix→vert→commit atomique)
s'est déroulé sans ambiguïté.

**3. À détecter plus tôt** : (a) le CMakeLists vcpkg-only et le gitignore mort
auraient été attrapés par une CI Linux minimaliste → item 22 ajouté (Sprint 5) ;
(b) la réconciliation réinitialise l'état sur `getPosition()`→nullopt même si le
nullopt vient d'une panne réseau (auto-réparant au cycle suivant, mais bruyant) →
l'item 10 (canal d'erreur) du Sprint 2 est explicitement motivé par ce cas.

**4. Notes** (baseline 68/60/38/35) :
- **Architecture 71** (+3) : nouvelle interface `IStateStore` propre, seam de test
  IBKR, `BotState` au bon endroit (Models). Les gros écarts (backtest ≠ prod,
  couplage SwingConfig) restent — Sprint 3.
- **Qualité 70** (+10) : +45 tests sur le cœur (moteur, risk, broker, store), mocks
  ressuscités, TDD systématique. Manquent : indicateurs purs, golden backtest.
- **FinTech 58** (+20) : les 5 bugs disqualifiants pour l'argent réel sont corrigés
  et testés. Pour dépasser 70 : kill-switch (18), stops broker (19), calendrier (20),
  distinction panne/donnée vide (10).
- **Production 42** (+7) : état persistant + réconciliation, watchdog calibré,
  hygiène git. Les data races du watchdog (6), curl global (7) et SQLite (9/D2)
  pèsent encore — c'est précisément le Sprint 2.
