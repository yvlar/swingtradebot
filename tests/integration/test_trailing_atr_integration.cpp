// ─── Tests d'intégration : verdict OOS du trailing ATR (Sprint 8-quater) ─────
// Item 8q.2. Le trailing FIXE en % est l'axe le plus fragile de la chaîne v2
// (8b.1 post-B2 : sensibilité 0,266 ; 8t.3 : le plateau resserré dérive
// 0,03 → 0,04). Hypothèse 8b.4 : la « bonne » distance de trailing dépend de
// la volatilité — un trailing en multiples d'ATR(14) (item 8q.1) s'adapte.
// Protocole (leçon 8-ter : tout gagnant est jugé sur des fenêtres NON-CHOISIES) :
//   1. mini-grille mult ∈ {2, 3, 4} CHOISIE sur le pavage FIN (500/300) ;
//   2. duel chaîne v2 (trail % 0,03) vs chaîne + ATR(mult choisi) sur les
//      pavages CANONIQUE (700/400) et DÉCALÉ (500/400, offset 90) — que la
//      mini-grille n'a pas vus. Acceptation ROADMAP : alpha OOS ≥ chaîne sur
//      ces pavages-là, sinon verdict « pas d'amélioration » (résultat VALIDE).
// Discipline : configs EXPLICITES champ par champ (D33), nombre de trades OOS
// poolés figé (D34). Aucun de ces verrous n'autorise à lui seul une adoption
// (gate 8q.3 = décision utilisateur).
// VERDICT GLOBAL MESURÉ (figé le 2026-07-03) : PAS D'AMÉLIORATION ROBUSTE —
// mult=3 choisi sur le fin (−6,55 vs chaîne −6,88), AMÉLIORE le canonique
// (−8,08 vs −9,90, +1,82 pt) mais fait légèrement pire sur le décalé (−13,08
// vs −12,83, −0,24 pt) : l'acceptation exigeait ≥ sur les DEUX pavages
// non-choisis. Résultat valide — consigné, aucune adoption (gate 8q.3).
#include <gtest/gtest.h>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>
#include "backtest/WalkForward.hpp"

using namespace trading;

namespace {

// Pavage FIN (8b.3) — celui où la mini-grille CHOISIT son mult : 4 fenêtres
// OOS [500,800) [800,1100) [1100,1400) [1400,1700) sur les 1790 barres.
constexpr size_t kIsFin   = 500;
constexpr size_t kOosFin  = 300;
constexpr size_t kStepFin = 300;

// Pavage CANONIQUE des verdicts 8.x — non vu par la mini-grille.
constexpr size_t kIsCan   = 700;
constexpr size_t kOosCan  = 400;
constexpr size_t kStepCan = 400;

// Pavage DÉCALÉ (8t.1) — non vu par la mini-grille, aucune borne commune
// avec les deux autres.
constexpr size_t kIsDec     = 500;
constexpr size_t kOosDec    = 400;
constexpr size_t kStepDec   = 400;
constexpr size_t kOffsetDec = 90;

// Multiplicateur retenu par la mini-grille sur le pavage fin (argmax de
// l'alpha OOS moyen, figé par AtrMiniGridFinePavingSelectionIsLocked) :
// mult=3 (−6,55) bat mult=2 (−7,01) et mult=4 (−6,70). Mesuré le 2026-07-03.
constexpr double kMultChoisi = 3.0;

// ── Config de la chaîne v2, EXPLICITE champ par champ (règle D33) ────────────
// Snapshot volontairement dupliqué des autres fichiers de verdict : une mesure
// historique ne doit pas bouger si les défauts de SwingConfig évoluent (en
// particulier si 8q.3 adopte le trailing ATR comme défaut).
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
    c.trailingAtrMult         = 0.0;     // 8q.1 : ATR désactivé — trailing % pur
    c.riskPerTradePct         = 0.02;
    c.minHoldDays             = 3;
    c.smaTrendPeriod          = 200;     // 8.1 : filtre de régime
    c.rsiSellOnlyIfRegimeDown = true;    // 8.4 : vente RSI gatée
    c.regimeReentry           = true;    // 8.5 : re-entrée sur régime
    return c;
}

// Chaîne + trailing ATR : seul trailingAtrMult diffère. trailingStopPct 0,03
// reste renseigné mais ne sert plus que de REPLI défensif (fenêtre < 15
// barres — n'arrive pas ici : lookback = smaTrendPeriod + 30 = 230 barres).
SwingConfig cfgAtr(double mult) {
    SwingConfig c = cfgChaineV2();
    c.trailingAtrMult = mult;
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

// ─── 8q.2a — Mini-grille mult ∈ {2,3,4} sur le pavage FIN (sélection) ────────
// C'est le pavage de CHOIX : le mult retenu (kMultChoisi) est l'argmax de
// l'alpha OOS moyen ici. Les trois mesures sont figées — si un changement de
// moteur déplace la sélection, ce verrou le dit AVANT que les duels 8q.2b/c
// ne deviennent silencieusement incomparables.
TEST(TrailingAtrIntegration, AtrMiniGridFinePavingSelectionIsLocked) {
    const double mults[] = {2.0, 3.0, 4.0};
    double alphas[3] = {0.0, 0.0, 0.0};
    size_t trades[3] = {0, 0, 0};

    std::cout << "  8q.2a — mini-grille ATR (pavage fin IS=500/OOS=300, 4 fenetres)\n";
    for (int i = 0; i < 3; ++i) {
        const auto w = WalkForward(cfgAtr(mults[i]), SWINGBOT_QQQ_CSV,
                                   kIsFin, kOosFin, kStepFin).run();
        ASSERT_EQ(w.size(), 4u);
        alphas[i] = meanOosAlpha(w);
        trades[i] = tradesOos(w).size();
        EXPECT_TRUE(std::isfinite(alphas[i]));
        std::cout << std::fixed << std::setprecision(4)
                  << "  mult=" << mults[i] << " : alpha OOS moyen " << alphas[i]
                  << " pt, " << trades[i] << " trades OOS pooles\n";
    }

    // Mesures figées le 2026-07-03. La sélection est PLATE (écart max−min
    // 0,47 pt) : aucun mult ne transforme le verdict, mult=3 est retenu.
    EXPECT_NEAR(alphas[0], -7.0137, 1e-2);
    EXPECT_NEAR(alphas[1], -6.5459, 1e-2);
    EXPECT_NEAR(alphas[2], -6.7007, 1e-2);
    EXPECT_EQ(trades[0], 14u);
    EXPECT_EQ(trades[1], 10u);
    EXPECT_EQ(trades[2], 10u);

    // Garde anti-dérive : kMultChoisi est bien l'argmax mesuré.
    int best = 0;
    for (int i = 1; i < 3; ++i)
        if (alphas[i] > alphas[best]) best = i;
    EXPECT_DOUBLE_EQ(mults[best], kMultChoisi);
}

// ─── 8q.2b — Duel chaîne vs ATR(mult choisi) sur le pavage FIN ───────────────
// Informatif, PAS décisif : le pavage fin est celui que la mini-grille a
// choisi (biais de sélection, D36) — le verdict d'acceptation se joue sur les
// pavages non-choisis (8q.2c/d). Le recoupement inter-fichiers de la chaîne
// (−6,8794 / 13 trades, verrou SprintChainFinePavingOosVerdictIsLocked)
// prouve au passage que la plomberie 8q.1 n'a pas bougé le chemin historique.
TEST(TrailingAtrIntegration, AtrChainFinePavingDuelIsLocked) {
    const auto wChaine = WalkForward(cfgChaineV2(), SWINGBOT_QQQ_CSV,
                                     kIsFin, kOosFin, kStepFin).run();
    const auto wAtr    = WalkForward(cfgAtr(kMultChoisi), SWINGBOT_QQQ_CSV,
                                     kIsFin, kOosFin, kStepFin).run();
    ASSERT_EQ(wChaine.size(), 4u);
    ASSERT_EQ(wAtr.size(),    4u);

    std::cout << "  8q.2b — duel pavage fin (choisi par la mini-grille)\n";
    imprime("chaine v2   ", wChaine);
    imprime("chaine + ATR", wAtr);

    // Recoupement : la chaîne sur ce pavage est déjà verrouillée ailleurs.
    EXPECT_NEAR(meanOosAlpha(wChaine), -6.8794, 1e-2);
    EXPECT_EQ(tradesOos(wChaine).size(), 13u);

    // Mesure figée le 2026-07-03 : l'ATR bat la chaîne de +0,33 pt sur SON
    // pavage de sélection (attendu — biais de sélection, d'où 8q.2c/d).
    EXPECT_NEAR(meanOosAlpha(wAtr), -6.5459, 1e-2);
    EXPECT_EQ(tradesOos(wAtr).size(), 10u);
}

// ─── 8q.2c — VERDICT : duel sur le pavage CANONIQUE (non-choisi) ─────────────
// VERDICT MESURÉ (figé le 2026-07-03) : sur les fenêtres canoniques le
// trailing ATR AMÉLIORE la chaîne — alpha OOS moyen −8,08 pt (7 trades)
// contre −9,90 pt (11 trades), soit +1,82 pt — mais reste NÉGATIF (aucun
// edge absolu). À lui seul ce pavage satisferait l'acceptation ; le verdict
// global se joue avec 8q.2d, où le signe s'inverse.
TEST(TrailingAtrIntegration, AtrCanonicalPavingOosVerdictIsLocked) {
    const auto wChaine = WalkForward(cfgChaineV2(), SWINGBOT_QQQ_CSV,
                                     kIsCan, kOosCan, kStepCan).run();
    const auto wAtr    = WalkForward(cfgAtr(kMultChoisi), SWINGBOT_QQQ_CSV,
                                     kIsCan, kOosCan, kStepCan).run();
    ASSERT_EQ(wChaine.size(), 2u);
    ASSERT_EQ(wAtr.size(),    2u);

    const double alphaChaine = meanOosAlpha(wChaine);
    const double alphaAtr    = meanOosAlpha(wAtr);
    EXPECT_TRUE(std::isfinite(alphaChaine));
    EXPECT_TRUE(std::isfinite(alphaAtr));

    std::cout << "  8q.2c — duel pavage canonique (IS=700/OOS=400, non-choisi)\n";
    imprime("chaine v2   ", wChaine);
    imprime("chaine + ATR", wAtr);

    // Recoupement : la chaîne sur ce pavage est déjà verrouillée ailleurs
    // (SprintChainVsBaselineOosVerdictIsLocked : −9,9023, 11 trades).
    EXPECT_NEAR(alphaChaine, -9.9023, 1e-2);
    EXPECT_EQ(tradesOos(wChaine).size(), 11u);

    // Verdict figé : amélioration sur CE pavage non-choisi (+1,82 pt),
    // sans edge absolu.
    EXPECT_NEAR(alphaAtr, -8.0849, 1e-2);
    EXPECT_EQ(tradesOos(wAtr).size(), 7u);
    EXPECT_GT(alphaAtr, alphaChaine);   // mieux ICI (+1,82 pt)…
    EXPECT_LT(alphaAtr, 0.0);           // …mais toujours aucun edge absolu
}

// ─── 8q.2d — VERDICT : duel sur le pavage DÉCALÉ (non-choisi) ────────────────
// VERDICT MESURÉ (figé le 2026-07-03) : sur le pavage décalé le trailing ATR
// fait légèrement PIRE que la chaîne — alpha OOS moyen −13,08 pt (10 trades)
// contre −12,83 pt (11 trades), soit −0,24 pt. L'acceptation ROADMAP 8q.2
// (« alpha OOS ≥ chaîne sur LES pavages non-choisis ») exige les DEUX pavages
// → NON satisfaite : verdict global « PAS D'AMÉLIORATION robuste » (résultat
// VALIDE — on consigne, on n'adopte pas). Nuance honnête : l'écart est un
// ordre de grandeur plus petit que l'inversion du candidat 8-ter (−0,24 pt
// contre −9,2 pts) — c'est une amélioration NON ROBUSTE, pas une réfutation
// brutale ; la décision de suite appartient au gate 8q.3.
TEST(TrailingAtrIntegration, AtrShiftedPavingOosVerdictIsLocked) {
    const auto wChaine = WalkForward(cfgChaineV2(), SWINGBOT_QQQ_CSV,
                                     kIsDec, kOosDec, kStepDec, kOffsetDec).run();
    const auto wAtr    = WalkForward(cfgAtr(kMultChoisi), SWINGBOT_QQQ_CSV,
                                     kIsDec, kOosDec, kStepDec, kOffsetDec).run();
    ASSERT_EQ(wChaine.size(), 3u);
    ASSERT_EQ(wAtr.size(),    3u);

    const double alphaChaine = meanOosAlpha(wChaine);
    const double alphaAtr    = meanOosAlpha(wAtr);
    EXPECT_TRUE(std::isfinite(alphaChaine));
    EXPECT_TRUE(std::isfinite(alphaAtr));

    std::cout << "  8q.2d — duel pavage decale (IS=500/OOS=400, offset=90, non-choisi)\n";
    imprime("chaine v2   ", wChaine);
    imprime("chaine + ATR", wAtr);

    // Recoupement : la chaîne sur ce pavage est déjà verrouillée ailleurs
    // (CandidateShiftedPavingOosVerdictIsLocked : −12,8332, 11 trades).
    EXPECT_NEAR(alphaChaine, -12.8332, 1e-2);
    EXPECT_EQ(tradesOos(wChaine).size(), 11u);

    // Verdict figé : PAS d'amélioration sur ce pavage non-choisi (−0,24 pt)
    // → l'acceptation 8q.2 échoue ici, verdict global « pas d'amélioration
    // robuste ».
    EXPECT_NEAR(alphaAtr, -13.0753, 1e-2);
    EXPECT_EQ(tradesOos(wAtr).size(), 10u);
    EXPECT_LT(alphaAtr, alphaChaine);   // NON : légèrement pire ici (−0,24 pt)
    EXPECT_LT(alphaAtr, 0.0);           // et aucun edge absolu
}
