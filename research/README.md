# research/ — banc de prototypage de stratégies (Sprint 8)

> **Le moteur de production reste le C++** (`../include`, testé, CI). Ce dossier est un
> banc d'essai Python pour **itérer vite** sur les idées de stratégie : on prototype ici
> (cycle de quelques secondes), on ne porte dans le C++ que la stratégie qui **bat le
> Buy & Hold en out-of-sample**. Aucun fichier ici n'est compilé par CMake ni lancé par
> la CI.

## Pourquoi un banc Python

Itérer une stratégie en C++ coûte un cycle compile/test ; en Python c'est instantané.
Le banc rejoue la **même donnée** (`../QQQ.csv`) avec les **mêmes métriques** que le
harnais C++ (`WalkForward.hpp`) : retour, alpha vs Buy & Hold, % de temps investi,
walk-forward IS/OOS. C'est l'application directe de la reco « prototyper en Python,
porter le gagnant en C++ » (skills quant de skills.sh, ex. vectorbt-backtesting-skills).

## Installation

```bash
cd research
pip install -r requirements.txt          # numpy + pandas suffisent
```

`vectorbt`/`quantstats` sont des accélérateurs **optionnels** (voir `requirements.txt`).
Ils ciblent des versions anciennes de pandas/numpy → à mettre dans un venv dédié.

## Utilisation

```bash
python3 run_baseline.py        # configs actuelles (défaut + prod) + leur verdict WF
python3 prototype_regime.py    # teste les leviers du Sprint 8 et compare l'OOS
```

## Fichiers

| Fichier | Rôle |
|---|---|
| `swingbench.py` | bibliothèque : indicateurs (EMA/RSI/SMA Wilder, miroir du C++), `run_backtest`, `anchored_split`, `rolling`, `Params` (avec les leviers du Sprint 8) |
| `run_baseline.py` | référence : rejoue config défaut + prod et leur walk-forward |
| `prototype_regime.py` | teste les items 8.1 (régime SMA200), 8.2 (laisser courir), 8.4 (pas de sortie RSI) et compare leur verdict OOS |

## ⚠️ Deux mises en garde (à garder en tête en lisant les chiffres)

1. **Modèle « tout investi quand en position »** : le banc mesure le **timing/structure**
   à pleine exposition. Le moteur C++, lui, *size* à 2 % de risque → exposition bien plus
   faible. C'est pourquoi la config prod fait **+207 % ici** mais **+36 % en C++** : une
   grosse part de la sous-performance C++ vient du **sizing ultra-conservateur**, levier
   distinct du timing. Ne pas comparer les retours absolus banc ↔ C++ ; comparer les
   **verdicts relatifs** (alpha vs B&H, bat/ne bat pas, en OOS).
2. **D28** : `QQQ.csv` n'est pas total-return (Close == Adj Close) → l'écart au B&H est
   sous-estimé. Tout verdict « la stratégie perd » est donc **conservateur**.

## Premiers résultats (read initial, à confirmer)

`prototype_regime.py` sur la période OOS (split 70/30) :

| Levier | OOS alpha vs B&H | Investi | Lecture |
|---|---|---|---|
| prod (référence) | −13,5 % | 24 % | perd |
| 8.1 régime SMA200 **seul** | **−40,8 %** | **3,3 %** | ❌ empilé sur l'entrée existante, sur-filtre → quasi jamais investi |
| 8.2 laisser courir (no TP) | −16,8 % | 31 % | mitigé (plus investi, 3/4 segments battent B&H) |
| 8.4 pas de sortie RSI | **−8,7 %** | 26 % | ✅ meilleur levier seul (confirme T2 : ne pas sortir des tendances) |

**Conclusion pour le Sprint 8** : ajouter un filtre de régime *par-dessus* le déclencheur
d'entrée actuel (croisement EMA + RSI<65) est contre-productif — ça compose le
sur-filtrage. Le régime doit **remplacer** la logique d'entrée (item 8.3), pas s'y
ajouter. Le levier le plus net isolément est **8.4** (ne pas vendre sur RSI). Ce banc
a produit ce diagnostic en quelques secondes — c'est exactement son rôle.
