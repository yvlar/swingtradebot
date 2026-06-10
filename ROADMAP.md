# ROADMAP — SwingBot C++

> **Source de vérité du workflow.** Ce fichier est lu par `prompt-executer-sprint.md`
> (exécution du sprint courant) et mis à jour via `prompt-mise-a-jour-roadmap.md`
> (clôture de sprint, re-priorisation, rétrospective). Ne pas le modifier à la main
> en dehors de ce cycle, sauf pour ajouter une découverte.

## Tableau de bord

| Dimension    | Note /100 | Baseline (audit 2026-06-10) |
|--------------|-----------|------------------------------|
| Architecture | 71        | 68                           |
| Qualité      | 70        | 60                           |
| FinTech      | 58        | 38                           |
| Production   | 42        | 35                           |

- **Dernière mise à jour** : 2026-06-10 (clôture Sprint 1)
- **Sprint courant** : Sprint 2 — Fiabilité / concurrence
- **État des tests** : 155/155 verts (110 baseline + 45 ajoutés au Sprint 1)
- **Environnement de référence** : conteneur vcpkg (`dev.ps1`) ; build aussi possible
  sur Linux avec paquets système depuis le fallback SQLite3 (voir Découverte D11)

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

## 🟠 SPRINT 2 — Fiabilité / concurrence (sprint courant)

- [ ] **6.** Data race UB : `Watchdog::last_heartbeat_` non atomique, écrit thread
  principal (watchdog.h:68), lu thread watchdog (watchdog.h:107). + `build_alert_msg_`
  lit `BotState` sans lock (watchdog.h:134-137). + alertes SMTP/SMS/webhook **sans
  `CURLOPT_TIMEOUT`** (watchdog.h:149-256) : le watchdog peut se geler lui-même.
  **Acceptation** : `last_heartbeat_` atomique (epoch ns ou time_point atomique) ;
  lecture de BotState sous `state_.mtx` ; CURLOPT_TIMEOUT sur les 3 canaux d'alerte ;
  tests Watchdog existants verts (ils couvrent déjà heartbeat/silence).
- [ ] **7.** `curl_global_init/cleanup` dans les ctors/dtors de 4 classes
  (IBKRDataFeed.hpp:50, AlpacaBroker.hpp:35, AlpacaDataFeed.hpp:41, watchdog.h:58)
  → init une seule fois (RAII au main). Aggravant : main_ibkr.cpp:70-82 détruit un
  `IBKRDataFeed` temporaire en début de programme.
  **Acceptation** : un seul `curl_global_init` par processus (guard RAII, ex.
  `CurlGlobal` dans core/), plus aucun appel dans les ctors/dtors des 4 classes.
- [ ] **8.** Client HTTP dupliqué 4× (`request()`/`writeCallback`) → extraire un
  `HttpClient` commun ; vérifier les codes HTTP ; retry + backoff exponentiel ; gérer
  le 429. **Contrainte** : préserver le seam de test de IBKRBroker (request() virtuel
  introduit au Sprint 1) — l'injection du HttpClient peut le remplacer proprement.
- [ ] **9.** Codes retour `sqlite3_prepare_v2`/`sqlite3_step` ignorés partout
  (db_logger.h) — aggravé par D2 : `stmt` non initialisé → UB si prepare échoue.
  **Acceptation** : chaque prepare/step vérifié (modèle : core/state_store.h fait déjà
  les vérifications) ; test « DB en lecture seule / requête invalide ne crashe pas ».
- [ ] **10.** `std::optional`/vecteur vide = à la fois « erreur réseau » et « pas de
  donnée » dans `IDataFeed`/`IBroker` → introduire un canal d'erreur (`Result<T>` maison
  ou `tl::expected`) pour distinguer panne et état vide. **Priorité montée d'un cran
  depuis le Sprint 1** : la réconciliation de l'item 1 réinitialise l'état quand
  `getPosition()`→nullopt — auto-réparant mais bruyant si le nullopt vient d'une panne
  réseau ; un canal d'erreur permettra de NE PAS réconcilier sur erreur.
- [ ] **D4.** `Session::send` (ws_server.cpp:54-62) lance `async_write` sans file
  d'attente → deux broadcasts rapprochés = écritures concurrentes sur le même stream
  WebSocket (UB Beast). Ajouter une queue d'écriture par session.

## 🟡 SPRINT 3 — Architecture

- [ ] **11.** Le backtest ne teste pas le code de prod : `Backtester::run()` réimplémente
  sorties + sizing inline (BackTester.hpp:60-195 ; le `RiskManager rm` ligne 78 n'est
  jamais utilisé) → faire tourner le backtest sur `TradingBot::runOnce` + `PaperBroker` +
  `RiskManager` réels, supprimer la logique dupliquée (+ D6 : stratégie recréée à chaque
  barre).
- [ ] **12.** Découpler `TradingBot` de `SwingConfig` (TradingBot.hpp:3 inclut la
  stratégie concrète juste pour sa config) → extraire un `RiskConfig` injecté dans le
  bot ; les paramètres stratégie restent dans la stratégie.
- [ ] **13.** `DayTradeStrategy.hpp:4` inclut `indicators/DayIndicators.hpp` **qui
  n'existe pas** (ATR, VWAP, VolumeOscillator manquants — le fichier ne compile que parce
  que rien ne l'inclut) → créer ces indicateurs + tests, OU supprimer le fichier.
  **Décision requise de l'utilisateur — à poser avant le Sprint 3.**
- [ ] **14.** Hygiène : `main.cpp:1` inclut `"backtest/Backtester.hpp"` vs fichier réel
  `BackTester.hpp` (casse Linux) ; `gitignore` sans point initial → rien d'ignoré,
  `cmake-build-debug/` et `.idea/` commités ; double `#pragma once` dans
  AlpacaBroker.hpp:1,13 ; `QQQv1.csv` non référencé (D12).

## 🟢 SPRINT 4 — Tests du moteur

- [ ] **15.** Compléter `test_trading_bot_unit.cpp` (démarré au Sprint 1) : matrice
  complète runOnce × {achat, vente, rejet, feed vide, marché fermé, désync, restart}.
- [ ] **16.** Compléter `test_risk_manager_unit.cpp` (démarré au Sprint 1 : sizing) :
  priorités de sortie, bornes. + `test_indicators_unit.cpp` : EMA (seed SMA),
  RSI ∈ [0,100], cas plat 0/0→50, CrossoverDetector + warmup.
- [ ] **17.** Test de non-régression du backtest : valeurs golden figées sur `QQQ.csv`
  (total return, nb trades, max DD) pour détecter toute dérive future. **À faire avant
  le Sprint 3 item 11** (le refactor du backtester doit être validé par le golden —
  attention : les fixes Sprint 1 items 3/5 changent légitimement le comportement, donc
  figer les valeurs APRÈS Sprint 1).

## 🔵 SPRINT 5 — Durcissement production

- [ ] **18.** Kill-switch dans `IRiskManager` : drawdown journalier max, pertes
  consécutives max, plafond d'ordres/jour.
- [ ] **19.** Stops côté broker (ordre stop résident) en complément du stop logiciel
  (réduit aussi le risque de double-vente sur ordre PENDING, cf. note item 2).
- [ ] **20.** Calendrier de marché : `isUsMarketHours` est en UTC-5 fixe
  (IBKRDataFeed.hpp:211), faux 8 mois/an (EDT) ; horodatages unifiés en UTC.
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

## Rétrospectives

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
