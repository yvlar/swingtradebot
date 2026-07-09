# Conclusion de la recherche d'edge — SwingBot (2026-07-07)

> Document de clôture du Sprint 16 (décision utilisateur (d), consignée au
> changelog de `ROADMAP.md`). Il synthétise en un seul endroit ce que la
> ROADMAP porte en fragments : le verdict, la méthodologie qui le rend
> solide, les leçons, et ce qui rouvrirait la recherche. Les chiffres cités
> sont figés par des tests d'intégration golden — toute dérive casserait la
> suite.

## 1. Verdict exécutif

**Aucune stratégie technique simple sur ETFs US n'a d'edge out-of-sample net
de coûts.** Cinq familles de signaux (six variantes) ont été jugées par le
même protocole discipliné ; toutes sont soldées sans edge. C'est de la
**connaissance négative solide** — pas un abandon faute d'essai, mais un
résultat mesuré, reproductible et verrouillé par test.

Conséquences opérationnelles :

- La production **reste en paper trading**. `liveTradingApproved` = `false`.
- Le gate live à quatre couches (`config/prod.json`, canal d'alerte, TTY,
  confirmation « OUI ») reste verrouillé, et le test
  `LiveTradingStaysDisapprovedUntilEdgeDoD` interdit mécaniquement de
  l'ouvrir tant que la DoD d'edge n'est pas atteinte.
- La recherche d'edge est **close** : tout nouveau candidat repasse par le
  protocole complet (§ 5), il n'y a plus de « prochain raffinement » planifié.

Ce verdict est cohérent avec l'état de l'art : sur un sous-jacent
structurellement haussier (QQQ), battre le buy & hold net de coûts avec des
règles techniques simples sur données journalières est précisément ce que la
littérature ne parvient pas non plus à démontrer hors-échantillon.

## 2. Pourquoi ce verdict est fiable — la méthodologie

Chaque hypothèse a été jugée par le même harnais offline (`./build/validate`,
verrouillé par les tests d'intégration), durci itérativement à mesure que des
biais étaient découverts :

| Garde-fou | Ce qu'il empêche | Référence |
|---|---|---|
| Le backtest pilote le **vrai moteur de prod** (`TradingBot::runOnce` + `PaperBroker`) | Divergence backtest/prod | ROADMAP item 11 |
| Fills **anti-look-ahead** : décision au close de la barre i, exécution à l'open de i+1 | Look-ahead structurel (toutes les mesures pré-correction étaient flattées) | D37, verrou `FillsAtNextBarOpenNotAtDecisionClose` |
| Données **total-return réelles** (dividendes inclus, `Adj Close ≠ Close`), garde `auditTotalReturnCsv` | Alpha surestimé (~+0,6 pt/an de dividendes QQQ ignorés) | D29/D31 |
| Données **longues** `*_max.csv` (~1999+, dot-com et 2008 inclus) et **multi-actifs** (QQQ/SPY/IWM/MDY) | Verdict prisonnier d'un seul régime haussier | Sprint 8-septies |
| Coûts : commission + **slippage dégradé** | Edge marginal positif brut mais négatif net | D22 |
| Walk-forward IS/OOS sur **trois pavages** (canonique / fin / décalé) ; acceptation exigée sur les DEUX pavages non-choisis | Sélection des fenêtres qui arrangent (le candidat 8b.1 inversait son signe d'un pavage à l'autre) | Sprint 8-ter |
| Verrous de config **champ par champ**, jamais depuis les défauts | Déplacement silencieux de la mesure historique quand un défaut change | D33 |
| Garde sur le **nombre de trades OOS** poolés | Verdict qui ne juge que le cash drag (0 ou 1 trade OOS) | D34/D47 |
| **Grille resserrée** autour du gagnant | Plateau « stable » qui n'est stable qu'à la maille où on l'a mesuré | D36/D39 |
| **Monte-Carlo size-aware** (bootstrap de `deployedFraction × pnlPct`, graine fixe) | MC aveugle à la taille de position (rendait tout schéma de sizing inerte) | D45 |
| **Confirmation hors-protocole** obligatoire avant adoption (données longues + multi-actifs + grille resserrée + MC) | Multiple-testing : le « meilleur de N » jugé sur ses propres fenêtres est un artefact probable | D43 |

Tous les verdicts chiffrés ci-dessous sont **figés par des tests
d'intégration golden** (suites citées) : ils se relisent, se re-exécutent et
ne peuvent pas dériver silencieusement.

## 3. Les cinq familles et leurs verdicts

### Famille 1 — Trend-following mono-actif (la chaîne de prod : EMA 9/21 + RSI + régime SMA200)

Cinq axes explorés sur la chaîne, Sprints 8 à 8-octies. Verrous :
`StrategyV2Integration`, `CandidateValidationIntegration`,
`MonteCarloIntegration`, `TrailingAtrIntegration`, `SignalFamiliesIntegration`,
`PullbackVolatilityIntegration`, `PullbackConfirmationIntegration`.

| Axe | Verdict | Découverte |
|---|---|---|
| Paramètres (grille 8b.1 : emaFast=9, smaT=250, trail=0,03) | **Réfuté hors-grille** : −19,10 pt vs −9,90 pour la chaîne sur le pavage canonique, signe inversé sur le décalé ; MC CAGR p50 4,41 % < 6,60 % ; la grille resserrée fait dériver le plateau (275/0,04) | D36/D39 |
| Trailing adaptatif ATR(14) | **Non robuste** : +1,82 pt canonique mais −0,24 sur le décalé | D40 |
| Sortie structurelle, entrée breakout | **Inertes** : masqués par les mécanismes existants, deltas exactement nuls | D41 |
| Entrée pullback (RSI ≤ 40, « s'ajoute ») | **Alpha relatif confirmé** (seul mécanisme à passer les 2 pavages non-choisis, généralise aux données longues et à 2/3 actifs) **mais couplé au risque** : DD de queue doublé (p95 10,80 → 19,27 %), aucun levier config-only ne les découple → **non adopté** | D42/D44 |
| Vol-sizing (taille modulée par la volatilité) | **Découplage partiel** (DD 7,88 → 6,51 à faible coût d'alpha) mais insuffisant, et l'alpha absolu reste négatif → non adopté | D45 |

**Bilan famille 1 : l'alpha vs buy & hold est négatif sur tous les pavages,
toutes les variantes.** Le pullback est la seule vraie connaissance positive
(un alpha relatif qui généralise) — inadoptable en l'état car son alpha et
son drawdown viennent des mêmes trades.

### Famille 2 — Rotation multi-actifs par régime (Sprint 8-nonies)

Détenir l'actif au régime SMA200 le plus fort parmi QQQ/SPY/IWM/MDY
(2000-2026, axe aligné 6562 barres). **Détruit de la valeur** : +186 % vs
+1836 % pour le meilleur B&H et +1120 % pour le panier équipondéré ; alpha
OOS négatif vs les DEUX références sur les 3 pavages ; 472 bascules coûteuses ;
DD de queue MC ~55 %. Le timing de régime whipsaw — achète après la hausse,
vend après la baisse. (D46, verrous `RotationOosIntegration`.)

### Famille 3 — Mean-reversion / contrarian (Sprints 10-11)

Le 1er jet (RSI ≤ 30 + régime SMA200) ne tradait quasi pas (1 trade OOS,
1,45 % du temps investi) — famille non jugée (D47). La variante
z-score/Bollinger TRADE réellement (3-4 trades OOS, jusqu'à 23 sans filtre de
régime) et confirme : **aucun edge** — alpha OOS négatif sur les 2 pavages,
les 3 actifs et tout le balayage (meilleur −9,15). Constat clé : retirer le
filtre de régime fait trader plus (4 → 23) mais dégrade l'alpha (−9,87 →
−15,68) — acheter les couteaux qui tombent perd, cohérent avec un QQQ
structurellement haussier. (D48, verrous `MeanReversionOosIntegration`.)

### Famille 4 — Pairs-trading market-neutral (Sprint 12)

La seule famille orthogonale (bêta ~0), jugée au Sharpe. Spread
log(P0)−log(P1) β=1, z-score sur fenêtre courte. TRADE massivement (209-212
allers-retours OOS) mais **Sharpe OOS négatif partout** : les 3 pavages
(−1,18/−1,20/−1,16), les 4 paires, tous les réglages (meilleur −0,44) ; MC
CAGR médian −3,62 %, DD p95 68,1 %. La clause « DD réduit ≥ 50 % » est
techniquement atteinte mais sans intérêt : un livre qui perd à faible
volatilité reste perdant. **Le spread naïf sans test de cointégration est du
bruit.** (D49, verrous `PairsOosIntegration`.)

### Famille 5 — Régime de volatilité, deux variantes (Sprints 13-14)

Filtre binaire long/cash selon le niveau de vol vs sa médiane glissante.

- **Vol réalisée** (20 j vs médiane 126 j) : Sharpe OOS positif
  (0,52/0,23/0,61) mais **sous le B&H** (1,09/0,77/1,04) sur les 3 pavages,
  les 4 actifs et les 9 réglages ; DD réduit de < 50 % (14,5 vs 18,1 %).
  (D50, verrous `VolRegimeOosIntegration`.)
- **Vol implicite** (niveau du ^VIX, exogène et anticipateur, 9192 barres
  1990+) : même verdict — Sharpe positif (0,55/0,42/0,45) mais sous le B&H
  (1,06/0,87/0,98), partout. La vol implicite ne fait pas mieux que la
  réalisée. (D51, verrous `VixRegimeOosIntegration`.)

**Bilan famille 5 : sortir du marché en régime de vol haute coûte plus en
rendement manqué que ça ne rapporte en risque réduit**, sur un sous-jacent
structurellement haussier (cash drag).

## 4. Leçons méthodologiques (transférables à toute recherche future)

1. **Un gagnant de grille n'est pas un edge, c'est une hypothèse** — à
   confirmer hors de la grille qui l'a choisie. Le candidat 8b.1 a survécu à
   sa première validation puis a été réfuté proprement (D36).
2. **La stabilité d'un plateau se teste en RESSERRANT la maille** autour du
   gagnant, pas seulement en élargissant la grille (D39).
3. **Un verdict OOS sans trades ne juge que le cash drag** — vérifier le
   nombre de trades poolés avant d'interpréter les ratios (D34/D47), et
   dimensionner les fenêtres OOS largement au-dessus du warmup (D35).
4. **Vérifier l'ACTIVATION d'un mécanisme avant d'interpréter son verdict** —
   un « ≥ chaîne » par égalité d'inertie ne valide rien (masquage, D41).
5. **Toute config de verrou se construit champ par champ**, jamais depuis les
   défauts — sinon changer un défaut déplace silencieusement l'histoire (D33).
6. **Multiple-testing** : plus on juge d'hypothèses, plus le « meilleur » est
   un artefact probable → confirmation hors-protocole complète obligatoire
   avant toute adoption (D43).
7. **La qualité des données prime** : une config « validée » sur un export
   sans dividendes n'est pas validée (D29/D30) ; un moteur avec look-ahead
   flatte TOUTES les mesures (D37). Corriger l'instrument avant de juger.
8. **Un harnais de mesure peut mentir par construction** : le Monte-Carlo
   aveugle à la taille rendait tout schéma de sizing inerte — et l'artefact a
   été pris pour une propriété du marché (D44/D45).
9. **Un backlog peut mentir** : rouvrir un item commence par vérifier
   fichier:ligne qu'il est encore réel (D52, dérive de type D20).
10. **La prémisse du sous-jacent domine la famille de signal** : sur un actif
    structurellement haussier, le trend-following est la bonne prémisse et le
    contrarian perd ; mais même la bonne prémisse ne bat pas le B&H net de
    coûts (D48).

## 5. Ce qui rouvrirait la recherche (critères, pas promesses)

Pistes identifiées à la clôture (Sprint 16), par ordre de coût croissant :

- **Cointégration propre pour le pairs-trading** : test d'Engle-Granger +
  hedge ratio roulant (une vraie paire cointégrée, pas un spread naïf β=1) —
  résidu explicite de D49. *→ EXPLORÉE au Sprint 18 (décision (r)) : verdict
  AUCUN EDGE — le gate filtre le bruit de D49 sans créer d'edge (D56,
  changelog Sprint 18, verrous `CointPairsOosIntegration`).*
- **Scaling continu Moreira-Muir** : pondération w = cible/vol (réalisée ou
  VIX) au lieu du filtre binaire long/cash — code moteur. *→ EXPLORÉE au
  Sprint 18 : verdict AUCUN EDGE — dSharpe vs B&H quasi comblé (le cash drag
  D50 était le bon diagnostic) mais alpha absolu toujours négatif (D55,
  changelog Sprint 18, verrous `VolScaledOosIntegration` /
  `VixScaledOosIntegration`).*
- **Term-structure de volatilité** (VIX/VIX3M) : le contango/backwardation
  comme signal de régime — nécessite un export de données supplémentaire.
- **Données alternatives / surface d'options** : gros chantier data, hors du
  périmètre offline actuel.

Toute réouverture est soumise au protocole complet : moteur anti-look-ahead,
données total-return longues, 3 pavages, garde de trades, grille resserrée,
MC size-aware, **et confirmation hors-protocole avant adoption** (D43). La
DoD d'edge reste inchangée : **alpha absolu positif vs buy & hold, net de
coûts, hors-échantillon, robuste au choix des fenêtres** — et le passage en
réel exige en plus la checklist pré-live signée du `RUNBOOK.md`.

## 6. Ce qui reste : un moteur sûr, correct et observable

La recherche d'edge échoue ; le **logiciel**, lui, est en bon état
(notes /100 : Architecture 90, Qualité 93, FinTech 88, Production 74 —
baseline d'audit 68/60/38/35) :

- 716 tests verts (575 unitaires + 141 intégration), build `-Werror`,
  CI Debug/Release/ASan-UBSan/TSan ciblé.
- Moteur interface-driven testé, réconciliation de position au redémarrage,
  kill-switch, stops résidents broker re-découverts après restart,
  observabilité de santé structurée (instantané `health`, Sprint 15).
- Backtester anti-look-ahead + harnais de validation offline (`validate`)
  réutilisables tels quels pour tout futur candidat.

Le bot reste utilisable en **paper trading** comme banc d'essai. Backlog
Production restant (si la voie durcissement est choisie) : test E2E email
SMTP (mock STARTTLS), durcissement CI (lint/static-analysis, pin des
versions, coverage), sonde `/healthz` HTTP, opérabilité ré-auth Gateway.

---

*Sources : `ROADMAP.md` (verdicts détaillés, découvertes D26-D52, changelog
par sprint), suites de tests `tests/integration/`, harnais `./build/validate`
(sections 1-18).*
