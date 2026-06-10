# ROADMAP — SwingBot C++

> **Source de vérité du workflow.** Ce fichier est lu par `prompt-executer-sprint.md`
> (exécution du sprint courant) et mis à jour via `prompt-mise-a-jour-roadmap.md`
> (clôture de sprint, re-priorisation, rétrospective). Ne pas le modifier à la main
> en dehors de ce cycle, sauf pour ajouter une découverte.

## Tableau de bord

| Dimension    | Note /100 | Baseline (audit 2026-06-10) |
|--------------|-----------|------------------------------|
| Architecture | 68        | 68                           |
| Qualité      | 60        | 60                           |
| FinTech      | 38        | 38                           |
| Production   | 35        | 35                           |

- **Dernière mise à jour** : 2026-06-10 (initialisation du workflow)
- **Sprint courant** : Sprint 1 — Sécurité financière
- **État des tests** : 110/110 verts (baseline avant Sprint 1)
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

## 🔴 SPRINT 1 — Sécurité financière (sprint courant)

Bugs disqualifiants pour l'argent réel. Chaque item : test rouge → fix → test vert → commit.

- [ ] **1. Position orpheline au redémarrage** — `trading::BotState` (TradingBot.hpp:14)
  vit en mémoire seule ; après restart avec position ouverte, `inPosition=false` et la
  branche de sortie (TradingBot.hpp:68) ne s'exécute plus jamais → position **sans
  stop-loss pour toujours** (le bot est aussi bloqué : `isTradeAllowed` refuse toute
  entrée tant que la position broker existe).
  **Fix** : interface `IStateStore` + implémentation SQLite + réconciliation avec
  `broker->getPosition()` à chaque cycle (couvre démarrage ET désynchronisation en cours
  de route, voir D3).
  **Acceptation** : un TradingBot neuf, store vide, broker en position → adopte la
  position (stop-loss actif) ; store pré-rempli → état restauré ; position disparue
  côté broker → état réinitialisé et persisté.
- [ ] **2. Statut d'ordre jamais vérifié** — `order.has_value()` traité comme un fill
  (TradingBot.hpp:84 vente, :108 achat). Un ordre REJECTED/PENDING met quand même le bot
  en position, au prix du signal et non du fill (P&L de vente aussi calculé au prix du
  signal, voir D5).
  **Fix** : seul `OrderStatus::FILLED` change l'état ; prix/quantité réels d'exécution
  utilisés ; PENDING → réconciliation au cycle suivant (dépend de l'item 1) ;
  vente rejetée → position conservée + log d'erreur.
  **Acceptation** : tests TradingBotUnit achat rejeté / achat pending / vente rejetée /
  fill à prix différent du signal.
- [ ] **3. Sémantique `holdDays` cassée** — incrément par cycle (TradingBot.hpp:69)
  alors que `main_ibkr.cpp:184` boucle toutes les 60 min → ~7 « jours » par jour de
  bourse ; `minHoldDays` et trailing stop faussés.
  **Fix** : compter par changement de date de barre (`lastBarDate` dans l'état persisté).
  **Acceptation** : deux `runOnce()` sur la même date de barre → holdDays inchangé ;
  nouvelle date → +1.
- [ ] **4. Risque de double-ordre IBKR** — après confirmation des `messageIds`, l'ordre
  complet est re-posté (IBKRBroker.hpp:143-149) au lieu de lire la réponse de
  `/iserver/reply` → deux ordres réels possibles.
  **Fix** : boucle de confirmation qui consomme la réponse du reply (sans re-POST) +
  `cOID` idempotent (granularité symbole+side+heure, adaptée au cycle 60 min).
  **Acceptation** : test avec HTTP simulé (seam `request()` virtuel) — scénario
  « confirmation demandée » ne génère qu'UN SEUL POST sur `/orders` ; payload contient
  `cOID`.
- [ ] **5. Sizing forcé à 1** — `return std::max(1, shares)` (RiskManager.hpp:38) achète
  1 action même quand le calcul dit 0 (cash insuffisant).
  **Fix** : retourner 0 ; `isTradeAllowed` étendu (prix+quantité) vérifie
  `cash ≥ coût total` ; TradingBot ne soumet pas d'ordre si qty ≤ 0.
  **Acceptation** : capital 100 $, prix 420 $ → `positionSize()==0` et aucun
  `submitBuy` émis.

## 🟠 SPRINT 2 — Fiabilité / concurrence

- [ ] **6.** Data race UB : `Watchdog::last_heartbeat_` non atomique, écrit thread
  principal (watchdog.h:68), lu thread watchdog (watchdog.h:107). + `build_alert_msg_`
  lit `BotState` sans lock (watchdog.h:134-137). + alertes SMTP/SMS/webhook **sans
  `CURLOPT_TIMEOUT`** (watchdog.h:149-256) : le watchdog peut se geler lui-même.
- [ ] **7.** `curl_global_init/cleanup` dans les ctors/dtors de 4 classes
  (IBKRDataFeed.hpp:50, AlpacaBroker.hpp:35, AlpacaDataFeed.hpp:41, watchdog.h:58)
  → init une seule fois (RAII au main). Aggravant : main_ibkr.cpp:70-82 détruit un
  `IBKRDataFeed` temporaire en début de programme.
- [ ] **8.** Client HTTP dupliqué 4× (`request()`/`writeCallback`) → extraire un
  `HttpClient` commun ; vérifier les codes HTTP ; retry + backoff exponentiel ; gérer le 429.
- [ ] **9.** Codes retour `sqlite3_prepare_v2`/`sqlite3_step` ignorés partout
  (db_logger.h) — aggravé par D2 : `stmt` non initialisé → UB si prepare échoue.
- [ ] **10.** `std::optional`/vecteur vide = à la fois « erreur réseau » et « pas de
  donnée » dans `IDataFeed`/`IBroker` → introduire un canal d'erreur (`Result<T>` maison
  ou `tl::expected`) pour distinguer panne et état vide (rend la réconciliation de
  l'item 1 plus sûre).
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

## Changelog

### Sprint 0 — Initialisation du workflow (2026-06-10)
- Audit Phase 0 : 21/21 constats confirmés, 12 découvertes (D1-D12), inventaire de couverture.
- `build:` fallback SQLite3 système dans CMakeLists.txt (D11).
- `chore:` création de ROADMAP.md, prompt-executer-sprint.md, prompt-mise-a-jour-roadmap.md.
- Baseline tests : 110/110 verts.

*(Un bloc par sprint : commits, tests ajoutés, métriques.)*

## Rétrospectives

*(Une entrée par sprint, écrite via `prompt-mise-a-jour-roadmap.md` : découpage, suffisance
des prompts du workflow, garde-fous manquants, notes /100 justifiées.)*
