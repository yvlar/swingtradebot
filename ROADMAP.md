# ROADMAP — SwingBot C++

> **Source de vérité du workflow.** Ce fichier est lu par `prompt-executer-sprint.md`
> (exécution du sprint courant) et mis à jour via `prompt-mise-a-jour-roadmap.md`
> (clôture de sprint, re-priorisation, rétrospective). Ne pas le modifier à la main
> en dehors de ce cycle, sauf pour ajouter une découverte.

## Tableau de bord

| Dimension    | Note /100 | Baseline (audit 2026-06-10) |
|--------------|-----------|------------------------------|
| Architecture | 83        | 68                           |
| Qualité      | 89        | 60                           |
| FinTech      | 80        | 38                           |
| Production   | 70        | 35                           |

- **Dernière mise à jour** : 2026-06-10 (consolidation Sprints 5 + 6 sur cette branche)
- **Sprint courant** : Sprint 8 — Refonte de la stratégie (capter la tendance). Sprints
  5/6/7 clos (7.4 multi-actifs reporté, données manquantes — D31). Le harnais du Sprint 7
  jugera chaque changement EN OOS. Reliquat item 19 (stop résident) replié en Sprint 9.5.

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
> | **Rentabilité** | **28** (15 audit, 22 après 6.1, 25 après S6) | Mesure honnête (S6) + **preuve OOS** (S7) : la stratégie n'a **aucun edge directionnel hors-échantillon** (walk-forward : 1/4 segment bat le B&H, le baissier), et l'optimisation des paramètres ne le crée pas (D30). Le bootstrap montre des trades robustes mais un cash drag décisif. On SAIT maintenant quoi corriger (structure, Sprint 8) et on a l'outil pour juger. Le +3 récompense la certitude, pas un gain de rendement — celui-ci viendra (ou non) du Sprint 8. |
- **État des tests** : 412/412 verts (356 unitaires + 56 intégration, après Sprint 7),
  recalé sur `ctest -N`. La CI GitHub Actions (item 22) vérifie ce décompte à chaque push.
  Rappel D20 : le « 198 » de la clôture Sprint 3 ignorait un lot de fondations mergé
  hors cycle (`15eb711`) — d'où le recalage systématique.
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
  « entrée bloquée » et « sortie NON bloquée »). Compteurs en mémoire → D29.
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

# 🟣 SPRINT 6 — Vérité du backtest & réalisme ✅ (clos le 2026-06-10)

> **On ne peut pas améliorer ce qu'on mesure mal.** Avant toute refonte de stratégie,
> le backtest doit dire la vérité : config réelle, coûts réels, dividendes, et une
> métrique d'objectif explicite. Bilan : les goldens historiques sont restés intacts
> (slippage défaut 0 ; Close == Adj Close dans le CSV actuel — D28) ; un golden
> « coûts réalistes » s'y ajoute, et le cash drag est désormais un chiffre (29 %
> de temps investi, CAGR 4,47 %/an vs ~19 %/an pour QQQ).

- [x] **6.1** (D21) Backtester la **config de production** et figer un 2e golden
  → `918613b` (exécuté par anticipation, avant le Sprint 5 — demande utilisateur).
  Config prod extraite de main_ibkr.cpp vers une **source unique**
  `include/config/ProdConfig.hpp` consommée par le main ET le golden (fin de la
  dérive ; l'externalisation JSON reste l'item 9.1). **Verdict** : la config prod
  SURPERFORME la config défaut — **+36,50 % vs +9,67 %**, Sharpe **1,82 vs 0,62**,
  11 trades (10G/1P), maxDD 1,97 % — la branche « Décision requise » ne s'ouvre pas
  (voir D27). 3 tests : 2 goldens prod + comparaison côte à côte qui verrouille
  prod > défaut et rappelle l'écart au B&H (−202 pts).
- [x] **6.2** (D22) Modèle de coûts réaliste dans `PaperBroker` → `6216e4b`
  `slippagePct` par côté (achat à close×(1+s), vente à close×(1−s), commission sur le
  fill, mark-to-market au prix coté) ; `Backtester` expose le paramètre (défaut 0 —
  goldens historiques intacts). Golden « coûts réalistes » figé : prod + 5 bps →
  **+36,14 %** (vs +36,50 %), mêmes 11 trades. Tests rouges : 5 PaperBrokerUnit
  (l'API n'existait pas) + 1 golden d'intégration.
- [x] **6.3** (D7) Rendement total : `Adj Close` par défaut dans `CsvDataFeed` → `03c9cda`
  (opt-out `useAdjustedClose=false`). Goldens inchangés et c'est JUSTIFIÉ : dans le
  QQQ.csv actuel, Close == Adj Close sur les 1858 lignes (vérifié) — le fichier n'est
  pas une série total-return (**D28**) ; le contrat est figé par test pour les futures
  données. Tests rouges : UsesAdjCloseByDefault (échouait sur l'ancienne colonne).
- [x] **6.4** (D23) Métriques d'objectif dans `BacktestResult` → `97c2b77`
  `cagrPct` (durée calendaire réelle), `sortinoRatio`, `calmarRatio`,
  `pctTimeInvested`, `beatsBuyHold` (verdict net de coûts) ; affichées par
  `printReport`, figées dans le golden prod, imprimées par la comparaison côte à
  côte. 7 tests BacktesterMetricsUnit sur séries synthétiques (rouges : champs
  inexistants). **Verdict figé : beatsBuyHold = NON** — c'est le booléen que les
  Sprints 7-8 doivent renverser.

# 🟣 SPRINT 7 — Harnais de validation (prouver l'edge) ✅ (clos le 2026-06-10 ; 7.4 reporté)

> Aucune confiance dans un paramètre sans validation hors-échantillon. Ce sprint
> construit l'outillage qui permettra de juger toute modif du Sprint 8 **honnêtement**.
> Dépend du Sprint 6 (coûts/métriques justes). **Note D28** : QQQ.csv n'étant pas
> total-return, l'écart au B&H est sous-estimé — tout verdict OOS « la stratégie perd »
> est donc conservateur (décision utilisateur : construire le harnais malgré D28).
> **Bilan** : 7.1-7.3 livrés ; 7.4 (multi-actifs) reporté faute de données (un seul CSV
> synthétique — même blocage que D28, voir D31). Le harnais a tranché : **aucun edge
> directionnel OOS**, et l'optimisation des paramètres ne le crée pas — c'est la
> structure de la stratégie qu'il faut refondre (Sprint 8).

- [x] **7.1** (D24) Split IS/OOS + **walk-forward** roulant → `0df2117`
  `include/backtest/WalkForward.hpp` (split ancré + N segments) sur `Backtester::runOn(bars)`
  (run() y délègue → golden inchangé) + `CsvDataFeed(vector<Bar>, FromBars)`. 8 tests
  unitaires + 2 d'intégration. **Verdict OOS figé (config prod, comm 0,1 % + slippage
  5 bps)** : split 70/30 → OOS **+6,56 % vs B&H +39,39 %** (perd) ; walk-forward 4
  segments → **1/4 seulement bat B&H, et c'est le segment BAISSIER** (la stratégie
  gagne en restant en cash, pas en captant la hausse). **Aucun edge directionnel hors
  échantillon** — c'est le verdict que le Sprint 8 doit renverser ICI (voir D30).
- [x] **7.2** Optimiseur de grille à **sélection robuste** → `6ab0bda`
  `include/backtest/GridOptimizer.hpp` : balaie emaFast × emaSlow (score = Sharpe),
  `selectBest` (pic) vs `selectRobust` (plateau = moyenne de voisinage 3×3). Logique de
  sélection pure et testée (un pic isolé 0,99 perd contre un plateau ~0,6). 5 tests
  unitaires + 2 d'intégration. **Démonstration figée** : pic IS EMA 9/21 (Sharpe 2,02),
  plateau EMA 13/20 — **évalués en OOS, perdent quand même** (+6,75 % vs B&H +41,5 %).
  Confirme que tuner les paramètres ne sauve pas la structure.
- [x] **7.3** **Monte-Carlo / bootstrap** des trades → `9554fef`
  `include/backtest/MonteCarlo.hpp` : ré-échantillonnage avec remise (déterministe),
  percentiles p5/p50/p95 du retour ET du drawdown + P(perte) ; `tradeReturns` relatifs
  à l'équité (pas au sizing). 8 tests unitaires + 1 d'intégration. **Distribution figée
  (config prod, 5000 chemins)** : retour p5 +29,8 % / p50 +43,6 % / p95 +56,2 %, maxDD
  p95 ~1 %, P(perte) 0 %. Leçon : les TRADES sont robustes, mais le p95 reste très sous
  le B&H → c'est le cash drag le problème, pas la qualité des trades.
- [ ] **7.4** (D31) **Multi-actifs** : charger plusieurs CSV (SPY, IWM, MDY…) pour tester
  la généralisation hors QQQ. **REPORTÉ** : un seul dataset disponible (QQQ.csv,
  synthétique) et réseau verrouillé sur les fournisseurs → même blocage que D28. À
  reprendre dès qu'un jeu de données multi-actifs est fourni. Le code (`WalkForward`,
  `Backtester`) accepte déjà des barres arbitraires — il ne manque QUE les données.

# 🟣 SPRINT 8 — Refonte de la stratégie pour capter la tendance (l'argent)

> Le sprint qui doit **transformer −229 pts d'alpha en alpha positif (ou neutre à moindre
> drawdown)**. Chaque item est jugé par le harnais du Sprint 7 en **OOS**, jamais en IS.
> Prérequis : E7/D18 (indicateurs sur `vector<Bar>`) pour des stops/VWAP corrects.
>
> **Banc de prototypage** : `research/` (Python, partage `QQQ.csv`) permet d'itérer les
> idées en quelques secondes avant de les porter en C++ (`prototype_regime.py`). **Read
> initial (D32)** : ajouter le filtre de régime SMA200 *par-dessus* l'entrée actuelle la
> sur-filtre (3,3 % investi) → le régime doit **remplacer** l'entrée (8.3 AVANT/AVEC 8.1),
> pas s'y empiler ; le levier le plus net seul est **8.4** (ne pas vendre sur RSI). Le banc
> isole aussi un 2e levier : à pleine exposition la config prod fait +207 % (vs +36 % en
> C++) → le **sizing 2 % est un frein distinct du timing** (à arbitrer dans le Sprint 8).

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

## 🟣 SPRINT 9.5 — Stop résident broker (reliquat de l'item 19)

> Replié ici lors de la consolidation Sprints 5+6 : l'autre branche l'avait
> numéroté « Sprint 6 », en collision avec la vérité du backtest. C'est un item de
> durcissement production — à traiter après l'edge OOS (Sprints 7-8), sauf priorité
> contraire. La numérotation 9.5 le rattache au Sprint 9 (mise en prod).

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
| D7 | 🟡 | CsvDataFeed utilise `Close` et non `Adj Close` (CsvDataFeed.hpp:119) → dividendes QQQ ignorés dans le backtest et la comparaison Buy&Hold | ✅ Corrigé au Sprint 6 (`03c9cda` — Adj Close par défaut ; mais voir D28 : le CSV actuel n'est pas total-return) |
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
| D28 | 🟠 | (Sprint 6) **QQQ.csv n'est pas une série total-return** : Close == Adj Close sur les 1858 lignes (vérifié par script). Les dividendes QQQ (~0,5-0,7 %/an) sont absents → le Buy & Hold réel est encore PLUS HAUT que +238,55 %, et la stratégie (en cash 71 % du temps) en capte moins que la détention passive. Le code lit désormais la bonne colonne (6.3) ; il reste une action de DONNÉES : re-télécharger un historique réellement ajusté avant le harnais du Sprint 7, sinon l'écart au B&H reste sous-estimé | Sprint 7 (prérequis données) |
| D22 | 🟠 | (Méta-audit) Backtest optimiste : `PaperBroker` exécute au close **sans slippage**, commission seule (`PaperBroker.hpp:29-31,47,73`). Tout edge mesuré est surévalué ; en prod les fills IBKR sont au marché. Un edge marginal peut être négatif net de slippage/spread | ✅ Corrigé au Sprint 6 (`6216e4b` — slippagePct par côté + golden « coûts réalistes ») |
| D23 | 🟠 | (Méta-audit) Pas d'objectif de performance explicite. « Faire de l'argent » = **alpha net vs Buy & Hold** + drawdown maîtrisé, pas « retour positif ». Le rapport (`BackTester.hpp`) ne tranche pas « bat-on QQQ net de coûts ? ». Manquent CAGR, Sortino, Calmar, % temps investi | ✅ Corrigé au Sprint 6 (`97c2b77` — 5 métriques + verdict beatsBuyHold figé à NON) |
| D24 | 🟠 | (Méta-audit) **Aucune validation hors-échantillon.** Paramètres = nombres magiques, validés sur un seul actif (QQQ) et un seul régime (~2018-2026, quasi 100 % haussier). Pas d'IS/OOS, pas de walk-forward, pas de Monte-Carlo, pas de multi-actifs → edge non démontré, risque de sur-ajustement | Sprint 7 |
| D25 | 🟡 | (Méta-audit) Prod : boucle 60 min sur barres **journalières** → la dernière barre n'est pas clôturée, le croisement EMA peut osciller intra-journée (flap/look-ahead absent du backtest qui ne voit que des barres complètes) | Sprint 9 (9.3) |
| D26 | 🔴 | (Méta-audit) **Défauts structurels de la stratégie (cause racine de l'alpha −229 pts)** : take-profit fixe qui ampute les gagnants (`RiskManager.hpp:81`) ; vente sur RSI > 70 qui sort des tendances haussières (`SwingStrategy.hpp:109`) ; filtre d'entrée contradictoire croisement-haussier + `RSI<55` (`SwingStrategy.hpp:96-99`) → 7 trades/5 ans ; aucun filtre de régime ; long-only mono-actif → cash drag massif | Sprint 8 (après le harnais Sprint 7) |
| D29 | 🟡 | (Sprint 5) Les compteurs du kill-switch (equity de début de jour, ordres/jour, pertes consécutives — TradingBot.hpp) sont **en mémoire** : un redémarrage en cours de journée les remet à zéro, affaiblissant la coupure (drawdown journalier réévalué depuis l'equity du redémarrage, série de pertes oubliée). Acceptable pour une boucle de 60 min, mais à persister (via `IStateStore`, à côté de l'état de position) pour une protection robuste | Sprint 9.5 (migration de schéma de l'item 19) |
| D32 | 🟢 | (Banc Python `research/`) **Read de prototypage Sprint 8** : (a) le filtre de régime SMA200 *ajouté par-dessus* l'entrée actuelle (croisement EMA + RSI<65) sur-filtre → 3,3 % investi, alpha OOS −40,8 % : le régime doit **remplacer** la logique d'entrée (8.3), pas s'y empiler ; (b) levier le plus net seul = **8.4** (ne pas vendre sur RSI, alpha OOS −8,7 % vs −13,5 %) ; (c) à pleine exposition la config prod fait +207 % (vs +36 % en C++ sizé à 2 %) → le **sizing conservateur est un frein distinct du timing**. À confirmer en portant le gagnant en C++ et en re-jugeant via `WalkForward` | Sprint 8 (oriente 8.1/8.3/8.4 + arbitrage sizing) |
| D31 | 🟠 | (Sprint 7, item 7.4) **Pas de données multi-actifs ni total-return.** Un seul CSV (QQQ.csv, synthétique jusqu'en 2026, Close==Adj Close — cf. D28) ; réseau verrouillé sur les fournisseurs (Yahoo `unauthorized`, Stooq mort). Bloque 7.4 (généralisation hors QQQ) et la résolution propre de D28. Le code (`WalkForward`/`Backtester`/`CsvDataFeed(FromBars)`) accepte déjà des barres arbitraires : il ne manque QUE les fichiers. Action DONNÉES : l'utilisateur fournit SPY/IWM/MDY réellement ajustés | Sprint 8 (généralisation) / prérequis fourni par l'utilisateur |
| D30 | 🔴 | (Sprint 7, item 7.1) **L'edge ne tient pas en out-of-sample.** Walk-forward de la config prod sur QQQ.csv : split 70/30 → OOS +6,56 % vs B&H +39,39 % ; 4 segments roulants → 1/4 seulement bat le B&H, et c'est le segment BAISSIER 2020-10→2022-07 (B&H −7,44 %). La stratégie « gagne » uniquement en étant hors marché pendant les baisses ; dans les 3 segments haussiers elle laisse 50-80 pts sur la table. C'est un **overlay défensif de réduction de risque, pas un générateur d'alpha** — confirmation OOS de D26. Tant que ce verdict n'est pas renversé (Sprint 8), ne PAS engager d'argent réel en visant un rendement supérieur à QQQ | Sprint 8 (à renverser ICI, en OOS) |

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

### Item 6.1 — Golden de la config de production (2026-06-10, exécuté par anticipation)

> Hors cycle de sprint : à la suite du méta-audit, l'utilisateur a demandé de
> quantifier immédiatement la config réellement tradée (risque D21 🔴). Le Sprint 5 (durcissement) et le Sprint 6 (vérité du backtest) ont été menés en
> parallèle sur deux branches puis consolidés.

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

### Sprint 6 — Vérité du backtest & réalisme (2026-06-10, consolidé avec le Sprint 5)

> Exécuté hors ordre sur demande utilisateur (suite logique de 6.1) : aucun item
> ne dépendait du Sprint 5, et 6.2-6.4 ne demandaient aucune décision produit.

**Commits** (ordre chronologique) :
- `918613b` test(backtest) : golden de la config de production + source unique (6.1)
- `8a496a7` docs : clôture item 6.1 — golden config prod, découverte D27
- `6216e4b` feat(backtest) : modèle de coûts réaliste — slippage + demi-spread (6.2)
- `03c9cda` feat(data) : rendement total — Adj Close par défaut dans CsvDataFeed (6.3)
- `97c2b77` feat(backtest) : métriques d'objectif — CAGR/Sortino/Calmar/investi/verdict (6.4)

**Tests** : 350 → **367** (17 ajoutés sur le sprint complet). PaperBrokerUnit +5
(slippage), CsvDataFeedUnit +1 net (UsesAdjCloseByDefault + RawCloseWhenAdjustedDisabled,
remplacent UsesCloseNotAdjClose), BacktesterMetricsUnit +7 (formules d'objectif),
BacktesterIntegration +4 (2 goldens prod, « coûts réalistes », comparaison côte à côte).

**Goldens** : les goldens historiques (défaut et prod) sont restés intacts —
slippage par défaut 0, et Close == Adj Close dans le CSV actuel (D28). Nouveau
golden figé : prod + commission 0,1 % + slippage 5 bps → **+36,1363 %**,
capital final 13 613,63 $, Sharpe 1,8005, mêmes 11 trades. Le golden prod fige
aussi les métriques d'objectif : **CAGR 4,4700 %/an**, Sortino 3,1820,
Calmar 2,2687, **investi 29,10 %**, **beatsBuyHold = NON**.

**Interfaces modifiées** : `PaperBroker(+slippagePct)`, `Backtester(+slippagePct)`,
`CsvDataFeed(+useAdjustedClose)` — défauts rétro-compatibles ; `BacktestResult`
+5 champs ; `printReport` +5 lignes ; nouveau `include/config/ProdConfig.hpp`.
Aucun changement dans le moteur de trading lui-même.

### Consolidation des branches Sprint 5 ⨝ Sprint 6 (2026-06-10)

Deux sessions ont avancé en parallèle depuis la clôture du Sprint 4 : l'une le
Sprint 5 (durcissement prod), l'autre le méta-audit + Sprint 6 (vérité du
backtest). Merge de `prompt-executer-sprint-0hneps` dans la branche d'analyse,
conflits résolus : CI unifiée (une seule), collision **D21** (config prod vs
compteurs kill-switch) → la seconde renumérotée **D29**, collision de
numérotation « Sprint 6 » → le stop résident broker devient **Sprint 9.5**.
387/387 verts après merge (mes 367 + leurs 20).

### Sprint 7 — Harnais de validation (2026-06-10)

**Commits** (ordre chronologique) :
- `0df2117` feat(backtest) : harnais walk-forward IS/OOS (7.1)
- `6ab0bda` feat(backtest) : optimiseur de grille à sélection robuste (7.2)
- `9554fef` feat(backtest) : Monte-Carlo bootstrap des trades (7.3)

**Tests** : 387 → **412** (25 ajoutés). WalkForward 7+2, GridOptimizer 5+2,
MonteCarlo 8+1 (unitaires + intégration). Tous les goldens existants **inchangés**
(le refactor `run()`→`runOn()` délègue à l'identique).

**Nouveaux fichiers** : `include/backtest/WalkForward.hpp`, `GridOptimizer.hpp`,
`MonteCarlo.hpp` + 3 fichiers de tests unitaires + 3 d'intégration.
**Interfaces** : `Backtester::runOn(bars)` (run() y délègue), `CsvDataFeed(vector<Bar>,
FromBars)`. Aucun changement du moteur de trading.

**Artefacts de décision figés** (config prod, comm 0,1 % + slippage 5 bps) :
- OOS 70/30 : **+6,56 % vs B&H +39,39 %** (perd) — D30.
- Walk-forward 4 segments : **1/4 bat le B&H**, le baissier.
- Optimisation EMA : pic IS 9/21 et plateau 13/20 **perdent en OOS** (+6,75 %).
- Monte-Carlo 5000 chemins : retour p5/p50/p95 = +29,8 / +43,6 / +56,2 %,
  maxDD p95 ~1 %, P(perte) 0 % — trades robustes, cash drag décisif.

**Reporté** : 7.4 multi-actifs (D31, données manquantes).

## Rétrospectives

### Sprint 7 — Harnais de validation (2026-06-10)

**1. Découpage** : ordre 7.1→7.2→7.3 juste — 7.1 a fourni la primitive `runOn(bars)`
que 7.2 (grille) et 7.3 (bootstrap) ont réutilisée. La séparation logique pure
(sélection robuste, percentiles) ⟂ évaluation par le moteur a permis un vrai TDD
(matrices et vecteurs fabriqués pour le rouge, intégration pour les chiffres réels).
Bonne décision utilisateur de finir le Sprint 7 avant le 8 : le harnais existe AVANT
qu'on touche à la stratégie, donc chaque changement du Sprint 8 sera jugé en OOS dès
le premier commit — pas d'auto-illusion in-sample possible.

**2. Suffisance des prompts** : aucune improvisation de workflow. Deux ajustements
techniques détectés par les chiffres (et non par les tests) : (a) la sélection robuste
choisit une cellule de bord du plateau (effet de voisinage tronqué) — le test a été
recadré sur « appartient au plateau » plutôt que « cellule exacte », ce qui est la
vraie propriété voulue ; (b) le bootstrap composait le rendement SUR POSITION (p50
208 % absurde) au lieu du rendement relatif à l'équité — corrigé avant de figer. Leçon :
imprimer la valeur réelle AVANT de figer un golden attrape les erreurs de modèle qu'un
test vert ne voit pas.

**3. À détecter plus tôt** : (a) le défaut de modèle du bootstrap (sur-position vs
équité) aurait dû être anticipé à la conception — garde-fou ajouté : tout chiffre de
sortie passe par une sanity-check « ordre de grandeur cohérent avec le backtest » avant
gel. (b) D31 (données mono-actif) était connu dès D28 ; 7.4 aurait dû être marqué
« bloqué » dès l'ouverture du sprint plutôt qu'en clôture.

**4. Notes** (précédent 82/88/76/70 + Rentabilité 25) :
- **Architecture 83** (+1) : `runOn(bars)` ouvre proprement le moteur au rejeu de
  sous-fenêtres sans dupliquer de logique ; les 3 outils de validation se composent sur
  les mêmes abstractions. Petit gain structurel.
- **Qualité 89** (+1) : +25 tests, dont des unités de logique pure (sélection, percentiles)
  faciles à raisonner ; goldens inchangés prouvés. Plafond : pas de test E2E du `main`.
- **FinTech 80** (+4) : le projet a enfin une **chaîne de validation hors-échantillon**
  (walk-forward + optimisation robuste + Monte-Carlo) — exigence FinTech non négociable
  avant de risquer de l'argent. Le verdict est négatif, mais l'OUTIL pour juger existe.
- **Production 70** (=) : rien de déployé n'a changé (outillage d'analyse).
- **Rentabilité 28** (+3) : aucun gain de rendement (pas le but), mais la question
  « a-t-on un edge ? » a une réponse PROUVÉE (non, pas en OOS) et un cap clair (Sprint 8).

### Sprint 6 — Vérité du backtest & réalisme (2026-06-10)

**1. Découpage** : bon calibre (4 items), et l'ordre 6.1→6.2→6.3→6.4 s'est avéré
exact — 6.1 a fourni la config de référence que 6.2 et 6.4 ont instrumentée. Le
choix d'exécuter le Sprint 6 AVANT le Sprint 5 (sur demande utilisateur) était
le bon : aucune dépendance ratée, et les items 18/19 du Sprint 5 attendent de
toute façon des décisions produit. Surprise utile : deux hypothèses du plan ne se
sont PAS réalisées — la config prod ne sous-performe pas (D27, « Décision
requise » évitée) et les goldens n'ont pas bougé en passant à Adj Close (D28,
le CSV n'est pas total-return). Dans les deux cas, c'est la VÉRIFICATION qui a
de la valeur : le plan prévoyait des deltas, la réalité a répondu autrement.

**2. Suffisance des prompts** : aucune improvisation de workflow. Le « test
rouge » a pris ses trois formes légitimes : erreur de compilation (6.2, 6.4 —
API/champs inexistants), inversion d'un test verrou (6.3 — UsesCloseNotAdjClose
figea l'ancien comportement, son inversion ÉTAIT le rouge), golden additionnel
(6.1). Outils jetables compilés hors build pour découvrir les valeurs golden
avant de les figer — pratique à retenir, aucun artefact committé.

**3. À détecter plus tôt** : (a) D28 (Close == Adj Close dans tout le CSV)
aurait dû être vérifié AVANT de planifier l'item 6.3 — une ligne d'awk aurait
requalifié l'item de « changement de comportement » en « contrat pour données
futures » ; garde-fou : tout item fondé sur une propriété de DONNÉES commence
par un script qui la vérifie. (b) Le cash drag (29 % investi) était calculable
depuis le Sprint 3 — si pctTimeInvested avait existé dès le golden initial, le
méta-audit aurait chiffré T4 au lieu de l'estimer. Leçon générale : chaque
hypothèse de plan mérite sa mesure avant son sprint.

**4. Notes** (précédent 81/84/66/58 + Rentabilité 22) :
- **Architecture 82** (+1) : paramètres de coût et de données aux bons endroits
  (broker simulé, feed), défauts rétro-compatibles, source unique ProdConfig —
  petite amélioration, pas de refonte.
- **Qualité 86** (+2) : 367 tests, chaque formule de métrique testée sur série
  synthétique, contrats de données figés par test. Toujours plafonnée par
  l'absence de CI (item 22).
- **FinTech 70** (+4) : le backtest dit enfin la vérité économique — coûts par
  côté, rendement total, CAGR/Sortino/Calmar, verdict net de coûts. C'est la
  condition d'entrée du Sprint 7 (walk-forward) : on validera sur des chiffres
  honnêtes.
- **Production 58** (=) : rien de déployé n'a changé (la prod bénéficie de la
  source unique ProdConfig, mais kill-switch/stops broker/CI restent au Sprint 5).
- **Rentabilité 25** (+3 vs 22 après 6.1) : aucune amélioration de l'edge (ce
  n'était pas le but), mais la mesure est désormais complète et honnête :
  beatsBuyHold=NON est figé par test, le cash drag est un chiffre (29 %), et le
  coût du réalisme est connu (−0,37 pt à 5 bps). Le levier est maintenant
  entièrement dans les Sprints 7-8.

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

**3. À détecter plus tôt** : (a) D29 (compteurs kill-switch en mémoire) aurait dû
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
  compteurs de risque (D29).
- **Production 70** (+12) : CI (22), trades enfin persistés et dashboard alimenté
  (21), journalisation unifiée vers SQLite. Le bot est observable et reproductible.
  Manquent : test E2E du main, persistance des compteurs kill-switch (D29).

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
