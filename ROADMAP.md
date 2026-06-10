# ROADMAP — SwingBot C++

> **Source de vérité du workflow.** Ce fichier est lu par `prompt-executer-sprint.md`
> (exécution du sprint courant) et mis à jour via `prompt-mise-a-jour-roadmap.md`
> (clôture de sprint, re-priorisation, rétrospective). Ne pas le modifier à la main
> en dehors de ce cycle, sauf pour ajouter une découverte.

## Tableau de bord

| Dimension    | Note /100 | Baseline (audit 2026-06-10) |
|--------------|-----------|------------------------------|
| Architecture | 81        | 68                           |
| Qualité      | 84        | 60                           |
| FinTech      | 66        | 38                           |
| Production   | 58        | 35                           |

- **Dernière mise à jour** : 2026-06-10 (item 6.1 exécuté par anticipation — voir changelog)
- **Sprint courant** : Sprint 5 — Durcissement production (puis 6-9 : rentabilité ; 6.1 déjà fait)

> ### ⚠️ Rentabilité : NON PROUVÉE — le bot ne fait pas (encore) d'argent
> Les notes ci-dessus mesurent la **sûreté** et la **correction** du moteur, pas sa
> capacité à gagner de l'argent. Le golden de non-régression (item 17) le dit
> noir sur blanc : sur QQQ.csv (~2018→2026), la stratégie rend **+9,67 %** quand
> **Buy & Hold rend +238,55 %** — un **alpha de −229 points**. Le bot transforme un
> des plus grands marchés haussiers de l'histoire en quasi-stagnation, en restant
> en cash l'essentiel du temps (7 trades en ~5 ans). ~~De plus, **la config qui tourne
> en production (`main_ibkr.cpp:104-114`) n'est PAS celle validée par le golden**~~ —
> **réglé par l'item 6.1** (`918613b`, exécuté par anticipation) : la config prod est
> désormais une source unique (`include/config/ProdConfig.hpp`) couverte par son
> propre golden, et le verdict est **+36,50 %, Sharpe 1,82** (vs +9,67 % pour la
> config défaut) — meilleure que craint, mais toujours **−202 pts d'alpha** vs B&H.
> C'est l'objet des **Sprints 6-9** (voir le méta-audit ci-dessous). Une 5e dimension
> est ajoutée au tableau de bord :
>
> | Dimension     | Note /100 | Justification |
> |---------------|-----------|---------------|
> | **Rentabilité** | **22** (15 à l'audit) | La config réellement tradée est enfin quantifiée (+36,50 %, Sharpe 1,82, maxDD 1,97 % — 6.1) et surperforme la config défaut. Reste : alpha −202 pts vs B&H, aucune validation hors-échantillon, coûts irréalistes (slippage). |
- **État des tests** : 353/353 verts (303 unitaires + 50 intégration, après 6.1). Décompte
  recalé sur la sortie réelle de `ctest` : le « 198 » documenté à la clôture du
  Sprint 3 ignorait un lot de couverture des fondations mergé hors cycle (commit
  `15eb711`, ~144 tests : brokers, PaperBroker, CsvDataFeed, métriques backtest,
  Logger, indicateurs) — voir D20.
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

## 🔵 SPRINT 5 — Durcissement production (sprint courant)

> Sprint orienté FinTech/Production : protections runtime réelles (et non plus
> seulement testées). Dépendances : l'item 21 touche les deux systèmes de logging
> (`trading::ILogger` ↔ `DbLogger`) et gagne à passer après l'item 18 (le kill-switch
> produit des événements à journaliser). L'item 22 (CI) est indépendant et devrait
> passer en premier : il sécurise tous les autres. Items 18/19 marqués « Décision
> requise » : seuils chiffrés et politique de stop résident à arbitrer avec l'utilisateur.

- [ ] **18.** Kill-switch dans `IRiskManager` : drawdown journalier max, pertes
  consécutives max, plafond d'ordres/jour. Étendre l'interface `IRiskManager`
  (Interfaces.hpp) + `RiskManager` (RiskManager.hpp) ; câbler dans `runOnce`
  (TradingBot.hpp, avant la branche d'entrée en position). **Décision requise** :
  valeurs par défaut des trois seuils. **Acceptation** : tests rouges (seuil franchi
  → aucune entrée, position existante laissée à ses stops) ; golden inchangé.
- [ ] **19.** Stops côté broker (ordre stop résident) en complément du stop logiciel
  (réduit aussi le risque de double-vente sur ordre PENDING, cf. note item 2 et D14).
  + D15 : donner un `client_order_id` idempotent aux ordres Alpaca (AlpacaBroker.hpp —
  le retry du HttpClient peut re-poster un POST /v2/orders). **Décision requise** :
  stop résident remplaçant ou doublant le stop logiciel.
- [ ] **20.** Calendrier de marché : `isUsMarketHours` est en UTC-5 fixe
  (IBKRDataFeed.hpp:191, commentaire « EST = UTC-5 » à la ligne 200), faux 8 mois/an
  (EDT) ; horodatages unifiés en UTC. **Acceptation** : test sur une date d'été (DST)
  où l'ancien calcul ouvrait/fermait le marché à la mauvaise heure.
- [ ] **21.** Câbler la persistance des trades en prod : `record_trade`/`close_trade` ne
  sont **jamais appelés** dans `main_ibkr.cpp` (table `trades` vide, dashboard sans
  positions — `botState.positions` jamais alimenté). Unifier les deux systèmes de logging
  (`trading::ILogger` ↔ `DbLogger`).
- [ ] **22.** (ajouté à la rétro Sprint 1) Pipeline CI GitHub Actions : build Linux
  (paquets système, fallback D11) + `ctest` sur chaque push — aurait attrapé le
  CMakeLists vcpkg-only, le gitignore mort et tout test rouge avant merge.

---

# 🧭 MÉTA-AUDIT « Analyse complète » (2026-06-10)

> Audit transverse demandé hors cycle, sous deux angles distincts : **ingénieur
> logiciel** et **swing trader expérimenté**. But explicite : *faire de l'argent*.
> Les Sprints 1-5 ont rendu le moteur **sûr et correct** (note FinTech 66, Production 58).
> Le verdict de ce méta-audit est sans appel : **la sûreté est résolue, la rentabilité
> ne l'est pas du tout.** Le bot, tel quel, perd contre le simple fait de détenir QQQ.
> Les solutions deviennent les **Sprints 6-9** ci-dessous.

## A. Défauts vus par l'ingénieur logiciel (+ solutions)

> Le gros de la dette d'ingénierie « dangereuse » est déjà traité (Sprints 1-5).
> Restent surtout des défauts qui faussent la **mesure** de la performance ou qui
> font diverger **prod ↔ backtest** — donc directement liés à « faire de l'argent ».

| # | Défaut (ingénieur) | Réf. | Solution → Sprint |
|---|--------------------|------|-------------------|
| E1 | **La config de prod n'est pas celle qui est backtestée.** `main_ibkr.cpp:104-114` câble EMA 13/21, RSI 65/80, SL 7 %, TP 15 %, minHold 2 ; le golden valide les **défauts** de `SwingConfig` (9/21, 55/70, 5 %/10 %, 3). Le live tourne sur des paramètres **jamais validés**. | `main_ibkr.cpp:104` vs `SwingStrategy.hpp:11` | Externaliser la config (JSON validé) + golden sur la config de prod → **Sprint 6.1 / 9.1** (D21) |
| E2 | **Coûts de transaction irréalistes.** PaperBroker exécute au close, **zéro slippage**, commission seule (`PaperBroker.hpp:29-31`). Un backtest optimiste surévalue tout edge ; en prod les fills IBKR sont au marché. | `PaperBroker.hpp:31,47,73` | Modèle slippage + spread paramétrable → **Sprint 6.2** (D22) |
| E3 | **Rendement total faussé : `Close` au lieu de `Adj Close`** → dividendes QQQ ignorés, pour la stratégie ET le Buy & Hold. | `CsvDataFeed.hpp:115` | Total-return (Adj Close / réinvestissement) → **Sprint 6.3** (D7) |
| E4 | **Aucun harnais d'optimisation / validation.** Les paramètres sont des nombres magiques ; pas de split in-sample/out-of-sample, pas de walk-forward, pas de Monte-Carlo. Impossible de savoir si un réglage **généralise** ou s'il est sur-ajusté. | (absence) | Harnais walk-forward + grille + bootstrap → **Sprint 7** (D24) |
| E5 | **Fenêtre de lookback codée en dur.** `getBars(symbol, 60)` en prod vs `emaSlow+30=51` au backtest : la taille de fenêtre influe sur le seed SMA des EMA, donc sur les **signaux**. Prod et backtest peuvent diverger silencieusement. | `TradingBot.hpp:62` | Lookback configurable, unifié → **Sprint 9.2** (D19) |
| E6 | **Barre du jour incomplète en prod.** La boucle tourne toutes les 60 min sur des barres **journalières** : la dernière barre n'est pas clôturée, le croisement EMA peut osciller intra-journée (look-ahead/flap absent du backtest). | `main_ibkr.cpp:196`, `TradingBot.hpp:62` | N'évaluer que sur barres clôturées → **Sprint 9.3** (D25) |
| E7 | **Les indicateurs jettent high/low.** `IIndicator::compute(vector<double> closes)` ne reçoit que les clôtures alors que `Bar` porte O/H/L/C/V → ATR dégradé (clôture-à-clôture), vrai true-range et VWAP corrects inaccessibles. | `Interfaces.hpp:66`, `Models.hpp:9` | `compute(vector<Bar>)` → **Sprint 8.x prérequis** (D18) |
| E8 | **Calendrier de marché faux 8 mois/an** (UTC-5 fixe, ignore l'heure d'été). | `IBKRDataFeed.hpp:201` | UTC + DST → **Sprint 5.20 / 9.3** |
| E9 | **Trades jamais persistés en prod** (table `trades` vide, dashboard sans positions). | `main_ibkr.cpp` (pas d'appel `record_trade`) | Câblage logging → **Sprint 5.21** |
| E10 | **Pas de CI** : rien ne garde la prod alignée sur les tests verts à chaque push. | (absence) | GitHub Actions → **Sprint 5.22** |

## B. Défauts vus par le swing trader (+ solutions)

> C'est ici que se joue « faire de l'argent ». Chaque défaut est jugé à l'aune d'une
> seule question : *est-ce que ça coûte du rendement net, après coûts, vs détenir QQQ ?*

| # | Défaut (swing trader) | Preuve / Réf. | Solution → Sprint |
|---|------------------------|---------------|-------------------|
| T1 | **Le take-profit fixe ampute les gagnants.** Une stratégie de tendance gagne sur les **queues** (laisser courir). Plafonner à +10 % (+15 % en prod) coupe les meilleures positions ; le ratio gain/risque réel s'effondre. | `RiskManager.hpp:81`, `SwingConfig.takeProfitPct` | Supprimer/assouplir le TP, sortie au trailing/structure → **Sprint 8.2** (D26) |
| T2 | **Vente sur RSI > 70 = sortir de la tendance au pire moment.** Sur QQQ, un RSI > 70 en marché haussier est historiquement un signal de **force**, pas de retournement. Vendre à chaque fois laisse d'énormes gains sur la table. | `SwingStrategy.hpp:109`, `rsiSellMin` | Filtre de régime : ne pas vendre sur RSI en tendance haussière de fond → **Sprint 8.4** (D26) |
| T3 | **Filtre d'entrée contradictoire.** Exiger un croisement **haussier** (momentum ↑) ET `RSI < 55` (peu de momentum) s'auto-annule : très peu d'entrées (7 en 5 ans) et on rate justement les **breakouts** forts où se fait l'argent. | `SwingStrategy.hpp:96-99` | Entrée sur la force (breakout/pente), pas la faiblesse → **Sprint 8.3** (D26) |
| T4 | **Cash drag massif / long-only mono-actif.** Hors position l'essentiel du temps, jamais investi sur un actif qui monte structurellement → l'alpha est −229 pts. Pas de re-entrée, pas de rotation, pas de couverture. | golden : 7 trades, +9,67 % vs +238,55 % | Filtre de régime « rester avec la tendance » + réduire le temps en cash → **Sprint 8.1 / 8.5** |
| T5 | **Aucun filtre de régime macro.** Un croisement EMA whipsaw en range et arrive en retard en tendance. Pas de filtre long terme (ex. prix > SMA200) pour rester investi en tendance haussière et couper les entrées à contre-tendance. | `SwingStrategy.hpp:65-122` | Filtre SMA200 / pente → **Sprint 8.1** (D26) |
| T6 | **Validé sur un seul régime, un seul actif, sans hors-échantillon.** QQQ ~2018-2026 est quasi exclusivement haussier ; aucune robustesse en marché baissier/range, aucun OOS, aucun multi-actif. Edge non démontré. | golden mono-fichier | Walk-forward + multi-actifs + Monte-Carlo → **Sprint 7** (D24) |
| T7 | **Objectif de performance non défini.** « Faire de l'argent » doit être **alpha net vs Buy & Hold** (et drawdown maîtrisé), pas « retour positif ». Le rapport ne tranche pas explicitement « bat-on QQQ ? ». | `BackTester.hpp` (rapport) | Métriques cibles : CAGR, alpha net, Sortino, Calmar, % temps investi → **Sprint 6.4** (D23) |
| T8 | **Coûts/dividendes ignorés faussent la décision.** Sans slippage ni Adj Close, on peut « valider » un edge qui n'existe pas net de frais et hors dividendes réinvestis. | E2, E3 | cf. Sprint 6.2 / 6.3 |

---

# 🟣 SPRINT 6 — Vérité du backtest & réalisme (rentabilité, fondations)

> **On ne peut pas améliorer ce qu'on mesure mal.** Avant toute refonte de stratégie,
> le backtest doit dire la vérité : config réelle, coûts réels, dividendes, et une
> métrique d'objectif explicite. Dépendances : 6.2/6.3 modifient le golden (item 17) —
> figer un **nouveau golden** documenté dans le même commit. 6.1 est le plus urgent
> (un éventuel money-loser tourne en prod aujourd'hui).

- [x] **6.1** (D21) Backtester la **config de production** et figer un 2e golden
  → `918613b` (exécuté par anticipation, avant le Sprint 5 — demande utilisateur).
  Config prod extraite de main_ibkr.cpp vers une **source unique**
  `include/config/ProdConfig.hpp` consommée par le main ET le golden (fin de la
  dérive ; l'externalisation JSON reste l'item 9.1). **Verdict** : la config prod
  SURPERFORME la config défaut — **+36,50 % vs +9,67 %**, Sharpe **1,82 vs 0,62**,
  11 trades (10G/1P), maxDD 1,97 % — la branche « Décision requise » ne s'ouvre pas
  (voir D27). 3 tests : 2 goldens prod + comparaison côte à côte qui verrouille
  prod > défaut et rappelle l'écart au B&H (−202 pts).
- [ ] **6.2** (D22) Modèle de coûts réaliste dans `PaperBroker` (`PaperBroker.hpp:47,73`) :
  slippage paramétrable (bps) + demi-spread, en plus de la commission. **Acceptation** :
  test rouge (mêmes trades, capital final inférieur slippage > 0) ; nouveau golden figé.
- [ ] **6.3** (D7) Rendement total : utiliser `Adj Close` (`CsvDataFeed.hpp:115`) pour la
  stratégie ET le Buy & Hold (dividendes réinvestis). **Acceptation** : B&H recalculé,
  documenté ; golden mis à jour avec justification du delta.
- [ ] **6.4** (D23) Métriques d'objectif dans `BacktestResult` : CAGR, **alpha net vs B&H**,
  Sortino, Calmar, % de temps investi, et un verdict booléen « bat B&H net de coûts ».
  **Acceptation** : tests unitaires des formules sur séries synthétiques.

# 🟣 SPRINT 7 — Harnais de validation (prouver l'edge)

> Aucune confiance dans un paramètre sans validation hors-échantillon. Ce sprint
> construit l'outillage qui permettra de juger toute modif du Sprint 8 **honnêtement**.
> Dépend du Sprint 6 (coûts/métriques justes).

- [ ] **7.1** (D24) Split in-sample / out-of-sample + **walk-forward** sur QQQ.csv (fenêtres
  glissantes). **Acceptation** : rapport IS vs OOS ; un edge qui ne tient qu'en IS est
  signalé comme sur-ajusté.
- [ ] **7.2** Optimiseur de grille de paramètres avec **sélection robuste** (plateau de
  performance, pas le pic isolé). **Acceptation** : carte de sensibilité des paramètres.
- [ ] **7.3** **Monte-Carlo / bootstrap** des trades → distribution de CAGR et de drawdown
  (pas un seul chemin). **Acceptation** : p5/p50/p95 du drawdown et du retour.
- [ ] **7.4** **Multi-actifs** : charger plusieurs CSV (ex. SPY, IWM, MDY) pour tester la
  généralisation hors QQQ. **Acceptation** : la stratégie est évaluée sur ≥ 3 actifs.

# 🟣 SPRINT 8 — Refonte de la stratégie pour capter la tendance (l'argent)

> Le sprint qui doit **transformer −229 pts d'alpha en alpha positif (ou neutre à moindre
> drawdown)**. Chaque item est jugé par le harnais du Sprint 7 en **OOS**, jamais en IS.
> Prérequis : E7/D18 (indicateurs sur `vector<Bar>`) pour des stops/VWAP corrects.

- [ ] **8.0** (D18) Enrichir `IIndicator` → `compute(const std::vector<Bar>&)` (high/low/volume
  disponibles) ; vrai ATR/true-range, VWAP correct. **Acceptation** : golden ATR mis à jour,
  DayTradeStrategy migrée.
- [ ] **8.1** (D26) **Filtre de régime** : n'ouvrir long que si tendance de fond haussière
  (ex. prix > SMA200 / pente positive). **Acceptation OOS** : participation aux tendances ↑,
  whipsaws de range ↓ ; alpha net OOS > version actuelle.
- [ ] **8.2** (D26) **Laisser courir les gagnants** : retirer/assouplir le take-profit fixe
  (`RiskManager.hpp:81`), sortie pilotée par trailing/structure. **Acceptation OOS** :
  gain moyen des gagnants ↑ sans dégrader le profit factor net.
- [ ] **8.3** (D26) **Réviser l'entrée contradictoire** (`SwingStrategy.hpp:96-99`) : entrer
  sur la force (breakout/momentum), pas exiger `RSI < 55`. **Acceptation OOS** : nombre de
  trades et exposition ↑, expectancy nette ≥ 0.
- [ ] **8.4** (D26) **Ne pas vendre sur RSI seul en tendance haussière** (gate par 8.1).
- [ ] **8.5** **Réduire le cash drag** : re-entrée / rester investi tant que le régime tient.
  **Acceptation OOS** : % de temps investi ↑, alpha net ↑.

> **Definition of Done du Sprint 8** (en plus de la DoD standard) : la stratégie retenue
> **bat le Buy & Hold net de coûts en out-of-sample** OU le sous-performe de moins de X %
> avec un drawdown sensiblement réduit (cible chiffrée à arbitrer — **Décision requise**).
> Sinon le sprint conclut « pas d'edge démontré » et on ne déploie PAS — c'est un résultat
> valide (ne jamais mettre d'argent réel sur un edge non prouvé).

# 🟣 SPRINT 9 — Mise en production de la stratégie validée

> Ne s'ouvre qu'après un edge OOS démontré (Sprint 8). Sinon, la prod reste en paper.

- [ ] **9.1** Externaliser la config (fichier JSON **validé** au démarrage) ; fin de la dérive
  prod ≠ backtest (E1/D21). **Acceptation** : la config de prod EST chargée par le golden.
- [ ] **9.2** (D19) Lookback configurable unifié prod/backtest (`TradingBot.hpp:62`).
- [ ] **9.3** (item 20 + E6/D25) Calendrier de marché correct (UTC/DST) + n'évaluer que sur
  **barres clôturées** (pas la barre du jour en formation).
- [ ] **9.4** Procédure de **re-calibration walk-forward** documentée (cadence, critère de
  reprise des paramètres) — pour que l'edge ne se dégrade pas en silence.

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
| D20 | 🟢 | (Sprint 4) Dérive ROADMAP ↔ dépôt : un lot « couverture des fondations » (commit `15eb711`, +~2528 lignes : brokers IBKR/Alpaca, PaperBroker, CsvDataFeed, métriques backtest, Logger, indicateurs) a été mergé hors du cycle `prompt-executer-sprint`. Le décompte de tests du tableau de bord (198) ne le reflétait pas (réel : 344 avant ce sprint). Aucune perte — la couverture est légitime et verte — mais le tableau de bord a menti pendant un sprint. **Garde-fou ajouté** : `prompt-executer-sprint.md` étape 2 exige désormais de recaler « État des tests » sur la sortie réelle de `ctest -N` et de signaler toute dérive ; `prompt-mise-a-jour-roadmap.md` rappelle d'absorber au changelog tout commit mergé hors cycle. La CI (item 22) reste le vrai remède de fond | Corrigé (workflow amendé ce sprint) |
| D21 | 🔴 | (Méta-audit) **La config qui tourne en prod n'est pas celle validée par le golden.** `main_ibkr.cpp:104-114` : EMA 13/21, RSI 65/80, SL 7 %, TP 15 %, minHold 2 — alors que le golden (item 17) valide les défauts de `SwingConfig` (9/21, 55/70, 5 %/10 %, 3). Le live trade des paramètres **jamais backtestés** : leur performance et leurs stops sont inconnus. Risque financier direct | 6.1 ✅ `918613b` (source unique ProdConfig.hpp + golden prod, voir D27) ; externalisation JSON → Sprint 9 (9.1) |
| D27 | 🟢 | (Item 6.1) **La config prod surperforme la config défaut sur QQQ.csv** : +36,50 % vs +9,67 %, Sharpe 1,82 vs 0,62, 11 trades (10G/1P, 0 SL / 4 TP / 2 trailing / 5 signal), maxDD 1,97 %, 1er achat 2019-06-18, dernière vente 2026-02-03. La « Décision requise » de 6.1 (aligner la prod sur la config défaut) est donc **sans objet**. Lecture trader : le RSI d'entrée plus permissif (65 vs 55) et le TP plus large (15 % vs 10 %) confirment les hypothèses T1/T3 du méta-audit — desserrer les freins augmente l'alpha. Mais −202 pts vs B&H restent, et ces chiffres sont **in-sample, sans slippage ni dividendes** : aucune conclusion définitive avant les items 6.2/6.3 et le harnais OOS (Sprint 7) | Constat ; alimente Sprints 7-8 |
| D22 | 🟠 | (Méta-audit) Backtest optimiste : `PaperBroker` exécute au close **sans slippage**, commission seule (`PaperBroker.hpp:29-31,47,73`). Tout edge mesuré est surévalué ; en prod les fills IBKR sont au marché. Un edge marginal peut être négatif net de slippage/spread | Sprint 6 (6.2) |
| D23 | 🟠 | (Méta-audit) Pas d'objectif de performance explicite. « Faire de l'argent » = **alpha net vs Buy & Hold** + drawdown maîtrisé, pas « retour positif ». Le rapport (`BackTester.hpp`) ne tranche pas « bat-on QQQ net de coûts ? ». Manquent CAGR, Sortino, Calmar, % temps investi | Sprint 6 (6.4) |
| D24 | 🟠 | (Méta-audit) **Aucune validation hors-échantillon.** Paramètres = nombres magiques, validés sur un seul actif (QQQ) et un seul régime (~2018-2026, quasi 100 % haussier). Pas d'IS/OOS, pas de walk-forward, pas de Monte-Carlo, pas de multi-actifs → edge non démontré, risque de sur-ajustement | Sprint 7 |
| D25 | 🟡 | (Méta-audit) Prod : boucle 60 min sur barres **journalières** → la dernière barre n'est pas clôturée, le croisement EMA peut osciller intra-journée (flap/look-ahead absent du backtest qui ne voit que des barres complètes) | Sprint 9 (9.3) |
| D26 | 🔴 | (Méta-audit) **Défauts structurels de la stratégie (cause racine de l'alpha −229 pts)** : take-profit fixe qui ampute les gagnants (`RiskManager.hpp:81`) ; vente sur RSI > 70 qui sort des tendances haussières (`SwingStrategy.hpp:109`) ; filtre d'entrée contradictoire croisement-haussier + `RSI<55` (`SwingStrategy.hpp:96-99`) → 7 trades/5 ans ; aucun filtre de régime ; long-only mono-actif → cash drag massif | Sprint 8 (après le harnais Sprint 7) |

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

### Item 6.1 — Golden de la config de production (2026-06-10, exécuté par anticipation)

> Hors cycle de sprint : à la suite du méta-audit, l'utilisateur a demandé de
> quantifier immédiatement la config réellement tradée (risque D21 🔴). Le Sprint 5
> reste le sprint courant ; le Sprint 6 démarre avec 6.1 déjà coché.

**Commits** :
- `c01f813` docs : méta-audit (défauts ingénieur E1-E10 + swing trader T1-T8,
  Sprints 6-9, découvertes D21-D26, dimension Rentabilité au tableau de bord)
- `918613b` test(backtest) : golden de la config de production + source unique (6.1)

**Tests** : 350 → **353** (3 ajoutés) : `GoldenProdConfigPerformanceOnQqqCsv`,
`GoldenProdConfigTradeBreakdownOnQqqCsv`, `ProdConfigOutperformsDefaultConfig`
(comparaison côte à côte, verrouille prod > défaut + écart au B&H).

**Valeurs golden prod figées** (QQQ.csv, 10 000 $, comm. 0,1 %) : retour total
**+36,5015 %**, capital final **13 650,15 $**, Sharpe **1,8192**, maxDD **1,9702 %**,
**11 trades** (10G/1P ; 0 SL / 4 TP / 2 trailing / 5 signal), 1er achat 2019-06-18,
dernière vente 2026-02-03, 1858 points d'équité. Golden défaut **inchangé**.

**Nouveau fichier** : `include/config/ProdConfig.hpp` (source unique de la config
prod, consommée par `main_ibkr.cpp` et par le golden — fin de la dérive D21).
Verdict et conséquences : voir D27 (la « Décision requise » de 6.1 est sans objet).

## Rétrospectives

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
