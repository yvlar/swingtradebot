# SwingBot — bot de swing trading QQQ (C++17)

Bot de swing trading sur QQQ (croisement EMA 9/21 + RSI + filtre de régime
SMA200), broker IBKR (Client Portal Gateway) ou Alpaca, backtester CSV
anti-look-ahead, persistance SQLite, flux WebSocket pour dashboard.

> ## ⚠️ Argent réel — lire avant tout
>
> **La DoD de rentabilité n'est PAS atteinte : la prod reste en paper.**
> La recherche d'edge est **close** : cinq familles de signaux (six
> variantes) ont été jugées hors-échantillon sans edge net de coûts —
> conclusion documentée dans `documentation/CONCLUSION_RECHERCHE_EDGE.md`,
> verdicts détaillés dans `ROADMAP.md`.
> Le mode `--live` est verrouillé par un gate mécanique en
> quatre couches (`config/prod.json` → `liveTradingApproved`, canal
> d'alerte configuré, terminal interactif, confirmation « OUI » tapée) et
> par un test d'intégration qui interdit `liveTradingApproved=true` tant
> que la DoD n'est pas verrouillée. La procédure complète de passage en
> réel est dans **`RUNBOOK.md`** (checklist pré-live signée).

## Démarrage rapide (paper trading)

```bash
# Dépendances (Ubuntu 24.04 — même chemin que la CI)
sudo apt-get install -y libboost-dev libboost-system-dev \
  nlohmann-json3-dev libcurl4-openssl-dev libsqlite3-dev \
  libgtest-dev googletest cmake ninja-build

cmake -B build -DCMAKE_BUILD_TYPE=Release -G Ninja
cmake --build build -j$(nproc)
cd build && ctest -j4          # la suite complète doit être verte

# 1. Démarrer le CP Gateway IBKR puis s'authentifier sur https://localhost:5000
# 2. Lancer le bot (paper par défaut)
./build/swing_bot
```

Ou via Docker (image Release testée au build, healthcheck, logs avec
rotation) :

```bash
cp .env.example .env    # remplir les canaux d'alerte SWINGBOT_* (jamais commité)
docker compose up -d --build
```

## Configuration

- **`config/prod.json`** — source unique de la config (stratégie + seuils
  kill-switch + `liveTradingApproved`). Chargée et VALIDÉE au démarrage
  (échec bruyant) ; **le même fichier est backtesté par les tests golden**
  — toute modification casse le golden tant qu'elle n'est pas re-validée
  et documentée (voir `CLAUDE.md`, règles de gouvernance).
- **`.env`** — secrets d'alerte (`SWINGBOT_WEBHOOK_URL`, `SWINGBOT_TWILIO_*`,
  `SWINGBOT_SMTP_*`). Modèle : `.env.example`. Jamais commité.

## Documents

| Document | Rôle |
|---|---|
| `RUNBOOK.md` | Opérations : démarrage/arrêt, incidents, kill-switch manuel, checklist pré-live, politique de risque |
| `ROADMAP.md` | Source de vérité du workflow : sprints, découvertes, changelog, verdicts d'edge |
| `documentation/CONCLUSION_RECHERCHE_EDGE.md` | Conclusion de la recherche d'edge : verdict des 5 familles, méthodologie, leçons, critères de réouverture |
| `CLAUDE.md`  | Règles pour l'agent de code (conventions, gotchas, règles de sécurité live) |
| `documentation/` | UML à jour ; les .pdf/.docx sont **obsolètes** (voir documentation/README.md) |

## Tests

716 tests GTest (575 unitaires + 141 intégration — décompte tenu à jour
dans `ROADMAP.md`, « État des tests »), un processus par test, goldens
de non-régression sur le backtest. CI : build Debug + Release, ASan/UBSan,
TSan ciblé. `ctest --test-dir build -j4`.
