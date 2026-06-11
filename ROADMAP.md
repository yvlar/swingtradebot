# ROADMAP — SwingBot C++

> **Source de vérité du workflow.** Ce fichier est lu par `prompt-executer-sprint.md`
> (exécution du sprint courant) et mis à jour via `prompt-mise-a-jour-roadmap.md`
> (clôture de sprint, re-priorisation, rétrospective). Ne pas le modifier à la main
> en dehors de ce cycle, sauf pour ajouter une découverte.

## Tableau de bord

| Dimension    | Note /100 | Baseline (audit 2026-06-10) |
|--------------|-----------|------------------------------|
| Architecture | 84        | 68                           |
| Qualité      | 87        | 60                           |
| FinTech      | 76        | 38                           |
| Production   | 71        | 35                           |

- **Dernière mise à jour** : 2026-06-11 (clôture Sprint 6 — vérité du backtest & réalisme)
- **Sprint courant** : Sprint 7 — Harnais de validation (prouver l'edge)

> ### ⚠️ Rentabilité : NON PROUVÉE — le bot ne fait pas (encore) d'argent
> Les notes ci-dessus mesurent la **sûreté** et la **correction** du moteur, pas sa
> capacité à gagner de l'argent. Depuis le Sprint 6, la mesure dit enfin la vérité
> (coûts réalistes, métriques d'objectif, config prod backtestée) — et la vérité
> reste mauvaise : sur QQQ.csv (~2019→2026), la config défaut rend **+9,53 %** et
> la config de production **+36,32 %** quand **Buy & Hold rend +238,55 %** — un
> alpha de **−229** et **−202 points** respectivement. Bonne nouvelle du Sprint 6 :
> la config prod (jamais backtestée jusqu'ici, D21) s'avère MEILLEURE que la config
> défaut (Sharpe 1,81 vs 0,61, 11 trades dont 10 gagnants) ; mauvaise nouvelle : le
> B&H est encore SOUS-ESTIMÉ car QQQ.csv ne porte aucun dividende (D29). Aucun edge
> n'est démontré hors-échantillon — c'est l'objet des **Sprints 7-8**.
>
> | Dimension     | Note /100 | Justification |
> |---------------|-----------|---------------|
> | **Rentabilité** | **20**  | La config prod est désormais MESURÉE honnêtement (+36,32 % net de coûts, alpha −202 pts) et bat la config défaut — mais aucune validation hors-échantillon, et le B&H de référence est encore sous-estimé (dividendes absents, D29). |
- **État des tests** : 396/396 verts (346 unitaires + 50 intégration). +18 ce
  sprint (378 → 396), aucune dérive hors cycle (`ctest -N` valait bien 378 à
  l'ouverture). Détail au changelog Sprint 6.
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

## 🔵 SPRINT 5 — Durcissement production ✅ (clos le 2026-06-11)

> Sprint orienté FinTech/Production : protections runtime réelles (et non plus
> seulement testées). Ordre réalisé 22 → 20 → 18 → 21 → 19 : la CI en premier
> (sécurise tout), puis le calendrier (indépendant), le kill-switch (gate
> d'entrée), la persistance (consomme les fills), enfin les stops broker.
> Décisions utilisateur arbitrées à l'ouverture : kill-switch « modéré »
> (5% / 4 / 10), stop résident « doubler » (logiciel primaire + filet broker).

- [x] **18.** Kill-switch dans `IRiskManager` → `a15cbb3`
  `KillSwitchConfig` (Models.hpp, profil modéré : drawdown journalier 5%, 4 pertes
  consécutives, 10 ordres/jour ; seuil ≤ 0 = désactivé) + `checkKillSwitch` (prédicat
  pur, IRiskManager + RiskManager.hpp). `TradingBot` suit les compteurs runtime
  (série de pertes, ordres du jour, équité de référence réinitialisée au changement
  de jour de bourse) et coupe la branche d'entrée de `runOnce` — position existante
  laissée à ses stops. Tests rouges : 8 RiskManagerUnit + 3 TradingBotUnit. Golden
  inchangé (les seuils modérés ne se déclenchent jamais sur QQQ.csv).
- [x] **19.** Stops côté broker (stop résident) + D15 → `6188e3c`
  Décision « doubler » : `IBroker::submitStopLoss`/`cancelStopLoss` (virtuels, no-op
  par défaut → PaperBroker/backtest inchangés, golden figé) ; IBKRBroker dépose un
  STP de vente GTC (cOID tag STOP distinct) et l'annule par DELETE à la sortie ;
  TradingBot le pose après un achat confirmé (fill × (1-stopLossPct)) EN COMPLÉMENT
  du stop logiciel primaire et l'annule à la sortie (pas de stop orphelin — réduit
  D14). D15 : `client_order_id` idempotent Alpaca (granularité horaire, schéma du
  cOID IBKR). Tests : 3 IbkrBrokerUnit + 1 AlpacaBrokerUnit + 3 TradingBotUnit.
- [x] **20.** Calendrier de marché UTC/DST → `b16a10f`
  `core/market_calendar.h` (fonctions pures) : règle DST US officielle (2e dimanche
  de mars → 1er dimanche de novembre), séance 9h30-16h00 ET, tout en UTC. Remplace le
  repli UTC-5 fixe d'IBKRDataFeed (faux 8 mois/an, ouvrait dès 9h00). Tests rouges sur
  l'ancien calcul (été : 13h30 UTC ouvert mais vu fermé ; clôture symétrique). 8
  MarketCalendarUnit.
- [x] **21.** Persistance des trades en prod + dashboard → `5f3c044`
  `TradingBot::setTradeObserver`/`TradeFill` émis sur chaque fill confirmé (achat
  ouvrant, vente clôturant, prix réel + P&L + raison). `main_ibkr` y branche
  `record_trade`/`close_trade` (sl/tp dérivés de la config) et alimente
  `BotState::positions`. Journalisation unifiée (`trading::ILogger` ↔ `DbLogger`) via
  CompositeLogger console + `DbLogSink`. Tests : 2 TradingBotUnit.
- [x] **22.** Pipeline CI GitHub Actions → `70b03fd`
  `.github/workflows/ci.yml` : build ubuntu-24.04 paquets système (fallback D11,
  `apt-get update` obligatoire), cmake/ninja Debug, `ctest` à chaque push/PR. Aurait
  attrapé le CMakeLists vcpkg-only, le gitignore mort et la dérive D20.

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
| E1 | **La config de prod n'est pas celle qui est backtestée.** `main_ibkr.cpp:122-132` câble EMA 13/21, RSI 65/80, SL 7 %, TP 15 %, minHold 2 ; le golden valide les **défauts** de `SwingConfig` (9/21, 55/70, 5 %/10 %, 3). Le live tourne sur des paramètres **jamais validés**. | `main_ibkr.cpp:122` vs `SwingStrategy.hpp:11` | Externaliser la config (JSON validé) + golden sur la config de prod → **Sprint 6.1 / 9.1** (D21) |
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

# 🟣 SPRINT 6 — Vérité du backtest & réalisme ✅ (clos le 2026-06-11)

> **On ne peut pas améliorer ce qu'on mesure mal.** Avant toute refonte de stratégie,
> le backtest doit dire la vérité : config réelle, coûts réels, dividendes, et une
> métrique d'objectif explicite. Ordre réalisé : 6.1 → 6.4 → 6.2 → 6.3, conforme au
> plan (6.1/6.4 sans effet golden, 6.2 re-fige les goldens, 6.3 delta nul — D29).

- [x] **6.1** (D21) Config de production backtestée → `146e362`
  `ProdConfig.hpp` = source UNIQUE de la config live (consommée par main_ibkr.cpp
  ET par le golden — modifier les paramètres live casse le golden). 2e golden figé :
  +36,50 % (re-figé +36,3189 % après 6.2), 11 trades (10G/1P), Sharpe 1,81. Test
  côte à côte défaut vs prod : la prod SURPERFORME le défaut → la « Décision
  requise » (prod sous-performante) ne s'est PAS déclenchée ; le test verrouille
  la relation et signalera toute inversion future.
- [x] **6.4** (D23) Métriques d'objectif → `9803349`
  `BacktestResult` : cagrPct (durée réelle via dates d'équité), sortinoRatio,
  calmarRatio, pctTimeInvested (mesure du cash drag T4), beatsBuyHold (verdict
  net de coûts). `printReport` affiche « OBJECTIF — BAT-ON LE BUY & HOLD ? ».
  9 tests unitaires sur séries synthétiques calculées à la main.
- [x] **6.2** (D22) Modèle de coûts réaliste → `713d2d8`
  `PaperBroker(capital, commission, slippageBps, halfSpreadBps)` : fills dégradés
  défavorablement (achat au-dessus du close, vente en dessous). Défauts Backtester
  conservateurs : 2 bps + 0,5 bp par côté. Test rouge d'acceptation (mêmes trades,
  capital inférieur). Goldens re-figés au même commit : défaut +9,6706 → +9,5289 %
  (−14,17 $), prod +36,5015 → +36,3189 % (−18,26 $) — coût pur, zéro signal modifié.
- [x] **6.3** (D7) Rendement total via Adj Close → `0f3f5d6`
  `CsvDataFeed` sert Adj Close (stratégie ET B&H), open/high/low mis à l'échelle
  par le facteur d'ajustement (ranges/stops cohérents). Test rouge : inversion du
  verrou UsesCloseNotAdjClose. Goldens INCHANGÉS et documentés : QQQ.csv a
  Adj Close == Close sur 1858/1858 lignes → delta structurellement nul (D29, le
  vrai correctif est un ré-export total-return, affecté au Sprint 7).

# 🟣 SPRINT 7 — Harnais de validation (prouver l'edge) — **sprint courant**

> Aucune confiance dans un paramètre sans validation hors-échantillon. Ce sprint
> construit l'outillage qui permettra de juger toute modif du Sprint 8 **honnêtement**.
> Dépend du Sprint 6 (coûts/métriques justes) — satisfait. Point d'entrée du code :
> `Backtester::run()` (`include/backtest/BackTester.hpp:110`) et les métriques de
> `computeMetrics` (`BackTester.hpp:265`). Ordre conseillé **7.1 → 7.4 → 7.2 → 7.3** :
> le walk-forward (7.1) est le socle de jugement ; les données multi-actifs (7.4,
> avec le ré-export total-return D29) doivent exister AVANT de lancer la grille
> (7.2) pour ne pas optimiser sur un seul actif sans dividendes ; le bootstrap
> (7.3) consomme les trades produits par 7.1/7.2.

- [ ] **7.1** (D24) Split in-sample / out-of-sample + **walk-forward** sur QQQ.csv :
  fenêtres glissantes (`Backtester` rejoué sur des sous-périodes du CSV — la fenêtre
  bornée de `ReplayDataFeed`, BackTester.hpp:25, sert déjà des sous-séries).
  **Acceptation** : rapport IS vs OOS par fenêtre (retour, alpha, Sharpe/Sortino de
  6.4) ; un edge qui ne tient qu'en IS est explicitement signalé « sur-ajusté ».
- [ ] **7.4** (D29) **Multi-actifs + données total-return** : charger plusieurs CSV
  (ex. SPY, IWM, MDY) et ré-exporter des séries où `Adj Close ≠ Close` (dividendes
  réels). Ajouter un garde-fou de qualité de données : avertir si un CSV a
  `Adj Close == Close` sur toutes ses lignes (symptôme D29). **Acceptation** : la
  stratégie est évaluée sur ≥ 3 actifs, dividendes comptés, garde-fou testé.
- [ ] **7.2** Optimiseur de grille de paramètres (`SwingConfig` : emaFast/emaSlow/
  rsiBuyMax/rsiSellMin/SL/TP) avec **sélection robuste** (plateau de performance,
  pas le pic isolé). **Acceptation** : carte de sensibilité des paramètres ; le choix
  retenu est un plateau (voisinage stable), jugé en OOS (7.1), jamais en IS.
- [ ] **7.3** **Monte-Carlo / bootstrap** des trades → distribution de CAGR et de
  drawdown (pas un seul chemin). **Acceptation** : p5/p50/p95 du drawdown et du
  retour sur les trades du backtest, avec graine aléatoire fixée (reproductible).

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
| D7 | ✅ | CsvDataFeed utilise `Close` et non `Adj Close` (CsvDataFeed.hpp:119) → dividendes QQQ ignorés dans le backtest et la comparaison Buy&Hold | Corrigé au Sprint 6 (6.3, `0f3f5d6`) — mécanisme en place, mais delta nul sur le QQQ.csv actuel (voir D29) |
| D8 | 🟢 | `volatile bool g_running` dans les handlers de signaux (main_ibkr.cpp:32, main_v2, main_alpaca) — devrait être `volatile std::sig_atomic_t` | Sprint 3 (item 14) |
| D9 | 🟢 | `using json = nlohmann::json;` au scope global dans bot_state.h:18 (pollution de tous les TU qui l'incluent) | Sprint 3 (item 14) |
| D10 | 🟢 | `IBKRBroker::lastError_` écrit mais jamais exposé ; `fetchFirstAccountId` ignore le code retour curl (IBKRBroker.hpp:101) | Sprint 2 (item 8) |
| D11 | ✅ | CMakeLists exigeait `unofficial-sqlite3` (config vcpkg uniquement) → fallback `find_package(SQLite3)` système ajouté, build/tests possibles hors conteneur vcpkg | Corrigé (commit `build:`) |
| D12 | 🟢 | `QQQv1.csv` non référencé par le code (donnée morte) | Sprint 3 (item 14) |
| D13 | ✅ | (Sprint 1) Le `gitignore` sans point a fait committer `build/` par accident pendant le sprint (commit amendé) — preuve vivante de l'item 14 | Corrigé (`52f9c95`) |
| D14 | ✅ | (Sprint 1) Risque résiduel : vente PENDING avec position encore visible au cycle suivant → re-tentative de vente possible. Mitigé par le cOID horaire (1 ordre/side/heure) ; le stop résident de l'item 19, annulé à la sortie, et la réconciliation `getPosition` (qui empêche un `submitSell` sur position absente) bornent le risque | Adressé au Sprint 5 (item 19, `6188e3c`) |
| D15 | ✅ | (Sprint 2) Le retry du HttpClient (item 8) peut re-poster un ordre déjà reçu par le serveur. IBKR est protégé par le cOID idempotent (Sprint 1, item 4) ; **AlpacaBroker n'envoyait aucun `client_order_id`** → double-ordre possible sur retry | Corrigé au Sprint 5 (item 19, `6188e3c` — `client_order_id` horaire idempotent sur achat/vente) |
| D16 | ✅ | (Sprint 2) `IDataFeed::getLatestPrice` n'a aucun consommateur (aucun appel hors implémentations) — converti à `Result` par cohérence, mais c'est une méthode d'interface morte : la supprimer ou la consommer | Corrigé au Sprint 3 (`356ba90` — supprimée, 5 implémentations retirées) |
| D17 | ✅ | (Sprint 3) `Logger.hpp` utilisait `std::shared_ptr`/`std::vector` sans inclure `<memory>`/`<vector>` — ne compilait que par inclusion transitive (détecté en compilant le header isolément pour le golden) | Corrigé au Sprint 3 (`356ba90`) |
| D18 | 🟡 | (Sprint 3) L'ATR de l'item 13 est une **approximation clôture-à-clôture** : `IIndicator<double>` ne reçoit que la série des clôtures, pas les high/low — le vrai true range est inaccessible via cette interface. Décision produit : enrichir l'interface (compute sur `vector<Bar>`) ou assumer l'approximation (documentée dans DayIndicators.hpp) | Backlog (décision produit, requis avant tout usage réel de DayTradeStrategy) |
| D19 | 🟢 | (Sprint 3) `TradingBot::runOnce` code en dur `getBars(symbol, 60)` (TradingBot.hpp:62) alors que le backtest sert une fenêtre de emaSlow+30=51 barres via ReplayDataFeed — la taille de fenêtre influence le seed SMA des EMA, donc les signaux. Bénin tant que les feeds prod renvoient ≥51 barres, mais un `lookback` configurable (RiskConfig ?) unifierait prod et backtest | **Re-priorisé Sprint 9 (9.2)** : l'item 20 du Sprint 5 s'est limité au calendrier/DST (aucun changement de fenêtre de données) ; le lookback unifié appartient à la mise en prod de la stratégie validée (E5), pas au durcissement runtime |
| D20 | 🟢 | (Sprint 4) Dérive ROADMAP ↔ dépôt : un lot « couverture des fondations » (commit `15eb711`, +~2528 lignes : brokers IBKR/Alpaca, PaperBroker, CsvDataFeed, métriques backtest, Logger, indicateurs) a été mergé hors du cycle `prompt-executer-sprint`. Le décompte de tests du tableau de bord (198) ne le reflétait pas (réel : 344 avant ce sprint). Aucune perte — la couverture est légitime et verte — mais le tableau de bord a menti pendant un sprint. **Garde-fou ajouté** : `prompt-executer-sprint.md` étape 2 exige désormais de recaler « État des tests » sur la sortie réelle de `ctest -N` et de signaler toute dérive ; `prompt-mise-a-jour-roadmap.md` rappelle d'absorber au changelog tout commit mergé hors cycle. La CI (item 22) reste le vrai remède de fond | Corrigé (workflow amendé ce sprint) |
| D21 | 🔴 | (Méta-audit) **La config qui tourne en prod n'est pas celle validée par le golden.** `main_ibkr.cpp:122-132` : EMA 13/21, RSI 65/80, SL 7 %, TP 15 %, minHold 2 — alors que le golden (item 17) valide les défauts de `SwingConfig` (9/21, 55/70, 5 %/10 %, 3). Le live trade des paramètres **jamais backtestés** : leur performance et leurs stops sont inconnus. Risque financier direct | ✅ Côté mesure : corrigé au Sprint 6 (6.1, `146e362` — ProdConfig.hpp source unique + golden prod, qui s'avère MEILLEURE que la config défaut). L'externalisation JSON reste au Sprint 9 (9.1) |
| D22 | 🟠 | (Méta-audit) Backtest optimiste : `PaperBroker` exécute au close **sans slippage**, commission seule (`PaperBroker.hpp:29-31,47,73`). Tout edge mesuré est surévalué ; en prod les fills IBKR sont au marché. Un edge marginal peut être négatif net de slippage/spread | ✅ Corrigé au Sprint 6 (6.2, `713d2d8` — fills dégradés, défauts 2 + 0,5 bps ; à réexaminer par actif au Sprint 7.4) |
| D23 | 🟠 | (Méta-audit) Pas d'objectif de performance explicite. « Faire de l'argent » = **alpha net vs Buy & Hold** + drawdown maîtrisé, pas « retour positif ». Le rapport (`BackTester.hpp`) ne tranche pas « bat-on QQQ net de coûts ? ». Manquent CAGR, Sortino, Calmar, % temps investi | ✅ Corrigé au Sprint 6 (6.4, `9803349` — métriques + verdict beatsBuyHold affiché dans le rapport) |
| D24 | 🟠 | (Méta-audit) **Aucune validation hors-échantillon.** Paramètres = nombres magiques, validés sur un seul actif (QQQ) et un seul régime (~2018-2026, quasi 100 % haussier). Pas d'IS/OOS, pas de walk-forward, pas de Monte-Carlo, pas de multi-actifs → edge non démontré, risque de sur-ajustement | Sprint 7 |
| D25 | 🟡 | (Méta-audit) Prod : boucle 60 min sur barres **journalières** → la dernière barre n'est pas clôturée, le croisement EMA peut osciller intra-journée (flap/look-ahead absent du backtest qui ne voit que des barres complètes) | Sprint 9 (9.3) |
| D26 | 🔴 | (Méta-audit) **Défauts structurels de la stratégie (cause racine de l'alpha −229 pts)** : take-profit fixe qui ampute les gagnants (`RiskManager.hpp:81`) ; vente sur RSI > 70 qui sort des tendances haussières (`SwingStrategy.hpp:109`) ; filtre d'entrée contradictoire croisement-haussier + `RSI<55` (`SwingStrategy.hpp:96-99`) → 7 trades/5 ans ; aucun filtre de régime ; long-only mono-actif → cash drag massif | Sprint 8 (après le harnais Sprint 7) |
| D27 | 🟡 | (Sprint 5) **Le stop résident broker (item 19) n'est posé qu'à l'ENTRÉE, pas à l'adoption.** `reconcilePosition_` (TradingBot.hpp) adopte une position broker non suivie (redémarrage) avec le stop logiciel, mais ne dépose aucun stop résident — une position adoptée n'est protégée côté broker que si elle avait été ouverte par ce process. Bénin tant que le stop logiciel tourne ; à combler pour une vraie symétrie entrée/adoption | Sprint 9 (mise en prod) ou backlog |
| D28 | 🟢 | (Sprint 5) Le drawdown journalier du kill-switch (item 18) est **inerte en backtest** : un `runOnce` = une barre journalière, donc `dayStartEquity` se recale à chaque cycle et le drawdown intra-séance vaut toujours ~0. Voulu (préserve le golden) et correct en prod (boucle 60 min, plusieurs cycles/jour), mais signifie que ce garde-fou précis n'est jamais exercé par le golden — seuls les tests unitaires purs le couvrent | Documenté (couvert par RiskManagerUnit) |
| D29 | 🟠 | (Sprint 6) **QQQ.csv ne porte aucune information de dividende** : `Adj Close == Close` sur les 1858 lignes (export sans ajustement). Le code 6.3 utilise bien Adj Close (stratégie ET B&H, OHL mis à l'échelle), mais le delta est NUL sur ce dataset — le B&H +238,55 % reste hors dividendes, et l'alpha réel est donc encore PIRE que mesuré (~+0,6 pt/an de dividendes QQQ non comptés). Ré-exporter un CSV total-return (Yahoo Finance, Close non ajusté + Adj Close) pour que 6.3 produise son effet | Sprint 7 (avec le chargement multi-CSV de 7.4) |

## Changelog

### Sprint 6 — Vérité du backtest & réalisme (2026-06-11)

**Baseline réelle à l'ouverture** : **378/378 verte**, conforme au tableau de bord
(`ctest -N` = 378, aucune dérive hors cycle).

**Commits** (ordre chronologique = ordre d'exécution 6.1 → 6.4 → 6.2 → 6.3) :
- `146e362` test(backtest) : golden de la config de production + source unique ProdConfig (item 6.1, D21)
- `9803349` feat(backtest) : métriques d'objectif — CAGR, Sortino, Calmar, temps investi, verdict vs B&H (item 6.4, D23)
- `713d2d8` feat(backtest) : modèle de coûts réaliste — slippage + demi-spread dans PaperBroker (item 6.2, D22)
- `0f3f5d6` feat(data) : rendement total — CsvDataFeed sert Adj Close, OHL mis à l'échelle (item 6.3, D7)

**Tests** : 378 → **396** (+18). Étendues : `BacktesterIntegration` 2 → 5 (+2 goldens
prod, +1 côte à côte défaut/prod), `BacktesterMetricsUnit` +9 (CAGR dates réelles +
repli, Sortino déviation basse + sentinelles, Calmar + sentinelle, % temps investi,
verdict, rapport), `PaperBrokerUnit` +4 (fills dégradés achat/vente, acceptation
« mêmes trades capital inférieur », compat slippage nul), `CsvDataFeedUnit` +2 net
(verrou UsesCloseNotAdjClose INVERSÉ en UsesAdjCloseForTotalReturn + mise à l'échelle
OHL + cas Adj == Close).

**Valeurs golden re-figées** (Sprint 6.2, justification : coûts réalistes 2 bps
slippage + 0,5 bp demi-spread par côté, mêmes trades, coût pur sans changement de
signal) :
- Config défaut : retour total **+9,5289 %** (était +9,6706), capital final
  **10 952,89 $** (−14,17 $), 7 trades inchangés (4G/3P, 0 SL / 1 TP / 1 trailing /
  5 signal), max DD **2,0647 %**, Sharpe **0,6138**, B&H +238,55 % inchangé.
- Config prod (NOUVEAU golden, item 6.1) : retour total **+36,3189 %**, capital
  final **13 631,89 $**, 11 trades (10G/1P, 0 SL / 4 TP / 2 trailing / 5 signal),
  max DD **1,9845 %**, Sharpe **1,8099**, 1er achat 2019-06-18.
- Sprint 6.3 (Adj Close) : delta **nul** — QQQ.csv a `Adj Close == Close` sur ses
  1858 lignes (D29) ; le mécanisme est en place et testé sur CSV synthétique.

**Nouveaux fichiers** : `include/strategies/ProdConfig.hpp`.
**Interfaces modifiées** (toutes rétro-compatibles, paramètres par défaut) :
- `PaperBroker(capital, commission, slippageBps = 0, halfSpreadBps = 0)` — défauts
  0/0 = ancien comportement (mocks/tests existants inchangés).
- `Backtester(cfg, csv, capital, commission, slippageBps = 2.0, halfSpreadBps = 0.5)`
  — les coûts réalistes sont le DÉFAUT du backtest.
- `BacktestResult` : +cagrPct, +sortinoRatio, +calmarRatio, +pctTimeInvested,
  +beatsBuyHold (additifs).
- `main_ibkr.cpp` ne câble plus la config en dur : `trading::prodSwingConfig()`.

**Enseignement central du sprint** : la config prod, jamais validée jusqu'ici,
s'avère NETTEMENT meilleure que la config défaut (+36,32 % vs +9,53 %, Sharpe 1,81
vs 0,61) — le risque D21 était réel mais dans le bon sens. Aucune des deux ne bat
le Buy & Hold (+238,55 %, encore sous-estimé sans dividendes — D29).

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

### Sprint 5 — Durcissement production (2026-06-11)

**Baseline réelle à l'ouverture** : **350/350 verte**, conforme au tableau de bord
(`ctest -N` = 350, aucune dérive hors cycle cette fois — D20 ne s'est pas reproduit).

**Commits** (ordre chronologique = ordre d'exécution 22 → 20 → 18 → 21 → 19) :
- `70b03fd` ci : pipeline GitHub Actions build Linux + ctest à chaque push (item 22)
- `b16a10f` fix(calendar) : heures de marché US en UTC avec DST (item 20)
- `a15cbb3` feat(risk) : kill-switch — coupe les entrées sur dérive de séance (item 18)
- `5f3c044` feat(prod) : persistance des trades + dashboard câblés en prod (item 21)
- `6188e3c` feat(broker) : stop résident broker + client_order_id Alpaca (item 19 + D15)

**Tests** : 350 → **378** (+28). Nouvelle suite : `MarketCalendarUnit` (8). Étendues :
`RiskManagerUnit` 20 → 28 (+8 kill-switch), `TradingBotUnit` 28 → 36 (+8 : 3
kill-switch, 2 observateur de trades, 3 stop résident), `IbkrBrokerUnit` 14 → 17
(+3 stop résident STP/DELETE), `AlpacaBrokerUnit` +1 (client_order_id idempotent).

**Nouveaux fichiers** : `.github/workflows/ci.yml`, `include/core/market_calendar.h`,
`tests/unit/test_market_calendar_unit.cpp`.
**Interfaces modifiées** :
- `IRiskManager::checkKillSwitch(KillSwitchConfig, dayStartEquity, currentEquity,
  consecutiveLosses, ordersToday)` (nouvelle, pure) ; `KillSwitchConfig` ajouté à
  `RiskConfig` (Models.hpp).
- `IBroker::submitStopLoss`/`cancelStopLoss` (nouvelles, **virtuelles non pures** —
  no-op par défaut → PaperBroker/Mock/backtest inchangés, golden figé ; seul
  IBKRBroker les surcharge).
- `TradingBot::setTradeObserver`/`TradeFill` (seam de journalisation prod).
- `AlpacaBroker` : `client_order_id` ajouté au corps des ordres (D15).
Les composition roots non compilés (main_alpaca, main_v2) restent compatibles
source (méthodes IBroker à valeur par défaut, signatures existantes inchangées).

**Golden backtest inchangé** : +9,6706 %, 10 967,06 $, 7 trades (4G/3P, 0 SL / 1 TP /
1 trailing / 5 signal), max DD 2,0262 %, Sharpe 0,6229. Critère central du sprint :
chaque protection runtime (kill-switch, stop résident) est neutre sur PaperBroker, donc
ne fausse pas la mesure de performance.

**DoD** : rebuild propre 0 warning ; 378/378 verts ; chaque bug avec test rouge
préalable (calendrier DST, kill-switch, observateur, stops) ou test de comportement
nommé ; golden non régressé ; commentaires/logs en français ; aucun secret committé ;
5 commits atomiques (1 par item). La **CI (item 22) tourne désormais à chaque push** —
fin de l'ère « qualité déclarative ».

## Rétrospectives

### Sprint 6 — Vérité du backtest & réalisme (2026-06-11)

**1. Découpage** : bon calibre (4 items cohérents autour d'un seul thème : la
mesure). L'ordre conseillé 6.1 → 6.4 → 6.2 → 6.3 a tenu exactement comme prévu :
les deux items « sans effet golden » d'abord, puis les deux qui le déplacent,
chacun re-figeant les goldens dans SON commit avec delta chiffré. Aucune
dépendance ratée. La « Décision requise » conditionnelle de 6.1 (prod
sous-performante ?) avait été bien anticipée comme CONDITIONNELLE : la condition
ne s'est pas réalisée (la prod surperforme), donc aucun blocage utilisateur —
formuler les décisions avec leur condition de déclenchement évite de solliciter
l'utilisateur pour rien. Petit dépassement de périmètre assumé sur 6.1 :
l'extraction de `ProdConfig.hpp` (source unique) n'était pas demandée
explicitement mais c'est le seul moyen de garantir « aucune config non
backtestée ne part en live » sans attendre 9.1 — consigné comme acquis, 9.1
reste nécessaire pour l'externalisation JSON.

**2. Suffisance des prompts** : aucune improvisation de workflow. La liste apt,
le recalage `ctest -N` (étape 2) et la discipline du test rouge se sont appliqués
tels quels (rouges réels : champs inexistants pour 6.4, constructeur inexistant
pour 6.2, verrou inversé pour 6.3 ; verrou de comportement nommé pour le golden
6.1, conforme à l'esprit « idéalement »). **Aucune modification des prompts
nécessaire.**

**3. À détecter plus tôt** : (a) **D29 est la leçon du sprint** — personne n'avait
vérifié que QQQ.csv portait réellement l'information de dividende avant de
planifier 6.3 ; une inspection d'une ligne (`Adj Close == Close`) l'aurait
révélé au méta-audit. Garde-fou ajouté au backlog (7.4) : le chargeur multi-CSV
avertira si `Adj Close ≡ Close` sur tout un fichier — la qualité des DONNÉES
doit être testée comme le code. (b) Le « delta golden » de 6.2 (−14 $ sur
10 953) est si faible qu'il valide rétroactivement le choix de QQQ comme actif
(coûts négligeables) — mais ce ne sera PAS vrai pour des actifs moins liquides
du Sprint 7.4 : les bps par défaut devront être réexaminés par actif.

**4. Notes** (précédent 83/86/74/70, Rentabilité 15) :
- **Architecture 84** (+1) : source unique de la config prod (ProdConfig.hpp),
  modèle de coûts paramétrable proprement injecté, métriques additives sans
  casser d'interface. Plafonné par D18 (indicateurs `vector<Bar>`, Sprint 8.0)
  et l'externalisation config (9.1).
- **Qualité 87** (+1) : +18 tests dont 3 vrais rouges et 2 goldens re-figés avec
  justification chiffrée dans le commit même ; le côte à côte défaut/prod est un
  verrou de relation, pas seulement de valeurs. Plafonné par l'absence de
  validation OOS (le golden reste un seul chemin sur un seul actif).
- **FinTech 76** (+2) : le backtest ne flatte plus l'edge — coûts défavorables
  par défaut, objectif explicite (« bat-on le B&H net de coûts ? » : NON, affiché),
  et la config qui trade de l'argent réel est enfin celle qui est mesurée (D21
  fermé côté mesure). C'est de l'intégrité de mesure, le cœur FinTech.
- **Production 71** (+1) : la prod ne câble plus de paramètres en dur non
  validés ; tout changement de config live casse la CI via le golden. Le reste
  de la marche (config externalisée, barres clôturées, lookback unifié) est au
  Sprint 9.
- **Rentabilité 20** (+5) : aucune stratégie n'a changé, mais la MESURE est
  désormais digne de confiance et la config prod réelle est connue : +36,32 %
  net de coûts, Sharpe 1,81 — meilleure que la config défaut, toujours −202 pts
  d'alpha vs un B&H lui-même sous-estimé (D29). Le chiffre est honnête ; il
  reste mauvais.

### Sprint 5 — Durcissement production (2026-06-11)

**1. Découpage** : bon calibre (5 items, dont 2 « Décision requise »). L'ordre
recommandé par le plan de sprint (22 CI en premier, puis les autres) a tenu sans
dépendance ratée. Une seule dépendance souple — l'item 21 « gagne à passer après
l'item 18 » — a été respectée (18 avant 21) mais s'est révélée inexistante en
pratique : le kill-switch ne produit pas d'événement consommé par la persistance,
les deux sont orthogonaux. Les décisions utilisateur ont été demandées **à
l'ouverture** (kill-switch « modéré », stop « doubler ») plutôt qu'au moment de
chaque item : cela a évité tout blocage en cours de sprint — anticiper les
décisions, comme au Sprint 3, fonctionne. L'item 19 était le plus gros (interface
IBroker + IBKR + TradingBot + D15) mais a tenu sans découpage grâce au choix
« virtuel non pur no-op » qui a isolé le changement des autres brokers.

**2. Suffisance des prompts** : aucune improvisation de workflow cette fois. La
liste apt (préfixée `apt-get update`, leçon Sprint 3) et le fallback SQLite système
(D11) ont permis baseline + build sans accroc. Le « test rouge » strict s'est
appliqué proprement là où un bug réel existait (calendrier DST : un test reproduit
l'ancien calcul UTC-5 et montre la divergence ; D15) ; pour les garde-fous de
non-régression (kill-switch, stop résident) les tests valent comme verrous de
comportement nommés, conforme à l'esprit « idéalement » du prompt. **Aucune
modification des prompts nécessaire** — le workflow s'est exécuté tel quel.

**3. À détecter plus tôt** : (a) la CI (item 22) est enfin en place — c'était le
garde-fou « le moins cher non installé » répété à chaque rétro depuis le Sprint 1 ;
il rend désormais le tableau de bord auto-vérifiable (un workflow qui publie le
décompte `ctest` à chaque merge aurait rendu la dérive D20 impossible). C'est le
remède de fond, pas un palliatif. (b) D27 (le stop résident ne couvre pas les
positions ADOPTÉES, seulement les entrées fraîches) aurait dû être anticipé à la
conception de l'item 19 — la symétrie entrée/adoption est un invariant déjà appris
au Sprint 1 (réconciliation). Consigné, affecté à la mise en prod. (c) D28 (le
drawdown journalier du kill-switch est inerte en backtest) est une limite
structurelle du golden mono-cycle/jour : un futur harnais intra-journalier (Sprint 7)
l'exercerait ; en attendant les tests unitaires purs le couvrent.

**4. Notes** (précédent 81/84/66/58) :
- **Architecture 83** (+2) : `IBroker` enrichi proprement (stops résidents en
  virtuels no-op — extension sans casser les implémentations existantes ni le
  golden), seam `TradeFill` qui découple la persistance du moteur, calendrier extrait
  en module pur réutilisable. Reste : D18 (interface indicateurs `vector<Bar>`), D19
  (lookback unifié, repoussé Sprint 9). Plafonné par la dette stratégie (Sprints 6-8)
  qui touchera l'architecture des signaux.
- **Qualité 86** (+2) : +28 tests ciblés, **CI active** (fin de la qualité
  déclarative — chaque push est vérifié), golden toujours vert sous les nouvelles
  protections. Plafonné tant que la couverture des composition roots (main_ibkr,
  non testé) repose sur la relecture.
- **FinTech 74** (+8) : premières protections runtime RÉELLES et non plus seulement
  testées — kill-switch (coupe les entrées sur dérive), stop résident broker (filet
  hors-ligne), calendrier de marché correct (plus de trade à la mauvaise heure 8
  mois/an), idempotence Alpaca (D15). Pour dépasser 80, il faut la rentabilité
  (Sprints 6-8), pas plus de sûreté.
- **Production 70** (+12) : le plus gros saut du sprint. Trades enfin persistés et
  dashboard alimenté en prod (item 21), CI qui garde la prod alignée sur les tests
  verts (item 22), horodatages unifiés en UTC. Manque, pour aller plus loin :
  externalisation de la config (E1/D21, Sprint 9.1), procédure de recalibration
  (Sprint 9.4), et surtout une stratégie qui gagne de l'argent.

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
