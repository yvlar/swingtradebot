// ─── Tests d'intégration : validation HORS-GRILLE du candidat d'edge ─────────
// Sprint 8-ter (items 8t.1 / 8t.2). Le candidat 8b.1 post-B2 — (emaFast=9,
// smaTrendPeriod=250, trailingStopPct=0,03), alpha OOS +0,23, verrouillé par
// V2ChainExtendedGridOosVerdictIsLocked — a été choisi par une grille jugée sur
// les MÊMES fenêtres OOS que son verdict (biais de sélection, D36). Ce fichier
// le juge sur des fenêtres qu'il n'a PAS choisies :
//   - pavage canonique IS=700/OOS=400 (celui des verdicts 8.x, jamais vu par
//     la grille 8b.1 qui jugeait sur le pavage fin 500/300) ;
//   - pavage DÉCALÉ IS=500/OOS=400/pas=400/offset=90 (aucune borne commune
//     avec AUCUN pavage historique — décision utilisateur du 2026-07-03).
// « Candidat non confirmé » est un résultat valide (ROADMAP 8t.1) ; aucun de
// ces verrous n'autorise à lui seul une adoption (gate 8t.4 = décision user).
#include <gtest/gtest.h>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <vector>
#include "backtest/WalkForward.hpp"

using namespace trading;

namespace {

// Pavage canonique des verdicts 8.x (mêmes valeurs que
// test_strategy_v2_integration.cpp — 2 fenêtres OOS : [700,1100) et [1100,1500)).
constexpr size_t kIsCan   = 700;
constexpr size_t kOosCan  = 400;
constexpr size_t kStepCan = 400;

// Pavage DÉCALÉ (8t.1) : IS=500/OOS=400/pas=400, offset=90 → 3 fenêtres OOS
// [590,990), [990,1390), [1390,1790) sur les 1790 barres de QQQ. Choix arbitré
// à l'ouverture du sprint (2026-07-03) contre l'exemple littéral OOS=300 de la
// ROADMAP : le warmup local du candidat est ~251 barres (SMA250, BackTester.hpp)
// → OOS=300 ne laisserait que ~49 barres tradables (le piège D34/D35) ; OOS=400
// en laisse ~149. Aucune borne d'OOS commune avec le pavage canonique
// ({[700,1100), [1100,1500)}) ni avec le pavage fin ({[500,800) … [1400,1700)}),
// et les 90 dernières barres ([1700,1790)) entrent en OOS pour la 1re fois.
constexpr size_t kIsDec     = 500;
constexpr size_t kOosDec    = 400;
constexpr size_t kStepDec   = 400;
constexpr size_t kOffsetDec = 90;

// ── Config de la chaîne v2, EXPLICITE champ par champ (règle D33) ────────────
// Un verdict est une mesure HISTORIQUE : il ne doit pas bouger quand les
// défauts de SwingConfig évoluent — en particulier si le Sprint 8-ter aboutit
// à l'adoption du candidat comme défaut. (Duplication volontaire avec les
// autres fichiers de verdict — chaque fichier porte son propre snapshot.)
SwingConfig cfgChaineV2() {
    SwingConfig c;
    c.symbol                  = "QQQ";
    c.emaFast                 = 9;
    c.emaSlow                 = 21;
    c.rsiPeriod               = 14;
    c.rsiBuyMax               = 100.0;   // 8.3 : plafond d'achat désactivé
    c.rsiSellMin              = 70.0;
    c.stopLossPct             = 0.05;
    c.takeProfitPct           = 0.0;     // 8.2 : take-profit désactivé
    c.trailingStopPct         = 0.03;
    c.riskPerTradePct         = 0.02;
    c.minHoldDays             = 3;
    c.smaTrendPeriod          = 200;     // 8.1 : filtre de régime
    c.rsiSellOnlyIfRegimeDown = true;    // 8.4 : vente RSI gatée
    c.regimeReentry           = true;    // 8.5 : re-entrée sur régime
    return c;
}

// Candidat 8b.1 post-B2 (D37) : (emaFast=9, smaT=250, trail=0,03). emaFast et
// trailingStopPct sont DÉJÀ les valeurs de la chaîne — le candidat ne diffère
// que par smaTrendPeriod 200 → 250 (le seul axe stable entre les grilles 18 et
// 81 combos, D36).
SwingConfig cfgCandidat() {
    SwingConfig c = cfgChaineV2();
    c.smaTrendPeriod = 250;
    return c;
}

double meanOosAlpha(const std::vector<WfWindow>& w) {
    if (w.empty()) return 0.0;
    double s = 0.0;
    for (const auto& x : w) s += x.oos.alpha;
    return s / static_cast<double>(w.size());
}

// Trades OOS POOLÉS sur toutes les fenêtres (D34 : chaque verdict fige aussi
// son NOMBRE de trades — un alpha OOS sans trades ne juge que le cash drag).
std::vector<TradeRecord> tradesOos(const std::vector<WfWindow>& w) {
    std::vector<TradeRecord> out;
    for (const auto& x : w)
        out.insert(out.end(), x.oos.trades.begin(), x.oos.trades.end());
    return out;
}

// Impression compacte d'un côté de comparaison (nom, alpha OOS moyen, trades).
void imprime(const char* nom, const std::vector<WfWindow>& w) {
    std::cout << std::fixed << std::setprecision(4)
              << "  " << nom << " : alpha OOS moyen " << meanOosAlpha(w)
              << " pt, " << tradesOos(w).size() << " trades OOS pooles ("
              << w.size() << " fenetres)\n";
}

} // namespace

// ─── Structure du pavage décalé : les fenêtres sont bien « non-choisies » ────
TEST(CandidateValidationIntegration, ShiftedPavingTilesWithOffset) {
    WalkForward wf(cfgCandidat(), SWINGBOT_QQQ_CSV, kIsDec, kOosDec, kStepDec,
                   kOffsetDec);
    const auto w = wf.run();

    ASSERT_EQ(w.size(), 3u);
    EXPECT_EQ(w[0].oosStart,  590u); EXPECT_EQ(w[0].oosEnd,  990u);
    EXPECT_EQ(w[1].oosStart,  990u); EXPECT_EQ(w[1].oosEnd, 1390u);
    EXPECT_EQ(w[2].oosStart, 1390u); EXPECT_EQ(w[2].oosEnd, 1790u);
    for (const auto& x : w) EXPECT_EQ(x.isEnd, x.oosStart);
    // Aucun départ d'OOS commun avec le pavage canonique (700/1100) ni fin
    // (500/800/1100/1400) : le candidat n'a choisi AUCUNE de ces fenêtres.
    for (const auto& x : w) {
        EXPECT_NE(x.oosStart,  700u); EXPECT_NE(x.oosStart, 1100u);
        EXPECT_NE(x.oosStart,  500u); EXPECT_NE(x.oosStart,  800u);
        EXPECT_NE(x.oosStart, 1400u);
    }
}

// ─── 8t.1a — Verdict verrouillé : candidat vs chaîne, pavage CANONIQUE ───────
// VERDICT MESURÉ (figé le 2026-07-03) : CANDIDAT NON CONFIRMÉ sur les fenêtres
// canoniques — alpha OOS moyen −19,10 pt (10 trades) contre −9,90 pt (11
// trades) pour la chaîne : le candidat fait PIRE que la chaîne de 9,2 pts sur
// des fenêtres que sa grille n'a jamais vues. Exactement le scénario D36 (un
// gagnant de grille n'est pas un edge). Résultat valide : consigner, ne pas
// adopter.
TEST(CandidateValidationIntegration, CandidateCanonicalPavingOosVerdictIsLocked) {
    const auto wChaine   = WalkForward(cfgChaineV2(), SWINGBOT_QQQ_CSV,
                                       kIsCan, kOosCan, kStepCan).run();
    const auto wCandidat = WalkForward(cfgCandidat(), SWINGBOT_QQQ_CSV,
                                       kIsCan, kOosCan, kStepCan).run();
    ASSERT_EQ(wChaine.size(),   2u);
    ASSERT_EQ(wCandidat.size(), 2u);

    const double alphaChaine   = meanOosAlpha(wChaine);
    const double alphaCandidat = meanOosAlpha(wCandidat);
    const auto   tChaine       = tradesOos(wChaine);
    const auto   tCandidat     = tradesOos(wCandidat);
    EXPECT_TRUE(std::isfinite(alphaChaine));
    EXPECT_TRUE(std::isfinite(alphaCandidat));

    std::cout << "  8t.1a — pavage canonique (IS=700/OOS=400, 2 fenetres)\n";
    imprime("chaine v2", wChaine);
    imprime("candidat ", wCandidat);

    // Recoupement : la chaîne sur ce pavage est déjà verrouillée ailleurs
    // (SprintChainVsBaselineOosVerdictIsLocked : −9,9023, 11 trades).
    EXPECT_NEAR(alphaChaine, -9.9023, 1e-2);
    EXPECT_EQ(tChaine.size(), 11u);

    // Verdict figé : le candidat SOUS-performe la chaîne sur le pavage
    // canonique (acceptation 8t.1 « alpha candidat > chaîne » NON satisfaite).
    EXPECT_NEAR(alphaCandidat, -19.1004, 1e-2);
    EXPECT_EQ(tCandidat.size(), 10u);
    EXPECT_LT(alphaCandidat, alphaChaine);   // NON CONFIRMÉ (−19,10 < −9,90)
    EXPECT_LT(alphaCandidat, 0.0);           // et aucun edge absolu
}

// ─── 8t.1b — Verdict verrouillé : candidat vs chaîne, pavage DÉCALÉ ──────────
// VERDICT MESURÉ (figé le 2026-07-03) : sur le pavage décalé le candidat BAT la
// chaîne (−5,38 vs −12,83, +7,45 pts, 9 trades vs 11) — mais reste NÉGATIF
// (aucun edge absolu), et le SIGNE de la comparaison s'inverse selon le pavage
// (canonique : −9,2 pts). Un edge qui dépend du choix des fenêtres n'est pas
// un edge : combiné à 8t.1a, le verdict global de l'item est NON CONFIRMÉ.
TEST(CandidateValidationIntegration, CandidateShiftedPavingOosVerdictIsLocked) {
    const auto wChaine   = WalkForward(cfgChaineV2(), SWINGBOT_QQQ_CSV,
                                       kIsDec, kOosDec, kStepDec, kOffsetDec).run();
    const auto wCandidat = WalkForward(cfgCandidat(), SWINGBOT_QQQ_CSV,
                                       kIsDec, kOosDec, kStepDec, kOffsetDec).run();
    ASSERT_EQ(wChaine.size(),   3u);
    ASSERT_EQ(wCandidat.size(), 3u);

    const double alphaChaine   = meanOosAlpha(wChaine);
    const double alphaCandidat = meanOosAlpha(wCandidat);
    const auto   tChaine       = tradesOos(wChaine);
    const auto   tCandidat     = tradesOos(wCandidat);
    EXPECT_TRUE(std::isfinite(alphaChaine));
    EXPECT_TRUE(std::isfinite(alphaCandidat));

    std::cout << "  8t.1b — pavage decale (IS=500/OOS=400, offset=90, 3 fenetres)\n";
    imprime("chaine v2", wChaine);
    imprime("candidat ", wCandidat);

    // Verdict figé : sur CES fenêtres le candidat bat la chaîne — mais les
    // deux restent négatifs, et 8t.1a montre l'inverse sur le pavage
    // canonique. Instabilité = pas d'edge démontré.
    EXPECT_NEAR(alphaChaine,   -12.8332, 1e-2);
    EXPECT_NEAR(alphaCandidat,  -5.3790, 1e-2);
    EXPECT_EQ(tChaine.size(),   11u);
    EXPECT_EQ(tCandidat.size(),  9u);
    EXPECT_GT(alphaCandidat, alphaChaine);   // mieux ICI (+7,45 pts)…
    EXPECT_LT(alphaCandidat, 0.0);           // …mais toujours aucun edge absolu
}
