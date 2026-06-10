# ROADMAP — SwingBot C++

> **Source de vérité du workflow.** Ce fichier est lu par `prompt-executer-sprint.md`
> (exécution du sprint courant) et mis à jour via `prompt-mise-a-jour-roadmap.md`
> (clôture de sprint, re-priorisation, rétrospective). Ne pas le modifier à la main
> en dehors de ce cycle, sauf pour ajouter une découverte.

## Tableau de bord

| Dimension    | Note /100 | Baseline (audit 2026-06-10) |
|--------------|-----------|------------------------------|
| Architecture | 82        | 68                           |
| Qualité      | 88        | 60                           |
| FinTech      | 74        | 38                           |
| Production   | 70        | 35                           |

- **Dernière mise à jour** : 2026-06-10 (clôture Sprint 5)
- **Sprint courant** : Sprint 6 — Stop résident broker (reliquat de l'item 19)
- **État des tests** : 370/370 verts (323 unitaires + 47 intégration), recalé sur
  `ctest -N`. CI GitHub Actions (item 22) vérifie ce décompte à chaque push.
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

## 🟡 SPRINT 3 — Architecture ✅ (clos le 2026-06-10)

- [x] **17.** Golden backtest → `f527318`
  `BacktesterIntegration` (2 tests) : `Backtester::run()` sur `QQQ.csv`, SwingConfig
  par défaut, 10 000 $, commission 0,1 %. Valeurs figées : retour total +9,67 %,
  7 trades (4G/3P, 0 SL / 1 TP / 1 trailing / 5 signal), max DD 2,03 %, Sharpe 0,623,
  1er achat 2020-10-29, dernière vente 2026-02-12, 1858 points d'équité. Chemin CSV
  injecté par CMake (`SWINGBOT_QQQ_CSV`).
- [x] **11.** Le backtest exécute le vrai moteur de prod → `66e84d8`
  `Backtester::run()` pilote `TradingBot::runOnce` + `PaperBroker` + `RiskManager`
  via un `ReplayDataFeed` (curseur, fenêtre bornée à emaSlow+30 pour préserver le
  seed SMA des EMA). Logique dupliquée supprimée ; D6 réglé (une seule instance de
  stratégie) ; seam `setExitObserver` ; P&L PaperBroker net de la commission de
  vente. **Golden inchangé au centime** avant/après le refactor.
- [x] **12.** TradingBot découplé de la stratégie → `bb3cc03`
  `RiskConfig` (models/Models.hpp) ; conversion implicite depuis `SwingConfig` ;
  TradingBot.hpp n'inclut plus ni strategies/ ni les implémentations concrètes
  (RiskManager, Logger) — interfaces uniquement.
- [x] **13.** Indicateurs day trading créés → `6dd4d4e`
  `indicators/DayIndicators.hpp` : ATR (approximation clôture-à-clôture, lissage
  Wilder — voir D18), VWAP (cumulatif par session, pondéré volume), VolumeOscillator
  (ratio sur moyenne des volumes précédents). 17 tests (`DayIndicatorsUnit`,
  `DayTradeStrategyUnit`) ; DayTradeStrategy compile et est incluse par un TU de test.
- [x] **14.** Hygiène → `8fda1c8` (purge git : 346 fichiers cmake-build-debug/.idea
  + QQQv1.csv) et `356ba90` (casse de main.cpp — les 4 mains passent
  `g++ -fsyntax-only` —, pragma dupliqué AlpacaBroker, D8 sig_atomic_t, D9 alias
  json scoped, D16 getLatestPrice supprimé, includes manquants de Logger.hpp/D17).

## 🟢 SPRINT 4 — Tests du moteur ✅ (clos le 2026-06-10)

> Pas de dépendance entre les items 15 et 16 ; le golden (item 17) a servi de filet
> global pendant tout le sprint. Une grande partie des matrices était déjà couverte
> par le lot de fondations `15eb711` (D20) et les Sprints 1-2 ; ce sprint a comblé
> les cellules résiduelles et verrouillé les priorités de sortie.

- [x] **15.** `tests/unit/test_trading_bot_unit.cpp` → `ac8196f`
  Matrice runOnce × {achat, vente, rejet, feed vide, marché fermé, désync, restart,
  panne réseau} complète. Cellules déjà couvertes (Sprints 1-2 + `15eb711`) : marché
  fermé (`MarketClosedDoesNothing`), feed Ok vide (`EmptyFeedDoesNothing`), CANCELLED
  (`CancelledBuyDoesNotEnterPosition`), échec de persistance
  (`SaveStateFailureDoesNotBlockTrading`). **Cellule ajoutée ce sprint** : adoption
  avec avgPrice=0 → repli sur le prix courant (`AdoptedPositionWithZeroAvgPriceUsesCurrentPrice`,
  TradingBot.hpp:249) — sinon buyPrice=0 désactiverait les stops. 28 tests TradingBotUnit.
- [x] **16.** `tests/unit/test_risk_manager_unit.cpp` → `1a38eca` + indicateurs déjà
  livrés par `15eb711`.
  **Volet RiskManager (ajouté ce sprint)** : priorités de sortie de
  `checkExitConditions` (RiskManager.hpp:62) verrouillées — SL > trailing
  (`StopLossTakesPriorityOverTrailing`) et TP > trailing
  (`TakeProfitTakesPriorityOverTrailing`) quand les deux conditions sont vraies ;
  minHoldDays ne gate que le trailing (`StopLossFiresRegardlessOfMinHoldDays`) ;
  garde peakPrice>0 (`TrailingSkippedWhenPeakPriceZero`) ; borne buyPrice<0
  (`NoExitWhenBuyPriceNegative`). 20 tests RiskManagerUnit.
  **Volet indicateurs (déjà couvert par `15eb711`)** : `test_indicators_unit.cpp`
  (24 tests) — EMA seed SMA + série trop courte → vide, RSI ∈ [0,100] / plat 0/0→50 /
  saturations, CrossoverDetector + warmup. Indicateurs testés directement, fichier
  déjà dans `unit_tests`. Rien à réécrire ; vérifié avant d'agir (consigne du sprint).

## 🔵 SPRINT 5 — Durcissement production ✅ (clos le 2026-06-10)

> Sprint orienté FinTech/Production : protections runtime réelles (et non plus
> seulement testées). Ordre suivi : 22 (CI, sécurise tout) → 18 → 20 → D15 → 21.
> Le stop résident broker de l'item 19 a été **reporté au Sprint 6** (décision
> utilisateur) : c'est un chantier money-path + migration du schéma d'état trop
> gros pour la fin du sprint — D15 (sa partie idempotence Alpaca) est livrée.

- [x] **18.** Kill-switch dans `IRiskManager` → `c7c1bb1`
  `KillSwitchConfig` (Models.hpp, membre de RiskConfig) + `checkKillSwitch` pur
  (Interfaces.hpp/RiskManager.hpp) : drawdown journalier max, pertes consécutives
  max, plafond d'ordres/jour (seuils retenus **8 % / 6 / 20**, décision utilisateur).
  Câblé dans `runOnce` : compteurs portés par TradingBot (equity de début de jour,
  ordres/jour, pertes consécutives), remis à zéro au changement de date de barre ;
  gate UNIQUEMENT la branche d'entrée. +11 tests (7 logique pure, 4 câblage dont
  « entrée bloquée » et « sortie NON bloquée »). Compteurs en mémoire → D21.
- [ ] **19.** Stops côté broker (ordre stop résident) — **reporté au Sprint 6**.
  - [x] **D15** → `90b1361` : `client_order_id` idempotent sur les ordres Alpaca
    (stable par symbole/side/heure UTC, même schéma que le cOID IBKR de l'item 4) —
    le retry du HttpClient ne peut plus re-poster un ordre. Test
    `OrdersCarryIdempotentClientOrderId`.
  - [ ] Stop résident broker (doubler le stop logiciel, décision utilisateur) :
    voir Sprint 6 (interface + 4 brokers + persistance de l'order-id + golden).
- [x] **20.** Calendrier de marché DST-aware → `5f05372`
  `isUsMarketHours` codait UTC-5 en dur (faux 8 mois/an en EDT) et ouvrait à 9h00
  (condition minute morte). Extrait en `isUsMarketOpenAtUtc(time_t)` pur/statique
  (DST US Eastern calculée : EDT du 2e dim. de mars au 1er dim. de novembre). +4
  tests sur instants UTC figés été/hiver/week-end qui échouaient sur l'ancien calcul.
- [x] **21.** Persistance des trades en prod + journalisation unifiée → `97b75af`
  `TradingBot::setTradeObserver` émet un `TradeEvent` (ENTRY/EXIT, prix/qty de fill,
  P&L, raison) sans coupler le moteur à DbLogger ; `main_ibkr` y branche
  `record_trade`/`close_trade` + `botState.positions[]`. `DbLogSink`
  (core/db_log_sink.h) route `trading::ILogger` → table `logs` (composé avec
  ConsoleLogger) ; `DbLogger::logs_json` ajouté. +4 tests.
- [x] **22.** Pipeline CI GitHub Actions → `313c5a9`
  `.github/workflows/ci.yml` : runner ubuntu-24.04, paquets système (fallback D11,
  `apt-get update` obligatoire), configure + build + `ctest` sur push et
  pull_request. Runs #1/#2/#3 verts sur le runner. Le garde-fou anti-D20 est en place.

## ⚪ SPRINT 6 — Stop résident broker (sprint courant)

> Reliquat de l'item 19 (Sprint 5), isolé car c'est le chantier le plus sensible
> du backlog : il touche le chemin de l'argent ET le schéma d'état persisté.
> Décision utilisateur déjà prise : le stop résident **double** le stop logiciel.

- [ ] **19 (suite).** Stop-loss résident chez le broker, en complément du stop
  logiciel (couvre crash/perte réseau du bot ; réduit le risque double-vente D14).
  Périmètre vérifié :
  - `IBroker` (Interfaces.hpp:43) : ajouter `submitStopOrder(symbol, qty, stopPrice)`
    et `cancelOrder(orderId)`.
  - `BotState` (Models.hpp:83) : ajouter `stopOrderId` **persisté** (sinon, après
    redémarrage, impossible d'annuler le stop résident → double-vente, le risque
    qu'on ferme). Implique une migration du schéma `SqliteStateStore`
    (core/state_store.h) + sérialisation.
  - `TradingBot::runOnce` : poser le stop résident après un BUY fill (à
    buyPrice×(1−stopLossPct)) ; l'annuler avant toute vente logicielle ; sur
    disparition de la position (stop résident exécuté), réconcilier et oublier l'id.
  - Implémentations : `IBKRBroker`/`AlpacaBroker` (ordres STP/`type:"stop"` + DELETE),
    `PaperBroker` (enregistre SANS auto-fill → **golden inchangé**), `MockBroker`.
  - **Acceptation** : tests rouges (stop résident posé après achat ; annulé quand le
    bot vend ; pas de double ordre) ; golden au centime ; les 4 mains compilent.

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
| D16 | ✅ | (Sprint 2) `IDataFeed::getLatestPrice` n'a aucun consommateur (aucun appel hors implémentations) — converti à `Result` par cohérence, mais c'est une méthode d'interface morte : la supprimer ou la consommer | Corrigé au Sprint 3 (`356ba90` — supprimée, 5 implémentations retirées) |
| D17 | ✅ | (Sprint 3) `Logger.hpp` utilisait `std::shared_ptr`/`std::vector` sans inclure `<memory>`/`<vector>` — ne compilait que par inclusion transitive (détecté en compilant le header isolément pour le golden) | Corrigé au Sprint 3 (`356ba90`) |
| D18 | 🟡 | (Sprint 3) L'ATR de l'item 13 est une **approximation clôture-à-clôture** : `IIndicator<double>` ne reçoit que la série des clôtures, pas les high/low — le vrai true range est inaccessible via cette interface. Décision produit : enrichir l'interface (compute sur `vector<Bar>`) ou assumer l'approximation (documentée dans DayIndicators.hpp) | Backlog (décision produit, requis avant tout usage réel de DayTradeStrategy) |
| D19 | 🟢 | (Sprint 3) `TradingBot::runOnce` code en dur `getBars(symbol, 60)` (TradingBot.hpp:62) alors que le backtest sert une fenêtre de emaSlow+30=51 barres via ReplayDataFeed — la taille de fenêtre influence le seed SMA des EMA, donc les signaux. Bénin tant que les feeds prod renvoient ≥51 barres, mais un `lookback` configurable (RiskConfig ?) unifierait prod et backtest | Sprint 5 (à câbler avec l'item 20, calendrier/données) |
| D21 | 🟡 | (Sprint 5) Les compteurs du kill-switch (equity de début de jour, ordres/jour, pertes consécutives — TradingBot.hpp) sont **en mémoire** : un redémarrage en cours de journée les remet à zéro, affaiblissant la coupure (drawdown journalier réévalué depuis l'equity du redémarrage, série de pertes oubliée). Acceptable pour une boucle de 60 min, mais à persister (via `IStateStore`, à côté de l'état de position) pour une protection robuste | Sprint 6 (à câbler avec la migration de schéma de l'item 19) |
| D20 | 🟢 | (Sprint 4) Dérive ROADMAP ↔ dépôt : un lot « couverture des fondations » (commit `15eb711`, +~2528 lignes : brokers IBKR/Alpaca, PaperBroker, CsvDataFeed, métriques backtest, Logger, indicateurs) a été mergé hors du cycle `prompt-executer-sprint`. Le décompte de tests du tableau de bord (198) ne le reflétait pas (réel : 344 avant ce sprint). Aucune perte — la couverture est légitime et verte — mais le tableau de bord a menti pendant un sprint. **Garde-fou ajouté** : `prompt-executer-sprint.md` étape 2 exige désormais de recaler « État des tests » sur la sortie réelle de `ctest -N` et de signaler toute dérive ; `prompt-mise-a-jour-roadmap.md` rappelle d'absorber au changelog tout commit mergé hors cycle. La CI (item 22) reste le vrai remède de fond | Corrigé (workflow amendé ce sprint) |

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

### Sprint 3 — Architecture (2026-06-10)

**Commits** (ordre chronologique) :
- `f527318` test(backtest) : golden de non-régression sur QQQ.csv (item 17)
- `66e84d8` refactor(backtest) : le backtest exécute le vrai moteur de prod (item 11)
- `bb3cc03` refactor(bot) : découple TradingBot de la stratégie via RiskConfig (item 12)
- `6dd4d4e` feat(indicators) : ATR, VWAP, VolumeOscillator (item 13)
- `8fda1c8` chore : purge du cache git — cmake-build-debug/, .idea/, QQQv1.csv (item 14)
- `356ba90` fix : hygiène du code — casse, pragma, json, sig_atomic_t, interface morte (item 14)

**Tests** : 179 → 198 (19 ajoutés). Nouvelles suites : `BacktesterIntegration` (2,
golden), `DayIndicatorsUnit` (13), `DayTradeStrategyUnit` (4).

**Valeurs golden figées** (QQQ.csv, SwingConfig défaut, 10 000 $, comm. 0,1 %) :
retour total **+9,6706 %**, capital final **10 967,06 $**, buy & hold +238,55 %,
max DD **2,0262 %**, Sharpe **0,6229**, **7 trades** (4 gagnants / 3 perdants ;
0 stop-loss, 1 take-profit, 1 trailing, 5 signal), 1858 points d'équité.
Inchangées après le refactor de l'item 11 (c'était le critère).

**Nouveaux fichiers** : `include/indicators/DayIndicators.hpp`,
`tests/integration/test_backtester_integration.cpp`, 2 fichiers de tests unitaires.
**Interfaces modifiées** : `IDataFeed::getLatestPrice` supprimée (D16) ;
`RiskConfig` ajouté (Models.hpp) ; `TradingBot::setConfig(RiskConfig)` (conversion
implicite depuis SwingConfig → composition roots inchangés) ;
`TradingBot::setExitObserver` (seam backtest). Dépôt : 346 artefacts CLion et
QQQv1.csv purgés du cache git ; les 4 mains passent `g++ -fsyntax-only` sur Linux.

### Sprint 4 — Tests du moteur (2026-06-10)

**Contexte** : à l'ouverture du sprint, la baseline réelle était **344/344 verte**
(et non 198 — D20) : le lot de fondations `15eb711`, mergé hors cycle, avait déjà
livré `test_indicators_unit.cpp` (le livrable indicateurs de l'item 16) et étoffé
`test_trading_bot_unit.cpp`. Conformément à la consigne du sprint (« compter les cas
déjà couverts avant d'écrire »), seules les cellules résiduelles ont été ajoutées.

**Commits** (ordre chronologique) :
- `ac8196f` test(bot) : adoption d'une position broker sans prix moyen (item 15)
- `1a38eca` test(risk) : priorités de sortie + bornes de checkExitConditions (item 16)

**Tests** : 344 → **350** (6 ajoutés ce sprint).
- TradingBotUnit 27 → 28 : `AdoptedPositionWithZeroAvgPriceUsesCurrentPrice` (repli
  avgPrice=0 → prix courant, sinon stops désactivés).
- RiskManagerUnit 15 → 20 : `NoExitWhenBuyPriceNegative`,
  `StopLossTakesPriorityOverTrailing`, `TakeProfitTakesPriorityOverTrailing`,
  `StopLossFiresRegardlessOfMinHoldDays`, `TrailingSkippedWhenPeakPriceZero`.

**Aucun nouveau fichier, aucune interface modifiée** : sprint 100 % additif sur les
tests. Golden backtest **inchangé** (toujours +9,6706 %, 7 trades — il fait partie
des 350). Build propre, 0 warning. Workflow amendé (D20) : `prompt-executer-sprint.md`
et `prompt-mise-a-jour-roadmap.md` dans le commit de clôture.

### Sprint 5 — Durcissement production (2026-06-10)

**Commits** (ordre chronologique) :
- `313c5a9` ci : pipeline GitHub Actions — build Linux + ctest à chaque push (item 22)
- `c7c1bb1` feat(risk) : kill-switch — drawdown/pertes/ordres (item 18)
- `5f05372` fix(ibkr) : calendrier marché US DST-aware + ouverture à 9h30 (item 20)
- `90b1361` fix(alpaca) : client_order_id idempotent sur les ordres (D15)
- `97b75af` feat(persistance) : trades en prod + journalisation unifiée (item 21)

**Tests** : 350 → **370** (20 ajoutés, tous unitaires). RiskManagerUnit +7
(kill-switch), TradingBotUnit +6 (4 câblage kill-switch, 2 observateur de trades),
IbkrDataFeedUnit +4 (calendrier DST), AlpacaBrokerUnit +1 (cOID), DbLoggerUnit +2
(logs_json, DbLogSink). Golden backtest **inchangé** (kill-switch jamais déclenché
sur barres journalières ; aucun observateur posé par le backtest).

**Nouveaux fichiers** : `.github/workflows/ci.yml`, `include/core/db_log_sink.h`.
**Interfaces modifiées** : `IRiskManager::checkKillSwitch` (+ `KillSwitchConfig`
dans Models.hpp, membre de RiskConfig) ; `TradingBot::setTradeObserver` (+ struct
`TradeEvent`) ; `IBKRDataFeed::isUsMarketOpenAtUtc`/`isUsEasternDst` publics ;
`DbLogger::logs_json`. Les composition roots non compilés (main_alpaca, main_v2)
restent compatibles (paramètres/méthodes additifs) ; les 4 mains passent
`g++ -fsyntax-only`. **Reporté** : le stop résident broker de l'item 19 (Sprint 6).

**DoD** : rebuild propre 0 warning ; 370/370 vert ; chaque correctif avec test
préalable (rouge à la compilation pour les nouvelles API, distinguant pour le
calendrier) ; golden inchangé ; CI verte sur le runner.

## Rétrospectives

### Sprint 5 — Durcissement production (2026-06-10)

**1. Découpage** : 5 livrables (22, 18, 20, D15, 21) + 1 reliquat assumé (stop
résident, item 19). L'ordre 22→18→20→D15→21 était le bon : la CI en tête a
sécurisé tous les commits suivants (3 runs verts), et l'item 21 après l'item 18
a profité du `TradeEvent` pour journaliser des trades produits par un moteur déjà
protégé. Bonne décision de **scinder l'item 19** : la partie idempotence (D15)
était petite et à fort enjeu, le stop résident est un chantier money-path +
migration de schéma qui méritait son propre sprint plutôt qu'une fin de course
bâclée — conforme à « découper plutôt que bâcler ». La décision a été remontée à
l'utilisateur (option « item 21 puis clôturer, 19 reporté »).

**2. Suffisance des prompts** : aucune improvisation d'infrastructure cette fois —
la liste apt et le recalage `ctest -N` (ajoutés aux rétros précédentes) ont tenu.
Le « test rouge » strict ne s'appliquait pas au kill-switch (on verrouille une
protection neuve, pas un bug existant) : red à la compilation (API nouvelle) +
assertions « entrée bloquée / sortie NON bloquée ». Pour le calendrier (20), les
tests sont de vrais distinguants : ils échouaient sur l'ancien UTC-5/9h00. Le
workflow n'a pas eu besoin d'être amendé.

**3. À détecter plus tôt** : (a) D21 (compteurs kill-switch en mémoire) aurait dû
être anticipé à la conception — toute protection à fenêtre journalière qui doit
survivre à un crash a besoin de persistance ; affecté au Sprint 6 où la migration
de schéma de l'item 19 ouvrira de toute façon `IStateStore`. (b) Le stop résident
(19) confirme la leçon FinTech récurrente : un ordre côté broker touche le chemin
de l'argent ET l'état persisté — à isoler dès qu'identifié, ce qui a été fait.
(c) La CI étant désormais en place, le prochain risque non couvert est l'absence
de test du composition root (`main_ibkr`) : le câblage de l'item 21 n'est vérifié
que par compilation — un test d'intégration de bout en bout (feed mock → trades en
base) serait le prochain garde-fou rentable.

**4. Notes** (précédent 81/84/66/58) :
- **Architecture 82** (+1) : le `TradeEvent`/observateur découple proprement la
  persistance et le dashboard du moteur (DIP préservé) ; le kill-switch reste pur
  dans RiskManager. Petit gain : l'essentiel du sprint est fonctionnel, pas structurel.
- **Qualité 88** (+4) : **la CI tourne** — les tests ne sont plus déclaratifs, ils
  s'exécutent à chaque push (fin de la dette d'infrastructure n°1, ouverte au
  Sprint 1) ; +20 tests ; le décompte est auto-vérifié (D20 verrouillé). Plafonné
  par l'absence de test du composition root.
- **FinTech 74** (+8) : trois protections réelles — kill-switch runtime (18),
  idempotence des ordres Alpaca (D15), calendrier de marché correct 12 mois/an (20).
  Le bot cesse d'aggraver une mauvaise journée et ne trade plus à la mauvaise heure.
  Pour dépasser 80 : stop résident broker (19, Sprint 6) et persistance des
  compteurs de risque (D21).
- **Production 70** (+12) : CI (22), trades enfin persistés et dashboard alimenté
  (21), journalisation unifiée vers SQLite. Le bot est observable et reproductible.
  Manquent : test E2E du main, persistance des compteurs kill-switch (D21).

### Sprint 4 — Tests du moteur (2026-06-10)

**1. Découpage** : deux items sans dépendance, ordre 15→16 sans surprise. Le sprint
s'est révélé bien plus petit que prévu côté livraison — l'essentiel des matrices
était déjà couvert (Sprints 1-2 + `15eb711`). C'est le bon résultat : la valeur du
sprint n'était pas d'écrire 50 tests mais de PROUVER que chaque cellule de la matrice
runOnce et chaque priorité de sortie ont un test nommé, puis de combler les trous
(adoption avgPrice=0, priorités SL/TP > trailing). Les deux trous comblés étaient des
angles morts réels : avgPrice=0 désactivait les stops, et aucun test ne verrouillait
l'ordre des `if` de `checkExitConditions` (un réordonnancement serait passé inaperçu).

**2. Suffisance des prompts** : une vraie dérive (D20). La baseline annoncée par le
tableau de bord (198) était fausse — le dépôt était à 344. Cause : un PR de couverture
mergé hors du cycle `prompt-executer-sprint`, jamais absorbé au changelog. Le workflow
ne s'auto-corrigeait pas sur ce point. **Corrigé dans ce commit** : l'étape 2 de
`prompt-executer-sprint.md` impose maintenant de recaler « État des tests » sur
`ctest -N` et de signaler toute dérive ; `prompt-mise-a-jour-roadmap.md` (étape 4)
rappelle d'absorber au changelog tout commit hors cycle. Le reste du workflow
(baseline verte → trou → test → commit atomique → DoD) a tenu sans improvisation.

**3. À détecter plus tôt** : (a) la dérive D20 aurait été impossible avec la CI
(item 22) — un workflow qui publie le décompte de `ctest` à chaque merge rend le
tableau de bord auto-vérifiable ; c'est désormais l'item d'infrastructure le plus
rentable et il OUVRE le Sprint 5. (b) Le « test rouge » strict n'était pas applicable
ici (on verrouille du comportement déjà correct, conforme à l'esprit « idéalement »
du prompt) : les tests de priorité valent comme garde-fous de non-régression
(EXPECT que « trailing » n'apparaît PAS quand SL/TP doivent l'emporter), pas comme
reproduction de bug.

**4. Notes** (précédent 81/80/66/58) :
- **Architecture 81** (=) : sprint purement additif sur les tests, aucune interface
  touchée — l'architecture n'a ni progressé ni régressé. Les écarts restants (D18
  interface indicateurs, D19 lookback, item 21 unification logging) sont au Sprint 5.
- **Qualité 84** (+4) : la matrice runOnce et les priorités de sortie sont désormais
  intégralement verrouillées par des tests nommés ; le décompte de tests est recalé
  sur la réalité (350, fin de la dérive D20). Plafonné par l'absence de CI (22) : tant
  qu'aucun garde-fou automatique ne tourne à chaque push, la qualité reste déclarative.
- **FinTech 66** (=) : aucune nouvelle protection runtime — le sprint a sécurisé la
  CONFIANCE dans le moteur existant (stops, priorités, sizing), pas ajouté de capacité.
  Le saut FinTech viendra du kill-switch (18) et des stops broker (19) au Sprint 5.
- **Production 58** (=) : rien de déployé n'a changé. La persistance des trades (21)
  et la CI (22) restent les deux dettes de production les plus visibles.

### Sprint 3 — Architecture (2026-06-10)

**1. Découpage** : l'ordre imposé 17→11 était la bonne décision du Sprint 2 — le
golden a servi de harnais pendant le refactor le plus risqué du projet (réécriture
complète de `Backtester::run()`) et a permis de PROUVER l'équivalence (mêmes 7
trades au centime près). L'analyse de parité préalable (fenêtre EMA de 51 barres,
commission dans le P&L, re-entrée même barre) a évité deux pièges qui auraient
cassé le golden silencieusement s'il n'avait pas existé. Items 12/13/14 sans
surprise ; l'item 13 ne s'est pas bloqué car la décision utilisateur avait été
demandée à la clôture du Sprint 2 — anticiper les décisions d'un sprint sur
l'autre fonctionne.

**2. Suffisance des prompts** : une improvisation d'infrastructure — l'index apt
du conteneur était périmé (404 sur libcurl4-openssl-dev), il a fallu
`apt-get update` avant l'installation. Corrigé dans ce commit :
`prompt-executer-sprint.md` préfixe la liste apt par `apt-get update`. Le reste du
workflow s'est déroulé sans ambiguïté ; le « test rouge » de l'item 13 était une
erreur de compilation (header inexistant), conforme à l'esprit du prompt.

**3. À détecter plus tôt** : (a) D17 (includes manquants de Logger.hpp) ne s'est
révélé qu'en compilant un header isolément — un check « chaque header compile
seul » (TU par header ou include-what-you-use) dans la CI (item 22) l'attraperait
systématiquement ; en attendant, les nouveaux TU de test (item 13) jouent ce rôle
pour DayTradeStrategy. (b) D18 (ATR dégradé faute de high/low dans l'interface)
aurait dû être identifié À LA CONCEPTION de l'interface IIndicator — consigné
comme décision produit AVANT tout usage réel de DayTradeStrategy. (c) La CI
(item 22) reste le garde-fou le moins cher non installé — c'est maintenant le
SEUL item d'infrastructure restant.

**4. Notes** (précédent 74/76/63/55) :
- **Architecture 81** (+7) : les deux gros écarts du backlog sont fermés — le
  backtest exécute le moteur réel (item 11) et TradingBot ne dépend plus que des
  interfaces (item 12) ; une interface morte supprimée (D16). Reste : D18
  (interface indicateurs), lookback codé en dur (D19), unification logging (21).
- **Qualité 80** (+4) : golden de non-régression en place (le filet le plus
  rentable du projet), +19 tests, DayTradeStrategy ne peut plus casser en silence,
  4/4 mains compilables. Manquent : matrices item 15/16, CI.
- **FinTech 66** (+3) : le rapport de backtest reflète désormais EXACTEMENT le
  comportement du moteur qui tradera (plus de divergence sizing/sorties) — la
  confiance dans les chiffres du backtest est une exigence FinTech de base.
  Aucune nouvelle protection runtime (kill-switch 18, stops broker 19 : Sprint 5).
- **Production 58** (+3) : dépôt assaini (346 artefacts purgés), handlers de
  signaux conformes (D8), plus de pollution d'alias global (D9). Manquent
  toujours : CI (22), persistance des trades en prod (21), calendrier (20).

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
