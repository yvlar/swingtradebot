---
name: sprint
description: >
  Exécute le cycle de sprint de SwingBot puis met à jour la ROADMAP. À invoquer
  quand l'utilisateur veut « faire le prochain sprint », « exécuter le sprint
  courant », « avancer la roadmap », « clôturer le sprint » ou « continuer le
  développement » sur ce dépôt. Orchestre prompt-executer-sprint.md (réalisation)
  puis prompt-mise-a-jour-roadmap.md (clôture, rétrospective, sprint suivant).
---

# Skill `/sprint` — cycle de développement SwingBot

Ce skill encapsule la discipline de développement éprouvée du dépôt (Sprints 1→7).
Il ne réinvente rien : il **orchestre les deux procédures détaillées** qui restent
la source de vérité, et rappelle les invariants à ne jamais violer.

## Source de vérité (à lire à chaque invocation)

1. **`ROADMAP.md`** — le tableau de bord (« Sprint courant »), les items non cochés,
   la section « Découvertes » et la dernière rétrospective.
2. **`prompt-executer-sprint.md`** — procédure de RÉALISATION du sprint courant.
3. **`prompt-mise-a-jour-roadmap.md`** — procédure de CLÔTURE (cocher, découvertes,
   re-priorisation, changelog, rétrospective, définition du sprint suivant).

Toujours relire ces fichiers : ils s'auto-corrigent de sprint en sprint (les leçons
y sont réinjectées). Ne pas se fier à la mémoire d'un sprint précédent.

## Déroulé

### Phase 1 — Réaliser (suivre `prompt-executer-sprint.md`)
1. Identifier le « Sprint courant » et ses items dans `ROADMAP.md`.
2. **Vérifier la baseline AVANT de coder** : `cmake -B build … && cmake --build build
   && ctest` 100 % vert, et **recaler le décompte** sur `ctest -N` (leçon D20 —
   signaler toute dérive du champ « État des tests »).
3. Pour chaque item, dans l'ordre : reproduire par un **test rouge** → fix minimal
   respectant l'architecture (interfaces `trading::`, injection au composition root)
   → **tout repasse au vert** → **commit atomique** (inspecter `git status --short`
   avant ; jamais de `git add -A` aveugle — leçon Sprint 1).
4. Choix d'architecture ambigu ou item « Décision requise » → **demander à
   l'utilisateur**, ne pas trancher seul.

### Phase 2 — Clôturer (suivre `prompt-mise-a-jour-roadmap.md`)
5. Cocher les items faits (+ hash court). Consigner toute découverte (`D<n>`, gravité).
6. Re-prioriser le backlog si une découverte est plus grave que le sprint suivant.
7. Changelog du sprint + **rétrospective** (4 questions : découpage, suffisance des
   prompts — et **amender les prompts dans le même commit** si on a dû improviser —,
   garde-fou manquant, notes /100 par dimension).
8. Définir le « Sprint courant » suivant (chaque item : réf fichier:ligne re-vérifiée,
   critère d'acceptation, dépendances).
9. **Committer** ROADMAP.md (+ prompts amendés) puis **pousser** la branche de travail.

## Invariants (Definition of Done — non négociables)

- `cmake --build build` sans erreur ni **nouveau warning**.
- `ctest --output-on-failure` : **100 % vert** (anciens + nouveaux).
- Chaque bug corrigé a un **test qui échouait avant** le fix.
- **Aucune régression du backtest golden** (Integration.Backtester*) — et, depuis le
  Sprint 7, **le verdict reste jugé en OUT-OF-SAMPLE** (harnais `WalkForward`) : un
  changement de stratégie qui n'améliore que l'in-sample ne compte pas.
- Commentaires/logs/doc **en français** ; code header-only dans `include/`.
- **Aucun secret committé** ; commits atomiques ; exclure `cmake-build-debug/` des recherches.
- « Faire de l'argent » = **battre le Buy & Hold net de coûts en OOS**, pas finir
  positif. Si un sprint stratégie ne le démontre pas → conclure « pas d'edge » et
  **ne pas déployer** (c'est un résultat valide).

## Build (rappel)

Conteneur vcpkg (`dev.ps1`) **ou** Linux nu avec paquets système (la CI fait ce
dernier) :

```bash
apt-get update && apt-get install -y libboost-dev libboost-system-dev \
  nlohmann-json3-dev libcurl4-openssl-dev libsqlite3-dev libgtest-dev googletest \
  cmake ninja-build g++
cmake -B build -DCMAKE_BUILD_TYPE=Debug -G Ninja && cmake --build build -j"$(nproc)"
cd build && ctest --output-on-failure -j4
```
