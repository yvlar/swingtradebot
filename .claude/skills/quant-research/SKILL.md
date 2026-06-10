---
name: quant-research
description: >
  Expertise VectorBT/backtest pour le banc de prototypage research/ de SwingBot.
  À utiliser dès qu'il s'agit de : prototyper ou comparer des stratégies (Sprint 8),
  backtester des signaux, optimiser des paramètres (heatmaps), walk-forward,
  tests de robustesse (Monte-Carlo, bruit, sensibilité), VectorBT, vectorbt,
  QuantStats, tearsheet, position sizing, equity curve, drawdown. Ne s'applique
  PAS au moteur C++ de production (lui se modifie via /sprint et se juge avec le
  harnais WalkForward.hpp).
user-invocable: false
---

# Skill quant-research — prototypage de stratégies sur le banc `research/`

Adapté (sous licence MIT) de [marketcalls/vectorbt-backtesting-skills](https://github.com/marketcalls/vectorbt-backtesting-skills),
re-câblé pour ce dépôt : données locales, coûts du moteur C++, verdict OOS obligatoire.

## Architecture à deux étages (ne jamais l'inverser)

1. **Prototyper ici** (`research/`, Python) : cycle de quelques secondes, balayages
   vectorisés VectorBT, idées jetables.
2. **Porter le gagnant en C++** (`include/strategies/`) et le re-juger avec le
   harnais `WalkForward.hpp` + golden. Le C++ reste LA source de vérité — aucun
   chiffre Python ne « valide » une stratégie pour la prod.

## Conventions de CE dépôt (surchargent l'upstream)

- **Données** : UNIQUEMENT locales — `swingbench.load_bars("../QQQ.csv")` ou
  `vbt_adapter.load_close()`. **Jamais de fetch réseau** (OpenAlgo/yfinance/CCXT
  de l'upstream : inapplicables ici, réseau données verrouillé — D31).
- **Coûts** : commission 0,1 % + slippage 5 bps par côté (`vbt_adapter.FEES`),
  alignés sur le moteur C++ (items 6.1/6.4). Pas de frais indiens/crypto.
- **Benchmark** : Buy & Hold du MÊME actif (`vbt_adapter.buy_hold`), jamais un
  indice externe. Toujours afficher le tableau Stratégie vs B&H (`compare`).
- **Indicateurs** : ceux de `swingbench` (EMA/RSI/SMA seedés SMA, miroir du C++)
  pour tout ce qui doit être porté en C++ ensuite ; TA-Lib n'est PAS installé.
  Les built-ins VectorBT sont tolérés pour des explorations jetables uniquement.
- **Verdict** : TOUJOURS en walk-forward IS/OOS (`vbt_adapter.oos_verdict`,
  `swingbench.anchored_split`/`rolling`) — un chiffre full-period seul ne conclut rien.
  Indicateurs recalculés PAR TRANCHE (pas de fuite IS→OOS).
- **Langue** : commentaires et sorties en français.

## Pièges spécifiques (appris sur ce dépôt)

- **Biais D28 asymétrique** : QQQ.csv n'est pas total-return → le B&H est
  SOUS-estimé. Un verdict « la stratégie perd » est conservateur ; un verdict
  « la stratégie gagne » doit survivre à ~+0,55 %/an de B&H en plus.
- **Exposition** : le banc et VectorBT tournent « tout investi » ; le moteur C++
  size à 2 % de risque. Comparer des verdicts relatifs (alpha, bat/ne bat pas),
  jamais les retours absolus au golden C++.
- **Empilement de filtres** (D32) : ajouter un filtre de régime par-dessus
  l'entrée actuelle sur-filtre (3 % investi). Penser remplacement, pas addition.

## Référence VectorBT (règles vendorisées, EN, MIT)

Lire le fichier pertinent avant d'écrire du code VectorBT non trivial :

| Fichier | Sujet |
|---|---|
| [simulation-modes](rules/simulation-modes.md) | from_signals / from_orders / from_holding, directions |
| [parameter-optimization](rules/parameter-optimization.md) | balayages broadcastés, heatmaps |
| [walk-forward](rules/walk-forward.md) | walk-forward, ratio WFE |
| [robustness-testing](rules/robustness-testing.md) | Monte-Carlo, bruit, sensibilité, délai |
| [performance-analysis](rules/performance-analysis.md) | stats, métriques, CAGR, comparaison |
| [position-sizing](rules/position-sizing.md) | Amount/Value/Percent/TargetPercent |
| [stop-loss-take-profit](rules/stop-loss-take-profit.md) | sl_stop, tp_stop, ts_stop natifs |
| [indicators-signals](rules/indicators-signals.md) | génération de signaux (réf. TA-Lib — non installé ici) |
| [long-short-trading](rules/long-short-trading.md) | long/short simultanés |
| [plotting](rules/plotting.md) | equity, underwater, Plotly |
| [quantstats-tearsheet](rules/quantstats-tearsheet.md) | rapports HTML QuantStats (optionnel) |
| [strategy-catalog](rules/strategy-catalog.md) | catalogue de stratégies avec snippets |
| [us-market-costs](rules/us-market-costs.md) | modèle de coûts US (réf.) |
| [csv-data-resampling](rules/csv-data-resampling.md) | chargement CSV, resampling |
| [pitfalls](rules/pitfalls.md) | erreurs classiques + checklist avant prod |

## Gabarit minimal (la voie normale)

```python
import vbt_adapter as va

close = va.load_close()                      # ../QQQ.csv, mêmes barres que le C++
verdict = va.oos_verdict(
    lambda c: va.ema_cross_signals(c, 13, 21), close)   # recalcul par tranche
# stops natifs vectorbt si besoin : va.portfolio(..., sl_stop=0.07, ts_stop=0.03)
```
