# ROADMAP — SwingBot C++

> **Source de vérité du workflow.** Ce fichier est lu par `prompt-executer-sprint.md`
> (exécution du sprint courant) et mis à jour via `prompt-mise-a-jour-roadmap.md`
> (clôture de sprint, re-priorisation, rétrospective). Ne pas le modifier à la main
> en dehors de ce cycle, sauf pour ajouter une découverte.

## Tableau de bord

| Dimension    | Note /100 | Baseline (audit 2026-06-10) |
|--------------|-----------|------------------------------|
| Architecture | 90        | 68                           |
| Qualité      | 93        | 60                           |
| FinTech      | 88        | 38                           |
| Production   | 72        | 35                           |

- **Dernière mise à jour** : 2026-07-06 (Sprint 13 — **5e famille d'alpha : VOL-REGIME / volatility-managed exposure** (décision utilisateur (c) : famille NEUVE au-delà des signaux de prix simples). Filtre binaire long/cash modulé par le RÉGIME DE VOLATILITÉ réalisée (long si vol réalisée 20 j ≤ seuil × médiane glissante 126 j, sinon cash — hypothèse Moreira & Muir 2017), jugé en OOS via un moteur SÉPARÉ `VolRegimeBacktester` (calqué sur Rotation/Pairs, offline, ne touche NI la prod NI aucun golden). **VERDICT 13.2 : AUCUN EDGE ajusté du risque** — le filtre TRADE réellement (107-135 stints OOS, D47 satisfait) et son Sharpe est POSITIF (0,52 canon / 0,23 fin / 0,61 décalé) mais SOUS le Sharpe du Buy & Hold (1,09 / 0,77 / 1,04) sur les 3 pavages, les 4 actifs (QQQ/SPY/IWM/MDY) et les 9 réglages du balayage {10,20,42}×{0,8;1;1,2} (meilleur vl=42/seuil=1,2 = dSharpe −0,16 < 0). Le filtre RÉDUIT le drawdown (14,5 vs 18,1 %) mais de < 50 % (clause DoD NON atteinte, contraste avec le pairs-trading) et au prix d'un alpha négatif (cash drag T4/D48) ; MC size-aware cagrP50 +5,03 % (la strat gagne de l'argent, moins efficacement que le B&H). Gate de confirmation FERMÉ. Prod paper, verrou live intact. **Les CINQ familles d'alpha pré-définies sont désormais soldées sans edge OOS (D50).**)
- **Sprint courant** : Sprint 14 — **Décision de suite requise** (CINQ familles d'alpha soldées sans edge OOS : trend-following mono-actif, rotation multi-actifs, mean-reversion, pairs-trading, vol-regime). Le moteur reste sûr, correct et bien testé (694 verts) ; la variante la plus faisable offline de la piste (c) « famille neuve » (régime de volatilité réalisée) est explorée — sans edge. **Décision d'ouverture requise** (candidates restantes) : (a) **parquer la prod paper + durcir l'opérationnel** (viser la note Production ; D18 interface ATR true-range, D19 lookback unifié, D25 flap intra-journalier au backlog) ; (b) **raffiner le pairs-trading proprement** (vraie cointégration Engle-Granger + hedge ratio roulant, D49) ; (c') **autre variante de 5e famille à données EXTERNES** (VIX/vol implicite — backlog D50 ; données alternatives ; surface d'options — gros chantier data, aucune donnée dans le dépôt aujourd'hui) ; (d) **documenter la conclusion** (les stratégies techniques simples sur ETFs US n'ont pas d'edge OOS net de coûts — 5 familles de connaissance négative) et clore la recherche d'edge. Discipline inchangée : jugée en OOS via le harnais Sprint 7, verdicts verrouillés (D33/D34/D47), aucune adoption sans confirmation hors-protocole complète (D42/D43). Prod reste paper tant qu'aucun edge OOS n'est démontré

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
> candidat non confirmé, AUCUNE adoption, la prod reste paper.** Le Sprint
> 8-quater a ensuite exploré l'axe désigné (trailing) avec un ATR adaptatif :
> même verdict discipliné — « pas d'amélioration robuste » (D40), l'avantage
> (+1,82 pt canonique) ne tient pas sur le pavage décalé (−0,24). Le Sprint
> 8-quinquies a exploré les familles de signaux (sortie structurelle, entrée
> breakout) : « aucune amélioration » — inertie ou dégradation hors du pavage
> de choix (D41). Le Sprint 8-sexies (3e famille : pullback, filtre de
> volatilité) a produit le **premier mécanisme qui PASSE l'acceptation sur les
> deux pavages non-choisis** — l'entrée pullback RSI ≤ 40 « s'ajoute »
> (+0,42/+1,52 pt) — mais SANS edge absolu (alpha négatif partout) : adoption
> différée à une confirmation hors-protocole (D42), le filtre ATR est réfuté.
> **CINQ axes de la chaîne mono-actif explorés (paramètres, trailing,
> structure/breakout, pullback/volatilité, sizing) ; le pullback voit son ALPHA
> confirmé hors-protocole (données longues + multi-actifs) mais reste
> inadoptable — le vol-sizing améliore la frontière DD/alpha (D45 a d'abord
> corrigé un MC aveugle à la taille) sans clore le critère, et l'alpha absolu
> reste négatif. La chaîne mono-actif est soldée ; CHANGEMENT DE PARADIGME :
> rotation multi-actifs pour viser l'alpha ABSOLU (Sprint 8-nonies, T4).**
>
> | Dimension     | Note /100 | Justification |
> |---------------|-----------|---------------|
> | **Rentabilité** | **28**  | Le candidat 8b.1 avait été réfuté proprement (−5 au Sprint 8-ter) ; les Sprints 8-quater/8-quinquies n'avaient rien changé (D40/D41). Le Sprint 8-sexies avait apporté +3 (pullback = premier candidat vivant). Le Sprint 8-septies **consolide sans ajouter (=)** : l'alpha du pullback est CONFIRMÉ hors de son protocole (données longues incluant dot-com/2008 : +0,14/+1,05 ; 2/3 actifs ; grille stable — la méfiance D36 est levée SUR L'ALPHA, vraie bonne nouvelle) MAIS il double le drawdown de queue (DD p95 10,80 → 19,27) et aucun levier config-only ne les découple (D44) → non adopté. La capacité DÉMONTRÉE à gagner de l'argent est inchangée (alpha absolu toujours négatif), d'où le maintien à 28 ; mais l'incertitude a rétréci (on sait que le pullback a un vrai alpha et où est le verrou). Le Sprint 8-octies **consolide encore sans ajouter (=)** : le vol-sizing est le premier levier à améliorer la frontière DD/alpha du pullback (DD 7,88 → 6,51 à faible coût d'alpha, après que D45 a corrigé un MC aveugle à la taille) mais le découplage est PARTIEL et l'alpha absolu reste négatif → non adopté. La note ne franchira 50 qu'avec un edge d'alpha ABSOLU — c'est l'objet du changement de paradigme (rotation multi-actifs, Sprint 8-nonies), premier axe à ne plus raffiner une chaîne mono-actif perdante ; et 70+ qu'en battant le B&H net de coûts avec la DoD complète. Le Sprint 8-nonies **consolide sans ajouter (=)** : la rotation multi-actifs est RÉFUTÉE (aucun edge, PIRE que le panier passif, D46) — le second grand axe (multi-actifs) est soldé comme le premier (mono-actif) ; la note reste à 28, les DEUX voies de recherche d'alpha étant épuisées sans edge démontré. Le Sprint 10 **consolide sans ajouter (=)** : la 3e famille explorée (mean-reversion, contrarian) n'a AUCUN alpha OOS (−8,56 fin QQQ, négatif sur les 4 actifs), mais son 1er jet ne trade quasi pas (D47 : cash drag, 1,45 % de temps investi) — la famille reste sous-explorée, l'incertitude n'a donc pas vraiment bougé ; la note reste à 28. Le Sprint 11 **consolide sans ajouter (=)** : la variante z-score/Bollinger de la famille MR TRADE enfin réellement (3-4 trades OOS, jusqu'à 23 sans filtre — D47 levé) mais reste SANS edge (alpha OOS négatif sur les 2 pavages, les 3 actifs et tous les réglages) → la 3e et dernière famille est désormais VRAIMENT jugée et soldée ; l'incertitude a rétréci (on SAIT maintenant que le mean-reversion n'a pas d'edge, ce n'était pas qu'un artefact de sous-échantillonnage) mais la capacité démontrée de gain est inchangée ; la note reste à 28. Le Sprint 12 **consolide sans ajouter (=)** : la 4e famille (pairs-trading market-neutral, la seule ORTHOGONALE) TRADE massivement (209-212 A/R OOS, D47 satisfait) mais n'a AUCUN edge — Sharpe OOS négatif sur les 3 pavages, les 4 paires et tous les réglages (D49) ; le spread log naïf β=1 sur fenêtre courte est du bruit. Les QUATRE familles d'alpha pré-définies sont soldées ; la note reste à 28, mais l'incertitude a encore rétréci (on SAIT que les stratégies techniques simples sur ETFs US n'ont pas d'edge OOS net de coûts — connaissance négative solide). Le Sprint 13 **consolide sans ajouter (=)** : la 5e famille explorée (vol-regime / volatility-managed, la variante (c) la plus faisable offline) TRADE réellement mais n'a AUCUN edge ajusté du risque — Sharpe OOS positif mais SOUS le B&H sur les 3 pavages, les 4 actifs et les 9 réglages du balayage (D50) ; le filtre réduit le DD de < 50 % et l'alpha vs B&H reste négatif (cash drag). La note reste à 28, les CINQ familles techniques simples étant désormais soldées (connaissance négative encore renforcée). La note ne bougera qu'avec une piste NON technique (données alternatives) ou une vraie cointégration testée, pas un raffinement de plus. |
- **État des tests** : 694/694 verts (560 unitaires + 134 intégration) — et la
  suite passe aussi en **Release**, sous **ASan/UBSan**, et TSan ciblé sur les
  suites concurrentes. +15 au Sprint 13 (679 → 694 : moteur `VolRegimeBacktester`
  + 8 tests VolRegimeBacktesterUnit ; 7 verrous OOS VolRegimeOosIntegration). La
  décomposition réelle est **560 + 134** ; le tableau de bord affichait « 551 + 128 »
  au Sprint 12 (décalé d'une unité dans chaque sens, total 679 exact — dérive
  cosmétique D20 recalée ici sur `ctest -N`). Détail au changelog.
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

# 🟣 SPRINT 8-QUATER — Trailing adaptatif ATR (ré-ouverture de 8b.4) ✅ (clos le 2026-07-03, verdict : pas d'amélioration robuste)

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

- [x] **8q.1** **Trailing ATR dans le moteur de sortie** → `1701c5c` + `5071b7b` + `0fb6dc5`
  `SwingConfig::trailingAtrMult` (défaut 0 = désactivé → goldens byte-identiques,
  vérifié à chaque commit) ; quand > 0, la sortie trailing utilise
  `peak − mult × ATR(14)` (vrai true-range, `computeBars`). Réalisé par une
  surcharge ADDITIVE de `IRiskManager::checkExitConditions` (fenêtre de barres +
  mult, défaut rétro-compatible qui délègue — patron `computeBars`/8.0 : les ~15
  appels de tests existants compilent inchangés) ; TradingBot passe la fenêtre
  qu'il détient déjà (interfaces-only préservé). Convention arbitrée à
  l'ouverture (décision utilisateur 2026-07-03) : l'ATR REMPLACE le %, avec
  repli défensif sur le % si l'ATR est incalculable (fenêtre < 15 barres ou
  ATR nul) — jamais de position sans filet. Câblage ConfigLoader/prod.json
  volontairement DIFFÉRÉ au gate 8q.3 (fichier gouverné intact, documenté dans
  ConfigLoader.hpp). **Acceptation satisfaite** : 10 tests rouges RiskManagerUnit
  (seuil 110 − 3×2 = 104 à la main, lissage de Wilder 30/14, « remplace »,
  replis, gating minHold/peak, priorités SL/TP).
- [x] **8q.2** **Verdict OOS du trailing ATR vs chaîne v2** → `b84bfaf` + `223714d`
  Nouvelle suite `TrailingAtrIntegration` (modèle 8-ter) : mult ∈ {2, 3, 4}
  choisi sur le pavage FIN → **mult=3** (−6,55 vs −7,01/−6,70 — sélection PLATE,
  écart 0,47 pt, les 3 mesures ET l'argmax figés), puis duels sur les pavages
  NON-CHOISIS. **Verdict verrouillé : PAS D'AMÉLIORATION ROBUSTE** — canonique :
  ATR **−8,08** (7 trades) vs chaîne −9,90 (11), +1,82 pt ; décalé : ATR
  **−13,08** (10) vs −12,83 (11), −0,24 pt. L'acceptation exigeait ≥ chaîne sur
  les DEUX pavages non-choisis → NON satisfaite (résultat valide). Recoupements
  inter-fichiers de la chaîne intacts (−9,9023/11, −6,8794/13, −12,8332/11) :
  la plomberie 8q.1 n'a pas bougé le chemin historique. CLI : section 8.
- [x] **8q.3** **Décision de suite (Décision requise)** → branche « sinon » appliquée (aucun code)
  La condition « amélioration robuste » est FAUSSE → **décision utilisateur
  (2026-07-03) : consigner sans adopter** — aucun changement de défaut,
  goldens/config/prod.json strictement inchangés, prod reste paper. Le
  mécanisme reste dans le moteur (flag additif, défaut 0) pour re-jugement
  futur. Suite décidée avec l'utilisateur : **Sprint 8-quinquies** (autres
  familles de signaux — l'axe trailing est soldé, D40).

# 🟣 SPRINT 8-QUINQUIES — Autres familles de signaux ✅ (clos le 2026-07-03, verdict : aucune amélioration)

> Décision utilisateur (2026-07-03) à la clôture du Sprint 8-quater : l'axe
> trailing est SOLDÉ (fixe fragile — 8t.3 —, adaptatif ATR non robuste — 8q.2,
> D40) ; la recherche d'edge continue par les SIGNAUX eux-mêmes. Le trailing %
> 0,03 reste la sortie par défaut. Discipline inchangée : flags additifs
> A/B-ables (défaut = comportement actuel, goldens intacts), verdicts OOS
> verrouillés sur les TROIS pavages (canonique 700/400, fin 500/300, décalé
> 500/400 offset 90), configs explicites (D33), comptes de trades figés (D34),
> et tout paramètre choisi par mini-grille est jugé sur les pavages NON-CHOISIS
> (leçon 8-ter/8-quater — `test_trailing_atr_integration.cpp` est le modèle).

- [x] **8s.1** **Sortie structurelle « plus bas de N jours »** → `f70394c` +
  `294ed6c` + `c067a33` + `8900bf1`
  `SwingConfig::exitOnLowestLowN` (défaut 0 = désactivé) : en position, clôture
  ≤ plus bas des N barres PRÉCÉDENTES (barre courante exclue) → « sortie
  structurelle ». Décisions utilisateur d'ouverture : gatée par minHoldDays
  (comme le trailing qu'elle concurrence), priorité SL > TP > structure >
  trailing. Réalisée par une 3e surcharge ADDITIVE de `checkExitConditions`
  (défaut rétro-compatible, patron 8q.1) ; plomberie RiskConfig + TradingBot ;
  câblage ConfigLoader/prod.json DIFFÉRÉ au gate 8s.4 (fichier gouverné
  intact). **Acceptation satisfaite** : 9 tests RiskManagerUnit calculés à la
  main (bornes de fenêtre verrouillées des deux côtés, N=0 = identité stricte,
  gating minHold, priorités % ET ATR) + verrou de conversion ; goldens intacts
  à chaque commit.
- [x] **8s.2** **Entrée breakout « plus haut de M jours »** → `d167eea` + `4edd528`
  `SwingConfig::entryBreakoutM` (défaut 0 = désactivé) : à plat, régime up et
  clôture STRICTEMENT au-dessus du plus haut des M barres précédentes (barre
  courante exclue) → BUY sans croisement. Bloc évalué après les VENTES et
  avant la re-entrée 8.5 ; flag indépendant de `regimeReentry`. **Acceptation
  satisfaite** : 8 tests SwingStrategyUnit sur indicateurs injectés (borne
  stricte, exclusion barre courante, gate de régime, priorité des ventes,
  coexistence re-entrée) ; goldens intacts.
- [x] **8s.3** **Verdicts OOS des deux mécanismes vs chaîne v2** → `81710f8` + `fe1836e`
  Nouvelle suite `SignalFamiliesIntegration` (modèle 8-quater) + section 9 du
  CLI `validate`. Mini-grilles sur le pavage FIN : structure N ∈ {10, 20, 55} ;
  breakout M ∈ {20, 55} × {remplace, s'ajoute} (décision utilisateur : juger
  les DEUX hypothèses de cohabitation avec la re-entrée 8.5). **Verdict
  verrouillé : AUCUNE AMÉLIORATION** — (a) la structure est INERTE : N=20/55
  rendent des mesures STRICTEMENT identiques à la chaîne sur les trois pavages
  (la cassure du plus bas n'arrive jamais avant le trailing 3 %), le seul N
  qui tire (10) dégrade (−7,04 vs −6,88 fin) ; (b) le breakout « s'ajoute »
  est partout inerte (sous-ensemble de la re-entrée, prédit à l'ouverture) ;
  (c) le breakout « remplace » (M=20, argmax fin −6,69, +0,19 pt) rend
  **−11,75 vs −9,90 (−1,85 pt)** sur le canonique et **−14,26 vs −12,83
  (−1,43 pt)** sur le décalé — biais de sélection pur (D36 à l'œuvre). Gate
  de la COMBINAISON fermé (le breakout échoue seul, la structure est inerte).
  Recoupements inter-fichiers de la chaîne intacts sur les trois pavages.
- [x] **8s.4** **Décision de suite (Décision requise)** → branche « sinon » appliquée (aucun code)
  La condition « amélioration robuste sur les pavages non-choisis » est FAUSSE
  (0 mécanisme sur 2) : **« aucune amélioration » consigné, AUCUNE adoption**
  (SwingConfig, config/prod.json et goldens strictement inchangés), la prod
  reste paper. Les deux mécanismes restent dans le moteur (flags additifs,
  défaut 0) pour re-jugement futur. Suite décidée avec l'utilisateur
  (2026-07-03) : **Sprint 8-sexies** (3e famille de signaux — pullback en
  tendance, filtre de volatilité).

# 🟣 SPRINT 8-SEXIES — 3e famille de signaux ✅ (clos le 2026-07-03, verdict : premier candidat robuste — adoption différée à confirmation)

> Décision utilisateur (2026-07-03) à la clôture du Sprint 8-quinquies :
> continuer la recherche d'edge par une 3e famille de signaux. Les axes soldés
> ne se re-testent pas (paramètres — 8-ter, trailing — 8-quater, structure/
> breakout — 8-quinquies) ; la piste inverse du breakout n'a JAMAIS été
> testée : acheter la FAIBLESSE dans la tendance (pullback), et filtrer les
> entrées par la VOLATILITÉ. Discipline inchangée : flags additifs A/B-ables
> (défaut = comportement actuel, goldens intacts), verdicts OOS sur les TROIS
> pavages, configs explicites (D33), trades poolés figés (D34), deltas figés,
> mécanismes jugés SEULS d'abord, et — leçon D41 — **vérifier l'ACTIVATION du
> mécanisme (le chemin change-t-il ?) avant d'interpréter son verdict**.
> NOTE de cycle : 8y.1–8y.3 ont été livrés dans une session interrompue avant
> le gate ; ses commits atomiques ont été collapsés par le squash-merge
> `81a18ec` (PR #20). Le cycle de clôture (2e session) a re-vérifié la
> baseline (605/605), posé le gate 8y.4 et clos — rien n'a été perdu
> (l'état vivait dans les commits et les verrous, leçon 8-quinquies).

- [x] **8y.1** **Entrée pullback en tendance** → `81a18ec` (squash PR #20)
  Flag `SwingConfig::entryPullbackRsiMax` (défaut 0 = désactivé) : à plat,
  régime up (prix > SMA200) et RSI ≤ seuil → BUY — SANS condition sur les
  EMA (le creux passe typiquement SOUS elles : masquage seulement PARTIEL
  par la re-entrée 8.5, anticipé D41 et vérifié à la mesure). Bloc inséré
  après les ventes et le breakout, avant la re-entrée
  (`SwingStrategy.hpp:285-303`) ; variantes « remplace »/« s'ajoute » jugées
  en 8y.3. **Acceptation satisfaite** : 8 tests rouges SwingStrategyUnit sur
  indicateurs injectés (borne exacte du seuil, gate de régime, priorité des
  ventes, tire SOUS les EMAs là où la re-entrée ne peut pas, coexistence
  re-entrée, flag off = identité) ; goldens intacts (défaut 0).
- [x] **8y.2** **Filtre de volatilité sur les entrées** → `81a18ec` (squash PR #20)
  Flag `SwingConfig::entryMaxAtrPct` (défaut 0 = désactivé) : TOUTE entrée
  (croisement, breakout, pullback, re-entrée) bloquée si ATR(14)/clôture >
  seuil, strictement ; les VENTES ne sont jamais bloquées. ATR vrai
  true-range via `computeBars`, 5e indicateur INJECTÉ de SwingStrategy
  (défaut ATR(14), sites d'appel historiques préservés) ; ATR incalculable →
  fail-open (décision utilisateur 2026-07-03 : le comportement chaîne est
  préservé). HOLD à raison explicite quand le filtre bloque (activation
  VISIBLE, D41). **Acceptation satisfaite** : 9 tests rouges
  SwingStrategyUnit (les 4 familles d'entrée bloquées, borne stricte, ventes
  jamais bloquées, fail-open, flag off = identité) ; goldens intacts.
- [x] **8y.3** **Verdicts OOS vs chaîne v2** → `81a18ec` (squash PR #20)
  Nouvelle suite `PullbackVolatilityIntegration` (modèle 8-quinquies) +
  section 10 du CLI `validate`. Mini-grilles sur le pavage FIN : pullback
  RSI ∈ {30, 40, 50} × {remplace, s'ajoute} ; filtre ATR ∈ {0,010, 0,015,
  0,025} (cran bas ajouté par décision utilisateur d'ouverture — réduire le
  risque d'inertie D41 ; il s'est avéré ACTIF : 1 seul trade). **Verdict
  verrouillé : PREMIÈRE AMÉLIORATION ROBUSTE du protocole** — (a) le
  pullback (argmax RSI ≤ 40 « s'ajoute », −6,5491 vs −6,8794, +0,33 pt fin,
  non-inerte : mêmes 13 trades mais alphas ≠ = entrées réellement déplacées)
  fait MIEUX que la chaîne sur les DEUX pavages non-choisis : canonique
  **−9,4861 (12) vs −9,9023 (11), +0,42 pt** ; décalé **−11,3159 (12) vs
  −12,8332 (11), +1,52 pt** — l'acceptation PASSE, signe stable partout ;
  (b) le filtre ATR (argmax 0,015, +0,40 pt fin, actif sur les 3 crans)
  NE GÉNÉRALISE PAS : canonique −11,5294 (6), −1,63 pt ; décalé −13,3341
  (9), −0,50 pt — biais de sélection (D36), « pas d'amélioration » ;
  (c) gate de la COMBINAISON fermé (le filtre échoue seul). Recoupements
  inter-fichiers de la chaîne intacts sur les trois pavages. L'alpha du
  pullback reste NÉGATIF partout : amélioration relative, PAS d'edge absolu.
- [x] **8y.4** **Décision de suite (Décision requise)** → décision utilisateur (2026-07-03), aucun code
  Amélioration robuste VRAIE pour le pullback → **décision utilisateur :
  « CONFIRMER avant d'adopter »** — AUCUNE adoption maintenant (défauts
  SwingConfig, goldens, config/prod.json strictement inchangés, prod reste
  paper) : la leçon D36 (le candidat 8b.1 avait survécu à sa première
  validation avant d'être réfuté hors-grille) impose une validation
  HORS-PROTOCOLE (walk-forward multi-actifs SPY/IWM/MDY, Monte-Carlo des
  trades OOS, grille resserrée RSI {35, 40, 45} — modèle 8-ter) comme
  PRÉREQUIS de toute adoption → consigné en **D42**, sprint de confirmation
  au backlog. Suite décidée avec l'utilisateur : **Sprint 8-septies**
  (4e famille de signaux — le pullback attend sa confirmation).

# 🟣 SPRINT 8-SEPTIES (redéfini le 2026-07-04) — Données longues & confirmation du pullback ✅ (clos le 2026-07-04, verdict : alpha confirmé mais non adopté — risque couplé)

> **RE-PRIORISATION (décision utilisateur du 2026-07-04)** : le contenu
> initial de ce sprint (« 4e famille de signaux », items 8z.x) part au
> BACKLOG sans avoir démarré (aucun code écrit). Motif chiffré : la
> trajectoire mesurée sur quatre familles de flags donne au MIEUX
> +0,4/+1,5 pt (pullback) contre un déficit d'alpha de −7/−13 pts selon le
> pavage — l'empilement de flags sur la chaîne plafonne. Les leviers
> re-priorisés, dans l'ordre : (1) la DONNÉE — le harnais juge tout sur
> 1790 barres 2019-2026, régime quasi uniquement haussier : faible
> puissance statistique (D34/D35) et aucun vrai marché baissier là où le
> filtre de régime doit payer → export total-return depuis la cotation
> (~1999+, dot-com et 2008 inclus) ; (2) statuer VITE sur le seul candidat
> vivant — la confirmation hors-protocole du pullback (D42) devient CE
> sprint ; (3) le prototype de rotation multi-actifs (T4 : le coût dominant
> est le temps en cash) visé au sprint suivant ; (4) un garde-fou
> multiple-testing (D43). Discipline inchangée : configs explicites (D33),
> trades poolés figés (D34), activation vérifiée (D41), verdicts
> verrouillés, commits atomiques. Les CSV 2019-2026 restent INTACTS
> (aucun verrou historique ne bouge) : les données longues vivent à côté
> (`*_max.csv`).

- [x] **8d.1** **Export total-return historique max (~1999+)** → `9ab98a5`
  script committé `scripts/export_total_return.py` (python3 stdlib uniquement,
  Yahoo v8 chart avec User-Agent navigateur — vérifié accessible depuis
  l'environnement —, `period2` FIGÉ au 2026-07-01 pour la reproductibilité
  des comptes et l'anti-barre-en-formation) → `QQQ_max.csv`, `SPY_max.csv`,
  `IWM_max.csv`, `MDY_max.csv` à la racine, format existant
  `Date,Open,High,Low,Close,Adj Close,Volume` (chaque actif depuis sa
  cotation : QQQ 1999-03, IWM 2000-05, SPY/MDY avant). Compile-defs
  `SWINGBOT_*_MAX_CSV` sur `integration_tests` et `validate` (modèle des
  defs existantes du CMakeLists). **Acceptation** : `auditTotalReturnCsv`
  (`include/backtest/DataQuality.hpp`) passe sur les 4 fichiers (Adj ≠
  Close — dividendes réels), comptes de barres FIGÉS + garde de densité
  vs jours de bourse attendus (solde le backlog D31), B&H QQQ_max figé
  (dot-com et 2008 inclus — première donnée honnête sur les régimes
  baissiers). **Réalisé** : QQQ_max 6870 barres (1999-03-10), SPY 8412,
  IWM 6562, MDY 7841 ; densité 0,998 partout ; B&H QQQ_max +1585,38 %.
- [x] **8d.2** **Pavages longs + référence chaîne v2 sur données max** → `1676727`
  pavages LONGS sur ~6 800 barres QQQ_max (dimensionnés à l'exécution en
  respectant D35 : warmup ~201 barres ≪ OOS) — un canonique-long et un
  décalé-long (offset, aucune borne commune). Verrouiller la référence :
  chaîne v2 (config explicite D33, modèle `cfgChaineV2()` de
  `tests/integration/test_pullback_volatility_integration.cpp`) — alpha
  OOS moyen + trades poolés FIGÉS (D34) sur les deux pavages. Première
  mesure du bot incluant deux vrais marchés baissiers. **Réalisé** :
  canonique-long **−17,3033/95** (15 fenêtres), décalé-long **−11,0503/96** —
  la chaîne souffre bien plus en régime baissier que sur 2019-2026 (−9,90).
- [x] **8d.3** **Confirmation hors-protocole du pullback (D42)** → `1676727`
  fichier `tests/integration/test_pullback_confirmation_integration.cpp`
  (modèle 8-ter : le candidat est jugé HORS du protocole qui l'a choisi) +
  section 11 du CLI `validate`. Quatre volets verrouillés : (a)
  **multi-actifs** SPY/IWM/MDY (pavage fin 2019-2026, duels chaîne vs
  chaîne+pullback RSI ≤ 40 « s'ajoute », un verrou par actif — modèle
  8b.2) ; (b) **grille resserrée** RSI ∈ {35, 40, 45} « s'ajoute » (pavage
  fin QQQ, mesures + argmax figés — stabilité à la maille, D39) ; (c)
  **Monte-Carlo** des trades OOS poolés (pavage canonique QQQ, chaîne vs
  pullback, p50/p95 CAGR et drawdown figés — modèle 8t.2) ; (d) **données
  longues** : duels sur les DEUX pavages longs de 8d.2. **Critère de
  confirmation (consigné dans le fichier de verdict)** : pullback ≥ chaîne
  sur les deux pavages longs ET ≥ chaîne sur ≥ 2 actifs sur 3 ET argmax de
  grille stable (40 ou plateau plat) ET Monte-Carlo non dégradé — sinon
  « non confirmé » (résultat valide, leçon 8-ter). **Résultat MESURÉ** :
  l'ALPHA est CONFIRMÉ (données longues : +0,14 canonique-long / +1,05
  décalé-long ; multi-actifs : ≥ chaîne sur IWM +0,18 et MDY +0,45, SPY
  −0,02 → 2/3 ; grille resserrée : argmax STABLE à 40) — premier mécanisme
  dont l'alpha généralise HORS de son protocole ET aux marchés baissiers.
  **MAIS le Monte-Carlo DISQUALIFIE** : DD p95 10,80 → **19,27 %**
  (quasi doublé) — le critère « non dégradé » échoue.
- [x] **8d.4** **Garde-fou statistique — multiple testing (D43, docs
  uniquement)** → clôture (ROADMAP) : registre des hypothèses jugées depuis le Sprint 8
  (recompté depuis le changelog : familles, variantes, combos de grilles)
  + règle adoptée : toute adoption future exige la confirmation
  hors-protocole COMPLÈTE (généralisation D42 : données longues +
  multi-actifs + grille resserrée + Monte-Carlo) — plus on tire
  d'hypothèses, plus le « meilleur » est un artefact probable (D36
  systématisé). AUCUNE modification de `prompt-*.md` ni de DoD de prompt
  (règle intangible) ; si un amendement semblait utile, le PROPOSER en
  diff à la rétrospective.
- [x] **8d.6** **Atténuation du drawdown (issue du gate 8d.5)** → `5c9d415`
  Décision utilisateur au gate : brider le drawdown avant de statuer.
  Testé (champs de config existants, aucun code moteur) : pullback +
  `entryMaxAtrPct` {0,015, 0,025} et + `stopLossPct` {0,03, 0,04}. **Aucune
  variante ne réussit** (DD p95 ≤ 12,80 ET alpha ≥ chaîne sur les 2 pavages
  longs) : le gating ATR ≤ 0,015 CASSE le DD (19,27 → **7,12**, sous la
  chaîne) mais perd l'alpha canonique (−17,83 < −17,30) ; le stop serré ne
  bouge pas le DD (le trailing 3 % tire d'abord). **D44 : alpha et drawdown
  du pullback sont COUPLÉS** (mêmes achats de creux volatils).
- [x] **8d.7** **3e levier de découplage (2e re-gate)** → `2078027`
  Décision utilisateur : creuser un levier de plus. Testé (config-only) :
  `riskPerTradePct` {0,010, 0,015} et sweep fin `entryMaxAtrPct` {0,018,
  0,020}. **Config-only ÉPUISÉ** : riskPerTradePct laisse le DD p95
  STRICTEMENT inchangé (19,27 — invariant d'échelle du drawdown en %) ;
  desserrer le gate ATR au-delà de 0,015 AGGRAVE le DD (21,96 / 25,09) — le
  0,015 était une falaise isolée, suspecte de sur-ajustement (D36). Le seul
  vrai levier restant = position-sizing modulé par la volatilité (code
  moteur) → backlog.
- [x] **8d.5** **Gate d'adoption (Décision requise)** → décision utilisateur (2026-07-04), aucun code
  Après le verdict 8d.3 (alpha confirmé, risque disqualifiant) et deux
  investigations d'atténuation (8d.6 puis 8d.7) qui ont épuisé l'espace
  config-only sans découpler alpha et risque : **décision utilisateur =
  « consigner + backlog vol-sizing »**. AUCUNE adoption
  (`entryPullbackRsiMax` reste 0 ; défauts SwingConfig, `config/prod.json`,
  goldens strictement inchangés ; prod reste paper). Acquis verrouillés :
  D42 (l'alpha du pullback généralise — connaissance positive), D44 (alpha
  et risque couplés, config-only épuisé). Le **position-sizing modulé par la
  volatilité** devient le vrai levier identifié → item de sprint MOTEUR au
  backlog (proposé comme Sprint 8-octies). `liveTradingApproved` reste
  `false`, verrou live intact.

> **Backlog — reporté sans démarrage (décision utilisateur 2026-07-04) :
> « 4e famille de signaux » (contenu initial du 8-septies), ré-ouvrable
> tel quel après la confirmation du pullback et le prototype rotation :**

- [ ] **8z.1** **Sortie temporelle « stagnation » (time-stop)** : flag
  `SwingConfig::exitIfNoNewHighN` (défaut 0 = désactivé) ; en position, si
  aucune clôture des N barres précédentes (barre courante incluse) n'a fait
  de nouveau plus-haut de clôture depuis l'entrée → sortie « stagnation »
  (libérer le capital d'une position qui ne tire pas, sans attendre le
  trailing). À réaliser dans le moteur de sortie par une surcharge ADDITIVE
  à défaut rétro-compatible (patron 8s.1/8q.1,
  `include/bot/RiskManager.hpp:123` — la surcharge « fenêtre de barres » y
  existe déjà) + plomberie SwingConfig→RiskConfig
  (`include/strategies/SwingStrategy.hpp:90`). Décisions d'ouverture à
  poser : gating par minHoldDays et rang de priorité (proposition : SL > TP
  > structure > stagnation > trailing). Attention D41 : le trailing 3 %
  masque probablement les grands N (une position qui stagne finit par
  retracer 3 % depuis le pic — même mécanisme que l'inertie 8s.1) ;
  vérifier l'ACTIVATION (comptes de trades vs chaîne). **Acceptation** :
  tests rouges unitaires (RiskManagerUnit, calculés à la main, bornes de
  fenêtre des deux côtés) ; flag off = goldens intacts (vérifié).
- [ ] **8z.2** **Filtre de gap sur les entrées** : flag
  `SwingConfig::entryMaxGapUpPct` (défaut 0 = désactivé) ; TOUTE entrée
  bloquée si l'open de la barre de décision gappe strictement au-dessus de
  `close précédent × (1 + seuil)` (ne pas acheter un prix déjà étiré à
  l'ouverture ; les ventes ne sont jamais bloquées). La stratégie reçoit
  les barres dans `evaluate` (`include/strategies/SwingStrategy.hpp:144`) ;
  patron d'insertion = filtre de volatilité 8y.2
  (`SwingStrategy.hpp:190-204` calcul unique + court-circuit après les
  ventes, HOLD à raison explicite — activation VISIBLE). Décision
  d'ouverture à poser : sémantique exacte (gap de la barre de décision vs
  gap de la barre de fill i+1 — la décision se prend au close, le fill est
  à l'open suivant, S.8). Attention D41 : sur barres JOURNALIÈRES un gap >
  1 % est rare — figer le nombre d'entrées bloquées (risque d'inertie).
  **Acceptation** : tests rouges unitaires (SwingStrategyUnit, indicateurs
  injectés) ; flag off = goldens intacts (vérifié).
- [ ] **8z.3** **Verdicts OOS vs chaîne v2** : mini-grilles sur le pavage FIN
  (proposition à re-dériver à l'exécution : N ∈ {10, 20, 40} pour 8z.1 —
  vérifier qu'aucun cran ne retombe dans le piège D34/D35 ; gap % ∈
  {0,005, 0,01, 0,02} pour 8z.2), duels verrouillés sur canonique + décalé
  (modèle `tests/integration/test_pullback_volatility_integration.cpp`) —
  chaque mécanisme jugé SEUL, la combinaison seulement si chacun est ≥
  chaîne sur les deux pavages non-choisis. **Acceptation** : verrous
  D33/D34/D41 (mesures + argmax figés, comptes de trades, inertie
  documentée), deltas figés ; « pas d'amélioration » est un résultat valide.
- [ ] **8z.4** **Décision de suite (Décision requise)** : si amélioration
  robuste → même branche que 8y.4 (candidat consigné, confirmation
  hors-protocole D42 prérequis d'adoption) ; sinon consigner et statuer
  avec l'utilisateur — options sur la table : sprint de confirmation du
  pullback (D42), changement de paradigme (rotation multi-actifs /
  détention par régime), ou pause stratégie → Sprint 9.x moteur (9.2, 9.4).

# 🟣 SPRINT 8-OCTIES — Position-sizing modulé par la volatilité ✅ (clos le 2026-07-04, verdict : découplage partiel, non adopté ; correctif harnais D45)

> Décision utilisateur (2026-07-04) à la clôture du Sprint 8-septies : le
> pullback est le premier candidat dont l'alpha GÉNÉRALISE (données longues +
> multi-actifs, D42), mais son alpha et son drawdown sont COUPLÉS (D44) — les
> trois leviers config-only (stop, gate ATR, riskPerTradePct) n'ont pas su
> les séparer. Le seul levier restant identifié est un **position-sizing
> modulé par la volatilité** : réduire la taille de position SEULEMENT en
> régime volatil (là où naissent les achats de creux dangereux), pour couper
> le risque de queue en préservant les bons trades. C'est un vrai axe MOTEUR
> (pas un flag), orthogonal aux signaux et jamais exploré — il pourrait aussi
> aider la chaîne v2 elle-même, indépendamment du pullback. Discipline
> inchangée : additif (défaut = comportement actuel, goldens intacts),
> verdicts OOS sur données longues + 3 pavages, D33/D34/D41, confirmation
> hors-protocole D42/D43 avant toute adoption.

> **DÉCOUVERTE À L'OUVERTURE (D45)** : avant tout code (leçon D41), l'exploration
> a montré que le Monte-Carlo était AVEUGLE à la taille de position — il
> composait `pnlPct` (rendement de la position) sur l'équité pleine, ignorant le
> capital réellement déployé. Le vol-sizing y aurait été INERTE (comme
> riskPerTradePct en 8d.7 : l'« invariance » était CET artefact). Décision
> utilisateur (voie B) : corriger le harnais d'abord (8o.1), puis mesurer.

- [x] **8o.1** **Monte-Carlo size-aware (correctif D45)** → `b64ffd7`
  `TradeRecord::deployedFraction` (capital investi / équité à l'entrée,
  calculé par PaperBroker) ; `MonteCarlo::run` bootstrappe
  `deployedFraction × pnlPct`. La chaîne ne déploie que ~40 % → CAGR et DD
  médians chutent (l'ancien MC surestimait tout). **Re-baseline documentée**
  des valeurs dérivées du MC (MonteCarloIntegration, CandidateValidation 8t.2,
  confirmation 8d.3c/8d.6/8d.7) — goldens BACKTEST byte-identiques
  (deployedFraction est seulement enregistré). MonteCarloUnit : reproductibilité
  intacte (défaut f=1.0) + 1 cas size-aware à la main. Nuance : riskPerTradePct
  n'est plus « invariant » (artefact D45 levé).
- [x] **8o.2** **Sizing modulé par la volatilité dans le moteur** → `ebbadd8`
  `SwingConfig::volSizingAtrRef` (défaut 0 = off), porté à RiskConfig.
  Surcharge « barres » de `positionSize` (patron additif 8q.1) : ×
  `min(1, volSizingAtrRef / (ATR(14)/prix))`, fail-open (ATR incalculable →
  pleine taille). Câblé dans TradingBot. **Acceptation satisfaite** : 5 tests
  RiskManagerUnit à la main (pleine taille sous réf, réduction au-dessus,
  borne à 1, fail-open, désactivé = identité) ; défaut 0 = goldens intacts.
- [x] **8o.3** **Verdict OOS : le vol-sizing découple-t-il ?** → `9060747`
  MC size-aware, DD p95 canonique (réf chaîne 4,28) + alpha long. **Verdict :
  DÉCOUPLAGE PARTIEL** — pull+vol 0,015 réduit le DD 7,88 → **6,51**
  (~35 % du chemin vers la chaîne) pour −0,11 pt d'alpha canonique (+0,84
  décalé) : PREMIÈRE frontière DD/alpha favorable, mais DD 6,51 > seuil 6,28
  ET alpha canonique < chaîne d'un cheveu → critère « DD ≤ chaîne+2 ET alpha
  ≥ chaîne » NON atteint. Réfs 0,025/0,040 quasi inertes (volatilité QQQ
  < 2,5 %) ; sur la chaîne SEULE le vol-sizing est inerte → levier SPÉCIFIQUE
  au pullback (orthogonalité confirmée). Section 12 du CLI.
- [x] **8o.4** **Gate d'adoption (Décision requise)** → décision utilisateur (2026-07-04), aucun code
  Découplage partiel, critère non atteint, et alpha absolu toujours négatif
  (ne bat pas le B&H) → **décision utilisateur : consigner sans adopter**.
  `volSizingAtrRef` et `entryPullbackRsiMax` restent à 0 ; défauts,
  `config/prod.json`, goldens intacts ; prod paper. Acquis verrouillés : D45
  (MC size-aware — correctif durable du harnais), vol-sizing = premier levier
  à réduire le risque du pullback sans casser l'alpha (mais insuffisant).
  Suite décidée avec l'utilisateur : **Sprint 8-nonies** (rotation
  multi-actifs — attaquer le déficit d'alpha lui-même, T4).

# 🟣 SPRINT 8-NONIES — Rotation multi-actifs / détention par régime — **sprint courant**

> Décision utilisateur (2026-07-04) à la clôture du Sprint 8-octies :
> CHANGEMENT DE PARADIGME. Cinq axes de raffinement de la chaîne mono-actif
> sont soldés (paramètres, trailing, structure/breakout, pullback/volatilité,
> sizing) ; le meilleur (pullback) confirme un alpha qui GÉNÉRALISE mais reste
> NÉGATIF (~−17 pt sur données longues). La cause racine est identifiée depuis
> le méta-audit (T4) : le bot est long-only mono-actif, en CASH l'essentiel du
> temps → il paie le temps hors marché face à un actif qui monte. La rotation
> attaque CE déficit : être investi dans l'actif au régime le plus fort parmi
> QQQ/SPY/IWM/MDY (données longues total-return disponibles depuis 8d.1), repli
> en cash seulement en régime baissier généralisé. C'est le premier axe à VISER
> l'alpha absolu, pas à raffiner une chaîne perdante. Discipline inchangée :
> additif, verdicts OOS sur données longues + 3 pavages, D33/D34/D42/D43
> (confirmation hors-protocole avant toute adoption), prod paper tant que
> l'edge n'est pas démontré.

- [x] **8n.1** (→ `970f90d` moteur + `d9ef6cd` tests) **Moteur de rotation (au niveau harnais, sans toucher la chaîne)** :
  un `RotationBacktester` (ou extension de `WalkForward`) qui, à chaque barre,
  classe les N actifs par force de régime (ex. rendement SMA200-relatif, ou
  pente de SMA) et détient le meilleur si son régime est haussier, sinon cash.
  Réutilise `CsvDataFeed`/`Backtester` par actif ; aligne les calendriers de
  barres (dates communes). Point d'entrée : `include/backtest/BackTester.hpp`
  (runRange) et les 4 CSV `*_max.csv`. **Acceptation** : tests unitaires du
  classement de régime (calculés à la main sur séries synthétiques) ;
  reproductibilité (mêmes dates → même choix).
- [x] **8n.2** (→ `1978b56` + CLI `7bc7cf4` ; **VERDICT : AUCUN EDGE** — voir changelog) **Verdict OOS : la rotation bat-elle le B&L (best buy-and-hold)
  net de coûts ?** La référence n'est plus « alpha vs QQQ B&H » mais vs le
  MEILLEUR actif détenu passivement, ET vs un panier équipondéré. Walk-forward
  sur données longues (dot-com/2008 = le test décisif d'un filtre de régime),
  3 pavages, coûts de rotation inclus (chaque bascule paie slippage+spread).
  Verrous D33/D34, alpha + DD p95 (MC size-aware) + Calmar figés.
- [x] **8n.3** (branche « sinon » : aucun alpha positif → consigné, aucune adoption ; redéfinition d'objectif = Décision requise Sprint 9) **Décision de suite (Décision requise)** : si la rotation
  démontre un alpha OOS positif net de coûts (enfin un edge !) → confirmation
  hors-protocole complète (D42/D43) puis mise en prod (Sprint 9) ; sinon
  consigner — la stratégie mono-actif ET la rotation seraient soldées, et la
  question deviendrait « ce moteur peut-il battre le B&H, ou l'accepte-t-on
  comme un outil de gestion du risque à rendement B&H ? » (redéfinition
  d'objectif avec l'utilisateur).

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

# 🟣 SPRINT 10 — Première famille de signal réellement différente : MEAN-REVERSION ✅ (clos le 2026-07-05, verdict : pas d'edge démontré)

> Décision utilisateur (c) à l'ouverture : les DEUX grands axes de recherche d'alpha
> (chaîne mono-actif Sprints 8→8-octies, rotation multi-actifs 8-nonies) sont soldés
> sans edge. Plutôt que de raffiner une famille perdante ou de redéfinir l'objectif,
> on attaque une famille de signal **RÉELLEMENT différente** : le **mean-reversion**
> (contrarian — acheter la faiblesse, sortir au retour à la moyenne), l'INVERSE exact
> de la prémisse trend-following. Jugée par le harnais du Sprint 7 en **OOS**, jamais
> en IS ; discipline inchangée (verdicts verrouillés, configs champ par champ D33,
> trades OOS poolés D34, OOS ≫ warmup D35, confirmation hors-protocole COMPLÈTE avant
> toute adoption D42/D43). Point de départ : `SwingStrategy::evaluate`
> (`include/strategies/SwingStrategy.hpp`) et le harnais `WalkForward.hpp`.

- [x] **10.1** **Signal mean-reversion (mode additif)** → `e6eb395`
  `enum class StrategyMode { TrendFollow, MeanReversion }` + `SwingConfig::mode` (défaut
  TrendFollow) + params `mrRsiEntryMax` (30) / `mrRsiExitMin` (55). En mode MeanReversion,
  `evaluate` emprunte un bloc SÉPARÉ à retour immédiat : achat contrarian sur RSI ≤ seuil
  EN régime haussier confirmé (réutilise le filtre SMA), sortie sur RSI ≥ seuil (retour à
  la moyenne) ; les stops/trailing du RiskManager restent la couche de sortie de sécurité.
  Additif : le mode par défaut ne touche jamais le bloc MR → goldens byte-identiques.
  Tests : 5 `MeanReversionUnit` (entrée sur survente, NON-entrée du trend-following sur les
  mêmes barres = preuve de divergence, sortie au retour à la moyenne, blocage en régime
  baissier, valeurs par défaut).
- [x] **10.2** **Juger la famille MR en OOS** → `b7c4eeb`
  Verrou `MeanReversionOosIntegration` (5 tests) + section 14 du CLI `validate`.
  **VERDICT : AUCUN EDGE** — alpha OOS négatif partout (QQQ canonique −13,11 / fin −8,56 ;
  SPY −6,93 / IWM −4,35 / MDY −2,79 ; meilleur seuil du balayage {25,30,35}×{50,55,60} :
  entry ≤ 30 / exit ≥ 60 → −8,38). La chaîne MR en régime ne déclenche presque pas
  (1 trade OOS poolé, 1,45 % de temps investi sur tout QQQ) → verdict majoritairement du
  cash drag (piège D34). Le filtre de régime rend l'alpha « moins mauvais » (−8,56 > −13,02)
  mais au prix d'un échantillon quasi vide (1 trade vs 6). **Écart au plan (D47)** :
  `GridOptimizer` laissé INTACT — l'étendre à 10 axes cassait
  `GridOptimizerUnit.AxisSensitivityRanksMostSensitiveAxis` (verrou `sens.size()==8`) ;
  le balayage MR réutilise `WalkForward`, le vrai juge de paix.
- [x] **10.3** **Confirmation hors-protocole (GATÉE)** → gate FERMÉ, aucun code
  Condition d'ouverture : un candidat alpha OOS > 0 en 10.2. Aucun seuil MR n'a produit
  d'alpha positif (meilleur −8,38) → branche « sinon » appliquée : pas de confirmation,
  pas d'adoption, prod reste paper. Résultat valide.
- [x] **10.4** **Décision de suite — TRANCHÉE (a)** le 2026-07-06
  Le 1er jet MR (RSI ≤ 30 en régime) est sans edge OOS MAIS a à peine tradé : la FAMILLE
  n'est pas vraiment épuisée, seulement ce réglage restrictif. 3 options soumises :
  (a) **variante MR qui trade réellement** (entrée Bollinger/z-score au lieu de RSI ≤ 30,
  et/ou retrait du filtre SMA200) — recommandée ; (b) **solder MR**, autre famille ;
  (c) **parquer la recherche → durcissement prod**. **Décision utilisateur : (a)**
  (l'`AskUserQuestion`, en panne au Sprint 10, a fonctionné cette session) → a ouvert
  le **Sprint 11** (variante z-score/Bollinger). Résultat du Sprint 11 : la variante
  TRADE réellement mais reste sans edge → D47 levé, famille MR soldée (voir Sprint 11).

> **Definition of Done du Sprint 10** (en plus de la DoD standard) : identique aux
> familles précédentes — la famille retenue **bat le B&H net de coûts en OOS** OU le
> sous-performe de **< 5 pts** avec **drawdown réduit ≥ 50 %**. Sinon « pas d'edge
> démontré », on ne déploie PAS (résultat valide). **VERDICT DE CLÔTURE : DoD NON
> ATTEINTE** — le 1er jet mean-reversion n'a aucun alpha OOS ; la prod reste paper,
> `liveTradingApproved` = false, verrou `LiveTradingStaysDisapprovedUntilEdgeDoD` intact.

---

# 🟣 SPRINT 11 — Variante mean-reversion qui TRADE réellement (z-score / Bollinger) ✅ (clos le 2026-07-06, verdict : la famille TRADE mais reste sans edge)

> Décision utilisateur 10.4 = (a) : le 1er jet MR (RSI ≤ 30 EN régime SMA200, Sprint 10)
> ne tradait qu'1 fois en OOS (1,45 % de temps investi) — un creux assez profond pour
> RSI ≤ 30 casse souvent le régime SMA200. Le verdict « pas d'edge » était donc
> majoritairement du cash drag (D47) : la FAMILLE mean-reversion n'était pas vraiment
> jugée, seul ce réglage restrictif l'était. Ce sprint construit une variante qui TRADE
> réellement — entrée **z-score / Bollinger** (achat de la clôture sous la bande basse,
> `z = (close − SMA)/σ ≤ −k`), moins couplée au régime — pour obtenir un échantillon de
> trades OOS réel et enfin juger la famille. Jugée en OOS comme toutes les précédentes
> (harnais Sprint 7, verdicts verrouillés, D33/D34/D35). Point de départ :
> `SwingStrategy::evaluate` bloc MeanReversion (`include/strategies/SwingStrategy.hpp`).

- [x] **11.0** **Écart-type glissant `RollingStdDev`** (prérequis z-score) → `bda2991`
  Nouvel `IIndicator<double>` calqué sur `SMA` : écart-type de population sur fenêtre
  glissante (somme glissante des valeurs ET des carrés, O(n)), même convention de sortie
  (vecteur aligné, warmup 0.0, vide si trop court). Brique du z-score. 5 tests
  IndicatorsUnit calculés à la main (cas d'école σ=2,0, série plate → 0, fenêtre
  glissante, série trop courte → vide, période invalide → throw). Aucun golden touché.
- [x] **11.1** **Entrée z-score / Bollinger (mode MeanReversion)** → `f3c3b70`
  Champs additifs `SwingConfig::mrBandPeriod` / `mrBandEntryK` / `mrBandExitZ` (défauts
  0 = désactivé → repli exact sur l'entrée RSI Sprint 10). Bloc z-score séparé dans
  `evaluate` : achat en régime haussier confirmé quand `z ≤ −k` (clôture sous la bande
  basse), sortie au retour à la moyenne (`z ≥ mrBandExitZ`, défaut 0). Indicateurs de
  bande (SMA + `RollingStdDev`) construits par `create()` seulement si mrBandPeriod > 0,
  injectés via des paramètres de ctor defaultés à nullptr (schéma `atr_`) → sites
  d'appel historiques inchangés. Priorité sortie-avant-entrée et non-gate des sorties
  par le régime préservées ; bande incalculable (σ nul) → repli RSI fail-safe.
  **5 tests MeanReversionUnit** (achat sous la bande basse, divergence vs trend-following
  sur les mêmes barres, sortie au retour à la moyenne, régime ON bloque / OFF autorise
  = levier « retrait SMA200 », défauts désactivés). **Goldens byte-identiques.**
- [x] **11.2** **Juger la variante qui TRADE en OOS** → `dc64657`
  4 verrous `MeanReversionOosIntegration.ZScore*` (chaîne canonique + fin avec contrôle
  `nbTradesOos > 1`, régime ON/OFF, multi-actifs SPY/IWM/MDY, balayage
  {10,20}×{1,5;2;2,5}) + section 15 du CLI `validate`. **VERDICT : la variante TRADE
  réellement** (3 trades OOS canonique / 4 fin, jusqu'à 23 sans filtre — D47 levé, la
  famille est enfin exercée) **MAIS AUCUN EDGE** : alpha OOS négatif partout — QQQ
  canonique −15,35 / fin −9,87 ; SPY −7,78 / IWM −5,14 / MDY −3,36 ; meilleur du balayage
  (period 10 / k 2,5) −9,15. Retirer le filtre de régime fait trader bien plus (4 → 23)
  mais EMPIRE l'alpha (−9,87 → −15,68 : acheter les creux hors tendance perd).
- [x] **11.3** **Confirmation hors-protocole (GATÉE)** → gate FERMÉ, aucun code
  Condition d'ouverture : un candidat alpha OOS > 0 en 11.2. Aucun réglage z-score n'a
  produit d'alpha positif (meilleur −9,15) → branche « sinon » : pas de confirmation,
  pas d'adoption, prod reste paper. Résultat valide.
- [x] **10.4/11.4** **Décision de suite → 4e famille d'alpha** (décision utilisateur
  2026-07-06). Les TROIS familles de signal sont désormais soldées sans edge OOS
  (trend-following mono-actif, rotation multi-actifs, mean-reversion — cette dernière
  VRAIMENT jugée maintenant qu'elle trade). L'utilisateur choisit de continuer la
  recherche d'edge sur une famille NEUVE (plutôt que parquer/durcir la prod) → **Sprint
  12**, famille précise = décision d'ouverture (vol targeting / RSI(2) Connors /
  pairs-trading / diversifiant non-actions).

> **Definition of Done du Sprint 11** (en plus de la DoD standard) : identique aux
> familles précédentes — la variante **bat le B&H net de coûts en OOS** OU le
> sous-performe de **< 5 pts** avec **drawdown réduit ≥ 50 %**. Sinon « pas d'edge
> démontré », on ne déploie PAS (résultat valide). **VERDICT DE CLÔTURE : DoD NON
> ATTEINTE** — la variante z-score TRADE réellement mais n'a aucun alpha OOS positif ;
> la prod reste paper, `liveTradingApproved` = false, verrou
> `LiveTradingStaysDisapprovedUntilEdgeDoD` intact. Acquis positif : D47 levé — la
> famille mean-reversion est désormais VRAIMENT jugée (elle trade jusqu'à 23 fois OOS)
> et confirmée sans edge, pas seulement sous-échantillonnée.

---

# 🟣 SPRINT 12 — 4e famille d'alpha : PAIRS-TRADING market-neutral ✅ (clos le 2026-07-06, verdict : la famille TRADE massivement mais reste sans edge)

> Décision utilisateur 11.4 = PAIRS-TRADING. Les TROIS familles précédentes
> (trend-following mono-actif, rotation multi-actifs, mean-reversion) sont soldées
> sans edge OOS — toutes DIRECTIONNELLES sur un QQQ structurellement haussier
> (D48 : « acheter les creux hors tendance perd »). Le pairs-trading est la seule
> famille ORTHOGONALE : on ne parie pas sur la hausse d'un actif mais sur le
> RETOUR À LA MOYENNE de l'ÉCART entre deux actifs corrélés (long une jambe, short
> l'autre, dollar-neutral). Meilleure chance d'un edge RÉEL ; coût le plus élevé
> (tout le harnais est mono-symbole). Jugée en OOS comme toutes les précédentes,
> via un moteur SÉPARÉ (calqué sur RotationBacktester, Sprint 8-nonies) qui ne
> touche NI la prod NI aucun golden.

- [x] **12.1** **Moteur `PairsBacktester` + `PairsWalkForward`** → `620c7aa`
  Nouveau `include/backtest/PairsBacktester.hpp` (header-only, moteur offline
  séparé). Signal = z-score du spread log `s = log(P0) − log(P1)` sur fenêtre
  glissante (réutilise `SMA` + `RollingStdDev`) ; position dollar-neutral 0,5/0,5
  (long P0/short P1 sur `z ≤ −entryK`, inverse sur `z ≥ +entryK`, sortie au retour
  à la moyenne, bande d'hystérésis `[exitZ, entryK]`). Anti-look-ahead (décision
  close[i], rendement i→i+1), ré-amorçage du spread/z par fenêtre (anti-leak),
  coûts par côté sur les DEUX jambes. **Correction clé vs rotation** : `pnlPct`
  dérivé de l'équité COMPOSÉE barre par barre (pas d'un ratio de prix — faux pour
  2 jambes) ; `deployedFraction = 1.0`. Réutilise `alignOnCommonDates`,
  `TradeRecord`, `MonteCarlo`. Simplifications documentées (brut = 1,0,
  constant-mix, coût d'emprunt short ignoré, β=1 ≠ beta-neutral). **8 tests
  PairsBacktesterUnit** via `fromAxis` synthétique, scénarios déterministes
  calculés à la main (zWindow=2 → z = signe de la variation) : spread log, garde
  positivité, arité ≠ 2 neutre, spread plat sans trade, P&L long/short exacts,
  deux allers-retours + cash plat entre les deux, fenêtre < warmup. Goldens
  byte-identiques.
- [x] **12.2** **Juger la famille en OOS** → `1d25b67`
  **7 verrous `PairsOosIntegration`** (axe commun, 3 pavages canonique/fin/décalé,
  Monte-Carlo size-aware, multi-paires, balayage) sur données longues `*_max.csv`,
  config champ par champ (D33), trades poolés (D34), garde `EXPECT_GT(> 1)` (D47),
  clause DoD « DD réduit ≥ 50 % » vérifiable (DD stratégie ET DD B&H jambe 0
  figés). **VERDICT : AUCUN EDGE.** La famille TRADE massivement (209-212 A/R OOS —
  D47 largement satisfait, la famille est VRAIMENT jugée) mais le **Sharpe OOS est
  NÉGATIF partout** : canonique −1,18 / fin −1,20 / décalé −1,16 ; multi-paires
  QQQ/SPY −1,20, QQQ/IWM −0,67, QQQ/MDY −1,01, SPY/IWM −0,88 ; meilleur du balayage
  {10,20}×{1,5;2;2,5} = z=10/k=2,5 à −0,44. MC size-aware : CAGR médian −3,62 %,
  DD p95 68,1 %. La clause « DD réduit ≥ 50 % » est techniquement atteinte (DD
  stratégie ~7 % vs DD B&H jambe 0 ~22 %) mais SANS INTÉRÊT : le rendement est
  négatif. Axe commun QQQ/SPY = 6870 barres (1999-03-10 → 2026-07-01, borné par
  QQQ), distinct des 6562 de la rotation (D49).
- [x] **12.3** **Section 16 du CLI `validate`** → `8bb0166`
  Inspection humaine (non golden) : Sharpe OOS QQQ/SPY sur les 3 pavages, clause
  DoD DD, multi-paires, balayage avec drapeau CANDIDAT, verdict. Chiffres
  cohérents avec les verrous 12.2.

> **Definition of Done du Sprint 12** (en plus de la DoD standard) : la famille
> market-neutral produit un **rendement ajusté du risque ABSOLU positif** (Sharpe
> OOS > 0, DD faible) net de coûts — un market-neutral (bêta ~0) ne bat pas un
> indice long, on le juge donc sur le Sharpe, pas sur l'alpha vs B&H. Cadrage sur
> la clause DoD EXISTANTE « DD réduit ≥ 50 % » sans reformuler le texte gouverné.
> **VERDICT DE CLÔTURE : DoD NON ATTEINTE** — Sharpe OOS négatif partout ; la prod
> reste paper, `liveTradingApproved` = false, verrou
> `LiveTradingStaysDisapprovedUntilEdgeDoD` intact. Acquis : la famille
> market-neutral est VRAIMENT jugée (elle trade ~210 fois OOS) et confirmée sans
> edge — le spread log naïf β=1 sur fenêtre courte est du bruit. Les **QUATRE**
> familles de signal sont désormais soldées sans edge OOS.

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
| D40 | 🟡 | (Sprint 8-quater) **L'axe trailing est exploré SANS gagnant robuste — la fragilité venait de l'axe, pas de sa paramétrisation.** Le trailing ATR(14) (mult=3, choisi sur le pavage fin où la sélection est PLATE : écart max−min 0,47 pt) améliore 2 pavages sur 3 (+0,33 fin, **+1,82 canonique**) mais rend **−0,24 pt sur le décalé** → « pas d'amélioration robuste » au critère strict (≥ chaîne sur les DEUX pavages non-choisis). Nuance consignée dans le verrou : l'écart défavorable est un ordre de grandeur sous l'inversion du candidat 8-ter (−0,24 vs −9,2 pts) — amélioration NON ROBUSTE, pas réfutation brutale. Le mécanisme reste dans le moteur (flag additif `trailingAtrMult`, défaut 0, hors ConfigLoader/prod.json jusqu'à adoption) et pourra être re-jugé si la chaîne change | Consigné (verrous `TrailingAtrIntegration`) ; recherche ré-orientée signaux (Sprint 8-quinquies) |
| D41 | 🟡 | (Sprint 8-quinquies) **Un mécanisme optionnel peut être structurellement MASQUÉ par un mécanisme existant — vérifier son ACTIVATION avant d'interpréter son verdict OOS.** Deux formes observées le même sprint : (a) le breakout « s'ajoute » est un quasi sous-ensemble de la re-entrée 8.5 → deltas EXACTEMENT nuls partout (prédit à l'ouverture, ce qui a motivé la variante « remplace » — la prédiction a été MESURÉE, pas crue sur parole) ; (b) la sortie structurelle N ≥ 20 ne tire JAMAIS avant le trailing % 0,03 → mesures identiques à la chaîne sur les trois pavages (non prédit, détecté par les verrous D34 : mêmes alphas ET mêmes comptes de trades que la chaîne). Un « ≥ chaîne » par ÉGALITÉ d'inertie ne valide rien — et un argmax de mini-grille peut être un ex æquo dégénéré (verrouillé : premier des ex æquo). Garde-fou adopté : tout verrou de mini-grille COMPARE ses mesures à la chaîne et documente l'inertie éventuelle ; tout nouvel item de mécanisme anticipe ses interactions de masquage dès sa rédaction (fait pour 8y.1/8y.2) | Consigné (verrous `SignalFamiliesIntegration`) ; règle intégrée aux items du Sprint 8-sexies |
| D42 | 🟡 | (Sprint 8-sexies) **Premier mécanisme candidat VIVANT du protocole : l'entrée pullback (RSI ≤ 40, « s'ajoute ») passe l'acceptation « ≥ chaîne sur les DEUX pavages non-choisis »** — première fois depuis que le protocole 3-pavages existe (fin +0,33 / canonique +0,42 / décalé +1,52 pt, signe stable partout — contraste avec le candidat 8-ter et le breakout 8s.3, réfutés à ce même juge). MAIS l'alpha OOS reste négatif sur les trois pavages (aucun edge absolu) et la leçon D36 s'applique : le candidat 8b.1 avait lui aussi survécu à sa PREMIÈRE validation avant d'être réfuté hors-grille. **Décision utilisateur (2026-07-03) : CONFIRMER avant d'adopter** — la validation hors-protocole (walk-forward multi-actifs SPY/IWM/MDY, Monte-Carlo des trades OOS, grille resserrée RSI {35, 40, 45} — modèle 8-ter) est le PRÉREQUIS de toute adoption ; d'ici là, défauts SwingConfig, goldens et config/prod.json inchangés (câblage ConfigLoader différé, règle 8q.3). Le sprint de confirmation reste au backlog (le sprint suivant est la 4e famille — décision utilisateur) | ✅ Confirmé sur l'ALPHA au Sprint 8-septies (8d.3, `1676727`) : l'alpha généralise aux données longues (dot-com/2008) ET à 2/3 actifs, argmax de grille stable — connaissance POSITIVE, la méfiance D36 est levée sur l'alpha. Mais l'adoption est bloquée par le RISQUE (D44), pas par l'alpha |
| D43 | 🟡 | (Sprint 8-septies) **Multiple-testing : plus on juge d'hypothèses, plus le « meilleur » est un artefact probable.** Depuis le Sprint 8, le protocole a jugé un grand nombre d'hypothèses (paramètres 8b.1, trailing 8q, structure/breakout 8s, pullback/volatilité 8y, + les variantes d'atténuation 8d.6/8d.7). Chaque « gagnant » d'une sélection sur les mêmes fenêtres est suspect (D36 en est un cas). **Règle adoptée** : toute adoption future exige la confirmation hors-protocole COMPLÈTE (généralisation D42 : données longues + multi-actifs + grille resserrée + Monte-Carlo, PAS seulement le pavage de choix) — c'est exactement ce qui a distingué le pullback (alpha confirmé) du candidat 8b.1 (réfuté). Aucune modification des `prompt-*.md` (règle intangible) : la règle vit dans la ROADMAP | Consigné (règle de DoD des verdicts) ; appliqué dès 8d.3 |
| D44 | 🟠 | (Sprint 8-septies) **L'alpha et le drawdown du pullback sont COUPLÉS — ils viennent des mêmes achats de creux en régime volatil.** Le pullback confirme son alpha (D42) mais double le drawdown de queue (DD p95 10,80 → 19,27 %, 8d.3c). Trois leviers config-only ont échoué à les séparer (8d.6/8d.7) : le gating ATR ne casse le DD qu'à une valeur ISOLÉE (0,015, voisins pires → sur-ajustement probable) et au prix de l'alpha ; le stop serré ne bouge pas le DD (le trailing 3 % tire d'abord) ; `riskPerTradePct` est INVARIANT d'échelle sur le drawdown en % (il scale l'équité uniformément). **Conclusion : le découplage config-only est épuisé ; le vrai levier est un position-sizing MODULÉ par la volatilité (code moteur), qui réduirait la taille seulement quand la volatilité est haute.** Décision utilisateur (2026-07-04) : pullback NON adopté, vol-sizing au backlog (Sprint 8-octies) | ⚠️ Le sous-constat « riskPerTradePct invariant d'échelle » s'est révélé être un ARTEFACT du MC aveugle à la taille (D45) : corrigé, riskPerTradePct RÉDUIT bien le DD (mais coûte l'alpha). Le vol-sizing a été exploré au Sprint 8-octies : découplage PARTIEL (DD 7,88 → 6,51 à faible coût d'alpha) mais insuffisant → non adopté. Le fond de D44 (alpha/risque du pullback difficiles à séparer) TIENT |
| D45 | 🟠 | (Sprint 8-octies) **Le Monte-Carlo était AVEUGLE à la taille de position.** `MonteCarlo::run` bootstrappait `pnlPct` (rendement de la POSITION, indépendant de la taille) en le composant sur l'équité PLEINE — donc tout schéma de sizing (riskPerTradePct, vol-sizing) y était INERTE (cause de l'invariance du DD constatée en 8d.7, prise à tort pour une propriété d'échelle en D44). Détecté par exploration AVANT d'écrire le vol-sizing (leçon D41). **Correctif (8o.1, `b64ffd7`)** : `TradeRecord::deployedFraction` (capital investi / équité à l'entrée, calculé par PaperBroker) ; le MC bootstrappe `deployedFraction × pnlPct`. Le MC reflète enfin la taille — et révèle que l'ancien SURESTIMAIT tout (la chaîne ne déploie que ~40 % : DD p95 8d.3c 19,27 → 7,88, chaîne 10,80 → 4,28). Re-baseline documentée des valeurs MC (goldens backtest intacts). C'est ce correctif qui a rendu le vol-sizing (8o.2/8o.3) MESURABLE | ✅ Corrigé au Sprint 8-octies (8o.1) ; garde-fou : MonteCarloUnit teste désormais la pondération par deployedFraction |
| D46 | 🟠 | (Sprint 8-nonies) **La rotation par régime détruit de la valeur — PIRE que le panier passif, net de coûts.** Sur données longues (2000-2026, QQQ/SPY/IWM/MDY total-return, axe commun aligné 6562 barres), détenir l'actif au SMA200-régime le plus fort (bascule quand l'actif de tête change) rend +186 % vs meilleur B&H +1836 % et panier équipondéré +1120 % ; alpha OOS négatif vs les DEUX références sur les 3 pavages (−11,6/−10,1/−13,3 vs meilleur ; −2,7/−5,9/−7,1 vs panier) et DD de queue MC size-aware ~55 %. Cause : le timing de régime whipsaw (achète après la hausse, vend après la baisse) + les 472 coûts de bascule ; un filtre SMA200 ne surperforme pas une simple diversification statique sur des actifs corrélés. Les DEUX axes de recherche d'alpha (mono-actif + rotation) sont soldés. | Consigné (verrous `RotationOosIntegration`) ; décision de suite = Décision requise Sprint 9 (redéfinition d'objectif) |
| D47 | 🟡 | (Sprint 10) **Le 1er jet mean-reversion n'a PAS d'edge OOS — mais surtout il ne trade quasi pas, donc la famille n'est pas vraiment jugée.** L'entrée contrarian « RSI ≤ 30 ET prix > SMA200 » est doublement restrictive : un creux assez profond pour RSI ≤ 30 casse souvent le régime SMA200 (le prix passe sous la SMA) → 1 seul trade OOS poolé, 1,45 % de temps investi sur tout QQQ. L'alpha OOS négatif (−8,56 fin) est donc majoritairement du cash drag (piège D34, comme la base 8.1). Le balayage de seuils {25,30,35}×{50,55,60} ne produit AUCUN candidat > 0. Leçon : avant de conclure « la famille MR est sans edge », il faut un réglage qui TRADE réellement (entrée moins restrictive : Bollinger/z-score, ou retrait du filtre SMA200) — sinon on ne juge que la rareté des signaux, pas leur qualité. **Écart au plan** : `GridOptimizer` non étendu aux axes MR (l'ajout de 2 axes cassait le verrou `GridOptimizerUnit.AxisSensitivityRanksMostSensitiveAxis`, `sens.size()==8`) ; le balayage réutilise `WalkForward`. | ✅ LEVÉ au Sprint 11 (10.4 tranchée (a), `f3c3b70`/`dc64657`) : l'entrée z-score/Bollinger TRADE réellement (3-4 trades OOS, jusqu'à 23 sans filtre) → la famille est enfin jugée. Verdict confirmé sans edge (voir D48) : le sous-échantillonnage n'était pas la cause de l'absence d'alpha |
| D48 | 🟡 | (Sprint 11) **La famille mean-reversion est confirmée SANS edge OOS une fois qu'elle TRADE réellement — le sous-échantillonnage de D47 masquait, mais n'expliquait pas, l'absence d'alpha.** La variante z-score/Bollinger (achat `z ≤ −k` sous la bande basse) déclenche 3-4 trades OOS (canonique/fin) et jusqu'à 23 sans filtre de régime — un vrai échantillon, contre 1 trade pour le 1er jet RSI ≤ 30. Malgré ça, alpha OOS négatif partout (QQQ −15,35/−9,87 ; SPY −7,78 / IWM −5,14 / MDY −3,36 ; meilleur balayage −9,15). Constat clé : **retirer le filtre de régime fait TRADER bien plus (4 → 23) mais DÉGRADE l'alpha (−9,87 → −15,68)** — acheter les creux hors d'une tendance haussière (« attraper un couteau qui tombe ») perd de l'argent, ce qui est cohérent avec le fait que QQQ est structurellement haussier (le trend-following y est la bonne prémisse, pas le contrarian). Les TROIS familles de signal (trend-following mono-actif, rotation, mean-reversion) sont désormais soldées sans edge. | Consigné (verrous `MeanReversionOosIntegration.ZScore*`) ; décision de suite 11.4 = **4e famille d'alpha** (Sprint 12, décision utilisateur) |
| D49 | 🟡 | (Sprint 12) **La 4e famille — pairs-trading market-neutral — TRADE massivement mais n'a AUCUN edge OOS ; le spread log naïf β=1 sur fenêtre courte est du bruit.** Contrairement au mean-reversion (D47), le sous-échantillonnage n'est PAS en cause : la stratégie déclenche 209-212 allers-retours OOS (D47 largement satisfait). Malgré ça, **Sharpe OOS NÉGATIF sur les 3 pavages** (canon −1,18 / fin −1,20 / décalé −1,16), les **4 paires** (QQQ/SPY −1,20 ; QQQ/IWM −0,67 ; QQQ/MDY −1,01 ; SPY/IWM −0,88) et **tous les réglages** du balayage {10,20}×{1,5;2;2,5} (meilleur z=10/k=2,5 = −0,44) ; MC size-aware CAGR médian −3,62 %, DD p95 68,1 %. Constat market-neutral : la clause DoD « DD réduit ≥ 50 % » est **techniquement atteinte** (DD stratégie ~7 % vs DD B&H jambe 0 ~22 %) mais SANS INTÉRÊT — un livre qui perd de l'argent à faible volatilité reste perdant. Cause probable : le spread log(P0)−log(P1) avec β=1 n'est PAS testé pour la stationnarité/cointégration ; un z-score sur fenêtre glissante courte de-mean localement un spread non stationnaire → trades de bruit. Les **QUATRE** familles de signal sont désormais soldées sans edge OOS. Piste de raffinement non explorée (backlog) : cointégration Engle-Granger + hedge ratio roulant (vraie paire, pas spread naïf). | Consigné (verrous `PairsOosIntegration`) ; décision de suite = **Décision requise Sprint 13** (4 familles soldées : parquer/durcir la prod, raffiner le pairs-trading, ou 5e famille) |
| D50 | 🟡 | (Sprint 13) **La 5e famille — vol-regime / volatility-managed — TRADE réellement mais n'a AUCUN edge ajusté du risque ; le cash drag coûte plus que la réduction de DD ne rapporte.** Le filtre binaire long/cash (long si vol réalisée 20 j ≤ médiane glissante 126 j de cette vol) déclenche 107-135 stints OOS (D47 satisfait) et son Sharpe est POSITIF (0,52 canon / 0,23 fin / 0,61 décalé) — mais SOUS le Sharpe du B&H (1,09 / 0,77 / 1,04) sur les 3 pavages, les 4 actifs (dSharpe QQQ −0,54, SPY −0,62, IWM −0,39, MDY −0,68) et les 9 réglages du balayage {10,20,42}×{0,8;1;1,2} (meilleur vl=42/seuil=1,2 = dSharpe −0,16 < 0). Il réduit le drawdown (14,5 vs 18,1 %) mais de < 50 % seulement (clause DoD « DD réduit ≥ 50 % » NON atteinte, contraste avec le pairs-trading D49) et l'alpha vs B&H est négatif (sous-performe le rendement du B&H — cash drag T4/D48, cohérent avec un QQQ structurellement haussier). MC size-aware cagrP50 +5,03 % (gagne de l'argent, moins efficacement que le B&H). Les CINQ familles pré-définies sont soldées sans edge OOS. Pistes de 5e famille NON explorées (backlog) : volatilité IMPLICITE (VIX/VXN — nécessite un export de données, aucune dans le dépôt ; export_total_return.py rejette un indice sans dividende, D29) ; scaling continu w = cible/réalisée (Moreira-Muir strict, code moteur) ; données alternatives / surface d'options (gros chantier data). | Consigné (verrous `VolRegimeOosIntegration`) ; décision de suite = **Décision requise Sprint 14** (parquer/durcir la prod, raffiner le pairs-trading, autre 5e famille à données externes, ou documenter la conclusion) |

## Changelog

### Sprint 13 — 5e famille d'alpha : vol-regime / volatility-managed (2026-07-06)

**Contexte** : décision utilisateur (c) = ouvrir une famille NEUVE au-delà des
signaux de prix simples, après que les QUATRE familles pré-définies (trend-following
mono-actif, rotation, mean-reversion, pairs-trading) ont été soldées sans edge OOS.
Parmi les exemples de (c) (données alternatives / régime de volatilité / surface
d'options), le **régime de volatilité RÉALISÉE** est la seule variante buildable
offline sans nouvelle infra de données (aucun VIX/VXN dans le dépôt ; la vol réalisée
se calcule des CSV existants via `RollingStdDev` sur les rendements). Premier jet
DISCRET long/cash (hypothèse « volatility-managed portfolios », Moreira & Muir 2017),
jugé en OOS via un moteur SÉPARÉ calqué sur Rotation/Pairs (offline, ne touche ni la
prod ni aucun golden).

**Baseline à l'ouverture** : **679/679 verte** (build −Werror sans warning, Linux
paquets système sans vcpkg). Recalage D20 : `ctest -N`=679, décomposition réelle
**552 unitaires + 127 intégration** (le tableau de bord affichait « 551 + 128 » —
décalé d'une unité dans chaque sens, total exact). Environnement de build recréé de
zéro (conteneur neuf) : `apt-get update` + install boost/nlohmann-json/curl/sqlite3/
gtest (leçon Sprint 3), configure sans toolchain vcpkg.

**Commits** (ordre chronologique = ordre d'exécution) :
- `1d215f9` feat(vol-regime) : moteur VolRegimeBacktester + VolRegimeWalkForward (item 13.1)
- `23e8801` test(vol-regime) : verdict OOS verrouillé (3 pavages, multi-univers, MC size-aware — item 13.2)
- `a6ddce3` test(validation) : section 17 famille vol-regime + verdict figé (item 13.3)
- (clôture) docs : mise à jour roadmap

**Tests** : 679 → **694** (+15 : 560 unitaires + 134 intégration). +8
VolRegimeBacktesterUnit (fonctions pures `closeReturns`/`realizedVol`/`trailingMedian`
causale ; série plate = calme mais P&L nul ; calme → long P&L exact ; agité → cash
plat ; deux stints + cash plat entre, D47 ; fenêtre < warmup neutre) ; +7
VolRegimeOosIntegration (série QQQ_max verrouillée, 3 pavages canonique/fin/décalé, MC
size-aware graine fixe, multi-univers 4 actifs, balayage volLookback×seuil).
**Goldens backtest byte-identiques** (moteur séparé, config prod ni golden existant
touché).

**Interfaces ajoutées** (additives, header-only) : `include/backtest/VolRegimeBacktester.hpp`
— `VolRegimeConfig` (volLookback=20, refLookback=126, thresholdMult=1,0, coûts),
`VolRegimeResult` (totalReturn, Sharpe/Sortino, DD stratégie ET DD/Sharpe/return B&H
de continuité, alphaVsBuyHold), fonctions pures `closeReturns`/`realizedVol`/
`trailingMedian`, `VolRegimeBacktester` (run/runRange, seam `fromSeries`),
`VolRegimeWalkForward`. Réutilise `RollingStdDev`, `CsvDataFeed`, `TradeRecord`,
`MonteCarlo`. Section 17 du CLI `validate`.

**Verdict OUT-OF-SAMPLE** :
- **La famille TRADE réellement** (D47 satisfait) : 107-135 stints OOS par pavage/actif.
- **MAIS AUCUN EDGE ajusté du risque** : le Sharpe stratégie est POSITIF (0,52 canon /
  0,23 fin / 0,61 décalé) mais SOUS le Sharpe du B&H (1,09 / 0,77 / 1,04) → dSharpe < 0
  sur les 3 pavages ; idem sur les 4 actifs (dSharpe QQQ −0,54, SPY −0,62, IWM −0,39,
  MDY −0,68) et les 9 réglages du balayage (meilleur vl=42/seuil=1,2 = −0,16).
- **Constat directionnel (D50)** : le filtre réduit le drawdown (14,5 vs 18,1 %) mais
  de < 50 % (clause DoD NON atteinte, contraste avec le pairs-trading où elle l'était)
  et l'alpha vs B&H est négatif (cash drag T4/D48) ; le B&H de QQQ, structurellement
  haussier, est difficile à battre en risque ajusté en sortant du marché. MC size-aware
  cagrP50 +5,03 %, DD p95 45,9 % (gagne de l'argent, moins efficacement que le B&H).
- Warmup 144 barres (volLookback+refLookback−2) → OOS dimensionné ≥ 400 (D35).

**Verdict de clôture** : DoD NON atteinte (dSharpe OOS négatif partout) → prod paper,
`liveTradingApproved` = false, verrou `LiveTradingStaysDisapprovedUntilEdgeDoD` intact.
Fichiers gouvernés (`config/prod.json`, `prompt-*.md`, CLAUDE.md live-safety) intacts.
**Les CINQ familles d'alpha pré-définies sont soldées sans edge OOS.** Décision de suite
= **Décision requise Sprint 14**.

**Rétrospective** :
1. *Découpage* : bon. 13.1 (moteur + unit tests TDD) → 13.2 (verrou de verdict OOS,
   sentinelle → mesure → figée) → 13.3 (CLI), linéaire et additif comme le Sprint 12.
   Calquer VolRegimeBacktester sur Rotation/Pairs (moteur offline séparé, mono-actif) a
   gardé les goldens byte-identiques. Deux points de conception résolus AVANT l'écriture
   (revue Plan) : (a) premier jet DISCRET long/cash plutôt que scaling continu — un champ
   `targetVol` serait du code mort tant que le moteur est binaire (D33) ; (b) seuil
   AUTO-NORMALISANT (médiane glissante causale) plutôt qu'absolu, pour la comparabilité
   entre actifs sans look-ahead. Aucune dépendance ratée.
2. *Prompts du workflow* : suffisants, AUCUN diff proposé. Le point de décision produit
   (quelle variante de (c)) a été tranché par la faisabilité offline (seul le régime de
   vol RÉALISÉE est buildable sans nouvelle donnée) + décision utilisateur (c). Capturé
   hors `AskUserQuestion` (outil en panne cette session : erreur « permission stream
   closed » sur 3 tentatives — la décision (c) est arrivée en texte libre) sans jamais
   improviser sur les fichiers gouvernés.
3. *À détecter plus tôt / garde-fou* : le warmup de cette famille (144 barres) est bien
   plus grand que celui du pairs-trading (19) → risque du piège D35 (fenêtre OOS quasi
   pur cash drag). Anticipé à la conception (OOS ≥ 400 dimensionné exprès, documenté dans
   le test). Nouveau garde-fou de fait : le verrou fige le DD stratégie ET le DD B&H ET
   les DEUX Sharpe, ce qui rend la clause « DD réduit ≥ 50 % » AUTO-vérifiable et
   distingue « DD réduit mais insuffisamment + Sharpe sous le B&H » d'un vrai edge de
   risque. Candidat backlog (D50) : avant de conclure « aucun régime de vol exploitable »,
   tester la volatilité IMPLICITE (VIX) et le scaling continu — le premier jet ne juge que
   le filtre binaire sur vol réalisée.
4. *Notes /100* : Architecture 90 (=) : 3e moteur offline calqué sur les deux précédents,
   propre mais sans terrain architectural neuf. Qualité 93 (=). FinTech 88 (=) : une
   hypothèse FinTech légitime (vol-managed) correctement jugée, mais sans edge. Production
   72 (=). **Rentabilité 28 (=)** : la 5e famille est jugée et soldée sans edge ; la
   capacité démontrée à gagner de l'argent est inchangée, mais la connaissance négative se
   renforce (on SAIT que même une famille NON directionnelle-sur-le-prix comme le régime
   de vol réalisée n'a pas d'edge ajusté du risque net de coûts sur ETFs US).

### Sprint 12 — 4e famille d'alpha : pairs-trading market-neutral (2026-07-06)

**Contexte** : décision utilisateur 11.4 = PAIRS-TRADING. Les trois familles
précédentes (trend-following mono-actif, rotation, mean-reversion) sont soldées
sans edge — toutes DIRECTIONNELLES sur un QQQ structurellement haussier. Le
pairs-trading est la seule famille ORTHOGONALE (valeur relative / market-neutral :
long une jambe, short l'autre, pari sur le retour à la moyenne de l'écart). Jugée
en OOS via un moteur SÉPARÉ (calqué sur RotationBacktester, offline, ne touche ni
la prod ni aucun golden).

**Baseline à l'ouverture** : **664/664 verte** (conforme au tableau de bord,
`ctest -N`=664, aucune dérive D20). Environnement : Linux, paquets système
(chemin CI, sans vcpkg), build −Werror sans warning.

**Commits** (ordre chronologique = ordre d'exécution) :
- `620c7aa` feat(pairs) : moteur PairsBacktester + PairsWalkForward (item 12.1)
- `1d25b67` test(pairs) : verrou de verdict OOS market-neutral (item 12.2)
- `8bb0166` feat(pairs) : section 16 validate PAIRS-TRADING (item 12.3)
- (clôture) docs : mise à jour roadmap

**Tests** : 664 → **679** (+15 : 551 unitaires + 128 intégration). +8
PairsBacktesterUnit (fonction pure logSpread + garde positivité, arité ≠ 2 neutre,
spread plat sans trade, P&L long/short exacts calculés à la main, deux
allers-retours + cash plat, fenêtre < warmup) ; +7 PairsOosIntegration (axe commun
QQQ/SPY verrouillé, 3 pavages canonique/fin/décalé, Monte-Carlo size-aware,
multi-paires, balayage). **Goldens backtest byte-identiques** (moteur séparé,
config prod ni golden existant touché).

**Interfaces ajoutées** (additives, header-only) : `include/backtest/PairsBacktester.hpp`
— `PairsConfig` (zWindow, entryK, exitZ, coûts), `PairsResult` (totalReturn,
Sharpe/Sortino, DD stratégie ET DD B&H jambe 0, alphaVsLeg0/Basket de continuité),
`PairsBacktester` (run/runRange, seam `fromAxis`), `PairsWalkForward`, fonction pure
`logSpread`. Réutilise `alignOnCommonDates`/`AlignedAxis` (RotationBacktester), `SMA`,
`RollingStdDev`, `TradeRecord`, `MonteCarlo`. Section 16 du CLI `validate`.

**Verdict OUT-OF-SAMPLE** :
- **La famille TRADE massivement** (D47 largement satisfait) : 209-212 allers-retours
  OOS — un vrai échantillon, la famille est VRAIMENT jugée.
- **MAIS AUCUN EDGE** : Sharpe OOS négatif partout — canonique **−1,18** / fin
  **−1,20** / décalé **−1,16** ; multi-paires QQQ/SPY −1,20, QQQ/IWM −0,67, QQQ/MDY
  −1,01, SPY/IWM −0,88 ; meilleur du balayage {10,20}×{1,5;2;2,5} = z=10/k=2,5 à
  **−0,44**. MC size-aware : CAGR médian **−3,62 %**, DD p95 **68,1 %**. Aucun
  candidat Sharpe > 0 → gate de confirmation FERMÉ.
- **Constat market-neutral (D49)** : la clause DoD « DD réduit ≥ 50 % » est
  techniquement atteinte (DD stratégie ~7 % vs DD B&H jambe 0 ~22 %) mais sans
  intérêt (rendement négatif). Cause probable : spread log β=1 non testé pour la
  stationnarité/cointégration → z-score sur fenêtre courte = trades de bruit.
- Axe commun QQQ/SPY = 6870 barres (1999-03-10 → 2026-07-01, borné par QQQ),
  distinct des 6562 de la rotation (bornée par IWM).

**Verdict de clôture** : DoD NON atteinte (Sharpe OOS négatif partout) → prod
paper, `liveTradingApproved` = false, verrou `LiveTradingStaysDisapprovedUntilEdgeDoD`
intact. Fichiers gouvernés (`config/prod.json`, `prompt-*.md`, CLAUDE.md
live-safety) intacts. **Les QUATRE familles d'alpha sont soldées sans edge OOS.**
Décision de suite = **Décision requise Sprint 13**.

**Rétrospective** :
1. *Découpage* : bon. 12.1 (moteur + unit tests TDD) → 12.2 (verrou de verdict OOS,
   « rouge d'abord » sentinelle → mesure → figée) → 12.3 (CLI), linéaire et additif.
   Le choix de calquer PairsBacktester sur RotationBacktester (moteur offline séparé
   plutôt qu'un mode de SwingStrategy) a gardé les goldens byte-identiques et évité de
   forcer la position 2-jambes market-neutral dans un moteur long-only mono-symbole —
   décision de conception validée par revue avant écriture. La correction critique
   (pnlPct dérivé de l'équité composée, pas d'un ratio de prix — faux pour 2 jambes) a
   été identifiée AVANT l'implémentation, pas après un test rouge. Aucune dépendance
   ratée.
2. *Prompts du workflow* : suffisants, AUCUN diff proposé. Le point de décision produit
   (11.4 = quelle famille) et le cadrage du verdict market-neutral (mapper sur la clause
   DoD existante « DD réduit ≥ 50 % » sans reformuler le texte gouverné) ont été traités
   dans les règles existantes (décision utilisateur + garde-fou live). `AskUserQuestion`
   a fonctionné pour capturer la décision 11.4.
3. *À détecter plus tôt / garde-fou* : le point de contrôle central — « la famille
   trade-t-elle réellement ? » (D47) — est encodé dans les verrous
   (`EXPECT_GT(pool, 1u)`, ~210 A/R). Nouveau : la clause DoD market-neutral « DD réduit
   ≥ 50 % » est rendue AUTO-VÉRIFIABLE par le gel du DD stratégie ET du DD B&H jambe 0
   (`EXPECT_LT(meanMaxDd, 0.5*meanLeg0Dd)`), ce qui distingue « DD réellement réduit mais
   ret < 0 » d'un vrai edge de risque. Candidat backlog (noté D49) : tester la
   stationnarité/cointégration du spread AVANT de conclure — le spread log naïf β=1
   n'était pas un vrai couple coïntégré, d'où des trades de bruit ; un test
   d'Engle-Granger + hedge ratio roulant serait le raffinement honnête si le
   pairs-trading est re-visité.
4. *Notes /100* : Architecture 89 → **90** (+1 : 2e moteur offline séparé propre,
   réutilise l'infra d'alignement multi-actifs sans la dupliquer — la séparation
   moteur-de-recherche / prod tient) ; Qualité 93 (=) ; FinTech 88 (=) ; Production 72
   (=). **Rentabilité 28 (=)** : la 4e et dernière famille pré-définie est jugée et
   soldée sans edge ; la capacité démontrée à gagner de l'argent est inchangée, mais
   l'incertitude a rétréci (on SAIT maintenant que les quatre familles techniques
   simples sur ETFs US n'ont pas d'edge OOS net de coûts — connaissance négative solide,
   pas un artefact).

### Sprint 11 — Variante mean-reversion z-score / Bollinger (2026-07-06)

**Contexte** : décision utilisateur 10.4 = (a) (l'`AskUserQuestion`, en panne au
Sprint 10, a fonctionné cette session) : le 1er jet MR (RSI ≤ 30 en régime) ne
tradait qu'1 fois en OOS — la FAMILLE mean-reversion n'était pas jugée, seul ce
réglage restrictif l'était (D47). Construire une variante qui TRADE réellement
(entrée z-score/Bollinger, moins couplée au régime) pour enfin juger la famille.

**Baseline à l'ouverture** : **650/650 verte** (conforme au tableau de bord,
`ctest -N`=650, aucune dérive). Environnement : Linux, paquets système (chemin CI,
sans vcpkg), build −Werror sans warning.

**Commits** (ordre chronologique = ordre d'exécution) :
- `bda2991` feat(indicateurs) : écart-type glissant RollingStdDev (brique z-score, item 11.0)
- `f3c3b70` feat(strategie) : entrée z-score/Bollinger pour la famille mean-reversion (item 11.1)
- `dc64657` test(validation) : verdict OOS de la variante z-score MR — TRADE mais AUCUN edge (item 11.2)
- (clôture) docs : mise à jour roadmap

**Tests** : 650 → **664** (+14 : 543 unitaires + 121 intégration). Nouvelle brique
`RollingStdDev` + 5 tests IndicatorsUnit (cas d'école σ=2,0, série plate → 0, fenêtre
glissante, série trop courte, période invalide) ; 5 tests MeanReversionUnit (achat sous
la bande basse, divergence vs trend-following, sortie au retour à la moyenne, régime
ON bloque / OFF autorise, défauts désactivés) ; 4 verrous
MeanReversionOosIntegration.ZScore* (chaîne canonique+fin avec contrôle nbTradesOos > 1,
régime ON/OFF, multi-actifs, balayage). **Goldens backtest byte-identiques** (mode défaut
= TrendFollow + champs mrBand* = 0 — aucun défaut, config prod ni golden existant touché).

**Interfaces ajoutées** (additives, header-only) : indicateur `RollingStdDev`
(Indicators.hpp) ; `SwingConfig::mrBandPeriod` / `mrBandEntryK` / `mrBandExitZ` ; bloc
z-score séparé dans `SwingStrategy::evaluate` (mode MeanReversion) ; indicateurs de bande
construits par `create()` seulement si mrBandPeriod > 0 (ctor params defaultés à nullptr,
schéma `atr_`). Section 15 du CLI `validate`.

**Verdict OUT-OF-SAMPLE de la variante z-score** :
- **La variante TRADE réellement** (D47 levé) : 3 trades OOS canonique / 4 fin, jusqu'à
  **23** sans filtre de régime — contre 1 seul pour le 1er jet RSI ≤ 30. La famille est
  enfin exercée.
- **MAIS AUCUN EDGE** : alpha OOS négatif partout — QQQ canonique **−15,35** (3 trades)
  / fin **−9,87** (4) ; multi-actifs SPY −7,78 / IWM −5,14 / MDY −3,36 ; meilleur du
  balayage {10,20}×{1,5;2;2,5} = period 10 / k 2,5 à **−9,15** (3 trades). Aucun candidat
  > 0 → gate de confirmation (11.3) FERMÉ.
- **Constat clé (D48)** : retirer le filtre de régime fait TRADER bien plus (4 → 23) mais
  DÉGRADE l'alpha (−9,87 → −15,68) — acheter les creux hors tendance perd. Cohérent avec
  un QQQ structurellement haussier (le trend-following est la bonne prémisse, pas le
  contrarian). Les TROIS familles de signal sont soldées sans edge.

**Verdict de clôture** : DoD NON atteinte (aucun alpha OOS positif) → prod paper,
`liveTradingApproved` = false, verrou `LiveTradingStaysDisapprovedUntilEdgeDoD` intact.
Fichiers gouvernés (`config/prod.json`, `prompt-*.md`, CLAUDE.md live-safety) intacts.
**Décision de suite 11.4 = 4e famille d'alpha** (décision utilisateur) → Sprint 12.

**Rétrospective** :
1. *Découpage* : bon. 11.0 (brique indicateur) → 11.1 (signal) → 11.2 (verdict) → 11.3
   (gate) → 11.4 (décision), linéaire et additif. Le choix de loger la variante DANS le
   mode MeanReversion existant (nouveaux champs `mrBand*`, bloc z-score séparé) plutôt
   qu'une nouvelle famille a gardé les goldens byte-identiques et chaque commit
   trivialement sûr. `RollingStdDev` calqué sur `SMA` : réutilisation propre de la
   convention de sortie. Aucune dépendance ratée.
2. *Prompts du workflow* : suffisants, AUCUN diff proposé. Note opérationnelle (hors
   prompts) : l'`AskUserQuestion` du harnais a d'abord réussi (décision 10.4 = a),
   permettant de démarrer le sprint, puis a échoué une fois (permission-stream — panne
   connue, D-note Sprint 10/8-nonies) avant de re-fonctionner à la clôture pour capturer
   la décision 11.4. Les deux décisions produit ont donc bien été prises par
   l'utilisateur (garde-fou « décision produit = utilisateur » respecté), aucune tranchée
   seule.
3. *À détecter plus tôt / garde-fou* : le point de contrôle central du sprint —
   « la variante trade-t-elle réellement ? » (D47) — a été explicitement encodé dans les
   verrous (`EXPECT_GT(nbTradesOos, 1)`), ce qui distingue désormais un verdict « pas
   d'edge » d'un artefact de cash drag (D34/D47). Candidat backlog (déjà noté à D47) :
   `WalkForward`/`MeanReversion` pourrait AVERTIR si le temps investi OOS < seuil, pour
   attraper automatiquement un futur verdict jugé sur trop peu de trades.
4. *Notes /100* : **Architecture 89 (=)** (capacité additive : variante de signal +
   nouvel indicateur dans le même moteur, harnais réutilisé tel quel). **Qualité 93 (=)**
   (664 verts, négatif discipliné verrouillé avec contrôle du nombre de trades, goldens
   intacts). **FinTech 88 (=)** (connaissance acquise — la 3e famille est VRAIMENT jugée
   et soldée — mais aucune capacité de gain ajoutée). **Production 72 (=)**.
   **Rentabilité 28 (=)** : toujours aucun edge ; la variante trade mais reste négative.
   L'incertitude a rétréci (on SAIT que le mean-reversion n'a pas d'edge, D48 levant D47)
   sans que la capacité démontrée de gain change.

### Sprint 10 — Famille mean-reversion (contrarian) (2026-07-05)

**Contexte** : décision utilisateur (c) à l'ouverture (l'AskUserQuestion du harnais a
fonctionné pour ce 1er choix, puis a échoué pour les suivants — permission-stream) :
les deux axes d'alpha soldés, attaquer une famille de signal RÉELLEMENT différente
plutôt que raffiner une famille perdante. Le mean-reversion (acheter la survente,
sortir au retour à la moyenne) est l'inverse exact du trend-following.

**Baseline à l'ouverture** : **640/640 verte** (conforme au tableau de bord,
`ctest -N`=640, aucune dérive). Environnement : Linux, paquets système (chemin CI,
sans vcpkg), build −Werror sans warning.

**Commits** (ordre chronologique = ordre d'exécution) :
- `e6eb395` feat(strategie) : famille mean-reversion (mode contrarian additif) (item 10.1)
- `b7c4eeb` test(validation) : verdict OOS de la famille mean-reversion — AUCUN edge (item 10.2)
- (clôture) docs : mise à jour roadmap

**Tests** : 640 → **650** (+10 : 533 unitaires + 117 intégration). Nouvelle suite
`MeanReversionUnit` (5 : entrée sur survente, non-entrée du trend-following sur les
mêmes barres = divergence, sortie au retour à la moyenne, blocage en régime baissier,
défauts) ; nouvelle suite `MeanReversionOosIntegration` (5 : tiling partagé,
verdict chaîne QQQ canonique+fin, filtre de régime ON/OFF, multi-actifs SPY/IWM/MDY,
balayage de seuils). **Goldens backtest byte-identiques** (mode défaut = TrendFollow —
aucun défaut, config prod ni golden existant touché).

**Interfaces ajoutées** (additives, header-only) : `StrategyMode { TrendFollow,
MeanReversion }` + `SwingConfig::mode` / `mrRsiEntryMax` / `mrRsiExitMin` ; bloc MR
séparé dans `SwingStrategy::evaluate`. Réutilise RSI, le filtre SMA de régime, le
RiskManager (stops/trailing), et tout le harnais `WalkForward`. Section 14 du CLI
`validate`.

**Verdict OUT-OF-SAMPLE de la famille mean-reversion** :
- **AUCUN EDGE** : alpha OOS moyen QQQ canonique **−13,11** (1 trade poolé) / fin
  **−8,56** (1 trade) ; multi-actifs SPY −6,93 / IWM −4,35 / MDY −2,79 ; meilleur seuil
  du balayage {25,30,35}×{50,55,60} : entry ≤ 30 / exit ≥ 60 → **−8,38**, aucun candidat > 0.
- **Constat clé (D47)** : la chaîne MR en régime déclenche à peine (1,45 % de temps
  investi sur tout QQQ) — l'entrée « RSI ≤ 30 ET prix > SMA200 » est doublement
  restrictive (un creux profond casse souvent le régime). Le verdict est donc
  majoritairement du cash drag (piège D34) : la famille n'est pas vraiment jugée, seul
  ce réglage restrictif l'est. Le filtre de régime rend l'alpha « moins mauvais »
  (−8,56 > −13,02 sans filtre) mais au prix d'un échantillon quasi vide (1 trade vs 6).

**Verdict de clôture (10.3, branche « sinon »)** : aucun candidat alpha OOS > 0 → gate
de confirmation hors-protocole FERMÉ, AUCUNE adoption, consigné. **10.4 = Décision
requise OUVERTE** (AskUserQuestion indisponible cette session) : 3 options (a variante
MR qui trade / b autre famille / c durcissement prod), recommandation (a). Fichiers
gouvernés intacts (`config/prod.json`, `prompt-*.md`, CLAUDE.md live-safety),
`liveTradingApproved` = false, verrou `LiveTradingStaysDisapprovedUntilEdgeDoD` intact.
Prod paper.

**Rétrospective** :
1. *Découpage* : bon. 10.1 (signal) → 10.2 (verdict) → 10.3 (gate) → 10.4 (décision),
   linéaire, additif. Loger le mode MR DANS `SwingStrategy`/`SwingConfig` (plutôt qu'une
   classe `IStrategy` séparée) a gardé les goldens byte-identiques et rendu chaque commit
   trivialement sûr — le harnais (Backtester→WalkForward→GridOptimizer) est câblé sur
   `SwingConfig`, donc une famille jugée par lui DOIT y vivre. Réutilisation propre du
   RSI, du filtre de régime et de `WalkForward` comme prévu.
2. *Prompts du workflow* : suffisants, AUCUN diff proposé. Note opérationnelle (hors
   prompts) : l'`AskUserQuestion` ET l'`ExitPlanMode` du harnais ont échoué de façon
   répétée cette session (permission-stream — même panne qu'au 8-nonies) → la décision
   10.4 n'a pas pu être capturée interactivement, consignée OUVERTE plutôt que tranchée
   seule (respect du garde-fou « décision produit = utilisateur »).
3. *À détecter plus tôt / garde-fou* : le plan prévoyait d'étendre `GridOptimizer` aux
   axes MR ; à l'implémentation, cela cassait `GridOptimizerUnit` (`sens.size()==8`
   verrouillé) → dévié vers un balayage `WalkForward` (le vrai juge). Leçon : un verrou
   de LOGIQUE (compte d'axes) est aussi contraignant qu'un golden — vérifier l'impact
   d'un changement de dimension AVANT de le planifier. Surtout : **D47** — un verdict OOS
   sur une stratégie qui ne trade presque pas ne juge que le cash drag (D34 sous une
   nouvelle forme) ; tout futur item MR doit d'abord garantir un échantillon de trades
   non trivial (candidat backlog : `WalkForward`/`MeanReversion` pourrait AVERTIR si le
   temps investi OOS < seuil).
4. *Notes /100* : **Architecture 89 (=)** (capacité additive : 2e famille de signal dans
   le même moteur, harnais réutilisé tel quel). **Qualité 93 (=)** (650 verts, négatif
   discipliné correctement verrouillé, goldens intacts). **FinTech 88 (=)** (connaissance
   acquise — 3e famille explorée, sans edge — mais aucune capacité de gain ajoutée).
   **Production 72 (=)**. **Rentabilité 28 (=)** : aucun edge ; 1er jet MR sans alpha OOS,
   mais la famille reste sous-explorée (D47) — l'incertitude n'a pas vraiment bougé.

### Sprint 8-nonies — Rotation multi-actifs / détention par régime (2026-07-05)

**Contexte** : changement de paradigme (décision utilisateur 2026-07-04) — les
cinq axes de la chaîne mono-actif sont soldés sans edge ; on attaque la cause
racine T4 (cash drag, long-only mono-actif) par une ROTATION : détenir l'actif
au régime le plus fort parmi QQQ/SPY/IWM/MDY, cash en régime baissier, jugé sur
données longues total-return. Décisions de conception (utilisateur, 2026-07-05) :
métrique de force = distance relative au SMA200 ((close−SMA)/SMA) ; règle de
bascule = « hold until rank-1 changes » (réévaluation quotidienne, pas de
rebalancement calendaire).

**Baseline à l'ouverture** : **623/623 verte** (conforme au tableau de bord,
`ctest -N` = 623, aucune dérive). Environnement : Linux, paquets système (chemin
CI, sans vcpkg), build −Werror sans warning.

**Commits** (ordre chronologique = ordre d'exécution) :
- `970f90d` feat : moteur de rotation multi-actifs (RotationBacktester) (item 8n.1)
- `d9ef6cd` test : tests unitaires classement/alignement/reproductibilité (item 8n.1)
- `1978b56` test : verrous OOS rotation vs meilleur B&H et panier, MC size-aware, Calmar (item 8n.2)
- `7bc7cf4` feat : section rotation dans le harnais validate (item 8n)
- (clôture) docs : mise à jour roadmap

**Tests** : 623 → **640** (+17 : 528 unitaires + 112 intégration). Nouvelle suite
`RotationBacktesterUnit` (11 : alignement sur dates communes, classement de régime
main-calculé, run complet 10000·200/143 ≈ 13986,01, reproductibilité au bit près,
coûts de bascule, cash sans trade) ; nouvelle suite `RotationOosIntegration` (6 :
axe commun figé, 3 pavages alpha vs meilleur B&H ET panier, MC size-aware DD p95,
run complet/Calmar). **Goldens backtest byte-identiques** (moteur SÉPARÉ de la
chaîne — aucun défaut, config prod ni golden existant touché).

**Interfaces ajoutées** (additives, header-only) : `include/backtest/
RotationBacktester.hpp` — `RotationConfig`, `AlignedAxis`, `RotationResult`,
fonctions pures `alignOnCommonDates`/`rankStrongest`, `RotationBacktester`
(run/runRange/axis, seam `fromAxis`), `RotationWalkForward`. Réutilise CsvDataFeed,
l'indicateur SMA, TradeRecord (deployedFraction=1.0) et MonteCarlo. Section 13 du
CLI `validate`.

**Verdict OUT-OF-SAMPLE de la rotation** (données longues, axe commun 2000-05-26 →
2026-07-01, 6562 barres, borné par IWM) :
- **AUCUN EDGE** : alpha OOS moyen vs le MEILLEUR B&H mono-actif (B&L) −11,61
  (canonique) / −10,07 (fin) / −13,33 (décalé) ; vs le PANIER équipondéré −2,73 /
  −5,90 / −7,15 — négatif vs les DEUX références sur les TROIS pavages.
- Run complet 2000-2026 : rotation +186,38 % vs meilleur B&H +1836,10 % (QQQ) et
  panier +1119,81 % → alpha −1649,72 / −933,43 ; DD plein 44,68 %, CAGR 4,11 %,
  Calmar 0,09, 472 bascules, temps investi 80,66 %.
- **Risque** (Monte-Carlo size-aware, graine 42) : DD p95 ~55 % (canonique 55,08 ;
  décalé 55,48 ; fin 70,05) — drawdown de queue MASSIF, bien pire que le passif.
- **Constat clé (D46)** : la rotation est PIRE que le panier passif — le timing de
  régime whipsaw (achète après la hausse, vend après la baisse) plus les coûts de
  472 bascules détruisent de la valeur nette. Un filtre de régime SMA200 ne suffit
  pas à surperformer une simple diversification statique sur des actifs corrélés.

**Verdict de clôture (8n.3, branche « sinon »)** : la rotation ne démontre AUCUN
alpha OOS positif → **AUCUNE adoption**, consigné. Les DEUX grands axes de
recherche d'alpha — chaîne mono-actif (Sprints 8 → 8-octies) ET rotation
multi-actifs (8-nonies) — sont désormais SOLDÉS sans edge démontré. La question
devient explicitement produit : « ce moteur peut-il battre le B&H, ou l'accepte-t-on
comme un outil de gestion du risque à rendement B&H ? » — **Décision requise**
portée au Sprint 9 (4 options au tableau de bord ; l'AskUserQuestion du harnais
étant indisponible cette session, la décision reste OUVERTE et aucun item ne
démarre sans réponse). Fichiers gouvernés intacts (`config/prod.json`,
`prompt-*.md`, CLAUDE.md live-safety), `liveTradingApproved` = false, verrou
`LiveTradingStaysDisapprovedUntilEdgeDoD` intact. Prod paper.

**Rétrospective** :
1. *Découpage* : bon. 8n.1 (moteur) → 8n.2 (verdict) → 8n.3 (décision), linéaire,
   additif, aucune dépendance ratée. Construire un moteur SÉPARÉ de la chaîne
   (plutôt qu'étendre WalkForward/SwingStrategy) a gardé les 623 goldens
   byte-identiques et rendu chaque commit trivialement sûr ; réutilisation propre de
   CsvDataFeed/SMA/TradeRecord/MonteCarlo comme prévu.
2. *Prompts du workflow* : suffisants, AUCUNE improvisation, AUCUN diff proposé.
   Note opérationnelle (hors prompts) : l'outil AskUserQuestion du harnais a échoué
   de façon répétée (permission-stream) → la décision de suite 8n.3 n'a pas pu être
   capturée interactivement ; elle est consignée comme Décision requise OUVERTE
   plutôt que tranchée seule (respect du garde-fou « décision produit = utilisateur »).
3. *À détecter plus tôt / garde-fou* : l'item 8n.2 exigeait à juste titre les DEUX
   références (meilleur B&H ET panier) — sans le panier, on aurait conclu « perd
   contre le meilleur actif » et manqué le constat plus tranchant (D46 : perd même
   contre une diversification naïve). Les verrous D34 (compte de trades) ont
   fonctionné. Candidat backlog : documenter que, pour une stratégie SANS ajustement
   (règle fixe), les fenêtres IS d'un walk-forward sont vestigiales (seul l'OOS juge).
4. *Notes /100* : **Architecture 88 → 89** (capacité réutilisable et testée :
   alignement inter-actifs par dates communes + moteur de rotation + walk-forward,
   100 % additif). Qualité 93 (=, 640 verts, négatif discipliné correctement
   verrouillé). FinTech 88 (=, connaissance acquise — T4 attaqué, la rotation
   destructrice de valeur — mais aucune capacité de gain ajoutée). Production 72 (=).
   **Rentabilité 28 (=)** : aucun edge ; le second grand axe soldé comme le premier,
   incertitude rétrécie, capacité démontrée à gagner de l'argent inchangée.

**Découvertes** : D46 (rotation par régime pire que le panier passif — timing +
coûts destructeurs ; les deux axes de recherche d'alpha soldés).

### Sprint 8-octies — Position-sizing modulé par la volatilité (2026-07-04)

**Contexte** : enchaîné directement après le Sprint 8-septies (décision
utilisateur « oui »). But : tester le vol-sizing (levier identifié par D44)
pour découpler l'alpha du risque du pullback.

**Baseline à l'ouverture** : **616/616 verte**. Environnement : Linux, paquets
système. **Découverte pré-code D45** (exploration avant écriture, leçon D41) :
le Monte-Carlo était aveugle à la taille de position → le vol-sizing y aurait
été inerte. Décision utilisateur (voie B) : corriger le harnais d'abord.

**Commits** :
- `b64ffd7` fix(backtest) : Monte-Carlo sensible à la taille (item 8o.1, D45)
- `ebbadd8` feat(risk) : position-sizing modulé par la volatilité (item 8o.2, D44)
- `9060747` test(validation) : verdict OOS du vol-sizing — découplage partiel (item 8o.3)
- (clôture) docs : mise à jour roadmap

**Tests** : 616 → **623** (+7 : 517 unitaires + 106 intégration). MonteCarloUnit
+1 (pondération deployedFraction, f=0,5 à la main — reproductibilité f=1.0
intacte) ; RiskManagerUnit +5 (vol-sizing : pleine taille sous réf, réduction
au-dessus, borne 1, fail-open, désactivé = identité) ; PullbackConfirmation +1
(`VolSizingDecouplingIsLocked`, 6 variantes figées). **Re-baseline documentée
D45** des valeurs dérivées du MC (MonteCarloIntegration 6,95/8,59 → 2,78/3,30 ;
CandidateValidation 8t.2 ; confirmation 8d.3c/8d.6/8d.7) — **goldens backtest
byte-identiques** (deployedFraction seulement enregistré, sizing inchangé).

**Interfaces modifiées** (additives) : `TradeRecord` gagne `deployedFraction`
(défaut 1.0) ; `IRiskManager`/`RiskManager` gagnent une surcharge « barres » de
`positionSize` (défaut délégant) ; `SwingConfig`/`RiskConfig` gagnent
`volSizingAtrRef` (défaut 0). `MonteCarlo::run` pondère par deployedFraction.

**Verdicts** :
- **D45 (MC size-aware)** : la chaîne ne déploie que ~40 % → l'ancien MC
  surestimait CAGR ET drawdown d'un facteur ~2,5. Conséquence rétroactive :
  le DD « disqualifiant » du pullback (19,27) était surestimé — corrigé à 7,88
  (vs chaîne 4,28, toujours ~1,8× : le verdict qualitatif tient) ; et
  riskPerTradePct n'est PLUS invariant (artefact D45 levé).
- **8o.3 (vol-sizing)** : DÉCOUPLAGE PARTIEL. pull+vol 0,015 réduit le DD
  7,88 → **6,51** (~35 % du chemin vers la chaîne) pour −0,11 pt d'alpha
  canonique (+0,84 décalé) — PREMIÈRE frontière DD/alpha favorable, mais
  n'atteint pas « DD ≤ chaîne+2 ET alpha ≥ chaîne ». Réfs hautes inertes ;
  levier SPÉCIFIQUE au pullback (inerte sur la chaîne seule).
- **8o.4** : découplage partiel + alpha absolu toujours négatif → décision
  utilisateur **consigner sans adopter**, prod paper. Suite : **Sprint
  8-nonies** (rotation multi-actifs — viser l'alpha absolu, T4).

**Découvertes** : D45 (MC aveugle à la taille — corrigé, garde-fou unitaire).

### Sprint 8-septies — Données longues & confirmation du pullback (2026-07-04)

**Contexte** : sprint RE-DÉFINI à l'ouverture (décision utilisateur 2026-07-04)
sur recommandation d'amélioration des chances de rentabilité. La « 4e famille
de signaux » (items 8z.x, jamais démarrée) part au backlog ; le sprint devient
« données longues + confirmation du seul candidat vivant » — la donnée et le
verdict sur le pullback ont une bien meilleure espérance qu'un flag de plus
(trajectoire : +0,4/+1,5 pt/famille vs déficit −7/−13 pts).

**Baseline réelle à l'ouverture** : **605/605 verte** (conforme au tableau de
bord après clôture 8-sexies). Environnement : Linux, paquets système (chemin CI,
sans vcpkg), build −Werror sans warning. Session interrompue plusieurs fois
(reprises propres : tout l'état dans les commits, aucun travail perdu).

**Décisions utilisateur** (2026-07-04) : (1) périmètre = re-prioriser ET
exécuter ; (2) historique = **max ~1999+** (dot-com 2000-2002 et 2008 inclus),
CSV 2019-2026 conservés intacts ; (3) gate 8d.5 après le verdict mixte =
**« atténuer le drawdown d'abord »** ; (4) re-gate après échec de l'atténuation
= **« creuser un 3e levier »** ; (5) re-re-gate après épuisement config-only =
**« consigner + backlog vol-sizing »**. Le protocole a transformé chaque débat
en MESURE avant de trancher (leçon 8-quinquies appliquée trois fois de suite).

**Commits** (ordre chronologique = ordre d'exécution) :
- `310331c` docs : re-priorisation — données longues + confirmation pullback avant toute nouvelle famille
- `9ab98a5` feat(data) : export total-return historique max (~1999+) + audit densité (item 8d.1)
- `1676727` test(validation) : confirmation hors-protocole du pullback — alpha confirmé, drawdown disqualifiant (items 8d.2/8d.3)
- `5c9d415` test(validation) : atténuation du drawdown — aucune variante ne réussit (item 8d.6, D44)
- `2078027` test(validation) : 3e levier de découplage — config-only épuisé (item 8d.7, D44)
- (clôture) docs : mise à jour roadmap

**Tests** : 605 → **616** (+11 : 512 unitaires + 104 intégration). Nouvelle
suite `PullbackConfirmationIntegration` (11 tests) : 2 données longues (audit
anti-D29, densité D31 soldée, B&H QQQ_max +1585,38 % figé), 2 références chaîne
v2 sur pavages longs, 4 confirmation (multi-actifs, grille resserrée,
Monte-Carlo, duels longs), 2 atténuation (8d.6 : ATR-gate/stop), 1 troisième
levier (8d.7 : risk/ATR-sweep). Nouveau livrable non testé : `scripts/
export_total_return.py` (Yahoo v8, stdlib). Section 11 du CLI `validate`.
**Goldens byte-identiques sur tout le sprint** (aucun défaut modifié).

**Verdict de confirmation du pullback (RSI ≤ 40 « s'ajoute », D42)** :
- **ALPHA CONFIRMÉ** (première fois qu'un candidat généralise hors de son
  protocole) : données longues QQQ_max (dot-com + 2008) canonique-long
  **−17,17 vs −17,30 (+0,14)**, décalé-long **−10,00 vs −11,05 (+1,05)** ;
  multi-actifs ≥ chaîne sur 2/3 (IWM +0,18, MDY +0,45 ; SPY −0,02) ; grille
  resserrée {35,40,45} argmax **stable à 40** (−6,55). La méfiance D36 est
  levée SUR L'ALPHA.
- **RISQUE DISQUALIFIANT** (Monte-Carlo, graine 42) : CAGR p50 +0,93 pt mais
  **DD p95 10,80 → 19,27 %** (quasi doublé). Le pullback achète les creux
  volatils : gain et risque couplés (D44).
- **Atténuation (8d.6/8d.7)** : gating ATR ≤ 0,015 casse le DD (**7,12**, sous
  la chaîne) mais perd l'alpha (−17,83) et est une falaise isolée (0,018/0,020
  pires → sur-ajustement) ; stop serré n'affecte pas le DD ; `riskPerTradePct`
  invariant d'échelle. **Découplage config-only ÉPUISÉ.**

**Verdict de clôture** : le pullback est le premier candidat dont l'alpha
généralise, mais son adoption est bloquée par un risque de queue que la config
ne sait pas découpler. **AUCUNE adoption** (`entryPullbackRsiMax` reste 0 ;
défauts, `config/prod.json`, goldens intacts ; prod paper). Le vrai levier
identifié — **position-sizing modulé par la volatilité** — devient le
**Sprint 8-octies**. Fichiers gouvernés intacts, `liveTradingApproved` = false,
verrou live intact.

**Découvertes** : D43 (multiple-testing → confirmation hors-protocole complète
obligatoire) ; D44 (alpha et risque du pullback couplés, config-only épuisé →
vol-sizing moteur). D42 mise à jour (alpha confirmé).

### Sprint 8-sexies — 3e famille de signaux (2026-07-03)

**Baseline réelle à l'ouverture du cycle de clôture** : **605/605 verte** —
DÉRIVE vs tableau de bord (582) : les 23 tests du sprint avaient déjà été
livrés par le squash-merge `81a18ec` (PR #20) dans une session interrompue
entre la mesure (8y.3) et le gate/clôture. Leçon D20 appliquée : dérive
détectée à l'ouverture (`ctest -N` = 605), absorbée ici, décompte recalé.
Environnement : Linux, paquets système (chemin CI, sans vcpkg) ; build
−Werror sans warning, suite 100 % verte re-vérifiée avant la clôture.

**Décisions utilisateur** (2026-07-03) : à l'ouverture (1re session) —
(1) mini-grille du filtre ATR élargie d'un cran BAS 0,010 (réduire le risque
d'inertie totale D41 si les fenêtres OOS sont calmes ; il s'est avéré actif
au point de sur-bloquer : 1 seul trade) ; (2) ATR incalculable → filtre
INOPÉRANT (fail-open : le comportement chaîne est préservé) ; (3) pullback
jugé sous les DEUX hypothèses de cohabitation avec la re-entrée 8.5
(« remplace »/« s'ajoute », comme 8s.3 — masquage PARTIEL anticipé, pas
sous-ensemble). Au gate (2e session, chiffres sur table) — (4) **8y.4 =
« CONFIRMER avant d'adopter »** : aucune adoption, le pullback devient le
premier candidat vivant, sa validation hors-protocole (D42) est PRÉREQUIS
d'adoption et reste au backlog ; (5) sprint suivant = **4e famille de
signaux** (8-septies).

**Commits** : l'intégralité du code du sprint (8y.1 + 8y.2 + 8y.3) tient
dans le squash-merge :
- `81a18ec` test(strategy) : entrée pullback en tendance — rouges (item 8y.1) (#20)
  — le titre du squash reprend le premier commit atomique de la session ;
  le contenu couvre l'implémentation des deux mécanismes (SwingStrategy :
  bloc pullback + filtre ATR fail-open + 5e indicateur injecté), les 17
  tests unitaires, les 6 verrous d'intégration, la section 10 du CLI
  `validate` et la note de câblage différé dans ConfigLoader.hpp.
- Le gate 8y.4 et cette clôture n'ajoutent AUCUN code (branche « confirmer
  avant d'adopter » : décision consignée, ROADMAP seule modifiée).

**Tests** : 582 → **605** (+23 : 512 unitaires + 93 intégration). Ajouts :
`SwingStrategyUnit` +17 — pullback +8 (seuil à la borne exacte, gate de
régime, priorité des ventes, tire SOUS les EMAs là où la re-entrée ne peut
pas, coexistence re-entrée, flag off = identité) et filtre ATR +9 (les 4
familles d'entrée bloquées, borne stricte ratio = seuil, ventes jamais
bloquées, fail-open ATR incalculable, flag off = identité) ; nouvelle suite
`PullbackVolatilityIntegration` +6 (2 mini-grilles avec mesures + argmax
figés, 4 duels sur pavages non-choisis).

**Interfaces modifiées** (additives uniquement) : `SwingConfig` gagne
`entryPullbackRsiMax` et `entryMaxAtrPct` (défauts 0, strategy-only — aucune
plomberie RiskConfig : les entrées appartiennent à la stratégie) ;
`SwingStrategy` gagne un 5e indicateur injecté (ATR, défaut `ATR(14)` vrai
true-range — sites d'appel historiques préservés). **Goldens byte-identiques
sur tout le sprint** (défauts 0 partout) ; recoupements inter-fichiers de la
chaîne dans le nouveau fichier de verdict : −9,9023/11 (canonique),
−6,8794/13 (fin), −12,8332/11 (décalé) — tous verts. Fichiers gouvernés
intacts (config/prod.json, prompt-*.md, verrou live) ; câblage ConfigLoader
des deux flags DIFFÉRÉ au gate d'adoption (règle 8q.3, documentée dans
ConfigLoader.hpp).

**Verdicts OUT-OF-SAMPLE du sprint** (tous verrouillés, configs explicites
D33, trades poolés D34, deltas figés) :
- **8y.3a (mini-grille pullback, pavage fin)** : « s'ajoute » RSI≤30
  −6,5713 (13) ; RSI≤40 **−6,5491 (13)** ; RSI≤50 −7,2903 (19) ;
  « remplace » RSI≤30 −8,0750 (6) ; RSI≤40 −8,9510 (8) ; RSI≤50 −6,9295
  (15) → argmax (RSI ≤ 40, « s'ajoute »), +0,33 pt sur SON pavage. PAS
  d'inertie (contraste 8s.3b) : mêmes comptes que la chaîne (13) mais
  alphas différents = entrées réellement déplacées au creux (D41 vérifiée
  par les deltas, pas par les comptes). « Remplace » dégrade : sans la
  re-entrée, attendre le creux fait rater le gros de la tendance.
- **8y.3b (mini-grille filtre ATR, pavage fin)** : 0,010 −8,3209 (1 trade —
  sur-bloque, cash drag pur) ; 0,015 **−6,4838 (6)** ; 0,025 −6,5651 (12 —
  quasi transparent) → argmax 0,015, +0,40 pt. Filtre ACTIF sur les trois
  crans (comptes ≠ chaîne partout — pas d'inertie à documenter).
- **8y.3c (pullback, pavages non-choisis)** : canonique **−9,4861 (12) vs
  −9,9023 (11) → +0,42 pt** ; décalé **−11,3159 (12) vs −12,8332 (11) →
  +1,52 pt** — l'acceptation « ≥ chaîne sur les deux pavages non-choisis »
  PASSE pour la PREMIÈRE fois depuis que le protocole existe (le breakout
  8s.3 et le trailing ATR 8q.2 avaient échoué à ce juge). Alpha négatif
  partout : amélioration relative, PAS d'edge absolu.
- **8y.3d (filtre ATR, pavages non-choisis)** : canonique −11,5294 (6) vs
  −9,9023 → **−1,63 pt** ; décalé −13,3341 (9) vs −12,8332 → **−0,50 pt** —
  échec sur les DEUX pavages (biais de sélection D36, même mécanisme
  d'échec que le breakout 8s.3d) → « pas d'amélioration » (résultat
  valide). Gate de la COMBINAISON fermé (le filtre échoue seul).
- **8y.4** : condition « amélioration robuste » VRAIE pour le pullback →
  décision utilisateur : **confirmer avant d'adopter** (D42) — aucune
  adoption, défauts/goldens/prod.json inchangés, prod reste paper. Suite
  décidée avec l'utilisateur : **Sprint 8-septies** (4e famille de signaux).

**Découvertes** : D42 (premier candidat vivant — la confirmation
hors-protocole devient le prérequis standard de toute adoption).

### Sprint 8-quinquies — Autres familles de signaux (2026-07-03)

**Baseline réelle à l'ouverture** : **559/559 verte**, conforme au tableau de bord
(`ctest -N` = 559, aucune dérive hors cycle). Environnement : Linux, paquets
système (chemin CI, sans vcpkg).

**Décisions utilisateur d'ouverture** (2026-07-03) : (1) périmètre = sprint
complet + clôture ; (2) 8s.2 jugé sous les DEUX hypothèses de cohabitation
avec la re-entrée 8.5 — « remplace » (regimeReentry=false) et « s'ajoute » —
car « s'ajoute » était suspecté quasi-inerte (sous-ensemble de la re-entrée) ;
(3) sortie structurelle gatée par minHoldDays, priorité SL > TP > structure >
trailing ; (4) mini-grilles telles que proposées (N ∈ {10, 20, 55},
M ∈ {20, 55} — re-dérivation vérifiée : pas de piège D34/D35, N et M ne
consomment que N+1/M+1 barres de la fenêtre ~230). Décision de gate (8s.4) :
branche « sinon » mécanique, suite = **Sprint 8-sexies** (3e famille).

**Commits** (ordre chronologique = ordre d'exécution) :
- `f70394c` test(risk) : sorties structurelles « plus bas de N jours » calculées à la main — rouges (item 8s.1)
- `294ed6c` feat(risk) : sortie structurelle sous le plus bas des N barres précédentes (item 8s.1)
- `c067a33` feat(bot) : plomberie exitOnLowestLowN — SwingConfig/RiskConfig et passage au moteur de sortie (item 8s.1)
- `8900bf1` test(bot) : verrou de conversion SwingConfig→RiskConfig d'exitOnLowestLowN (complément 8s.1)
- `d167eea` test(strategy) : entrée breakout « plus haut de M jours » — rouges (item 8s.2)
- `4edd528` feat(strategy) : entrée breakout au-dessus du plus haut des M barres précédentes (item 8s.2)
- `81710f8` test(validation) : verdicts OOS verrouillés des familles de signaux — mini-grilles fines + duels canonique/décalé (item 8s.3)
- `fe1836e` feat(validate) : section 9 FAMILLES DE SIGNAUX dans le harnais (item 8s.3)

**Tests** : 559 → **582** (+23 : 495 unitaires + 87 intégration). Ajouts :
`RiskManagerUnit` +9 (seuil structure aux deux bornes de fenêtre — barre
ancienne hors fenêtre ET barre courante exclue dans le même cas de test —,
N=0 = identité stricte, N+1 barres requises, gating minHold, priorités sur le
trailing % ET le trailing ATR) ; `SwingStrategyUnit` +8 (+1 assertion de
conversion) sur indicateurs injectés (borne stricte du breakout, gate de
régime, priorité des ventes, coexistence re-entrée) ; nouvelle suite
`SignalFamiliesIntegration` +6 (2 mini-grilles avec mesures + argmax figés,
4 duels sur pavages non-choisis). Discipline rouge→vert sur les tests de
comportement (5 rouges structure, 2 rouges breakout) ; sentinelles → mesure →
figer sur les 6 verrous d'intégration.

**Interfaces modifiées** (additives uniquement) : `IRiskManager::checkExitConditions`
gagne une 3e surcharge « + exitOnLowestLowN » à défaut rétro-compatible
(patron 8q.1) ; `SwingConfig` gagne `exitOnLowestLowN` et `entryBreakoutM`
(défauts 0), `RiskConfig` gagne `exitOnLowestLowN`. **Goldens byte-identiques
sur tout le sprint** (défauts 0 partout) ; recoupements inter-fichiers de la
chaîne dans le nouveau fichier de verdict : −9,9023/11 (canonique),
−6,8794/13 (fin), −12,8332/11 (décalé) — tous verts. Fichiers gouvernés
intacts (config/prod.json, prompt-*.md, verrou live) ; câblage ConfigLoader
des deux flags DIFFÉRÉ au gate d'adoption (règle 8q.3, documentée dans
ConfigLoader.hpp).

**Verdicts OUT-OF-SAMPLE du sprint** (tous verrouillés, configs explicites
D33, trades poolés D34, deltas figés) :
- **8s.3a (mini-grille structure, pavage fin)** : N=10 −7,0435 (15 trades) ;
  N=20 **−6,8794 (13)** ; N=55 −6,8794 (13) — N=20/55 INERTES (identiques à
  la chaîne), argmax dégénéré → N=20 (premier des ex æquo, figé).
- **8s.3b (mini-grille breakout, pavage fin)** : « s'ajoute » M=20/55
  −6,8794 (13) — EXACTEMENT inertes ; « remplace » M=20 **−6,6850 (9)**,
  M=55 −6,7147 (9) → argmax (M=20, remplace), +0,19 pt sur SON pavage.
- **8s.3c (structure, non-choisis)** : canonique −9,9023 (11) = chaîne ;
  décalé −12,8332 (11) = chaîne — inertie totale, « ≥ chaîne » par égalité,
  rien à adopter.
- **8s.3d (breakout, non-choisis)** : canonique **−11,7526 (9) vs −9,9023
  (11) → −1,85 pt** ; décalé **−14,2640 (10) vs −12,8332 (11) → −1,43 pt**
  — l'acceptation échoue sur les DEUX pavages, biais de sélection pur.
- **8s.4** : condition d'adoption FAUSSE (0/2) → « aucune amélioration »,
  branche « sinon » : consigner sans adopter, prod paper. Suite décidée avec
  l'utilisateur : **Sprint 8-sexies** (3e famille de signaux — pullback en
  tendance, filtre de volatilité).

**Découvertes** : D41 (mécanisme masqué par un mécanisme existant — vérifier
l'ACTIVATION avant d'interpréter un verdict ; argmax d'ex æquo figé).

### Sprint 8-quater — Trailing adaptatif ATR (2026-07-03)

**Baseline réelle à l'ouverture** : **545/545 verte**, conforme au tableau de bord
(`ctest -N` = 545, aucune dérive hors cycle). Environnement : Linux, paquets
système (chemin CI, sans vcpkg).

**Décisions utilisateur d'ouverture** (2026-07-03) : (1) périmètre = sprint
complet + clôture ; (2) convention : quand les deux trailing sont configurés,
**l'ATR REMPLACE le %** (pas de cumul — A/B propre), avec repli DÉFENSIF sur le
% si l'ATR est incalculable (fenêtre < 15 barres ou ATR nul) ; (3) câblage
ConfigLoader/config/prod.json **DIFFÉRÉ au gate d'adoption 8q.3** — le fichier
gouverné ne bouge pas avant un verdict (exception documentée dans
ConfigLoader.hpp). Décision de gate (8q.3) : **consigner sans adopter**.

**Commits** (ordre chronologique = ordre d'exécution) :
- `1701c5c` test(risk) : sorties trailing ATR calculées à la main — rouges (item 8q.1)
- `5071b7b` feat(risk) : trailing ATR(14) remplace le trailing % quand trailingAtrMult > 0 (item 8q.1)
- `0fb6dc5` feat(bot) : plomberie trailingAtrMult — SwingConfig/RiskConfig et passage des barres au moteur de sortie (item 8q.1)
- `b84bfaf` test(validation) : verdicts OOS verrouillés du trailing ATR — mini-grille fine + duels canonique/décalé (item 8q.2)
- `223714d` feat(validate) : section 8 TRAILING ATR dans le harnais (item 8q.2)

**Tests** : 545 → **559** (+14 : 478 unitaires + 81 intégration). Ajouts :
`RiskManagerUnit` +10 (seuil ATR à la main 110 − 3×2 = 104, lissage de Wilder
30/14 encadré strictement, « remplace le % », replis fenêtre courte/ATR nul,
gating minHoldDays, priorités SL/TP sur l'ATR, garde peakPrice, identité de
délégation mult=0) ; nouvelle suite `TrailingAtrIntegration` +4 (sélection de
mini-grille figée avec garde anti-dérive d'argmax, duel fin, verdicts
canonique/décalé). Discipline rouge→vert sur les 4 tests de comportement ;
sentinelles → mesure → figer sur les 4 verrous d'intégration.

**Interfaces modifiées** (additives uniquement) : `IRiskManager::checkExitConditions`
gagne une surcharge « fenêtre de barres + trailingAtrMult » à défaut
rétro-compatible (délègue à la surcharge historique — patron `computeBars`/8.0 :
les ~15 appels de tests existants compilent inchangés) ; `SwingConfig`/`RiskConfig`
gagnent `trailingAtrMult` (défaut 0). **Goldens byte-identiques sur tout le
sprint** (défaut 0 partout) ; recoupements inter-fichiers de la chaîne après la
plomberie : −9,9023/11 (canonique), −6,8794/13 (fin), −12,8332/11 (décalé) —
tous verts, le chemin historique n'a pas bougé. Fichiers gouvernés intacts
(config/prod.json, prompt-*.md, verrou live).

**Verdicts OUT-OF-SAMPLE du sprint** (tous verrouillés, configs explicites D33,
trades poolés D34 — chaîne v2 trail % 0,03 vs chaîne + ATR) :
- **8q.2a (mini-grille, pavage fin)** : mult=3 retenu (−6,55) contre mult=2
  (−7,01) et mult=4 (−6,70) — sélection PLATE (écart 0,47 pt), argmax figé.
- **8q.2b (fin, pavage CHOISI — informatif)** : ATR −6,55 (10 trades) vs chaîne
  −6,88 (13), +0,33 pt.
- **8q.2c (canonique, non-choisi)** : ATR **−8,08** (7 trades) vs chaîne −9,90
  (11) → **+1,82 pt**.
- **8q.2d (décalé, non-choisi)** : ATR **−13,08** (10 trades) vs chaîne −12,83
  (11) → **−0,24 pt**.
- **8q.3** : l'acceptation « alpha OOS ≥ chaîne sur les DEUX pavages
  non-choisis » est NON satisfaite → **« pas d'amélioration robuste »**,
  branche « sinon » : décision utilisateur = consigner sans adopter, prod
  paper. Suite décidée avec l'utilisateur : **Sprint 8-quinquies** (autres
  familles de signaux).

**Découvertes** : D40 (l'axe trailing soldé sans gagnant robuste ; sélection de
mini-grille plate → figer les 3 mesures ET l'argmax, pas l'argmax seul).

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

### Sprint 8-octies — Position-sizing modulé par la volatilité (2026-07-04)

**1. Découpage** : excellent, et le point saillant du sprint s'est joué AVANT
tout code. L'exploration du chemin de sizing (leçon D41 : vérifier l'ACTIVATION
d'un mécanisme) a révélé que le Monte-Carlo était aveugle à la taille — donc
que le sprint tel qu'écrit dans la ROADMAP (« cible = DD p95 réduit ») aurait
mesuré « aucun changement » et conclu à tort. Cette découverte a été portée à
l'utilisateur comme un FORK (corriger le harnais / cible backtest / pivoter),
transformant un piège silencieux en décision éclairée. Le découpage qui en a
résulté — 8o.1 harnais, 8o.2 mécanisme, 8o.3 verdict, 8o.4 gate — est le plus
propre du cycle : chaque commit atomique, rouge→vert, goldens prouvés intacts.

**2. Suffisance des prompts** : suffisants. La procédure a absorbé un correctif
de harnais (re-baseline d'un outil validé) sans improviser : re-baseline
DOCUMENTÉE, goldens backtest prouvés byte-identiques, chaque valeur MC re-mesurée
avec un commentaire D45. Le garde-fou « re-baseline documentée » de la DoD a
exactement servi. **Aucune modification des prompts nécessaire.**

**3. À détecter plus tôt / garde-fous** : (a) **D45 aurait dû être vu au Sprint
7.3** (création du Monte-Carlo) : bootstrapper `pnlPct` sur l'équité pleine
suppose 100 % de déploiement, hypothèse jamais explicitée. Elle a faussé TOUS
les DD du MC pendant 8 sprints (heureusement des comparaisons relatives, donc
les verdicts qualitatifs tiennent). Garde-fou adopté : MonteCarloUnit teste
désormais la pondération par la taille. Leçon générale : un outil de mesure
mérite le même « test d'activation » qu'un mécanisme de trading (que
mesure-t-il RÉELLEMENT ?). (b) L'invariance de riskPerTradePct (8d.7) était le
SYMPTÔME de D45 — on l'a consignée comme « propriété d'échelle » sans creuser ;
la creuser une barre plus tôt aurait trouvé D45 au sprint précédent. Signal
retenu : une invariance PARFAITE (byte-identique) est suspecte, elle cache
souvent un chemin mort.

**4. Notes /100** (précédent 88/93/87/72, Rentabilité 28) :
- **Architecture 88** (=) : extensions strictement additives (surcharge
  positionSize à défaut délégant, champ de config, champ TradeRecord) ; le
  RiskManager consomme l'ATR concret, frontière d'injection respectée.
- **Qualité 93** (=) : +7 tests, un correctif de harnais avec re-baseline
  DOCUMENTÉE et goldens backtest prouvés intacts, un garde-fou unitaire nouveau
  (pondération taille). Discipline sans faille — mais D45 rappelle qu'un outil
  de mesure non testé sur son hypothèse centrale est une dette silencieuse.
- **FinTech 88** (+1) : le harnais de risque est désormais CORRECT (le MC
  reflète la taille) — un gain durable qui fiabilise tout jugement de
  risque futur, et le moteur sait moduler la taille par la volatilité. +1
  seulement : l'edge n'a pas progressé.
- **Production 72** (=) : rien de prod ce sprint (voulu) ; prod.json intact.
- **Rentabilité 28** (=) : le vol-sizing est le premier levier à améliorer la
  frontière DD/alpha du pullback (progrès réel de CONNAISSANCE), mais le
  découplage est partiel, le critère non atteint, et l'alpha absolu reste
  négatif. Cinq axes de la chaîne mono-actif sont soldés ; la capacité
  démontrée à gagner de l'argent n'a pas bougé. Le prochain sprint (rotation)
  est le premier à viser l'alpha ABSOLU — c'est là que la note pourra enfin
  franchir 50 si un edge apparaît.

### Sprint 8-septies — Données longues & confirmation du pullback (2026-07-04)

**1. Découpage** : le sprint a été RE-DÉFINI à chaud (recommandation
rentabilité) puis a grandi de 5 à 7 items sous l'effet de deux gates successifs
(8d.6 atténuation, 8d.7 3e levier) — croissance MAÎTRISÉE : chaque extension
répondait à une décision utilisateur explicite prise AVEC les chiffres, et
chacune était config-only (aucun code moteur, réutilisation des champs
SwingConfig existants et des helpers 8d.6 en 8d.7). L'ordre avait une logique
de coût : la donnée d'abord (8d.1, elle bénéficie à tout), puis le verdict, puis
seulement l'atténuation quand le verdict l'a exigée. Le point le plus rentable :
avoir livré les **données longues** — la première mesure de la chaîne sur
dot-com/2008 (−17,30 vs −9,90) est en soi un acquis durable, indépendant du
pullback. Seule friction : le sprint a débordé son plan initial de deux items,
mais c'est le protocole « mesurer avant de trancher » qui l'a voulu, pas une
dérive.

**2. Suffisance des prompts** : suffisants. Le cycle exécuter→gate→re-gate a été
entièrement porté par `AskUserQuestion` + la discipline de verrouillage, sans
improviser de workflow. Cinq décisions utilisateur, toutes posées AVEC les
chiffres sur la table. **Aucune modification des prompts nécessaire** — et la
re-définition d'un sprint courant en cours de route (garde-fou : « décision
utilisateur explicite ») a fonctionné sans amender `prompt-executer-sprint.md`.

**3. À détecter plus tôt / garde-fous** : (a) le critère de confirmation de 8d.3
listait « Monte-Carlo non dégradé » comme une case parmi d'autres — or c'est
elle, SEULE, qui a disqualifié un candidat par ailleurs excellent sur l'alpha.
Leçon : un critère multi-volets doit pondérer le RISQUE au moins autant que
l'alpha (la DoD risque/rendement le disait déjà, mais le protocole de verdict ne
l'incarnait pas). (b) `riskPerTradePct` invariant d'échelle sur le drawdown en %
était PRÉVISIBLE par le raisonnement (le sizing scale l'équité, le DD% est un
ratio) — mais on l'a MESURÉ plutôt que supposé (discipline 8-quinquies), et bien
en a pris car le résultat exact (19,2742 au bit près sur les deux crans) est une
preuve, pas une intuition. (c) le gate ATR 0,015 « falaise isolée » (voisins
0,018/0,020 pires) est un cas d'école de D36 attrapé par le sweep fin : figer
UNIQUEMENT un argmax sans tester ses voisins immédiats aurait laissé croire à un
plateau. Garde-fou déjà en place (verrous D39), re-confirmé utile.

**4. Notes /100** (précédent 88/93/85/72, Rentabilité 28) :
- **Architecture 88** (=) : aucune modification moteur ce sprint (tout config-
  only + un script Python autonome) ; les frontières n'ont pas bougé. Le
  Sprint 8-octies touchera enfin le RiskManager (sizing).
- **Qualité 93** (=) : +11 tests avec la discipline habituelle (sentinelles →
  mesure → figer, valeurs Monte-Carlo au bit près, garde de densité qui solde
  D31) ; goldens byte-identiques ; nouveau garde-fou données (`auditTotalReturnCsv`
  étendu aux 4 fichiers longs). Rien de structurellement neuf dans l'outillage.
- **FinTech 85** (+2 → **87**) : le harnais a rendu son verdict le plus RICHE à
  ce jour — un candidat qui PASSE l'alpha (données longues + multi-actifs +
  grille) et ÉCHOUE le risque (Monte-Carlo), avec la cause (D44) et le prochain
  levier (vol-sizing) identifiés. La capacité à juger honnêtement s'est étoffée
  d'un cran (données incluant deux vrais marchés baissiers) — c'est du solide
  qui servira tous les verdicts futurs.
- **Production 72** (=) : rien de prod ce sprint (voulu) ; prod.json intact.
- **Rentabilité 28** (=) : le pullback confirme son alpha (bonne nouvelle
  durable, D42 levée sur l'alpha) mais reste inadoptable (risque, D44) et l'alpha
  absolu reste négatif partout — la capacité DÉMONTRÉE à gagner de l'argent n'a
  pas bougé. La note ne montera qu'avec un mécanisme qui réduit le risque SANS
  tuer l'alpha (l'espoir du Sprint 8-octies) ou un changement de paradigme
  (rotation). Le sprint a néanmoins RÉTRÉCI l'incertitude : on sait maintenant
  que le pullback a un vrai alpha et où est le verrou.

### Sprint 8-sexies — 3e famille de signaux (2026-07-03)

**1. Découpage** : bon — même gabarit que 8-quinquies (deux mécanismes
indépendants, une mesure, un gate), aucune dépendance ratée, et le gabarit a
tourné une 4e fois sans amendement. Particularité de CE cycle : la session
qui a livré 8y.1–8y.3 a été interrompue AVANT le gate et la clôture, et le
squash-merge (PR #20) a collapsé ses commits atomiques en un seul `81a18ec` ;
le cycle suivant a re-vérifié la baseline (605/605), posé le gate avec les
chiffres et clos. RIEN n'a été perdu — la preuve (vécue une 2e fois, après
8-quinquies) que « tout l'état dans les commits et les verrous » rend le
workflow résilient aux pannes de session. Deux frictions réelles : (a) le
tableau de bord a menti (582 vs 605) le temps d'un inter-cycle — la dérive
D20 a été détectée par le recalage d'ouverture, exactement comme le
garde-fou le prévoit ; (b) le squash-merge fait perdre la granularité
rouge→vert de l'HISTORIQUE (la discipline reste prouvée par les verrous et
le changelog, mais plus par le log git).

**2. Suffisance des prompts** : suffisants — la procédure (baseline → items
→ gate → clôture) a couvert sans improvisation un sprint à cheval sur deux
sessions. Cinq décisions utilisateur posées au bon moment : trois à
l'ouverture par la 1re session (cran ATR bas 0,010, fail-open, variantes
A/B du pullback) et deux au gate par ce cycle (« confirmer avant
d'adopter », sprint suivant = 4e famille), posées AVEC les chiffres.
**Aucune modification des prompts nécessaire.**

**3. À détecter plus tôt / garde-fous** : (a) le juge binaire « ≥ chaîne sur
les deux pavages non-choisis » vient de rendre son premier verdict POSITIF —
et c'est là qu'on voit qu'il ne dit rien de l'AMPLEUR ni du niveau absolu :
+0,42/+1,52 pt sur des alphas de −9,9/−12,8 est une amélioration relative
mince. Le garde-fou D42 (confirmation hors-protocole PRÉREQUIS d'adoption)
comble exactement ce trou — érigé en règle standard pour tout futur
candidat. (b) l'anticipation de masquage D41 écrite dès la rédaction de
l'item 8y.1 (« la re-entrée couvre prix > EMAs, le pullback tire SOUS les
EMAs ») s'est VÉRIFIÉE à la mesure (mêmes comptes, alphas différents) :
anticiper l'interaction dans l'item même fonctionne, à conserver. (c) les
deltas figés (consigne 8-quater) ont encore payé : sans eux, « mêmes 13
trades que la chaîne » aurait pu passer pour de l'inertie — ce sont les
alphas différents qui prouvent l'activation.

**4. Notes /100** (précédent 88/93/85/72, Rentabilité 25) :
- **Architecture 88** (=) : extensions strictement additives (2 flags
  strategy-only — les entrées restent à la stratégie, aucune plomberie
  RiskConfig —, 5e indicateur injecté à défaut rétro-compatible) ; aucune
  frontière déplacée. Toujours plafonnée par 9.2 (lookback unifié).
- **Qualité 93** (=) : +23 tests avec la même discipline (bornes exactes
  des deux côtés, fail-open verrouillé, argmax anti-dérive, activation
  documentée dans les verrous mêmes) ; goldens byte-identiques ; rien de
  structurellement nouveau dans l'outillage qualité.
- **FinTech 85** (=) : le protocole a rendu son premier verdict POSITIF
  sans amendement — preuve qu'il discrimine dans les DEUX sens (il n'était
  pas structurellement « toujours non »), précieuse pour la crédibilité de
  tous les verdicts négatifs passés. Pas de mécanisme de jugement NOUVEAU
  pour autant (la confirmation D42 réutilise l'outillage 8-ter existant).
- **Production 72** (=) : rien de prod ce sprint (voulu) ; prod.json intact
  par construction (câblage différé au gate d'adoption, règle 8q.3).
- **Rentabilité 28** (+3) : premier mécanisme à PASSER l'acceptation
  robuste du protocole (pullback : +0,33/+0,42/+1,52 pt, signe stable sur
  les trois pavages) — un candidat VIVANT après trois familles soldées,
  l'espace de recherche redevient productif. +3 seulement : l'alpha OOS
  reste négatif partout (aucun edge absolu), et D36 rappelle qu'un
  survivant de première validation peut être réfuté à la confirmation
  (8b.1 l'a été). La note ne franchira 50 qu'avec la confirmation
  hors-protocole (D42) réussie ET un candidat confirmé.

### Sprint 8-quinquies — Autres familles de signaux (2026-07-03)

**1. Découpage** : bon — deux mécanismes indépendants (8s.1/8s.2, chacun
rouge→vert en 2-4 commits atomiques), une mesure (8s.3), un gate mécanique
(8s.4) ; aucune dépendance ratée. Le point le plus rentable du sprint s'est
joué À L'OUVERTURE : l'analyse « le breakout “s'ajoute” est un sous-ensemble
de la re-entrée 8.5 » a été posée à l'utilisateur AVANT d'écrire une ligne, et
la décision (juger les deux variantes) a transformé un débat en MESURE — le
delta exactement nul de « s'ajoute » est la preuve, pas l'opinion. Le
protocole 3-pavages a tourné une 3e fois sans amendement ; la session a même
été interrompue entre le gate et la clôture SANS rien perdre : tout l'état
était dans les commits et les verrous — la discipline « committer l'item
seul » paie aussi contre les pannes d'environnement.

**2. Suffisance des prompts** : suffisants, aucune improvisation de workflow.
Quatre décisions produit posées à l'ouverture (périmètre ; variantes A/B du
breakout ; gating/priorité de la sortie structurelle ; mini-grilles avec
re-dérivation vérifiée — leçon 8-ter appliquée : les constantes d'un item se
re-dérivent, ne se copient pas) et la décision de clôture posée avec les
chiffres. **Aucune modification des prompts nécessaire.**

**3. À détecter plus tôt / garde-fous** : (a) **D41** — l'inertie de la sortie
structurelle (N ≥ 20 ne tire jamais avant le trailing 3 %) n'a été vue qu'à la
mesure ; elle était pourtant DÉTECTABLE par raisonnement (sur QQQ haussier, un
retracement de 20 jours sous le plus bas dépasse presque toujours 3 % depuis
le pic — le trailing tire d'abord). Garde-fou adopté : anticiper les
interactions de MASQUAGE d'un nouveau mécanisme dès la rédaction de l'item
(fait pour 8y.1/8y.2), et comparer chaque ligne de mini-grille à la chaîne
(mêmes alphas + mêmes trades = inertie, à documenter dans le verrou même).
(b) Un argmax de mini-grille peut être un EX ÆQUO dégénéré (N=20/55 à
−6,8794) : figer « premier des ex æquo » explicitement, sinon le verrou est
fragile à tout re-figeage. (c) Les verrous D34 (comptes de trades) ont encore
payé : ce sont eux qui distinguent « inerte » (mêmes trades) de « équivalent »
(trades différents, même alpha) — sans eux, l'inertie serait passée pour une
égalité de performance.

**4. Notes /100** (précédent 88/93/85/72, Rentabilité 25) :
- **Architecture 88** (=) : deux extensions strictement additives (3e
  surcharge à défaut rétro-compatible, flag de stratégie), frontières
  interfaces-only respectées. La pile de surcharges de `checkExitConditions`
  (8/10/11 args) commence à compter — un regroupement en struct de paramètres
  est envisageable mais n'est PAS urgent ; toujours plafonnée par 9.2.
- **Qualité 93** (=) : +23 tests avec la même discipline (bornes de fenêtre
  verrouillées des deux côtés dans un même cas, identités de délégation,
  verrous d'inertie documentés), goldens byte-identiques à chaque commit —
  mais rien de structurellement NOUVEAU dans l'outillage qualité ce sprint.
- **FinTech 85** (=) : le protocole de jugement est rodé (3e exécution sans
  amendement) et la connaissance négative s'accumule proprement (D41), mais
  la capacité à juger n'a pas gagné de mécanisme nouveau et l'edge n'a pas
  progressé.
- **Production 72** (=) : rien de prod ce sprint (voulu) ; prod.json intact
  par construction (câblage différé au gate).
- **Rentabilité 25** (=) : troisième axe soldé (signaux structure/breakout
  après paramètres et trailing) — connaissance négative utile qui RÉTRÉCIT
  honnêtement l'espace de recherche, mais la capacité démontrée à gagner de
  l'argent n'a pas bougé. La chaîne v2 reste seule en lice (−6,88/−9,90/
  −12,83 selon le pavage).

### Sprint 8-quater — Trailing adaptatif ATR (2026-07-03)

**1. Découpage** : bon — moteur (8q.1, 3 commits atomiques rouge→vert), mesure
(8q.2, verrous + CLI), gate (8q.3, mécanique) ; aucune dépendance ratée. Le seul
point technique non trivial — comment amener les BARRES jusqu'au RiskManager
sans casser l'interface ni les ~15 appels de tests existants — a été identifié
À LA PLANIFICATION et résolu par le patron déjà éprouvé du dépôt (surcharge
additive à défaut rétro-compatible, comme `computeBars` à l'item 8.0). Le
protocole de jugement (choisir sur UN pavage, verrouiller sur les non-choisis)
a été réutilisé du 8-ter TEL QUEL : deuxième exécution, zéro amendement — il
est rodé et c'est désormais le standard de tout item « nouveau mécanisme ».

**2. Suffisance des prompts** : suffisants, aucune improvisation de workflow.
Trois décisions produit posées à l'utilisateur À L'OUVERTURE (périmètre ;
convention « l'ATR remplace le % » — l'option proposée par la ROADMAP,
confirmée ; report du câblage prod.json au gate d'adoption — le fichier
gouverné n'est pas touché par un axe de recherche) et le gate 8q.3 posé AVEC
les chiffres. **Aucune modification des prompts nécessaire.**

**3. À détecter plus tôt / garde-fous** : (a) l'acceptation binaire « ≥ chaîne
sur les deux pavages non-choisis » est un bon garde-fou anti-adoption mais
ÉCRASE l'information (+1,82 et −0,24 pèsent pareil) : consigne adoptée — les
verrous et le changelog figent aussi les DELTAS chiffrés, pour que la nuance
(« non robuste » ≠ « réfuté ») survive à la clôture (fait ici, D40). (b) Une
sélection de mini-grille peut être PLATE (0,47 pt entre 3 mults) : figer
l'argmax seul serait fragile au moindre re-figeage — le verrou 8q.2a fige les
TROIS mesures ET l'argmax (garde anti-dérive), motif à réutiliser. (c) Le repli
défensif « jamais de position sans filet » (ATR incalculable → trailing %)
n'existait dans aucune consigne : décidé à l'ouverture et verrouillé par 2
tests — à ériger en règle pour tout futur mécanisme de sortie optionnel.

**4. Notes /100** (précédent 88/92/84/72, Rentabilité 25) :
- **Architecture 88** (=) : une extension strictement additive (surcharge à
  défaut rétro-compatible, plomberie de config) ; TradingBot reste
  interfaces-only, RiskManager consomme l'indicateur concret — la frontière
  d'injection est respectée. Toujours plafonnée par 9.2 (lookback unifié).
- **Qualité 93** (+1) : +14 tests, dont un verrou de sélection anti-dérive
  (3 mesures + argmax) et des recoupements inter-fichiers systématiques sur les
  3 pavages ; goldens prouvés byte-identiques à CHAQUE commit ; bornes de tests
  en doubles exactement représentables (104 = 110 − 3×2) et encadrement strict
  du lissage de Wilder — zéro flakiness ajoutée.
- **FinTech 85** (+1) : le moteur de sortie sait s'adapter à la volatilité
  (trailing ATR A/B-able, repli « jamais sans filet ») et le protocole de
  validation hors-choix a tourné une 2e fois sans amendement — la capacité à
  juger honnêtement un mécanisme est acquise. +1 seulement : l'edge n'a pas
  progressé.
- **Production 72** (=) : rien de prod ce sprint (voulu) — et c'est un choix
  EXPLICITE cette fois : prod.json laissé intact par décision d'ouverture.
- **Rentabilité 25** (=) : « pas d'amélioration robuste » — l'axe désigné deux
  fois par la mesure (trailing) est soldé sans gagnant. Connaissance négative
  utile (D40 : la fragilité venait de l'axe, pas de sa paramétrisation), mais
  la capacité démontrée à gagner de l'argent n'a pas bougé.

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
