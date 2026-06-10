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
      Si oui : **modifier `prompt-executer-sprint.md` / `prompt-mise-a-jour-roadmap.md`
      dans le même commit** pour que la prochaine itération n'improvise plus
      (le workflow doit s'auto-corriger).
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
7. **Committer** ROADMAP.md (+ les prompts s'ils ont été amendés) avec un message
   `docs: clôture sprint N — mise à jour roadmap`, puis pousser la branche.
