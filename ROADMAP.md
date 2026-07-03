# ROADMAP — SwingBot C++

> **Source de vérité du workflow.** Ce fichier est lu par `prompt-executer-sprint.md`
> (exécution du sprint courant) et mis à jour via `prompt-mise-a-jour-roadmap.md`
> (clôture de sprint, re-priorisation, rétrospective). Ne pas le modifier à la main
> en dehors de ce cycle, sauf pour ajouter une découverte.

## Tableau de bord

| Dimension    | Note /100 | Baseline (audit 2026-06-10) |
|--------------|-----------|------------------------------|
| Architecture | 88        | 68                           |
| Qualité      | 92        | 60                           |
| FinTech      | 84        | 38                           |
| Production   | 72        | 35                           |

- **Dernière mise à jour** : 2026-07-03 (Sprint 8-ter — validation hors-grille du candidat d'edge : candidat RÉFUTÉ sur les 3 volets (fenêtres non-choisies, Monte-Carlo, grille resserrée) → « candidat non confirmé » consigné, aucune adoption, prod reste paper. Nouveau mécanisme additif : offset de départ du pavage WalkForward)
- **Sprint courant** : Sprint 8-quater — Trailing adaptatif ATR (ré-ouverture de 8b.4 : gate OUVERT depuis B2 — trailingStopPct est l'axe le plus sensible — et 8t.3 confirme que le trailing FIXE est l'axe fragile du plateau) — décision utilisateur du 2026-07-03

> ### ⚠️ Rentabilité : le premier candidat d'edge (8b.1) est RÉFUTÉ hors-grille (Sprint 8-ter)
> Les notes ci-dessus mesurent la **sûreté** et la **correction** du moteur, pas sa
> capacité à gagner de l'argent. Le Sprint 8-ter a fait passer au candidat post-B2
> (emaFast=9, smaT=250, trail=0,03 — alpha OOS +0,23 sur SES fenêtres de grille)
> une validation dédiée HORS de la grille qui l'a choisi. Verdict triple, tous
> verrouillés par test : **8t.1** — sur le pavage canonique (fenêtres jamais vues
> par la grille) le candidat rend **−19,10 pt vs −9,90** pour la chaîne (pire de
> 9,2 pts), et le signe s'INVERSE sur le pavage décalé (−5,38 vs −12,83) : un
> avantage qui dépend du choix des fenêtres n'est pas un edge. **8t.2** — Monte-
> Carlo des trades OOS : CAGR p50 **4,41 % < 6,60 %** (seul acquis : DD p95
> 11,71 % vs 16,48 %). **8t.3** — la grille resserrée autour du candidat dérive
> vers (9, **275**, **0,04**) : même l'axe « stable » (smaT, D36) bouge avec la
> maille — artefact de sélection. **Conclusion (8t.4, branche « sinon ») :
> candidat non confirmé, AUCUNE adoption, la prod reste paper. Retour à la
> recherche : Sprint 8-quater (trailing ATR, gate 8b.4 ouvert).**
>
> | Dimension     | Note /100 | Justification |
> |---------------|-----------|---------------|
> | **Rentabilité** | **25**  | Le premier candidat d'edge est réfuté proprement (3 verrous indépendants) : retour à « aucun candidat vivant », d'où −5. La chaîne v2 conserve sa progression (−6,88 sur pavage fin) et le PROCESSUS de validation hors-grille existe désormais (offset de pavage, triple verdict) — c'est lui qui empêchera d'adopter un artefact. La note ne franchira 50 qu'avec un candidat CONFIRMÉ hors-grille, et 70+ qu'en battant le B&H net de coûts avec la DoD complète. |
- **État des tests** : 545/545 verts (468 unitaires + 77 intégration) — et la
  suite passe aussi en **Release**, sous **ASan/UBSan**, et TSan ciblé sur les
  suites concurrentes. +8 au Sprint 8-ter (537 → 545), aucune dérive hors
  cycle (`ctest -N` recalé à l'ouverture : 537 conforme). Détail au changelog.
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

# 🟣 SPRINT 7 — Harnais de validation (prouver l'edge) ✅ (clos le 2026-06-25)

> Aucune confiance dans un paramètre sans validation hors-échantillon. Ce sprint
> construit l'outillage qui permettra de juger toute modif du Sprint 8 **honnêtement**.
> Dépend du Sprint 6 (coûts/métriques justes) — satisfait. Point d'entrée du code :
> `Backtester::run()` (`include/backtest/BackTester.hpp:110`) et les métriques de
> `computeMetrics` (`BackTester.hpp:265`). Ordre conseillé **7.1 → 7.4 → 7.2 → 7.3** :
> le walk-forward (7.1) est le socle de jugement ; les données multi-actifs (7.4,
> avec le ré-export total-return D29) doivent exister AVANT de lancer la grille
> (7.2) pour ne pas optimiser sur un seul actif sans dividendes ; le bootstrap
> (7.3) consomme les trades produits par 7.1/7.2.

- [x] **7.1** (D24) Split IS/OOS + **walk-forward** → `8e618fd`
  `WalkForward.hpp` : fenêtres glissantes IS→OOS contiguës. Refactor additif du moteur
  (golden préservé au centime) : `Backtester::run()` délègue à `runRange(csv, start, end)`
  qui backteste une sous-fenêtre ; `ReplayDataFeed` gagne un `floorIdx` (défaut 0) qui
  empêche le feed de servir avant le début de la fenêtre → chaque sous-période ré-amorce
  ses indicateurs (isolation IS/OOS honnête). Verdict « sur-ajusté » = alpha IS > 0 mais
  alpha OOS ≤ 0. Tests : 6 WalkForwardUnit (pavage, `runRange==run` = verrou golden,
  verdict, isolation floorIdx) + 1 WalkForwardIntegration (3 fenêtres figées sur QQQ).
- [x] **7.4** (D29) **Multi-actifs + total-return** → `eb46e38` + `43b4015`
  Ré-export de QQQ/SPY/IWM/MDY en série total-return RÉELLE depuis Yahoo (Adj Close ≠
  Close, dividendes réinvestis) ; `DataQuality.hpp::auditTotalReturnCsv` lève
  `suspectNoDividends` si Adj == Close sur toutes les lignes (relit les colonnes brutes,
  car CsvDataFeed écrase Close par Adj Close). Conséquences : B&H +238,55 → +262,08 % ;
  1790 barres (l'ancien CSV en portait 1858, ~68 parasites) ; **config prod alignée sur
  le défaut** (D30, voir Découvertes). Tests : 5 DataQualityUnit + 3 MultiAssetIntegration
  (≥ 3 actifs, dividendes comptés, garde-fou testé). Golden re-figé.
- [x] **7.2** Optimiseur de grille + sélection plateau → `7d34349`
  `GridOptimizer.hpp` : produit cartésien de SwingConfig, fonction objectif INJECTÉE
  (en intégration : Sharpe OOS moyen du walk-forward, jamais l'IS), sélection du PLATEAU
  (`argmax(neighborhoodAvg)`, voisinage ±1 cran), filtre dur alpha > 0. Tests : 4
  GridOptimizerUnit (plateau battant le pic isolé, sur objectif factice) + 1
  GridOptimizerIntegration (grille 8 combos jugée en OOS, carte de sensibilité ; verdict
  réel : « AUCUN edge — ne pas deployer »).
- [x] **7.3** **Monte-Carlo / bootstrap** des trades → `4a29c1d`
  `MonteCarlo.hpp` : ré-échantillonne la séquence de P&L avec remise (`std::mt19937`,
  graine fixe), reconstruit des milliers de chemins → distribution CAGR/drawdown
  (p5/p50/p95). CAGR sur le dénominateur d'années observé. Tests : 6 MonteCarloUnit
  (reproductibilité au bit près, trade unique dégénéré calculé à la main, ordre des
  percentiles) + 1 MonteCarloIntegration (p50 figés sur les trades prod QQQ).
- [x] **CLI de validation** (livrable) → `c674c8e`
  `main_validate.cpp` + cible `validate` : imprime backtest + walk-forward + carte de
  grille + Monte-Carlo + audit qualité multi-actifs. `./build/validate`.

# 🟣 SPRINT 8 — Refonte de la stratégie pour capter la tendance (l'argent) ✅ (clos le 2026-07-01, verdict : pas d'edge démontré)

> Le sprint qui doit **transformer −257 pts d'alpha en alpha positif (ou neutre à moindre
> drawdown)**. Chaque item est jugé par le harnais du Sprint 7 en **OOS**, jamais en IS
> (walk-forward `WalkForward.hpp`, grille `GridOptimizer.hpp`, robustesse `MonteCarlo.hpp` ;
> exécutable `./build/validate`). Le point de départ est honnête et chiffré : la config
> actuelle (= défaut, D30) rend +4,85 % pour un B&H total-return de +262,08 %, sans aucun
> edge OOS. Prérequis : E7/D18 (indicateurs sur `vector<Bar>`) pour des stops/VWAP corrects.

- [x] **8.0** (D18) Enrichir `IIndicator` → `computeBars(const std::vector<Bar>&)` (high/low/volume
  disponibles) ; vrai ATR/true-range, VWAP correct → `8e3f72f` + `b855974`.
  Méthode `computeBars` AJOUTÉE (défaut rétro-compatible qui extrait les clôtures et délègue à
  `compute` — EMA/RSI inchangés, aucun golden touché) plutôt que de changer la signature
  pure-virtuelle. ATR surcharge avec le vrai TR `max(H−L, |H−Cprev|, |L−Cprev|)` ; VWAP délègue
  à `computeWithVolume`. DayTradeStrategy migrée. **Acceptation** : nouveaux tests ATR true-range
  (calcul à la main + dépassement de l'approx clôture-seule), VWAP `computeBars`≡`computeWithVolume`.
- [x] **8.1** (D26) **Filtre de régime** : n'ouvrir long que si prix > SMA200 → `1264f7a` (SMA) +
  `1e6f6fd` (filtre). Nouvel indicateur `SMA`, `SwingConfig::smaTrendPeriod` (défaut 200, ≤ 1 =
  désactivé pour la base de comparaison OOS). La fenêtre de données du bot suit la période via
  `RiskConfig::lookback` (amorce D19/9.2 — voir D32). **Acceptation OOS satisfaite** : alpha OOS
  moyen (walk-forward 2 fenêtres QQQ) **−14,10 pts vs −16,09 pts pour la base (+1,99 pt)** →
  « alpha net OOS > version actuelle » VRAI, alors même que le plein échantillon semblait dégradé
  (−1,07 % vs +4,85 %) — la valeur du jugement OOS. Les deux restent négatifs (pas d'edge, pas de
  déploiement). Verrou : `test_strategy_v2_integration.cpp`.
- [x] **8.2** (D26) **Laisser courir les gagnants** → `26d645d` (+ prep `a0ab08d`)
  Garde `takeProfitPct > 0` dans `checkExitConditions` (convention ≤ 0 = désactivé) ;
  défaut adopté : TP 0,10 → 0 (SwingConfig ET RiskConfig). **Acceptation OOS
  (« gain moyen des gagnants ↑ sans dégrader le profit factor »)** : INDÉCIDABLE à
  cet item — les fenêtres OOS ne portaient AUCUN trade sous la config 8.1 (D34),
  delta strictement nul → adopté comme contrainte MORTE (0 TP aussi sur le plein
  échantillon), puis **VALIDÉ positivement** à la ré-exam sur la chaîne finale
  (alpha OOS −10,03 TP off vs −10,20 TP 10 %). Goldens inchangés à cet item.
- [x] **8.3** (D26) **Réviser l'entrée contradictoire** → `57a4014`
  `rsiBuyMax ≥ 100 = plafond désactivé` (RSI 100,0 atteignable) ; défaut adopté
  55 → 100. **Acceptation OOS SATISFAITE** : trades 0 → 5, exposition 0 → 22,11 %,
  espérance +2,97 $/trade, alpha ~inchangé. Goldens re-figés (4 → 20 trades,
  in-sample −4,52 % — jugement en OOS, leçon 8.1).
- [x] **8.4** (D26) **Ne pas vendre sur RSI seul en tendance haussière** → `85f0483`
  Flag `rsiSellOnlyIfRegimeDown` (indépendant de smaTrendPeriod, base A/B 8.1
  préservée) ; croisement baissier vend toujours ; défaut adopté true.
  **Acceptation OOS (définie ce sprint : gain moyen des gagnants ↑ et alpha ≥ base,
  sans dégrader le PF) SATISFAITE** : gagnants 2,46 → 4,19 %, PF 1,06 → 1,84,
  alpha −14,12 → −13,11 (+1,01 pt). Goldens re-figés (in-sample +0,02 %).
- [x] **8.5** **Réduire le cash drag** → `4887817`
  Flag `regimeReentry` : à plat, régime up et prix > EMAs → BUY sans croisement,
  évalué APRÈS les ventes (priorité aux sorties) ; défaut adopté true.
  **Acceptation OOS SATISFAITE** : temps investi 30,65 → 54,02 %, alpha
  −13,11 → −10,03 (+3,08 pts), trades 5 → 11. Goldens re-figés (in-sample
  +18,70 %, Sharpe 0,72, 22 trades 13 G / 9 P, position finale ouverte).

> **Definition of Done du Sprint 8** (en plus de la DoD standard) : la stratégie retenue
> **bat le Buy & Hold net de coûts en out-of-sample** OU le sous-performe de **moins de 5
> points** avec un **drawdown réduit d'au moins 50 %** (cible arbitrée avec l'utilisateur le
> 2026-06-25, ex-« Décision requise »). Sinon le sprint conclut « pas d'edge démontré » et on
> ne déploie PAS — c'est un résultat valide (ne jamais mettre d'argent réel sur un edge non prouvé).
>
> **Point d'étape 2026-06-25** : 8.0 (prérequis indicateurs) et 8.1 (filtre de régime) livrés,
> validés en OOS. **Pause de réévaluation** (décision utilisateur) avant 8.2–8.5 : l'alpha OOS
> est passé de −16,09 à −14,10 pts grâce au régime — il reste très négatif (B&H +226 % sur la
> fenêtre, temps investi ~2,4 %). Le levier dominant restant est le **cash drag** (8.3 entrée
> sur la force, 8.5 rester investi) ; à arbitrer pour la session suivante.
>
> **VERDICT DE CLÔTURE 2026-07-01 : DoD NON ATTEINTE — pas d'edge démontré, pas de
> déploiement.** La chaîne complète 8.1→8.5 (verrouillée par
> `SprintChainVsBaselineOosVerdictIsLocked`) rend un alpha OOS moyen de **−10,03 pts**
> (< −5 exigés), même si le sprint a produit **+4,07 pts** et des trades OOS de qualité
> (11 trades, gagnants +4,46 %, PF 3,71, espérance +77,93 $). La grille du harnais
> (`./build/validate`) conclut « AUCUN edge (alpha ≤ 0 partout) — ne pas deployer ».
> La prod reste en paper. Suite décidée avec l'utilisateur : **Sprint 8-bis** (ci-dessous).

# 🟣 SPRINT 8-BIS — Chercher l'edge avec le harnais ✅ (clos le 2026-07-02, verdict : premier candidat d'edge, non adopté)

> Décision utilisateur (2026-07-01) à la clôture du Sprint 8 : la stratégie progresse
> (+4,07 pts d'alpha OOS ce sprint) mais ne bat pas le B&H — continuer la recherche
> d'edge AVEC le harnais avant tout durcissement de prod. Point d'entrée : la chaîne
> v2 (défauts actuels de `SwingConfig`, `include/strategies/SwingStrategy.hpp:12-49`)
> et le juge de paix `SprintChainVsBaselineOosVerdictIsLocked`
> (`tests/integration/test_strategy_v2_integration.cpp`). Discipline inchangée :
> verdicts OOS verrouillés, configs de verdict EXPLICITES (D33), jamais de jugement
> sur le plein échantillon.
> **Ordre exécuté 8b.3 → 8b.1 → 8b.2 → (8b.4)** — décision utilisateur d'ouverture
> (2026-07-02) : le pavage fin d'abord, pour que la grille et le multi-actifs soient
> jugés sur ≥ 4 fenêtres OOS (leçon D34) et non 2.

- [x] **8b.1** **Grille étendue sur la chaîne v2** → `77ff922` + `de7bdcf`
  `GridOptimizer` étendu ADDITIVEMENT (axes `smaTrendPeriod`/`trailingStopPct`
  optionnels, vide = singleton depuis la base — appels historiques inchangés) +
  `axisSensitivities` (écart moyen max−min par axe, le mécanisme du gate 8b.4).
  Grille du verrou : emaFast {9,13} × smaT {150,200,250} × trail {0,03/0,05/0,08}
  = 18 combos, base chaîne v2 EXPLICITE (D33), objectif Sharpe/alpha OOS sur le
  pavage FIN. **Acceptation satisfaite ET premier candidat d'edge** : le filtre
  alpha > 0 PASSE au plateau (9, 250, 0,05) — Sharpe OOS 1,22, alpha +0,10
  (`V2ChainExtendedGridOosVerdictIsLocked`). CLI : grille pleine 81 combos +
  sensibilités. Réserve : biais de sélection — voir D36, adoption refusée.
- [x] **8b.2** **Validation multi-actifs de la chaîne v2** → `ba22444`
  Walk-forward par actif (pavage fin, mêmes fenêtres que QQQ), un verrou par actif
  (`V2ChainOosVerdictLockedOnSpy/Iwm/Mdy`). **Acceptation satisfaite** : aucun
  alpha OOS > 0 (SPY −5,52 / IWM −3,01 / MDY −3,60 vs QQQ −7,15) mais comportement
  COHÉRENT partout (10-17 trades OOS, exposition 53-81 %) — la chaîne n'est pas un
  artefact QQQ. Bonus : le CLI imprime aussi le candidat 8b.1 par actif (alpha > 0
  sur 3/4 actifs — la donnée qui a motivé le Sprint 8-ter).
- [x] **8b.3** **Pavage walk-forward plus fin** → `481f713`
  IS=500/OOS=300/pas=300 → 4 fenêtres OOS (OOS=300 et non l'exemple 250 : warmup
  local ~201 barres par fenêtre, D35 — décision utilisateur d'ouverture).
  **Acceptation satisfaite** : verdict chaîne re-verrouillé
  (`SprintChainFinePavingOosVerdictIsLocked`), AUCUNE inversion vs 2 fenêtres —
  chaîne −7,15 vs état 8.1 −9,05 (delta +1,91), 13 trades OOS poolés (D34 étoffé),
  DoD toujours non atteinte. L'état 8.1 reste à 0 trade OOS même sur 4 fenêtres.
- [x] **8b.4** **Trailing adaptatif ATR** (exploratoire, gaté par 8b.1) → gate FERMÉ, aucun code
  Le classement de sensibilité (verrouillé dans le verdict 8b.1) place
  `smaTrendPeriod` premier (0,244 vs trail 0,210 sur 18 combos ; 0,312 vs 0,254 —
  3e sur 4 — sur la grille pleine 81 combos) : `trailingStopPct` n'est PAS l'axe
  le plus sensible → l'item ne s'ouvre pas, conformément à sa condition. Reste au
  backlog, ré-ouvrable si un futur verdict de grille inverse le classement.

# 🔴 SPRINT SÉCURITÉ-RÉEL — Audit complet + correctifs bloquants ✅ (clos le 2026-07-02)

> Demande utilisateur : analyse complète du projet (docs, workflow, code, bot) +
> verdict « prêt à trader ? » + améliorations. Verdict de l'audit : **NON prêt**
> — 4 défauts bloquants (B1-B4) et 2 modérés (M2-M3) identifiés et corrigés dans
> ce sprint. Chaque item : test rouge → fix → test vert → commit atomique.
> Baseline d'ouverture : 470/470 verts (`ctest -N` conforme au tableau de bord).

- [x] **S.1 (B3.1)** **getAccount IBKR : schéma incomplet masqué en ACTIVE** → `b0410fe`
  Un résumé Gateway sans `availablefunds`/`netliquidation` donnait cash=0 +
  status=ACTIVE → positionSize=0 → le bot ne tradait plus JAMAIS, sans erreur
  visible (le log disait « Cash insuffisant »). Fix : INACTIVE bruyant +
  `lastError_`, et TradingBot logue « Compte broker non ACTIF » en ERROR.
  Tests rouges : AccountSummaryWithoutBalancesIsInactive, …WithSingleBalance…,
  InactiveAccountLogsExplicitErrorAndSubmitsNoOrder (+ MockLogger dans Mocks.hpp).
- [x] **S.2 (B3.2)** **Historique < SMA200 = HOLD muet éternel** → `0d2c2ad`
  Raison de HOLD explicite « Régime inconnu : historique insuffisant (N/200
  barres) » + WARN TradingBot si le feed renvoie moins que `lookback`. La vente
  reste possible (la SMA ne gate que les achats — verrouillé).
  Tests rouges : InsufficientHistoryHoldReasonIsExplicit, ShortHistoryLogsWarn.
- [x] **S.3 (M3)** **main_alpaca sans stateStore** → `36a0823`
  `SqliteStateStore("data/swingbot_alpaca_state.db")` câblé (holdDays/peakPrice
  survivent au restart, comme main_ibkr). Vérifié par `g++ -fsyntax-only`.
- [x] **S.4 (B4)** **Jours fériés NYSE absents du calendrier de repli** → `d62287e`
  `isNyseHoliday` : fériés calculables (MLK, Presidents, Memorial, Labor,
  Thanksgiving), Good Friday via Pâques (Butcher/Meeus), observances NYSE
  (sam→ven, dim→lun, New Year samedi NON observé — 2021-12-31 ouvert, cas de
  test), Juneteenth ≥ 2022. Demi-séances hors périmètre (documenté).
  Tests : 6 nouveaux MarketCalendarUnit (Pâques 2024/25/26, 10 fériés 2024…).
- [x] **S.5 (socle)** **BotState.stopArmed + lastExitDate persistés** → `dd939ca`
  Champs ajoutés EN FIN de struct (init agrégat des tests préservée), migration
  `ALTER TABLE` tolérante (« duplicate column » avalé), save/load 7 colonnes.
  Tests rouges : RoundTripPersistsStopArmedAndLastExitDate,
  OpensAndMigratesLegacySchema (base à l'ancien schéma créée en direct).
- [x] **S.6 (B1 — BLOQUANT)** **Le stop résident ne s'armait presque jamais** → `c7949d8`
  `submitStopLoss` n'était appelé que dans la branche FILLED de l'achat ; or le
  CP Gateway répond typiquement Submitted → PENDING → adoption par
  `reconcilePosition_` SANS stop (idem après tout restart) : la protection
  « position couverte bot hors-ligne » était inopérante dans le cas nominal
  (c'était D27, requalifié bloquant). Fix : `armResidentStopIfNeeded_` idempotent
  via `stopArmed` persisté, appelé à l'achat FILLED, à l'adoption, et en filet à
  chaque cycle (retente un dépôt échoué) ; position disparue → `cancelStopLoss`.
  6 tests rouges→verts (AdoptedPositionArmsResidentStop, PendingBuyThen…, etc.).
  Goldens INTACTS (no-op PaperBroker). Risque résiduel → D38.
- [x] **S.7 (M2)** **Churn : rachat possible le jour même d'une sortie** → `d2d96d1`
  Cycle prod 60 min + regimeReentry : sortie stop à 10h30 → rachat à 11h30.
  Fix dans TradingBot (stratégie stateless préservée) : `lastExitDate` posé à
  chaque sortie (vente FILLED + position fermée hors bot), entrée bloquée tant
  que la date de barre n'a pas changé ; persisté (survit au restart). Impact
  backtest nul par construction (vérifié : goldens intacts après S.7).
  4 tests rouges→verts (SameDayReentryAfterStopExitIsBlocked, etc.).
- [x] **S.8 (B2 — BLOQUANT)** **LOOK-AHEAD du backtest** → `f89d35a`
  Décision ET fill au close de la même barre — impossible en réel. Fix :
  décision au close i, exécution à l'OPEN de i+1 (PaperBroker
  `setNextFillPrice/Date`, rétro-compatible ; valorisation toujours au close).
  Preuve : `FillsAtNextBarOpenNotAtDecisionClose` (CSV synthétique à gap).
  RE-BASELINE de ~40 goldens (5 fichiers) — changement de vérité de mesure,
  AUCUNE inversion des verdicts OOS (DoD toujours non atteinte)… sauf la grille
  8b.1 où le candidat SURVIT et se renforce → D37. Contrôles de santé intacts
  (B&H +226,12 %, 1790 points d'équité).
- [x] **S.9 (doc/infra)** → `9f859fe`
  docker-compose réparé (`dockerfile: Dockerfile.multistage` pour le stage
  `builder` ; env vars mortes BOT_MODE/BOT_DRY_RUN supprimées), DB déplacées
  dans `data/` (le volume `./data` ne persistait rien), sed anti-CMP0167 retiré
  (Dockerfile + dev.ps1, no-op depuis le Sprint 1), CLAUDE.md rafraîchi
  (gotchas périmés retirés), `SwingBot_UML.mermaid` réécrit sur l'architecture
  réelle (Result<T>, IStateStore, stops résidents, kill-switch, harnais 7/8).

## 2e passe (même jour) — corrections restantes de l'audit

- [x] **S.10 (D38)** **Re-découverte de l'orderId du stop résident** → `75339e2`
  `residentStopOrderId_` (mémoire seulement) rendait `cancelStopLoss` no-op
  après restart → stop GTC orphelin chez IBKR. Fix : si l'id est inconnu,
  GET `/v1/api/iserver/account/orders` et re-découverte par le tag cOID
  « swingbot-SYM-STOP- » (order_ref/cOID, orderId nombre ou chaîne, statuts
  terminaux ignorés) avant le DELETE. Tests rouges→verts :
  CancelStopLossRediscoversOrderAfterRestart (+ 2 verrous négatifs).
- [x] **S.11 (9.3, volet barres clôturées)** **La barre du jour en formation
  n'est plus livrée** → `2d2d962`
  Le HMDS IBKR incluait la barre journalière EN FORMATION pendant la séance
  (close mouvant → flap du croisement EMA intra-journée, E6/D25). Nouveau
  helper pur `usEasternDateOfUtc` (market_calendar.h, bascule DST gérée) ;
  `IBKRDataFeed::getBars` retire la dernière barre si datée d'aujourd'hui-ET
  (horloge injectable `now_`). Alpaca bornait déjà à end=hier ; CsvDataFeed/
  ReplayDataFeed non concernés — goldens intacts. 5 tests rouges→verts.
- [x] **S.12 (watchdog)** **E2E réel du canal SMS Twilio** → `ac9382a`
  L'URL api.twilio.com était codée en dur → jamais exercée. `twilio_base_url`
  configurable (défaut prod inchangé) + test SmsDeliveredToLocalServer
  (MiniHttpServer : chemin Twilio complet, form-urlencoded, %2B, Basic auth).
  L'email reste couvert par son chemin d'échec (pas de mock SMTP — documenté).
- [x] **S.13 (9.1)** **Config prod externalisée en JSON validé** → `1e617a6`
  `config/prod.json` (14 champs) chargé STRICTEMENT par `ConfigLoader.hpp`
  (champs requis, types, bornes ; échec bruyant nommant le champ) ;
  `prodSwingConfig()` charge le fichier injecté à la compilation
  (`SWINGBOT_PROD_CONFIG_JSON` sur swing_bot/validate/integration_tests) →
  LE MÊME fichier sert la prod ET le golden (acceptation 9.1) ; main_ibkr et
  main_validate refusent de démarrer sur config invalide ; Dockerfile.multistage
  copie le JSON dans l'image runtime. 6 tests ConfigLoaderUnit rouges→verts,
  les 3 goldens « config prod » passent par le JSON réel SANS re-figeage.

## 3e passe (même jour) — durcissement production (audit workflow/docs/règles)

> Demande utilisateur : « tout doit être solide, l'app va trader de l'argent
> réel ». Audit croisé (CI/déploiement/secrets/monitoring + docs/règles) :
> le moteur était mûr mais l'ENVIRONNEMENT de production était de niveau
> prototype. Neuf items, chacun rouge→vert :

- [x] **S.14 (B1')** **`-Wall -Wextra -Werror` sur tout le build** → `4fea48e`
  Fixes : sign-compare (BackTester), variable morte (DayTradeStrategy),
  dangling-else ×2, agrégats BotState incomplets remplacés par un helper.
- [x] **S.15 (A2)** **Santé du cycle** → `d949da2`
  `runOnce` → enveloppe de `runCycle_()` ; `lastCycleHealthy()` — pannes
  feed/broker/compte = cycle non sain ; marché fermé/HOLD/kill-switch =
  sain. 5 tests.
- [x] **S.16 (A6)** **Notification kill-switch** → `8c0cc60`
  `setHaltObserver` (dédup par raison/séance) + `Watchdog::alertNow`. 3 tests.
- [x] **S.17 (C1)** **Seuils kill-switch dans prod.json** → `f0048ad`
  `KillSwitchConfig` rejoint SwingConfig (copié dans RiskConfig) ; objet
  `killSwitch` REQUIS et validé ; valeurs = défauts → goldens intacts. 4 tests.
- [x] **S.18 (A1)** **Gate live mécanique en 4 couches** → `336f0f1`
  `liveTradingApproved` (ProdSettings) + canal d'alerte + TTY + « OUI » tapé
  (LiveGate.hpp pur). Verrou d'intégration
  `LiveTradingStaysDisapprovedUntilEdgeDoD` : le « paper tant que pas
  d'edge » devient un MÉCANISME. 11 tests.
- [x] **S.19 (A4)** **Secrets par env `SWINGBOT_*`** → `402cafc`
  `alertConfigFromEnv` (canal activé ssi variables complètes), défauts
  exemples purgés, `.env.example` + `!.env.example` au .gitignore. 4 tests.
- [x] **S.20 (A3/A5 + câblage)** **Boucle live défensive** → `9142ee6`
  Auth Gateway re-vérifiée à chaque cycle (session ~24 h), heartbeat émis
  seulement sur cycle sain, sync equity sautée si compte non ACTIF (fini
  l'equity=0 fictive), haltObserver branché. Gate vérifié manuellement
  (`--live < /dev/null` refuse, « NON » refuse, config false refuse).
- [x] **S.21 (B2')** **CI Release + ASan/UBSan + TSan ciblé** → `5500e73`
  Le binaire de prod (Release) est enfin compilé ET testé en CI. Les
  sanitizers ont IMMÉDIATEMENT payé : fuites sqlite3 dans les
  constructeurs qui lèvent (DbLogger, SqliteStateStore) et data race
  `MiniHttpServer::stop()/loop_()` — corrigées, 3 suites vertes en local
  (Release 537, ASan 537, TSan ciblé 59).
- [x] **S.22 (B3')** **Docker prod** → `f28899b`
  Service bot → image multistage runtime RELEASE testée au build,
  HEALTHCHECK TCP (process gelé → unhealthy), `network_mode: host` (le
  Gateway localhost:5000 était structurellement injoignable), `env_file
  .env` optionnel, logs json-file avec rotation.
- [x] **S.23 (docs/règles)** → `aa488c0`
  README (avertissement argent réel), RUNBOOK (incidents, kill-switch
  manuel, politique de risque chiffrée, checklist pré-live signée),
  CLAUDE.md « Live-safety rules », prompts : l'auto-amendement des règles
  requiert désormais une décision utilisateur ; DoD complétée (pas de live
  sans gate, fichiers de règles protégés) ; docs binaires marquées
  obsolètes ; UML complété.

# 🟣 SPRINT 8-TER — Valider le candidat d'edge hors-grille ✅ (clos le 2026-07-03, verdict : candidat RÉFUTÉ)

> Décision utilisateur (2026-07-02) à la clôture du Sprint 8-bis : la grille a produit
> le premier candidat d'edge. **RE-CALÉ post-B2 (D37, correction du look-ahead)** : le
> candidat est désormais **(emaFast=9, smaTrendPeriod=250, trailingStopPct=0,03)**,
> alpha OOS **+0,23** sur QQQ, Sharpe OOS 1,267 (les valeurs pré-B2 — trail 0,05,
> +0,10 — sont historiques ; les alphas par actif +1,26 IWM / +0,81 MDY / −0,53 SPY
> datent aussi d'avant B2 et sont à re-mesurer en 8t.1). Il reste entaché d'un biais
> de sélection (meilleur de 18 combos jugés sur les MÊMES fenêtres OOS que son
> verdict) et d'échantillons minces (4-8 trades OOS/actif). **Consigner, ne pas
> adopter** : ce sprint fait passer au candidat une validation dédiée HORS de la grille
> qui l'a choisi. Aucune adoption de défaut sans que la DoD ci-dessous passe. Point
> d'entrée : le verdict 8b.1 re-figé post-B2 (`test_grid_optimizer_integration.cpp`,
> `V2ChainExtendedGridOosVerdictIsLocked`) et la section 6 du CLI (`main_validate.cpp`).
> NB : le gate 8b.4 (trailing ATR) est OUVERT depuis B2 (trailing = axe le plus
> sensible) — à ré-examiner après ce sprint.

- [x] **8t.1** **Verdict du candidat sur des fenêtres qu'il n'a PAS choisies** → `f332f33` + `401067a`
  Prérequis livré : `WalkForward` gagne un paramètre `offset` additif (défaut 0,
  rétro-compat bit-identique verrouillée par `OffsetZeroMatchesLegacyCtor`).
  Pavage décalé arbitré à l'ouverture (décision utilisateur 2026-07-03) :
  IS=500/**OOS=400**/pas=400/**offset=90** (3 fenêtres inédites [590,990) [990,1390)
  [1390,1790), les 90 dernières barres jugées en OOS pour la 1re fois) — et non
  l'exemple littéral OOS=300 (warmup candidat ~251 barres → ~49 tradables, piège
  D34/D35). **Verdict verrouillé (`test_candidate_validation_integration.cpp`) :
  NON CONFIRMÉ** — pavage canonique : candidat **−19,10** vs chaîne −9,90 (10 vs 11
  trades poolés) ; pavage décalé : candidat −5,38 vs chaîne −12,83. Le SIGNE de la
  comparaison s'inverse selon le pavage : pas un edge.
- [x] **8t.2** **Monte-Carlo du candidat** → `fff8a2a`
  Bootstrap (graine 42, 2000 chemins) des trades OOS poolés du pavage décalé
  (fenêtres disjointes et non-choisies), même dénominateur d'années (4,77 ans).
  **Verdict verrouillé : NON CONFIRMÉ** — CAGR p50 candidat **4,41 % < 6,60 %**
  chaîne (le critère « ≥ » échoue même sur le pavage favorable au candidat).
  Seul acquis, verrouillé aussi : risque plus faible (DD p95 11,71 % vs 16,48 %).
- [x] **8t.3** **Grille de CONFIRMATION resserrée autour du candidat** → `05569e1`
  27 combos emaFast {7,9,11} × smaT {225,250,275} × trail {0,02/0,03/0,04},
  candidat au CENTRE du cube, mêmes objectif/pavage que 8b.1.
  **Verdict verrouillé : ARTEFACT DE GRILLE** — le filtre alpha > 0 passe mais le
  plateau resserré est (9, **275**, **0,04**) : il ne contient PAS smaT=250 ; à
  crans resserrés, même l'axe réputé stable (D36) dérive. Verrou
  `EXPECT_NE(smaT, 250)` = cœur du verdict. Exécution 2,3 s (timeout intact).
- [x] **8t.4** **Décision d'adoption** → branche « sinon » appliquée (aucun code)
  La condition « 8t.1 ET 8t.2 ET 8t.3 confirment » est FAUSSE (0 volet sur 3) :
  **« candidat non confirmé » consigné, AUCUNE adoption** (SwingConfig, ProdConfig,
  config/prod.json et goldens strictement inchangés), la prod reste paper.
  Retour à la recherche : Sprint 8-quater (8b.4, décision utilisateur 2026-07-03).

# 🟣 SPRINT 8-QUATER — Trailing adaptatif ATR (ré-ouverture de 8b.4) — **sprint courant**

> Décision utilisateur (2026-07-03) à la clôture du Sprint 8-ter : le candidat de
> grille est réfuté, retour à la recherche par l'axe désigné DEUX FOIS par la
> mesure : `trailingStopPct` est l'axe le plus sensible depuis B2 (verrou 8b.1,
> sensibilité 0,266) ET l'axe qui fait dériver le plateau resserré (8t.3 :
> 0,03 → 0,04). Hypothèse de l'item 8b.4 : un trailing FIXE en % est fragile parce
> que la « bonne » distance dépend de la volatilité — un trailing en multiples
> d'ATR (`include/indicators/DayIndicators.hpp:20`, vrai true-range depuis 8.0/D18)
> s'adapte. Discipline inchangée : flag additif A/B-able, verdicts OOS verrouillés
> (configs EXPLICITES D33, comptes de trades D34), et — leçon 8-ter — tout
> gagnant de grille est jugé sur des fenêtres NON-CHOISIES avant toute adoption.

- [ ] **8q.1** **Trailing ATR dans le moteur de sortie** : nouveau réglage
  `SwingConfig::trailingAtrMult` (défaut 0 = désactivé → comportement et goldens
  strictement inchangés) ; quand > 0, la sortie trailing de `checkExitConditions`
  (`include/bot/RiskManager.hpp:62`, branche trailing `:90`) utilise
  `peak − mult × ATR(14)` au lieu de `peak × (1 − trailingStopPct)`. L'ATR est
  calculé sur les barres de la fenêtre courante (`computeBars`, vrai true-range).
  Convention à trancher à l'implémentation : priorité si les deux trailing sont
  configurés (proposition : ATR remplace le %, il ne s'y ajoute pas).
  **Acceptation** : tests rouges unitaires (RiskManagerUnit) sur le calcul de
  sortie ATR calculé à la main ; flag off = aucun golden touché (vérifié).
- [ ] **8q.2** **Verdict OOS du trailing ATR vs chaîne v2** : chaîne + ATR (grille
  courte de `mult` ∈ {2, 3, 4} pour choisir, jugée en OOS) vs chaîne v2 trail fixe
  0,03, sur les TROIS pavages existants (canonique 700/400, fin 500/300, décalé
  500/400 offset 90 — leçon 8-ter : juger d'emblée sur des fenêtres variées,
  `test_candidate_validation_integration.cpp` comme modèle).
  **Acceptation** : verdicts verrouillés (D33/D34) ; alpha OOS ≥ chaîne sur les
  pavages non-choisis par la mini-grille, sinon « pas d'amélioration » (valide).
- [ ] **8q.3** **Décision de suite (Décision requise)** : si 8q.2 confirme une
  amélioration robuste, poser l'adoption du défaut (goldens re-figés, delta
  chiffré) ; sinon consigner et statuer avec l'utilisateur sur la suite de la
  recherche (autres familles de signaux vs pause stratégie).

# 🟣 SPRINT 9 — Mise en production de la stratégie validée

> Ne s'ouvre qu'après un edge OOS démontré (Sprint 8-bis). Sinon, la prod reste en paper.

- [x] **9.1** Externaliser la config (fichier JSON **validé** au démarrage) ; fin de la dérive
  prod ≠ backtest (E1/D21). **Acceptation ATTEINTE** : la config de prod EST chargée par le
  golden — fait au Sprint Sécurité-Réel 2e passe (S.13, `1e617a6` : config/prod.json +
  ConfigLoader strict + macro compile-time partagée prod/golden).
- [ ] **9.2** (D19) Lookback configurable unifié prod/backtest (`TradingBot.hpp:62`).
- [x] **9.3** (item 20 + E6/D25) Calendrier de marché correct (UTC/DST) + n'évaluer que sur
  **barres clôturées** (pas la barre du jour en formation). Fait en deux temps : calendrier
  UTC/DST au Sprint 5 (item 20) + fériés NYSE (S.4/B4, `d62287e`) ; barres clôturées côté
  IBKR au Sprint Sécurité-Réel 2e passe (S.11, `2d2d962` — Alpaca bornait déjà à end=hier).
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
| D26 | ✅ | (Méta-audit) **Défauts structurels de la stratégie (cause racine de l'alpha −229 pts)** : take-profit fixe qui ampute les gagnants (`RiskManager.hpp:81`) ; vente sur RSI > 70 qui sort des tendances haussières (`SwingStrategy.hpp:109`) ; filtre d'entrée contradictoire croisement-haussier + `RSI<55` (`SwingStrategy.hpp:96-99`) → 7 trades/5 ans ; aucun filtre de régime ; long-only mono-actif → cash drag massif | ✅ Les 5 défauts corrigés au Sprint 8 (8.1 régime, 8.2 TP, 8.3 entrée, 8.4 vente RSI, 8.5 cash drag), chacun validé en OOS. L'edge reste à démontrer (Sprint 8-bis) |
| D27 | ✅ | (Sprint 5) **Le stop résident broker (item 19) n'est posé qu'à l'ENTRÉE, pas à l'adoption.** `reconcilePosition_` (TradingBot.hpp) adopte une position broker non suivie (redémarrage) avec le stop logiciel, mais ne dépose aucun stop résident — une position adoptée n'est protégée côté broker que si elle avait été ouverte par ce process. Requalifié BLOQUANT à l'audit 2026-07-02 : le CP Gateway répond typiquement PENDING à l'achat → la position passe par l'adoption dans le cas NOMINAL, pas seulement au restart | ✅ Corrigé au Sprint Sécurité-Réel (S.6/B1, `c7949d8` — armement idempotent via `stopArmed` persisté : achat FILLED, adoption, filet par cycle). Résidu : D38 |
| D28 | 🟢 | (Sprint 5) Le drawdown journalier du kill-switch (item 18) est **inerte en backtest** : un `runOnce` = une barre journalière, donc `dayStartEquity` se recale à chaque cycle et le drawdown intra-séance vaut toujours ~0. Voulu (préserve le golden) et correct en prod (boucle 60 min, plusieurs cycles/jour), mais signifie que ce garde-fou précis n'est jamais exercé par le golden — seuls les tests unitaires purs le couvrent | Documenté (couvert par RiskManagerUnit) |
| D29 | ✅ | (Sprint 6) **QQQ.csv ne porte aucune information de dividende** : `Adj Close == Close` sur les 1858 lignes (export sans ajustement). Le code 6.3 utilise bien Adj Close (stratégie ET B&H, OHL mis à l'échelle), mais le delta est NUL sur ce dataset — le B&H +238,55 % reste hors dividendes, et l'alpha réel est donc encore PIRE que mesuré (~+0,6 pt/an de dividendes QQQ non comptés). Ré-exporter un CSV total-return (Yahoo Finance, Close non ajusté + Adj Close) pour que 6.3 produise son effet | ✅ Corrigé au Sprint 7 (7.4, `eb46e38`) : QQQ/SPY/IWM/MDY ré-exportés en total-return réel (Adj ≠ Close), B&H QQQ +262,08 %. Garde `auditTotalReturnCsv` (`43b4015`) pour ne plus jamais charger un export sans dividende sans le voir |
| D30 | 🟠 | (Sprint 7) **La config prod historique était dominée par le défaut sur données honnêtes.** Sur QQQ total-return, l'ancienne config prod (EMA 13/21, RSI 65/80, SL 7 %, TP 15 %, minHold 2 — celle figée au Sprint 6.1, jamais validée hors d'un dataset sans dividende) rend +4,45 % (Sharpe 0,30, 23 trades) vs le défaut +4,85 % (Sharpe 0,39, 6 trades). Le test côte à côte (6.1) a déclenché sa « Décision requise ». **Décision utilisateur** : aligner `prodSwingConfig()` sur le défaut validé (`eb46e38`). Leçon : une config « validée » sur de mauvaises données n'est pas validée — la qualité des données prime (D29). La vraie refonte est le Sprint 8 | ✅ Décidé/appliqué au Sprint 7 (prod = défaut) ; sera re-jugée en OOS au Sprint 8 |
| D31 | 🟢 | (Sprint 7) **L'ancien QQQ.csv comptait 1858 lignes pour une fenêtre de 1790 jours de bourse** (2019-01-02 → 2026-02-13) : ~68 lignes parasites (doublons/jours non boursiers) jamais détectées. Le ré-export total-return (Yahoo, 7.4) donne le compte correct (1790). Un garde-fou « nombre de barres ≈ jours de bourse attendus » dans `auditTotalReturnCsv` aurait pu l'attraper plus tôt | Documenté (résolu par le ré-export) ; garde-fou « densité de barres » au backlog qualité données |
| D32 | 🟠 | (Sprint 8) **Le filtre de régime SMA200 (8.1) a besoin de ≥ 200 barres, or la prod câble `getBars(symbol, 60)`** (TradingBot.hpp). Sans correctif, le bot LIVE ne recevrait jamais assez de barres → SMA vide → aucune entrée possible. **Amorce posée au Sprint 8.1** : `RiskConfig::lookback` (défaut 60, aligné sur `smaTrendPeriod+30` par la conversion `SwingConfig→RiskConfig`) ; `TradingBot::runOnce` demande `getBars(symbol, riskCfg_.lookback)`. Côté prod, `main_ibkr` (via `prodSwingConfig`) demandera donc 230 barres — à vérifier sur le CP Gateway IBKR. Le volet COMPLET (lookback unifié prod/backtest, barres clôturées) reste **D19/Sprint 9.2** | Amorce Sprint 8.1 (`1e6f6fd`) ; volet complet Sprint 9.2 |
| D33 | ✅ | (Sprint 8) **Les verrous de verdict OOS étaient construits sur `prodSwingConfig()` = les DÉFAUTS de SwingConfig** (`test_strategy_v2_integration.cpp`) : le premier item de 8.2–8.5 qui adoptait son réglage comme nouveau défaut aurait déplacé silencieusement la mesure HISTORIQUE du verdict 8.1. Corrigé AVANT tout changement de défaut (`a0ab08d`) : chaque verrou construit sa config champ par champ (`cfg81()`, `cfg82()`…). **Règle de DoD adoptée : tout verrou de verdict construit sa config EXPLICITEMENT, jamais depuis les défauts** | Corrigé (commit prep du sprint) ; règle ajoutée à la DoD des verdicts |
| D34 | 🟡 | (Sprint 8) **Les 2 fenêtres OOS du pavage IS=700/OOS=400 ne portaient AUCUN trade sous la config 8.1** : l'alpha OOS −14,10 du verdict 8.1 était du pur cash drag (jamais en position). Conséquence : l'acceptation de 8.2 (« gain moyen des gagnants ↑ ») était INDÉCIDABLE à son tour de rôle — adoptée comme retrait de contrainte morte, ré-examinée (et validée : −10,03 vs −10,20) sur la chaîne finale une fois 8.3/8.5 livrés. Leçon : un verdict OOS sans trades ne juge que le temps en cash ; vérifier le nombre de trades poolés AVANT d'interpréter les ratios. Le pavage fin (8b.3) réduit ce risque structurellement | ✅ Adressé au Sprint 8-bis (8b.3, `481f713`) : 13 trades OOS poolés sur 4 fenêtres, chaque verrou fige désormais son NOMBRE de trades. Résidu : l'état 8.1 reste à 0 trade OOS même sur 4 fenêtres (constat, pas un défaut du pavage) |
| D35 | 🟡 | (Sprint 8-bis) **Le warmup local de `runRange` (~201 barres, SMA200) consomme le début de CHAQUE fenêtre** (`BackTester.hpp:142-145` : chaque sous-fenêtre ré-amorce ses indicateurs — isolation honnête, mais coût fixe). Une fenêtre OOS de 250 barres n'aurait que ~49 barres tradables → verdict quasi pur cash drag (le piège D34 sous une autre forme). Règle adoptée : **dimensionner toute fenêtre OOS largement au-dessus du warmup** (OOS=300 → ~99 tradables pour le pavage fin ; l'exemple littéral « OOS=250 » de l'item 8b.3 a été ajusté en conséquence, décision utilisateur 2026-07-02). Garde-fou possible (backlog) : `WalkForward` pourrait AVERTIR si `oosBars ≤ warmup + marge` | Documenté (choix consigné dans `test_strategy_v2_integration.cpp`) ; garde-fou au backlog qualité harnais |
| D36 | 🟠 | (Sprint 8-bis) **Premier candidat d'edge — mais le plateau de grille n'est PAS stable entre les tailles de grille.** La grille 18 combos retient (emaFast=9, smaT=250, trail=0,05) ; la grille pleine 81 combos (axes emaSlow/emaFast élargis) retient (emaFast=5, emaSlow=50, smaT=250, trail=0,03). Seul **smaTrendPeriod=250** est commun aux deux plateaux. S'y ajoutent le biais de sélection (le « meilleur de N combos » est jugé sur les MÊMES fenêtres OOS que son verdict) et des échantillons minces (4-8 trades OOS/actif). Leçon : un gagnant de grille n'est pas un edge — c'est une HYPOTHÈSE à confirmer hors de la grille qui l'a choisie. **Décision utilisateur (2026-07-02) : consigner sans adopter** | ✅ Soldée au Sprint 8-ter (2026-07-03) : la validation hors-grille RÉFUTE le candidat sur les 3 volets (8t.1 fenêtres non-choisies : −19,10 vs −9,90 ; 8t.2 Monte-Carlo : CAGR p50 4,41 < 6,60 ; 8t.3 grille resserrée : plateau dérivé vers 275/0,04). La méfiance de D36 était justifiée — le processus a fonctionné |
| D37 | 🔴 | (Sprint Sécurité-Réel) **Le backtest avait un biais de LOOK-AHEAD structurel** : décision ET fill au close de la même barre (`BackTester.hpp` + `PaperBroker`), alors qu'en prod la décision porte sur le close d'hier et l'exécution se fait au prix d'aujourd'hui. TOUTES les mesures historiques (goldens, verdicts OOS 8.x, grille 8b.1, Monte-Carlo) étaient prises sur un moteur flatté. Corrigé (S.8, `f89d35a` : exécution à l'open i+1) et re-mesuré : aucune inversion des verdicts qualitatifs (DoD toujours non atteinte, pas d'edge multi-actifs), l'in-sample QQQ monte même de +18,70 à +19,33 %. **MAIS le verdict de grille 8b.1 change** : le plateau devient (emaFast=9, smaT=250, **trail=0,03**) avec alpha OOS **+0,23** (vs 0,05/+0,10 pré-B2 — le candidat SURVIT à la correction, bon signe), et l'axe le plus sensible devient **trailingStopPct** (0,266 vs smaT 0,194) → **le gate 8b.4 (trailing adaptatif ATR) s'OUVRE désormais**. Le Sprint 8-ter doit re-caler son point d'entrée (candidat = 9/250/0,03) et ses grilles de confirmation | ✅ Re-calage fait au Sprint 8-ter (grille 8t.3 centrée sur 9/250/0,03, section 6 du CLI corrigée trail 0,05 → 0,03) ; le candidat re-calé est ensuite RÉFUTÉ (voir D36). Le gate 8b.4 ouvert par cette découverte devient le Sprint 8-quater |
| D38 | ✅ | (Sprint Sécurité-Réel) **`IBKRBroker::residentStopOrderId_` n'est pas persisté** : après un restart, `cancelStopLoss` ne retrouve pas l'orderId du stop résident déposé par le process précédent → l'annulation à la sortie est un no-op et le stop IBKR peut rester orphelin chez le broker (se déclencherait après coup). Mitigé par : (a) `stopArmed` persisté empêche d'empiler un 2e stop, (b) le cas « position fermée par le stop résident » est réconcilié proprement (reset + cooldown de ré-entrée) | ✅ Corrigé à la 2e passe (S.10, `75339e2`) : re-découverte broker-locale via GET `/iserver/account/orders` + tag cOID « swingbot-SYM-STOP- » — pas de persistance nécessaire |
| D39 | 🟡 | (Sprint 8-ter) **Un axe de grille n'est « stable » qu'à la maille où on l'a mesuré.** D36 concluait « seul smaT=250 est stable » entre les grilles 18 et 81 combos (crans 150/200/250) ; la grille de CONFIRMATION resserrée (crans 225/250/275, 8t.3) fait dériver ce même axe vers 275 (et le trailing vers 0,04). Leçon générale : la stabilité d'un plateau doit être testée en RESSERRANT les crans autour du gagnant (le mécanisme 8t.3, désormais réutilisable), pas seulement en élargissant la grille. Garde-fou adopté de fait : toute future validation de candidat inclut une grille resserrée verrouillée | Documenté (mécanisme en place : `CandidateConfirmationTightGridOosVerdictIsLocked` sert de modèle) |

## Changelog

### Sprint 8-ter — Valider le candidat d'edge hors-grille (2026-07-03)

**Baseline réelle à l'ouverture** : **537/537 verte**, conforme au tableau de bord
(`ctest -N` = 537, aucune dérive hors cycle). Environnement : Linux, paquets
système (chemin CI, sans vcpkg).

**Décisions utilisateur d'ouverture** : (1) pavage décalé de 8t.1 =
IS=500/**OOS=400**/pas=400/**offset=90** et non l'exemple littéral OOS=300
(warmup candidat ~251 barres → ~49 tradables, piège D34/D35 — même arbitrage
que D35 au Sprint 8-bis) ; (2) périmètre = sprint complet + clôture.

**Commits** (ordre chronologique = ordre d'exécution) :
- `f332f33` feat(backtest) : offset de départ additif du pavage walk-forward (préparation 8t.1)
- `401067a` test(backtest) : verdict hors-grille du candidat vs chaîne v2, pavages canonique et décalé (item 8t.1)
- `fff8a2a` test(backtest) : Monte-Carlo des trades OOS poolés du candidat vs chaîne v2 (item 8t.2)
- `05569e1` test(backtest) : grille de confirmation resserrée autour du candidat, 27 combos (item 8t.3)

**Tests** : 537 → **545** (+8 : 468 unitaires + 77 intégration). Ajouts :
`WalkForwardUnit` +3 (pavage décalé aux bornes exactes, rétro-compat offset=0
bit-identique, série trop courte pour l'offset), nouvelle suite
`CandidateValidationIntegration` +4 (structure du pavage décalé, verdicts
canonique/décalé, Monte-Carlo), `GridOptimizerIntegration` +1 (grille resserrée).
Discipline « sentinelles → passe rouge → figer » sur les 4 verrous de mesure ;
rouge de compilation (ctor 6 arguments inexistant) pour l'offset. Sprint 100 %
MESURE : le seul code produit touché est `WalkForward.hpp` (paramètre additif,
défaut 0 = pavage historique inchangé) — **goldens et verrous historiques
strictement intacts** (recoupement : la chaîne rend exactement −9,9023 / 11
trades sur le pavage canonique, conforme au verrou de
`test_strategy_v2_integration.cpp`).

**Verdicts OUT-OF-SAMPLE du sprint** (tous verrouillés par test — le candidat
post-B2 (9/250/0,03) vs la chaîne v2, configs explicites D33) :
- **8t.1 (fenêtres non-choisies)** : **NON CONFIRMÉ** — pavage canonique
  700/400 : candidat −19,10 vs chaîne −9,90 (pire de 9,2 pts, 10 vs 11 trades) ;
  pavage décalé 500/400 offset 90 : candidat −5,38 vs chaîne −12,83 (+7,45 pts).
  Le signe de la comparaison s'INVERSE selon le pavage → l'avantage mesuré par
  la grille 8b.1 était un artefact de ses fenêtres, pas un edge.
- **8t.2 (Monte-Carlo, trades OOS du pavage décalé, 4,77 ans)** : **NON
  CONFIRMÉ** — CAGR p50 candidat 4,41 % < chaîne 6,60 % (critère « ≥ » échoué
  même sur le pavage favorable). Acquis verrouillé : risque plus faible
  (DD p95 11,71 % vs 16,48 %, DD p50 5,32 % vs 7,93 %).
- **8t.3 (grille resserrée 27 combos, candidat au centre)** : **ARTEFACT DE
  GRILLE** — alpha > 0 passe mais le plateau dérive vers (9, 275, 0,04) : il ne
  contient pas smaT=250 (D39 — même l'axe « stable » de D36 bouge avec la maille).
- **8t.4** : condition d'adoption FAUSSE (0/3) → branche « sinon » mécanique :
  « candidat non confirmé » consigné, aucune adoption, prod paper. Suite
  décidée avec l'utilisateur : **Sprint 8-quater** (trailing ATR, gate 8b.4).

**Découvertes** : D39 (la stabilité d'un axe de grille dépend de la maille —
le mécanisme « grille resserrée verrouillée » devient le garde-fou standard).
D36 et D37 soldées (validation hors-grille rendue ; re-calage post-B2 fait).

**Interfaces modifiées** (additives uniquement) : `WalkForward` gagne un 6e
paramètre `offset` (défaut 0, rétro-compat bit-identique verrouillée) ;
`printReport` affiche le décalage s'il est non nul. CLI `validate` : section 6
re-calée sur le candidat post-B2 (trail 0,05 → 0,03, D37), nouvelles sections
7 (duel par pavage), 7-bis (Monte-Carlo du duel), 7-ter (grille resserrée).

### Sprint Sécurité-Réel, 3e passe — durcissement production (2026-07-02)

**Baseline réelle à l'ouverture** : **510/510 verte** (`ctest -N` conforme).

**Origine** : demande utilisateur — « analyse le workflow, les fichiers md,
skills et rules ; tout doit être solide, l'app va trader de l'argent réel ».
Verdict d'audit : moteur mûr, environnement de production de niveau prototype
(gate live inexistant, watchdog vert pendant les pannes, secrets hardcodés,
binaire de prod jamais testé en Release, ni runbook ni checklist, processus
capable de réécrire ses propres règles).

**Commits** : `4fea48e` -Werror · `d949da2` santé du cycle · `8c0cc60`
alerte kill-switch · `f0048ad` kill-switch dans prod.json · `336f0f1` gate
live · `402cafc` secrets env · `9142ee6` boucle live défensive · `5500e73`
CI Release+sanitizers · `f28899b` Docker prod · `aa488c0` docs/règles.

**Tests** : 510 → **537** (+27 : 465 unitaires + 72 intégration — santé du
cycle 5, kill-switch alerte 3, loader killSwitch 4, LiveGate 7, ProdSettings 3,
env 4, verrou live 1). La suite passe en Debug, **Release**, **ASan/UBSan**
et TSan ciblé. **Goldens strictement inchangés** (A1/C1 golden-neutres par
construction, vérifié).

**Trouvailles des sanitizers (corrigées dans la passe)** : fuites du handle
sqlite3 dans les constructeurs qui lèvent (`DbLogger`, `SqliteStateStore` —
LeakSanitizer) ; data race `MiniHttpServer::stop()/loop_()` (TSan) ;
`-Wno-tsan` requis pour `atomic_thread_fence` de Boost (faux positif compile).

**Nouveaux verrous de gouvernance** :
- `LiveTradingStaysDisapprovedUntilEdgeDoD` : `liveTradingApproved` ne peut
  passer à true sans re-figer ce test (décision utilisateur + checklist
  RUNBOOK) — le « paper tant que pas d'edge » est un mécanisme.
- Les fichiers de règles (`prompt-*.md`, DoD, Live-safety rules de CLAUDE.md)
  ne sont plus auto-amendables : proposition de diff → décision utilisateur.
- Politique de risque = bloc `killSwitch` de prod.json, backtesté par le
  golden ; RUNBOOK §6 la documente en clair.

**Reste hors périmètre** (assumé) : dashboard React ; mock SMTP (email testé
en échec seulement) ; branch protection GitHub (réglage du dépôt, côté
utilisateur) ; épinglage des versions apt ; ré-auth AUTOMATIQUE du Gateway
(la détection + alerte existent, la ré-auth reste manuelle — navigateur).

### Sprint Sécurité-Réel, 2e passe — corrections restantes de l'audit (2026-07-02)

**Baseline réelle à l'ouverture** : **495/495 verte** (`ctest -N` conforme).

**Origine** : demande utilisateur « fait les corrections » — les 4 restes de
l'audit (hors features : dashboard et Sprint 8-ter restent à part).

**Commits** :
- `75339e2` fix(ibkr) : re-découverte de l'orderId du stop résident (D38)
- `2d2d962` fix(feed) : barres clôturées seulement côté IBKR (9.3, E6/D25)
- `ac9382a` test(watchdog) : E2E réel du canal SMS Twilio (twilio_base_url)
- `1e617a6` feat(config) : config prod externalisée en JSON validé (9.1, E1/D21)

**Tests** : 495 → **510** (+15 : 439 unitaires + 71 intégration — D38 +3,
calendrier +2, feed +3, ConfigLoader +6, SMS +1). **Goldens strictement
inchangés** : les 3 goldens « config prod » passent désormais par
config/prod.json sans re-figeage (c'est l'acceptation 9.1), le backtest ne
touche ni CsvDataFeed ni ReplayDataFeed.

**Items ROADMAP soldés** : D38 ✅ (re-découverte broker-locale, pas de
persistance nécessaire), 9.1 ✅ (JSON strict partagé prod/golden), 9.3 ✅
(volet barres clôturées ; le calendrier UTC/DST+fériés datait de la 1re
passe). Restent ouverts au Sprint 9 : 9.2 (lookback unifié D19) et 9.4
(procédure de re-calibration).

**Interfaces modifiées** (additives uniquement) : `AlertConfig.twilio_base_url`
(défaut prod inchangé) ; `IBKRDataFeed::now_()` protégé virtuel (horloge
injectable) ; `usEasternDateOfUtc` (market_calendar.h) ; nouveau
`ConfigLoader.hpp` (`loadSwingConfigJson`) ; macro compile-time
`SWINGBOT_PROD_CONFIG_JSON` sur swing_bot/validate/integration_tests.

### Sprint Sécurité-Réel — Audit complet + correctifs bloquants (2026-07-02)

**Baseline réelle à l'ouverture** : **470/470 verte**, conforme au tableau de bord.
Environnement : Linux, paquets système (chemin CI, sans vcpkg).

**Origine** : demande utilisateur d'analyse complète (docs, workflow, code, bot) +
verdict « prêt à trader ? » + améliorations. **Verdict d'audit : NON prêt** —
l'architecture et les tests sont solides, mais 4 bloquants réels : le stop résident
broker ne s'armait presque jamais (B1), le backtest avait un look-ahead structurel
(B2), deux pannes silencieuses « le bot ne trade jamais sans le dire » (B3.1/B3.2),
et le calendrier de repli ignorait les fériés NYSE (B4).

**Commits** (ordre chronologique = ordre d'exécution) :
- `b0410fe` fix(ibkr) : résumé de compte incomplet = INACTIVE bruyant (B3.1)
- `0d2c2ad` fix(strategy) : historique < SMA200 signalé explicitement (B3.2)
- `36a0823` fix(alpaca) : SqliteStateStore câblé dans main_alpaca (M3)
- `d62287e` feat(calendar) : jours fériés NYSE dans le repli horaire (B4)
- `dd939ca` feat(state) : BotState.stopArmed + lastExitDate, migration SQLite (socle)
- `c7949d8` fix(bot) : armement idempotent du stop résident à l'adoption + filet (B1)
- `d2d96d1` fix(bot) : cooldown de ré-entrée le jour d'une sortie (M2)
- `f89d35a` fix(backtest) : correction du look-ahead, exécution à l'open i+1 (B2)
- `9f859fe` chore(infra+doc) : docker-compose, DB dans data/, CLAUDE.md, UML

**Tests** : 470 → **495** (+25 : 425 unitaires + 70 intégration). Discipline
rouge→vert respectée item par item ; les goldens d'intégration sont restés
INCHANGÉS jusqu'à S.7 inclus (preuve que B1/M2/B3 ne touchent pas le backtest),
puis re-figés en une seule passe à S.8.

**RE-BASELINE B2 (anciennes → nouvelles valeurs ; changement de vérité de mesure,
pas de stratégie)** :
- Golden QQQ in-sample : retour +18,6980 → **+19,3257 %**, finalValue 11 869,80 →
  **11 932,57 $**, Sharpe 0,7246 → **0,7347**, maxDD 6,5477 → **6,2429 %**,
  22 trades (13G/9P → **12G/10P**), 1re entrée 2019-10-18 → **2019-10-21**,
  dernière vente 2022-02-07 → **2022-02-14**. Contrôles de santé INCHANGÉS :
  B&H +226,1235 %, 1790 points d'équité.
- Verdicts OOS v2 (2 fenêtres) : alpha chaîne −10,0277 → **−9,9023** ; 8.3 espV
  2,9667 → **12,0572**, alpha −14,1234 → **−13,8959** ; 8.4 gainV 4,1858 →
  **5,3494**, PF 1,8411 → **2,2147**, alpha −13,1085 → **−12,5272** ; ré-exam TP
  −10,2012 → **−10,1140** ; gagnants 4,4644 → **5,6792 %**, PF 3,7088 → **3,2203**,
  espérance 77,9315 → **80,2136 $**. Pavage fin : alpha chaîne −7,1454 →
  **−6,8794**, expo 73,7374 → **74,7475 %**, gagnants 7,4076 → **8,0393 %**,
  PF 1,9784 → **2,0571**, espérance 62,5162 → **70,6659 $**. Aucune inversion :
  DoD toujours non atteinte.
- Multi-actifs : SPY −5,5162 → **−5,6813**, IWM −3,0086 → **−3,4564**,
  MDY −3,6029 → **−3,5306** (expo MDY 68,4343 → **68,9394**). Toujours aucun
  alpha OOS > 0 pour la chaîne par défaut.
- Grille 8b.1 : plateau (9, 250, 0,05) → **(9, 250, 0,03)**, Sharpe OOS 1,2193 →
  **1,2674**, alpha +0,0971 → **+0,2312** ; axe le plus sensible smaT (0,244) →
  **trailingStopPct (0,266)** → gate 8b.4 OUVERT. Voir D37.
- Monte-Carlo : CAGR p50 6,7628 → **6,9504 %**, DD p50 7,6477 → **8,5920 %**.

**Découvertes** : D37 (look-ahead + inversion du verdict de grille), D38 (orderId
du stop résident IBKR non persisté). D27 est soldée par S.6 (c'était son fix).

**Reste à faire avant tout réel** (inchangé sur le fond) : edge non démontré
(Sprint 8-ter à re-caler post-B2), externalisation de config (9.1), barres
clôturées (9.3), alertes watchdog non testées de bout en bout, pas de dashboard.

### Sprint 8-bis — Chercher l'edge avec le harnais (2026-07-02)

**Baseline réelle à l'ouverture** : **460/460 verte**, conforme au tableau de bord
(`ctest -N` = 460, aucune dérive hors cycle).

**Décisions utilisateur d'ouverture** : (1) sprint complet + clôture ; (2) ordre
**8b.3 → 8b.1 → 8b.2 → (8b.4)** — le pavage fin d'abord pour que grille et
multi-actifs soient jugés sur 4 fenêtres OOS (leçon D34) ; (3) pavage fin
IS=500/**OOS=300**/pas=300 et non l'exemple littéral OOS=250 (warmup ~201 barres
par fenêtre → D35).

**Commits** (ordre chronologique = ordre d'exécution) :
- `481f713` test(backtest) : pavage walk-forward fin (IS=500/OOS=300, 4 fenêtres OOS) — verdict chaîne re-verrouillé (item 8b.3, D34)
- `77ff922` feat(backtest) : axes smaTrendPeriod/trailingStopPct + sensibilité par axe dans GridOptimizer (item 8b.1)
- `de7bdcf` test(backtest) : grille étendue sur la chaîne v2 jugée en OOS fin — premier candidat d'edge verrouillé (item 8b.1)
- `ba22444` test(backtest) : verdicts OOS de la chaîne v2 par actif SPY/IWM/MDY, mêmes fenêtres (item 8b.2)

**Tests** : 460 → **470** (+10 : 401 unitaires + 69 intégration). Ajouts :
`GridOptimizerUnit` +4 (énumération étendue, repli singleton rétro-compatible,
plateau sur les nouveaux axes, classement de sensibilité), `StrategyV2Integration`
+2 (structure du pavage fin, verdict chaîne fin), `GridOptimizerIntegration` +1
(verdict grille v2), `MultiAssetIntegration` +3 (un verdict par actif). Discipline
rouge→vert : rouges de compilation (ctor 8 axes inexistant) puis sentinelles de
mesure figées après la passe rouge. Sprint 100 % MESURE : **aucun changement de
comportement de la stratégie, goldens INCHANGÉS** (le seul code produit touché est
`GridOptimizer.hpp`, hors chemin d'exécution du bot ; extension prouvée
rétro-compatible par les 4 tests unitaires historiques inchangés).

**Verdicts OUT-OF-SAMPLE du sprint** (tous verrouillés par test) :
- **8b.3 (pavage fin, 4 fenêtres)** : AUCUNE inversion vs 2 fenêtres — chaîne
  −7,15 vs état 8.1 −9,05 (delta +1,91), 13 trades OOS poolés (gagnants +7,41 %,
  PF 1,98, espérance +62,52 $), exposition 73,74 %. DoD toujours non atteinte.
  L'état 8.1 reste à 0 trade OOS même sur 4 fenêtres (cash drag pur).
- **8b.1 (grille étendue)** : **premier candidat d'edge du projet** — le filtre
  alpha > 0 PASSE au plateau (emaFast=9, smaT=250, trail=0,05) : Sharpe OOS 1,22,
  alpha OOS +0,10. Axe le plus sensible : smaTrendPeriod (0,244 vs trail 0,210
  sur 18 combos ; confirmé 0,312 vs 0,254 sur 81 combos).
- **8b.2 (multi-actifs, chaîne v2)** : aucun alpha OOS > 0 (SPY −5,52, IWM −3,01,
  MDY −3,60 vs QQQ −7,15, chacun vs SON B&H) mais comportement cohérent partout
  (10-17 trades, exposition 53-81 %) — pas un artefact QQQ. Le candidat 8b.1,
  imprimé par le CLI par actif : +0,10 QQQ / −0,53 SPY / +1,26 IWM / +0,81 MDY
  → alpha > 0 sur 3/4 actifs (la donnée qui fonde le Sprint 8-ter).
- **8b.4 (gate)** : FERMÉ — trailingStopPct n'est pas l'axe le plus sensible
  (3e sur 4 sur la grille pleine). Aucun code, item résolu par sa condition.

**Décision utilisateur de clôture** : le candidat est **consigné, PAS adopté**
(biais de sélection, échantillons minces, plateau EMA instable entre 18 et 81
combos — D36). Le **Sprint 8-ter** lui fait passer une validation hors-grille
(fenêtres non-choisies, Monte-Carlo, grille resserrée) avant toute décision
d'adoption. Défauts de SwingConfig, ProdConfig et goldens : inchangés.

**Interfaces modifiées** (additives uniquement) :
- `GridOptimizer` : 2 axes optionnels en fin de ctor (`smaTrendPeriod`,
  `trailingStopPct` ; vide = singleton depuis la base → appels historiques
  inchangés), `axisSensitivities()` + `axisName()` (mécanisme du gate 8b.4),
  carte de sensibilité à 8 colonnes + classement par axe.
- CLI `validate` : pavage fin (2-bis), grille pleine 81 combos sur les axes v2
  (3), walk-forward multi-actifs chaîne + candidat (6).

### Sprint 8 (clôture) — Laisser courir, entrer sur la force, rester investi (2026-07-01)

**Baseline réelle à l'ouverture** : **440/440 verte**, conforme au tableau de bord
(`ctest -N` = 440, aucune dérive hors cycle).

**Portée** (décision utilisateur d'ouverture) : TOUT le reliquat 8.2 → 8.3 → 8.4 → 8.5
+ clôture. Design 8.5 arbitré : re-entrée sur régime (flag, A/B-able, jugée OOS).

**Commits** (ordre chronologique = ordre d'exécution) :
- `a0ab08d` test : fige la config du verdict OOS 8.1 indépendamment des défauts (préparation 8.2-8.5, D33)
- `26d645d` feat(risque) : take-profit désactivable (≤ 0) — laisser courir les gagnants (item 8.2, D26)
- `57a4014` feat(strategie) : entrée sur la force — plafond RSI d'achat désactivable (item 8.3, D26)
- `85f0483` feat(strategie) : pas de vente sur RSI seul en régime haussier (item 8.4, D26)
- `4887817` feat(strategie) : re-entrée sur régime haussier sans croisement — réduire le cash drag (item 8.5, D26/T4)

**Tests** : 440 → **460** (+20 : 397 unitaires + 63 intégration). Ajouts :
`RiskManagerUnit` +4 (TP désactivé ×2, trailing/SL toujours actifs),
`SwingStrategyUnit` +11 (plafond RSI off ×2, vente RSI gatée ×4, re-entrée ×5,
défauts verrouillés), `StrategyV2Integration` +5 (verdicts 8.2/8.3/8.4/8.5 +
verdict de sprint chaîne-vs-8.1). Chaque fix précédé d'un test ROUGE réel
(comportement pathologique TP=0, HOLD à RSI 100, SELL en régime up, HOLD sans
croisement).

**Défauts de SwingConfig adoptés** (chaque adoption gatée par son acceptation OOS) :
`takeProfitPct` 0,10 → 0 (≤ 0 = off) — aussi dans `RiskConfig` ; `rsiBuyMax`
55 → 100 (≥ 100 = off) ; `rsiSellOnlyIfRegimeDown` (nouveau) = true ;
`regimeReentry` (nouveau) = true. `prodSwingConfig()` reste `SwingConfig{}`
(prod ≡ défaut, D30 — verrou inchangé).

**Verdicts OUT-OF-SAMPLE par item** (walk-forward IS=700/OOS=400, 2 fenêtres QQQ,
chaîne cumulative — chaque item jugé avec les précédents retenus ; tous verrouillés
dans `test_strategy_v2_integration.cpp`) :
- 8.2 : delta STRICTEMENT NUL (0 trade OOS sous la config 8.1 — D34) → contrainte
  morte retirée ; ré-exam sur la chaîne finale : TP off VALIDÉ (−10,03 vs −10,20).
- 8.3 : SATISFAITE — trades 0 → 5, exposition 0 → 22,11 %, espérance +2,97 $/trade.
- 8.4 : SATISFAITE — gagnants 2,46 → 4,19 %, PF 1,06 → 1,84, alpha −14,12 → −13,11.
- 8.5 : SATISFAITE — temps investi 30,65 → 54,02 %, alpha −13,11 → −10,03, 5 → 11 trades.
- **Verdict de sprint** (chaîne 8.1→8.5 vs état 8.1) : alpha OOS **−14,10 → −10,03**
  (+4,07 pts), 11 trades OOS (gagnants +4,46 %, PF 3,71, espérance +77,93 $).
  **DoD NON ATTEINTE** (sous-performance > 5 pts vs B&H) → « pas d'edge démontré,
  pas de déploiement » — verrouillé par test (`EXPECT_LT(alphaC, -5.0)` : si ce
  verrou casse « dans le bon sens », re-dérouler la DoD complète avant déploiement).

**Golden backtest RE-FIGÉ trois fois** (une par changement de comportement, chacun
dans son commit avec delta chiffré ; 8.2 : goldens INCHANGÉS, vérifié) :
- 8.3 : retour total −1,0678 → −4,5165 %, 4 → 20 trades (9 G / 11 P), max DD 7,61 %,
  Sharpe −0,27.
- 8.4 : −4,5165 → +0,0176 % (mêmes 20 trades, 4 sorties « signal » devenues trailing :
  15 → 19 trailing / 5 → 1 signal), max DD 7,19 %, Sharpe +0,02.
- 8.5 : +0,0176 → **+18,6980 %**, 22 trades (13 G / 9 P, 21 trailing / 1 signal),
  max DD **6,5477 %**, Sharpe **0,7246**, 1re entrée 2019-10-18, position finale
  ouverte jusqu'au bout (dernière vente clôturée 2022-02-07), 1790 points d'équité.
- Invariant vérifié aux trois re-figeages : **B&H +226,1235 % inchangé** (warmup intact).
- Monte-Carlo (graine 42, 2000 chemins) : p50 CAGR −0,32 → **+6,76 %**, p50 drawdown
  5,45 → **7,65 %** (re-figés à 8.3, 8.4 et 8.5).

**Interfaces modifiées** (toutes additives / config-toggleables, aucun changement
de TradingBot ni de CrossoverDetector) :
- `RiskManager::checkExitConditions` : convention `takeProfitPct ≤ 0 = désactivé`.
- `SwingConfig` : conventions ≤ 0 (TP) et ≥ 100 (rsiBuyMax) ; +`rsiSellOnlyIfRegimeDown`,
  +`regimeReentry` ; `SwingStrategy::evaluate` gagne le gate de vente et la branche
  de re-entrée (APRÈS les ventes — priorité aux sorties).
- Tests : `cfg81()`…`cfg84()` (snapshots explicites) + agrégats sur trades OOS poolés
  (`tradesOos`, `gainMoyenGagnants`, `facteurProfit`, `esperanceParTrade`, `moyenneOos`).

**Enseignement central** : la chaîne cumulative A/B a rendu chaque euro d'alpha
traçable — régime +1,99, entrée sur la force ~0 (mais crée l'échantillon), vente
gatée +1,01, re-entrée +3,08. Les deux items « cash drag » (8.3 + 8.5) pèsent
l'essentiel de l'exposition (0 → 54 %), et la qualité des sorties (8.4) ne devient
mesurable qu'une fois l'échantillon créé par 8.3 : l'ORDRE des items était la bonne
dépendance. Et la leçon 8.1 tient toujours : le plein échantillon (+18,70 %) reste
un VERROU, pas une preuve — le juge est l'OOS, qui dit « pas encore d'edge ».

### Sprint 8 (point d'étape) — Prérequis indicateurs + filtre de régime (2026-06-25)

**Baseline réelle à l'ouverture** : **423/423 verte**, conforme au tableau de bord
(`ctest -N` = 423, aucune dérive hors cycle).

**Portée de cette session** (décision utilisateur) : 8.0 + 8.1 uniquement, validés en
OOS, puis **pause de réévaluation** avant 8.2–8.5.

**Commits** (ordre chronologique = ordre d'exécution) :
- `8e3f72f` refactor(core) : `IIndicator::computeBars` sur barres OHLCV, défaut rétro-compatible (item 8.0, D18)
- `b855974` feat(indicateurs) : vrai true-range ATR via `computeBars` + VWAP `computeBars` + migration DayTradeStrategy (item 8.0, D18)
- `1264f7a` feat(indicateurs) : indicateur SMA (moyenne mobile simple) (item 8.1)
- `1e6f6fd` feat(strategie) : filtre de régime SMA200 à l'entrée + verdict OOS verrouillé (item 8.1, D26)

**Tests** : 423 → **440** (+17). Répartition : 382 unitaires + 58 intégration.
Ajouts : `IndicatorsUnit` +8 (computeBars EMA/RSI, SMA ×6), `DayIndicatorsUnit` +4
(ATR true-range ×3, VWAP computeBars), `SwingStrategyUnit` +3 (régime gaté ×2, lookback),
`StrategyV2Integration` +2 (pavage de fenêtres, verdict OOS). Goldens existants re-figés
(pas de nouveaux fichiers de test côté backtest/Monte-Carlo, valeurs mises à jour).

**Golden backtest RE-FIGÉ** (item 8.1, justification : ajout du filtre de régime SMA200,
changement de comportement volontaire — warmup 37 → 201 car la SMA200 a besoin de 200
clôtures) :
- Config défaut ET prod (toujours identiques, D30) : retour total **−1,0678 %**
  (était +4,8537), capital final **9 893,22 $**, **4 trades** (2 G / 2 P ; 0 SL / 0 TP /
  4 trailing / 0 signal), max DD **3,7547 %**, Sharpe **−0,1166**, B&H **+226,1235 %**
  (mesuré après le warmup décalé), 1er achat 2020-04-08, dernière vente 2026-02-04,
  1790 points d'équité (inchangé).
- Monte-Carlo (graine 42, 2000 chemins, 4 trades prod) : p50 CAGR **−0,3219 %**, p50
  drawdown **5,4521 %** (figés).

**Verdict OUT-OF-SAMPLE 8.1** (walk-forward IS=700 / OOS=400 / pas=400, 2 fenêtres QQQ,
`test_strategy_v2_integration.cpp`) : alpha OOS moyen **−14,10 pts (régime SMA200) vs
−16,09 pts (filtre désactivé)** → le filtre AMÉLIORE l'alpha OOS de **+1,99 pt**.
Acceptation 8.1 (« alpha net OOS > version actuelle ») **satisfaite**, alors même que le
plein échantillon (in-sample) semblait DÉGRADÉ (−1,07 % vs +4,85 %) — illustration directe
de la valeur du jugement OOS sur l'IS. **Les deux restent négatifs** : aucune ne bat le
Buy & Hold → pas d'edge, pas de déploiement.

**Interfaces modifiées** (toutes additives / rétro-compatibles) :
- `IIndicator<T>::computeBars(const std::vector<Bar>&)` — virtuelle avec défaut (extrait les
  clôtures, délègue à `compute`) ; surchargée par ATR (vrai TR) et VWAP.
- `SwingConfig::smaTrendPeriod` (défaut 200 ; **≤ 1 désactive** le filtre — base de comparaison OOS).
- `RiskConfig::lookback` (défaut 60 ; aligné sur `smaTrendPeriod+30` par la conversion).
- `SwingStrategy` : 4e indicateur injecté (SMA de régime) au constructeur et au factory.
- `Backtester::runRange` : warmup et lookback de replay couvrent désormais `smaTrendPeriod`.

**Enseignement central** : on ne juge JAMAIS une modif de stratégie sur le plein échantillon.
Le filtre de régime a un alpha in-sample plus mauvais MAIS un alpha OOS meilleur — sans le
harnais OOS du Sprint 7, on l'aurait rejeté à tort. Le levier dominant restant est le **cash
drag** (temps investi ~2,4 %) : c'est l'objet de 8.3/8.5 (à arbitrer en session suivante).

**Rétrospective (point d'étape Sprint 8)** :
1. *Découpage* : bon. Mettre 8.0 (prérequis indicateurs) avant 8.1 était correct ; 8.1 seul
   est une unité validable de bout en bout. La **dépendance cachée** la plus importante n'était
   pas entre items mais entre 8.1 et la plomberie de fenêtre de données (`getBars(60)` + lookback
   de replay) : sans elle la SMA200 reste vide. Repérée tôt grâce à la lecture du code (le plan
   l'avait anticipée), pas en cours de route.
2. *Prompts du workflow* : suffisants, aucune improvisation nécessaire. La consigne « recaler le
   décompte via `ctest -N` » (D20) a confirmé 423 à l'ouverture sans dérive. Pas de modif des
   prompts requise.
3. *À détecter plus tôt / garde-fou* : le risque prod `getBars(60)` < SMA200 (D32) aurait pu être
   un piège silencieux (bot live qui n'achète jamais). Garde-fou ajouté de fait : `RiskConfig::lookback`
   dérivé de la config + test `RiskConfigLookbackCoversTrendPeriod`. Garde-fou DoD à considérer pour
   9.x : un test qui vérifie que `prodSwingConfig().lookback` couvre bien `smaTrendPeriod`.
4. *Notes /100 (point d'étape, re-scoring complet à la clôture du sprint)* : Architecture 85 → **86**
   (interface indicateurs enrichie proprement, additive), Qualité 89 → **89** (couverture +17, mais
   périmètre partiel), FinTech 79 → **79** (régime correct mais stratégie incomplète), Production 72
   → **72** (inchangé). **Rentabilité 20 → 20** : l'alpha OOS s'améliore (+1,99 pt) mais reste
   franchement négatif — la note ne montera qu'avec une stratégie qui bat (ou approche) le B&H.

### Sprint 7 — Harnais de validation (2026-06-25)

**Baseline réelle à l'ouverture** : **396/396 verte**, conforme au tableau de bord
(`ctest -N` = 396, aucune dérive hors cycle).

**Commits** (ordre chronologique ; ordre d'exécution réordonné 7.4-données → 7.1 →
7.2 → 7.3 → CLI : le ré-export total-return de QQQ devait précéder les verrous
d'intégration du walk-forward, décision utilisateur d'ouverture) :
- `eb46e38` feat(data) : ré-export QQQ total-return + SPY/IWM/MDY ; config prod alignée sur le défaut (item 7.4, D29, D30)
- `43b4015` feat(backtest) : garde qualité Adj Close==Close + évaluation multi-actifs (item 7.4, D29)
- `8e618fd` feat(backtest) : walk-forward IS/OOS + flag sur-ajusté (item 7.1, D24)
- `7d34349` feat(backtest) : optimiseur de grille + sélection plateau robuste en OOS (item 7.2)
- `4a29c1d` feat(backtest) : Monte-Carlo bootstrap → distribution CAGR/drawdown p5/p50/p95 (item 7.3)
- `c674c8e` feat(cli) : exécutable de validation walk-forward/grille/Monte-Carlo (livrable)

**Tests** : 396 → **423** (+27). Nouvelles suites : `DataQualityUnit` (5),
`WalkForwardUnit` (6), `GridOptimizerUnit` (4), `MonteCarloUnit` (6),
`MultiAssetIntegration` (3), `WalkForwardIntegration` (1), `GridOptimizerIntegration`
(1), `MonteCarloIntegration` (1). Répartition : 367 unitaires + 56 intégration.

**Golden backtest RE-FIGÉ** (item 7.4, justification : ré-export de QQQ.csv en série
total-return réelle — Adj Close ≠ Close, dividendes réinvestis, clôture de D29 — et
alignement de la config prod sur le défaut, D30) :
- Données : 1790 barres (au lieu de 1858 ; ~68 lignes parasites éliminées, D31),
  fenêtre [2019-01-02, 2026-02-13].
- Config défaut ET config prod (désormais identiques, D30) : retour total **+4,8537 %**,
  capital final **10 485,37 $**, **6 trades** (5 G / 1 P ; 0 SL / 0 TP / 5 trailing /
  1 signal), max DD **2,7044 %**, Sharpe **0,3941**, B&H **+262,0808 %**, alpha −257 pts.
- Monte-Carlo (graine 42, 2000 chemins, 6 trades prod) : p50 CAGR **1,9039 %**, p50
  drawdown **2,4668 %** (figés).

**Nouveaux fichiers** : `include/backtest/{WalkForward,GridOptimizer,MonteCarlo,DataQuality}.hpp`,
`main_validate.cpp`, `SPY.csv`/`IWM.csv`/`MDY.csv` (données total-return), 8 fichiers de tests.
**QQQ.csv remplacé** (total-return). `.gitignore` : exceptions explicites pour les CSV
de référence (la règle `*.csv` les masquait).
**Interfaces modifiées** (toutes additives) :
- `Backtester::runRange(csv, startIdx, endIdx)` (public) ; `run()` y délègue sur
  `[0, N)` → golden inchangé par construction.
- `ReplayDataFeed(csv, lookback, floorIdx = 0)` — floorIdx par défaut 0 = ancien
  comportement (isolation par fenêtre pour le walk-forward).
- `prodSwingConfig()` (ProdConfig.hpp) renvoie désormais le `SwingConfig{}` par défaut (D30).

**Enseignement central** : le harnais ne prouve pas un edge — il prouve son ABSENCE
en OOS, et c'est précisément sa valeur. La grille affiche « AUCUN edge — ne pas
deployer ». Le Sprint 8 part d'un diagnostic honnête et chiffré, pas d'un espoir.

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

### Sprint 8-ter — Valider le candidat d'edge hors-grille (2026-07-03)

**1. Découpage** : bon — 3 items de mesure indépendants + un gate de décision, et
l'ORDRE avait une logique de coût : 8t.1 (le juge le plus dur, fenêtres non-choisies)
d'abord ; dès son verdict, 8t.2 et 8t.3 ne pouvaient plus que confirmer ou nuancer.
Les trois volets ont convergé (0/3) SANS se recouvrir : 8t.1 juge la généralisation
aux fenêtres, 8t.2 la distribution des trades, 8t.3 la stabilité à la maille — c'est
la bonne décomposition d'une validation de candidat et elle est désormais réutilisable
telle quelle (fichier `test_candidate_validation_integration.cpp` + grille resserrée
comme modèles). Aucune dépendance ratée ; le seul prérequis technique (offset de
pavage) avait été identifié À LA PLANIFICATION en relisant `WalkForward.hpp:61`.

**2. Suffisance des prompts** : suffisants, aucune improvisation de workflow. Les
deux décisions produit (fenêtres du pavage décalé — l'exemple littéral OOS=300 de la
ROADMAP retombait dans le piège D34/D35 —, et périmètre) posées à l'utilisateur À
L'OUVERTURE (leçon récurrente depuis le Sprint 3) ; la décision de clôture (sprint
suivant) posée avec le triple verdict chiffré. Le gate 8t.4 s'est résolu
MÉCANIQUEMENT (condition « 3/3 confirment » fausse → branche « sinon » prescrite par
la ROADMAP) : aucune question d'adoption à poser, c'est le scénario prévu.
**Aucune modification des prompts nécessaire.**

**3. À détecter plus tôt / garde-fous** : (a) **D39** — « seul smaT=250 est stable »
(D36) n'était vrai qu'à la maille 150/200/250 : la grille resserrée l'a réfuté en une
passe de 2,3 s. Garde-fou adopté : toute validation de candidat inclut désormais une
grille RESSERRÉE verrouillée (le test 8t.3 est le modèle), pas seulement des grilles
élargies. (b) L'exemple littéral d'un item de ROADMAP (OOS=300) peut être piégé par
une interaction découverte APRÈS sa rédaction (warmup 251 du candidat) : re-dériver
les constantes d'un item au moment de l'exécuter, jamais les copier — c'est
exactement ce que la relecture d'ouverture a fait. (c) Le CLI portait un candidat
pré-B2 périmé (trail=0,05, section 6) : quand un verdict de test est re-figé (D37),
balayer AUSSI les outils d'inspection qui citent les mêmes valeurs.

**4. Notes /100** (précédent 88/91/83/72, Rentabilité 30) :
- **Architecture 88** (=) : une seule extension, strictement additive (offset de
  pavage, défaut 0 bit-identique) — propre mais trop petite pour bouger la note.
  Toujours plafonnée par 9.2 (lookback unifié) côté moteur.
- **Qualité 92** (+1) : +8 tests dont un recoupement INTER-FICHIERS (la chaîne rend
  −9,9023/11 trades dans le nouveau fichier, identique au verrou historique), chaque
  verdict avec direction MESURÉE puis figée (jamais inventée), goldens intacts sur
  tout le sprint, rétro-compat de l'offset prouvée par test dédié.
- **FinTech 84** (+1) : le projet sait désormais RÉFUTER un candidat — validation
  hors-grille en trois volets indépendants, tous verrouillés. C'est le pendant
  négatif indispensable du harnais (un processus qui ne peut que confirmer ne prouve
  rien). +1 seulement car l'edge lui-même n'a pas progressé ; > 85 exige un candidat
  CONFIRMÉ hors-grille.
- **Production 72** (=) : rien de prod ce sprint (voulu) ; D32 et 9.x inchangés.
- **Rentabilité 25** (−5) : le premier candidat est mort — honnêtement, la capacité
  démontrée à gagner de l'argent recule à « aucun candidat vivant ». Ce qui reste :
  la chaîne v2 (−6,88 en OOS fin) et un processus de validation qui a coûté 4 commits
  pour éviter d'adopter un artefact — moins cher qu'un déploiement raté.

### Sprint 8-bis — Chercher l'edge avec le harnais (2026-07-02)

**1. Découpage** : bon, et le RÉORDONNANCEMENT décidé à l'ouverture (8b.3 avant
8b.1/8b.2) était la bonne dépendance : le pavage fin est devenu le juge de la grille
et du multi-actifs — sans lui, le candidat 8b.1 aurait été sélectionné sur 2 fenêtres
(exactement le défaut que 8b.3 devait corriger). C'est la 2e fois (après le Sprint 7)
qu'un ordre de ROADMAP est réordonné pour une dépendance de MESURE : la leçon
générale est « l'outillage de jugement se livre AVANT ce qu'il doit juger ».
Le gate data-driven de 8b.4 (classement de sensibilité verrouillé par test) a
parfaitement fonctionné : l'item s'est fermé tout seul, sans débat — un item
conditionnel dont la condition est TESTÉE ne coûte rien à trancher.

**2. Suffisance des prompts** : suffisants, aucune improvisation de workflow. Les
trois décisions produit (périmètre, ordre, fenêtres du pavage fin) posées à
l'utilisateur À L'OUVERTURE (leçon Sprints 3/5), et la décision conditionnelle
« candidat d'edge → demander avant de retenir » anticipée au plan et déclenchée
exactement comme prévu. La discipline « sentinelles → passe rouge → figer » s'est
appliquée à tous les verrous de mesure (précédent Sprints 4-8, conforme à l'esprit
« idéalement » du test rouge pour des verrous de comportement).
**Aucune modification des prompts nécessaire.**

**3. À détecter plus tôt / garde-fous** : (a) **D35** — le warmup ~201 barres par
fenêtre aurait rendu l'exemple littéral de la ROADMAP (OOS=250) quasi inopérant
(49 barres tradables) ; attrapé À LA PLANIFICATION par lecture de `runRange`, pas en
cours de route — mais seulement parce qu'on a relu le code : un garde-fou
`WalkForward` (« OOS ≤ warmup + marge → avertir ») l'attraperait mécaniquement, au
backlog. (b) **D36** — l'instabilité du plateau entre 18 et 81 combos n'est visible
QUE parce que le CLI exécute une grille plus large que le verrou : garder
systématiquement « verrou petit + CLI exhaustif » comme paire. (c) Le candidat
d'edge est arrivé avec son biais de sélection DOCUMENTÉ dans le test même — c'est
la bonne pratique à conserver : un verrou qui dit « je passe » doit aussi dire
« voici pourquoi se méfier ».

**4. Notes /100** (précédent 87/90/81/72, Rentabilité 25) :
- **Architecture 88** (+1) : extension de GridOptimizer purement additive
  (normalisation en singleton, appels historiques bit-identiques), sensibilité par
  axe = petit mécanisme réutilisable qui transforme un débat (« faut-il explorer
  l'ATR ? ») en donnée verrouillée. Plafonné par l'externalisation config (9.1) et D27.
- **Qualité 91** (+1) : +10 tests, chaque verdict avec config explicite (D33) et
  nombre de trades figé (D34), goldens intouchés sur tout le sprint, rétro-compat
  prouvée par les tests historiques inchangés. Plafonné tant que les composition
  roots ne sont couverts que par relecture.
- **FinTech 83** (+2) : l'intégrité de mesure franchit un palier — verdicts sur 4
  fenêtres, par actif, sensibilité par axe, et le PREMIER alpha OOS > 0 du projet
  trouvé par le processus (pas par enthousiasme) puis traité avec la défiance
  requise (D36). Pour dépasser 85 : un edge CONFIRMÉ hors-grille.
- **Production 72** (=) : rien de prod ce sprint (voulu) ; D32 et 9.x inchangés.
- **Rentabilité 30** (+5) : premier réglage à alpha OOS > 0 sur 3/4 actifs,
  verrouillé honnêtement — mais non validé hors de la grille qui l'a choisi, et la
  chaîne par défaut reste négative (−7,15). La note ne franchira 50 qu'avec le
  candidat confirmé (Sprint 8-ter).

### Sprint 8 (clôture) — Laisser courir, entrer sur la force, rester investi (2026-07-01)

**1. Découpage** : bon, et l'ORDRE 8.2 → 8.3 → 8.4 → 8.5 s'est révélé porteur d'une
dépendance de MESURE que le plan n'avait qu'à moitié anticipée : 8.2 et 8.4 (qualité
des sorties) ne sont mesurables qu'avec des trades, or seul 8.3 (entrée) crée
l'échantillon. Résultat : l'acceptation 8.2 était indécidable à son tour (D34, 0 trade
OOS) et n'a été tranchée qu'à la ré-exam sur la chaîne finale — prévue dès le commit
8.2, donc sans improvisation. La chaîne cumulative A/B (chaque item jugé avec les
précédents retenus, snapshots `cfg81()`→`cfg84()`) est LE bon outil de ce sprint :
elle a attribué chaque point d'alpha à son item. Le commit préparatoire (D33, verrous
sur défauts) fait AVANT tout changement de défaut a évité de re-litiger le verdict 8.1.

**2. Suffisance des prompts** : suffisants, aucune improvisation de workflow. La
discipline du test rouge s'est appliquée à l'identique (4 rouges réels, dont le
comportement pathologique TP=0 qui sortait dès +0 %). Les DEUX décisions produit du
sprint (périmètre 8.2–8.5 et design de la re-entrée 8.5 ; puis le sprint suivant à la
clôture) ont été posées à l'utilisateur conformément à l'étape 4 du prompt exécuteur.
**Aucune modification des prompts nécessaire.**

**3. À détecter plus tôt / garde-fous** : (a) **D33** — les verrous de verdict
construits sur les défauts étaient une bombe à retardement posée au point d'étape 8.1 ;
attrapée ici par relecture AVANT le premier changement de défaut. Garde-fou adopté
(règle de DoD des verdicts) : toute config de verrou est construite champ par champ.
(b) **D34** — un alpha OOS mesuré sur 0 trade ne juge que le cash drag ; personne ne
comptait les trades OOS avant ce sprint. Garde-fou : les agrégats poolés impriment et
verrouillent désormais le NOMBRE de trades dans chaque verdict ; le pavage fin (8b.3)
attaque la racine (2 fenêtres, c'est trop peu). (c) Le risque kill-switch anticipé au
plan (4 pertes consécutives → gel du backtest) ne s'est PAS matérialisé (13 G / 9 P
bien répartis) — vérifié, pas seulement supposé.

**4. Notes /100** (précédent 86/89/79/72, Rentabilité 20) :
- **Architecture 87** (+1) : cinq changements de comportement livrés sans toucher ni
  TradingBot ni CrossoverDetector — tout en flags de config A/B-ables avec conventions
  cohérentes (≤ 0 / ≥ 100 = off), la re-entrée placée APRÈS les ventes préserve la
  priorité des sorties. Plafonné par l'externalisation config (9.1) et D27.
- **Qualité 90** (+1) : +20 tests, 4 rouges réels, verrous de verdict auto-protégés
  des défauts (D33), goldens re-figés trois fois avec invariant B&H vérifié à chaque
  fois. Plafonné tant que les composition roots ne sont couverts que par relecture.
- **FinTech 81** (+2) : chaque décision de stratégie du sprint est passée par un
  verdict OOS chiffré et VERROUILLÉ, y compris le verdict de sprint et la DoD
  elle-même (`EXPECT_LT(alphaC, -5.0)`) — le processus de décision est désormais
  aussi testé que le code. Pour dépasser 85 : un edge démontré.
- **Production 72** (=) : rien de prod ce sprint (voulu) ; D32 (230 barres au CP
  Gateway) et 9.x inchangés.
- **Rentabilité 25** (+5) : progression OOS réelle (+4,07 pts, PF 3,71, espérance
  +77,93 $/trade) mais alpha toujours négatif (−10,03) et grille « ne pas deployer ».
  La note ne franchira 50 qu'avec un alpha OOS ≥ 0 (Sprint 8-bis).

### Sprint 7 — Harnais de validation (2026-06-25)

**1. Découpage** : bon calibre (4 items + un livrable CLI), thème unique (l'outillage
de jugement). L'ordre conseillé 7.1 → 7.4 → 7.2 → 7.3 a dû être **réordonné** :
la décision utilisateur de ré-exporter QQQ en total-return (7.4-données) a créé une
dépendance que le plan d'origine n'avait pas — le changement de données doit précéder
les verrous d'intégration du walk-forward, sinon 7.1 fige des valeurs sur des données
condamnées. Réordonné en 7.4-données → 7.1 → 7.2 → 7.3 → CLI, justifié. La dépendance
7.2 → 7.1 (la grille juge en OOS via le walk-forward) a tenu telle quelle. Le refactor
le plus risqué (run() → runRange) a été protégé par le golden ET un test unitaire
d'équivalence dédié (`RunRangeEqualsFullRunOnFullWindow`) — la bonne discipline.

**2. Suffisance des prompts** : le workflow a tenu sans improvisation de fond. Le « test
rouge » strict s'est appliqué là où un vrai contrat existait (garde D29 rouge sans garde,
reproductibilité Monte-Carlo, équivalence runRange) ; ailleurs, verrous de comportement
nommés, conforme à l'esprit « idéalement ». **Une vraie décision produit en cours de
sprint** (D30 : la config prod, dominée sur données honnêtes) — gérée comme le prompt
l'exige (étape 4 : poser la question plutôt que trancher seul) via le test côte à côte
qui l'avait justement anticipée comme « Décision requise » dès le Sprint 6.1. Le
mécanisme « décision conditionnelle déclenchée par un test » a parfaitement fonctionné.
**Aucune modification des prompts nécessaire.** Petite friction notée (non bloquante) :
la règle `.gitignore *.csv` masquait les nouvelles données — attrapée par la discipline
`git status --short` avant commit (les fichiers manquaient à la liste), preuve que ce
garde-fou (leçon D13) paie encore.

**3. À détecter plus tôt** : (a) **D31 est la leçon du sprint** — l'ancien QQQ.csv avait
1858 lignes pour 1790 jours de bourse réels ; personne n'avait compté. Comme D29 au
Sprint 6, c'est la qualité des DONNÉES qui n'était pas testée. Garde-fou proposé : étendre
`auditTotalReturnCsv` d'un contrôle « densité de barres ≈ jours de bourse attendus » (au
backlog qualité données). (b) D30 (config prod dominée) aurait pu être anticipée au
Sprint 6.1 : figer une config « de prod » sur un dataset SANS dividende, c'était valider
sur de mauvaises données — le test côte à côte l'a heureusement rendue visible dès que les
données sont devenues honnêtes. La CI (item 22) exécute désormais ces verrous à chaque push.

**4. Notes** (précédent 84/87/76/71, Rentabilité 20) :
- **Architecture 85** (+1) : harnais entièrement header-only et additif (runRange en seam,
  floorIdx par défaut neutre, objectif de grille injecté → testable sans backtest). La
  simplification de ProdConfig (= défaut) retire de la dette. Plafonné par D18 (indicateurs
  `vector<Bar>`, Sprint 8.0) et l'externalisation config (9.1).
- **Qualité 89** (+2) : +27 tests, golden re-figé sur données honnêtes avec justification
  chiffrée, D29 fermé, garde-fou de qualité de données introduit (la donnée testée comme le
  code). Le verrou d'équivalence runRange sécurise le refactor. Plafonné tant que la
  couverture des composition roots (main_ibkr, main_validate) repose sur la relecture.
- **FinTech 79** (+3) : la mesure est désormais hors-échantillon (walk-forward), multi-actifs,
  et distributionnelle (Monte-Carlo) — l'intégrité de mesure, cœur FinTech, franchit un palier.
  Le backtest ne ment plus sur les dividendes (B&H +262 %). Pour dépasser 80 : un edge réel.
- **Production 72** (+1) : la prod ne trade plus une config dominée (D30) ; un exécutable de
  validation outille la décision de déploiement. Le reste (config externalisée, barres
  clôturées, recalibration) reste aux Sprints 9.
- **Rentabilité 20** (=) : aucune stratégie n'a changé ; la mesure est plus honnête et
  CONFIRME l'absence d'edge en OOS (alpha −257 pts, grille « ne pas deployer »). Savoir
  qu'il n'y a pas d'edge est un acquis — mais la note ne monte qu'avec un edge réel (Sprint 8).

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
