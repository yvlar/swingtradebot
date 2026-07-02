# PROMPT — Mettre à jour ROADMAP.md après un sprint (SwingBot)

> À exécuter immédiatement après `prompt-executer-sprint.md`, dans la même session ou
> une nouvelle. Le sprint n'est clos que lorsque ce prompt a été déroulé et committé.

## Procédure

1. **Cocher le fait** : pour chaque item du sprint terminé, cocher la case dans
   `ROADMAP.md` et ajouter le hash court du commit correspondant
   (ex. `- [x] **5. Sizing forcé à 1** … → \`a1b2c3d\``). Un item non terminé reste
   décoché, avec une note expliquant le reliquat.
2. **Consigner les découvertes** : tout constat nouveau (bug, dette, hypothèse câblée)
   rencontré pendant le sprint entre dans la section « Découvertes » avec un numéro
   `D<n>`, une gravité (🔴🟠🟡🟢) et une affectation à un sprint.
3. **Re-prioriser le backlog** : si une découverte est plus grave que les items du
   sprint suivant, réordonner. Justifier tout changement d'ordre en une ligne.
4. **Changelog** : ajouter un bloc `### Sprint N — <titre> (<date>)` listant les
   commits, les tests ajoutés (avant → après), et toute métrique pertinente
   (ex. valeurs du backtest golden si elles ont changé, et pourquoi c'est légitime).
   Le décompte « avant » est le décompte RÉEL de `ctest -N`, pas celui que le tableau
   de bord affichait : si des commits ont été mergés hors de ce cycle (leçon D20),
   les absorber ici (les lister, recaler « État des tests ») pour que le tableau de
   bord cesse de mentir.
5. **Rétrospective** : écrire une entrée répondant explicitement aux 4 questions de la
   méta-évaluation :
   1. Le découpage en sprints était-il bon (taille, ordre, dépendances ratées) ?
   2. Les prompts du workflow ont-ils été suffisants ou as-tu dû improviser ?
      Si oui : **PROPOSER un diff** de `prompt-executer-sprint.md` /
      `prompt-mise-a-jour-roadmap.md` et le soumettre à une **décision
      utilisateur explicite** — ne JAMAIS l'appliquer soi-même dans le commit
      de clôture. Un processus qui trade de l'argent réel ne réécrit pas ses
      propres garde-fous sans revue humaine (règle miroir dans CLAUDE.md,
      « Live-safety rules »). Le diff accepté est appliqué dans un commit
      dédié qui cite la décision.
   3. Qu'est-ce qui aurait dû être détecté plus tôt, et quel garde-fou (test, check CI,
      règle de DoD) l'aurait attrapé automatiquement ? Si le garde-fou est peu coûteux,
      l'ajouter au backlog (ou à la DoD) immédiatement.
   4. Note d'avancement /100 par dimension (Architecture, Qualité, FinTech, Production)
      avec justification de l'écart vs la note précédente. Mettre à jour le tableau de
      bord. Baseline de référence : 68/60/38/35 (audit 2026-06-10).
6. **Définir le sprint suivant** : mettre à jour « Sprint courant » dans le tableau de
   bord et s'assurer que chaque item du nouveau sprint a : une référence fichier:ligne
   (re-vérifiée — les lignes bougent), un critère d'acceptation vérifiable, et ses
   dépendances explicites. `prompt-executer-sprint.md` doit pouvoir repartir sans
   aucune ambiguïté.
7. **Committer** ROADMAP.md avec un message `docs: clôture sprint N — mise à
   jour roadmap`, puis pousser la branche. (Les amendements de prompts suivent
   la règle du point 5.2 : décision utilisateur d'abord, commit dédié ensuite.)

## Garde-fou live (rappel, ne se négocie pas)

Aucune clôture de sprint ne peut activer le trading réel : `liveTradingApproved`
reste `false` et le test `LiveTradingStaysDisapprovedUntilEdgeDoD` reste tel
quel tant que la DoD d'edge n'est pas verrouillée ET que la checklist pré-live
du `RUNBOOK.md` n'a pas été signée par l'utilisateur (décision consignée au
changelog).
