# RUNBOOK — SwingBot en production

> Procédures opérateur. À relire AVANT tout démarrage réel. La référence
> technique est `ROADMAP.md` (verdicts, découvertes) et `CLAUDE.md` (règles).

## 1. Démarrage

1. **CP Gateway IBKR** : lancer `clientportal.gw/bin/run.sh conf.yaml`,
   ouvrir `https://localhost:5000`, s'authentifier avec le compte IBKR.
   La session expire (~24 h) : le bot le détecte à chaque cycle (log
   `Gateway non authentifié` + alerte watchdog après 65 min de silence) —
   la ré-authentification reste MANUELLE (navigateur).
2. **Secrets d'alerte** : `cp .env.example .env`, remplir au moins UN canal
   (`SWINGBOT_WEBHOOK_URL` est le plus simple). Sans canal, `--live` refuse.
3. **Lancer** : `docker compose up -d --build` (paper) ou `./build/swing_bot`.
   Vérifier au démarrage : `Gateway authentifié`, `Compte IBKR actif`,
   `data/swingbot_ibkr.db ouvert`, dashboard `ws://localhost:9001`.

## 2. Arrêt propre

- `docker compose stop` (ou Ctrl+C / SIGTERM) : le bot s'arrête à la fin du
  cycle courant. **Il ne ferme PAS la position ouverte** — c'est voulu :
  le **stop résident GTC reste armé chez IBKR** et protège la position
  pendant l'arrêt. L'état (`inPosition`, `stopArmed`, cooldown) est
  persisté dans `data/swingbot_ibkr_state.db` et réconcilié au redémarrage.
- Après un arrêt prolongé EN POSITION : surveiller la position dans le
  portail IBKR — seul le stop GTC la protège (ni trailing ni take-profit
  logiciels pendant l'arrêt).

## 3. Mise à jour du bot (position potentiellement ouverte)

1. Préférer une fenêtre HORS séance (16h00-9h30 ET) ou position à plat.
2. Vérifier `stopArmed=true` (dashboard ou
   `sqlite3 data/swingbot_ibkr_state.db 'SELECT * FROM bot_state;'`).
3. `docker compose up -d --build` : pendant le rebuild, seul le stop GTC
   protège. Au redémarrage, le bot ré-adopte la position broker, restaure
   holdDays/peak, re-découvre l'orderId du stop (D38) et reprend.
4. Vérifier le premier cycle dans les logs (`docker logs swing_bot`).

## 4. Incidents

| Symptôme | Diagnostic | Action |
|---|---|---|
| Alerte watchdog « bot silencieux » | Panne feed/broker OU Gateway dé-authentifié (le heartbeat n'est battu que sur cycle SAIN) | Lire les logs ; si `Gateway non authentifié` → ré-auth navigateur sur `https://localhost:5000` ; sinon vérifier le Gateway/réseau |
| Alerte « KILL-SWITCH déclenché » | Un garde-fou de risque a bloqué les entrées (drawdown journalier, pertes consécutives, plafond d'ordres) | AUCUNE action automatique requise (les positions ouvertes gardent leurs stops). Analyser la cause avant la prochaine séance ; ne PAS relancer pour « débloquer » |
| Conteneur `unhealthy` / redémarre en boucle | Process gelé (healthcheck TCP 9001 échoue) | `docker logs swing_bot` ; après redémarrage le bot ré-adopte la position (réconciliation) |
| DB corrompue (`data/*.db`) | Erreurs SQLite dans les logs | Arrêter le bot ; sauvegarder le répertoire `data/` ; supprimer la DB corrompue ; au redémarrage, la position broker fait foi (réconciliation) — l'historique logs/trades de la DB est perdu, pas l'état de trading |
| Stop orphelin chez IBKR (position fermée mais ordre STP encore visible) | `cancelStopLoss` a échoué | Portail IBKR → Orders → annuler l'ordre STP `swingbot-…-STOP-…` manuellement |
| Position ouverte hors bot / vente manuelle | Le bot détecte la disparition au cycle suivant : reset + cooldown de ré-entrée du jour | Rien — vérifier le log `Position absente chez le broker` |

## 5. Kill-switch MANUEL (arrêt d'urgence)

Dans l'ordre, sans sauter d'étape :

1. `docker compose stop swing_bot` (ou SIGTERM) — plus aucun ordre ne part.
2. Portail IBKR → **Orders** : annuler tout ordre résiduel `swingbot-*`
   (dont le stop GTC si vous fermez la position à la main).
3. Décider du sort de la position : la clôturer manuellement dans le
   portail, OU la laisser courir protégée par le stop GTC (dans ce cas NE
   PAS annuler le stop).
4. Consigner l'incident (date, cause, décision) avant tout redémarrage.

## 6. Politique de risque (chiffrée)

Source de vérité : `config/prod.json` (validé au démarrage, backtesté par
le golden — toute modification = re-baseline documenté, voir CLAUDE.md).

| Garde-fou | Valeur | Mécanisme |
|---|---|---|
| Risque par trade | 2 % du capital (`riskPerTradePct`) | sizing `positionSize` |
| Stop-loss | −5 % (`stopLossPct`) | stop logiciel + stop GTC résident chez IBKR |
| Trailing stop | −3 % depuis le pic (`trailingStopPct`) | logiciel |
| Perte journalière max | −5 % d'équité (`killSwitch.maxDailyDrawdownPct`) | plus AUCUNE entrée + alerte |
| Pertes consécutives max | 4 (`killSwitch.maxConsecutiveLosses`) | plus aucune entrée + alerte |
| Ordres par jour max | 10 (`killSwitch.maxOrdersPerDay`) | plus aucune entrée + alerte |
| Ré-entrée après sortie | interdite le même jour de bourse (cooldown) | TradingBot |

Non couvert automatiquement (surveillance humaine) : perte MENSUELLE max,
exposition multi-comptes, risque de gap overnight au-delà du stop.

## 7. Checklist PRÉ-LIVE (obligatoire, à signer)

Le mode `--live` refuse de démarrer tant que tout n'est pas vrai :

- [ ] **DoD d'edge atteinte et verrouillée par test** (Sprint 8-ter conclu
      positivement — voir ROADMAP ; aujourd'hui : NON atteinte).
- [ ] Suite de tests complète verte (Debug, Release, ASan) sur le commit déployé.
- [ ] Au moins un canal d'alerte **testé réellement** (envoyer une alerte
      de test : couper le heartbeat en paper et vérifier la réception).
- [ ] Paper trading stable ≥ 2 semaines sur ce même commit (pas de panne
      silencieuse, réconciliations correctes, stops armés).
- [ ] Politique de risque (§6) relue et acceptée ; taille de compte décidée.
- [ ] `liveTradingApproved` passé à `true` dans `config/prod.json` **et**
      le test `LiveTradingStaysDisapprovedUntilEdgeDoD` re-figé dans le
      MÊME commit, avec la décision consignée au changelog ROADMAP.
- [ ] Démarrage en terminal interactif, confirmation « OUI » tapée.
- [ ] Plan de surveillance : qui reçoit les alertes, qui peut exécuter le
      kill-switch manuel (§5), sous quel délai.

Signature (date + décideur) : ______________________
