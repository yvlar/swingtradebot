// ─── Tests d'intégration : verdicts OOS des familles de signaux (Sprint 8-quinquies) ─
// Item 8s.3. L'axe trailing est soldé sans gagnant robuste (D40) : la recherche
// d'edge continue par les SIGNAUX — sortie STRUCTURELLE « plus bas de N jours »
// (item 8s.1, exitOnLowestLowN) et entrée BREAKOUT « plus haut de M jours »
// (item 8s.2, entryBreakoutM). Protocole (modèle test_trailing_atr_integration.cpp,
// leçon 8-ter : tout gagnant est jugé sur des fenêtres NON-CHOISIES) :
//   1. mini-grilles CHOISIES sur le pavage FIN (500/300) — N ∈ {10, 20, 55}
//      pour la sortie ; M ∈ {20, 55} × {remplace, s'ajoute} pour l'entrée
//      (décision utilisateur 2026-07-03 : juger les DEUX hypothèses de
//      cohabitation avec la re-entrée 8.5, car « s'ajoute » est presque un
//      sous-ensemble de la re-entrée) ;
//   2. duels chaîne v2 vs chaîne + mécanisme (réglage choisi) sur les pavages
//      CANONIQUE (700/400) et DÉCALÉ (500/400, offset 90) — que les
//      mini-grilles n'ont pas vus. Acceptation ROADMAP : alpha OOS ≥ chaîne
//      sur ces pavages-là, sinon « pas d'amélioration » (résultat VALIDE) ;
//   3. la COMBINAISON des deux mécanismes n'est jugée que si chacun est ≥
//      chaîne sur les deux pavages non-choisis (gate 8s.3).
// Discipline : configs EXPLICITES champ par champ (D33), nombre de trades OOS
// poolés figé (D34), les 3/4 mesures ET l'argmax des mini-grilles figés
// (garde anti-dérive, leçon 8-quater), deltas chiffrés consignés. Aucun de
// ces verrous n'autorise à lui seul une adoption (gate 8s.4 = décision
// utilisateur).
// VERDICT GLOBAL MESURÉ (figé le 2026-07-03) : AUCUNE AMÉLIORATION —
//   • structure : N=20 (argmax plat) est INERTE sur les TROIS pavages
//     (mesures identiques à la chaîne : la sortie ne tire jamais avant le
//     trailing % 0,03) ; le seul N qui tire (10) dégrade le pavage fin.
//   • breakout : M=20 « remplace » gagnait +0,19 pt sur le pavage de CHOIX,
//     mais rend −1,85 pt sur le canonique (−11,75 vs −9,90) et −1,43 pt sur
//     le décalé (−14,26 vs −12,83) : biais de sélection pur, l'acceptation
//     (≥ chaîne sur les DEUX pavages non-choisis) échoue des deux côtés.
//   • combinaison : gate FERMÉ (le breakout échoue seul ; la structure est
//     inerte — rien à combiner). Résultat valide — consigné, aucune adoption.
#include <gtest/gtest.h>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>
#include "backtest/WalkForward.hpp"

using namespace trading;

namespace {

// Pavage FIN (8b.3) — celui où les mini-grilles CHOISISSENT leur réglage :
// 4 fenêtres OOS [500,800) [800,1100) [1100,1400) [1400,1700) sur 1790 barres.
constexpr size_t kIsFin   = 500;
constexpr size_t kOosFin  = 300;
constexpr size_t kStepFin = 300;

// Pavage CANONIQUE des verdicts 8.x — non vu par les mini-grilles.
constexpr size_t kIsCan   = 700;
constexpr size_t kOosCan  = 400;
constexpr size_t kStepCan = 400;

// Pavage DÉCALÉ (8t.1) — non vu par les mini-grilles, aucune borne commune
// avec les deux autres.
constexpr size_t kIsDec     = 500;
constexpr size_t kOosDec    = 400;
constexpr size_t kStepDec   = 400;
constexpr size_t kOffsetDec = 90;

// Réglages retenus par les mini-grilles sur le pavage fin (argmax de l'alpha
// OOS moyen, figés par les verrous *MiniGridFinePavingSelectionIsLocked,
// mesurés le 2026-07-03) :
//   • structure : N=20 — sélection PLATE et DÉGÉNÉRÉE : N=20 et N=55 sont
//     INERTES (mesures identiques à la chaîne au 1e-4 : la sortie structure
//     ne se déclenche jamais avant le trailing sur ce pavage), N=10 dégrade
//     (−7,04). L'argmax strict retient le premier des ex æquo (N=20).
//   • breakout : M=20 en variante « remplace » (−6,69 vs chaîne −6,88) ;
//     la variante « s'ajoute » est EXACTEMENT inerte (delta nul, confirmant
//     l'analyse d'ouverture : sous-ensemble de la re-entrée 8.5).
constexpr int  kNChoisi = 20;              // sortie structurelle
constexpr int  kMChoisi = 20;              // entrée breakout
constexpr bool kBreakoutRemplaceReentry = true;   // variante retenue

// ── Config de la chaîne v2, EXPLICITE champ par champ (règle D33) ────────────
// Snapshot volontairement dupliqué des autres fichiers de verdict : une mesure
// historique ne doit pas bouger si les défauts de SwingConfig évoluent (en
// particulier si 8s.4 adopte un des mécanismes comme défaut).
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
    c.exitOnLowestLowN        = 0;       // 8s.1 : sortie structurelle désactivée
    c.riskPerTradePct         = 0.02;
    c.minHoldDays             = 3;
    c.smaTrendPeriod          = 200;     // 8.1 : filtre de régime
    c.rsiSellOnlyIfRegimeDown = true;    // 8.4 : vente RSI gatée
    c.regimeReentry           = true;    // 8.5 : re-entrée sur régime
    c.entryBreakoutM          = 0;       // 8s.2 : entrée breakout désactivée
    return c;
}

// Chaîne + sortie structurelle : seul exitOnLowestLowN diffère (le trailing %
// 0,03 reste actif — la structure passe AVANT lui dans les priorités).
SwingConfig cfgStructure(int n) {
    SwingConfig c = cfgChaineV2();
    c.exitOnLowestLowN = n;
    return c;
}

// Chaîne + entrée breakout. Deux hypothèses de cohabitation avec la
// re-entrée 8.5 (décision utilisateur 2026-07-03) :
//   • « s'ajoute »  : regimeReentry reste true (lecture littérale de l'item) ;
//   • « remplace »  : regimeReentry=false — l'entrée hors-croisement exige la
//     force DÉMONTRÉE (breakout), pas le seul état « régime up + prix > EMAs ».
SwingConfig cfgBreakout(int m, bool remplaceReentry) {
    SwingConfig c = cfgChaineV2();
    c.entryBreakoutM = m;
    c.regimeReentry  = !remplaceReentry;
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

// ─── 8s.3a — Mini-grille structure N ∈ {10,20,55} sur le pavage FIN ──────────
// C'est le pavage de CHOIX : le N retenu (kNChoisi) est l'argmax de l'alpha
// OOS moyen ici. Les trois mesures sont figées — si un changement de moteur
// déplace la sélection, ce verrou le dit AVANT que les duels ne deviennent
// silencieusement incomparables. Recoupement inter-fichiers : la chaîne rend
// −6,8794 / 13 trades sur ce pavage (SprintChainFinePavingOosVerdictIsLocked).
TEST(SignalFamiliesIntegration, StructureMiniGridFinePavingSelectionIsLocked) {
    const auto wChaine = WalkForward(cfgChaineV2(), SWINGBOT_QQQ_CSV,
                                     kIsFin, kOosFin, kStepFin).run();
    ASSERT_EQ(wChaine.size(), 4u);
    std::cout << "  8s.3a — mini-grille structure (pavage fin IS=500/OOS=300, 4 fenetres)\n";
    imprime("chaine v2", wChaine);
    EXPECT_NEAR(meanOosAlpha(wChaine), -6.8794, 1e-2);
    EXPECT_EQ(tradesOos(wChaine).size(), 13u);

    const int ns[] = {10, 20, 55};
    double alphas[3] = {0.0, 0.0, 0.0};
    size_t trades[3] = {0, 0, 0};
    for (int i = 0; i < 3; ++i) {
        const auto w = WalkForward(cfgStructure(ns[i]), SWINGBOT_QQQ_CSV,
                                   kIsFin, kOosFin, kStepFin).run();
        ASSERT_EQ(w.size(), 4u);
        alphas[i] = meanOosAlpha(w);
        trades[i] = tradesOos(w).size();
        EXPECT_TRUE(std::isfinite(alphas[i]));
        std::cout << std::fixed << std::setprecision(4)
                  << "  N=" << ns[i] << " : alpha OOS moyen " << alphas[i]
                  << " pt, " << trades[i] << " trades OOS pooles\n";
    }

    // Mesures figées le 2026-07-03. N=20 et N=55 sont INERTES (identiques à
    // la chaîne : la structure ne tire jamais avant le trailing % 0,03 sur ce
    // pavage) ; N=10 tire (15 trades) et DÉGRADE (−7,04). La sélection est
    // donc plate ET dégénérée — l'argmax strict retient N=20.
    EXPECT_NEAR(alphas[0], -7.0435, 1e-2);
    EXPECT_NEAR(alphas[1], -6.8794, 1e-2);
    EXPECT_NEAR(alphas[2], -6.8794, 1e-2);
    EXPECT_EQ(trades[0], 15u);
    EXPECT_EQ(trades[1], 13u);
    EXPECT_EQ(trades[2], 13u);

    // Garde anti-dérive : kNChoisi est bien l'argmax mesuré (premier des
    // ex æquo — verrouillé pour que les duels restent comparables).
    int best = 0;
    for (int i = 1; i < 3; ++i)
        if (alphas[i] > alphas[best]) best = i;
    EXPECT_EQ(ns[best], kNChoisi);
}

// ─── 8s.3b — Mini-grille breakout M ∈ {20,55} × {remplace, s'ajoute} ─────────
// Décision utilisateur (2026-07-03) : les DEUX hypothèses de cohabitation avec
// la re-entrée 8.5 sont jugées sur le pavage fin — « s'ajoute » est presque un
// sous-ensemble de la re-entrée (delta attendu quasi nul), « remplace » teste
// vraiment « la force démontrée plutôt que l'état ». Les quatre mesures ET
// l'argmax sont figés.
TEST(SignalFamiliesIntegration, BreakoutMiniGridFinePavingSelectionIsLocked) {
    struct Combo { int m; bool remplace; const char* nom; };
    const Combo combos[] = {
        {20, false, "M=20 s'ajoute "},
        {55, false, "M=55 s'ajoute "},
        {20, true,  "M=20 remplace "},
        {55, true,  "M=55 remplace "},
    };
    double alphas[4] = {0.0, 0.0, 0.0, 0.0};
    size_t trades[4] = {0, 0, 0, 0};

    std::cout << "  8s.3b — mini-grille breakout (pavage fin IS=500/OOS=300, 4 fenetres)\n";
    for (int i = 0; i < 4; ++i) {
        const auto w = WalkForward(cfgBreakout(combos[i].m, combos[i].remplace),
                                   SWINGBOT_QQQ_CSV, kIsFin, kOosFin, kStepFin).run();
        ASSERT_EQ(w.size(), 4u);
        alphas[i] = meanOosAlpha(w);
        trades[i] = tradesOos(w).size();
        EXPECT_TRUE(std::isfinite(alphas[i]));
        std::cout << std::fixed << std::setprecision(4)
                  << "  " << combos[i].nom << ": alpha OOS moyen " << alphas[i]
                  << " pt, " << trades[i] << " trades OOS pooles\n";
    }

    // Mesures figées le 2026-07-03. « S'ajoute » (regimeReentry conservé) est
    // EXACTEMENT inerte — deltas nuls vs chaîne (−6,8794 / 13 trades), le
    // breakout est un sous-ensemble de la re-entrée comme anticipé à
    // l'ouverture. « Remplace » change le chemin (9 trades) et fait MIEUX que
    // la chaîne sur ce pavage : M=20 −6,6850 (+0,19), M=55 −6,7147 (+0,16).
    EXPECT_NEAR(alphas[0], -6.8794, 1e-2);
    EXPECT_NEAR(alphas[1], -6.8794, 1e-2);
    EXPECT_NEAR(alphas[2], -6.6850, 1e-2);
    EXPECT_NEAR(alphas[3], -6.7147, 1e-2);
    EXPECT_EQ(trades[0], 13u);
    EXPECT_EQ(trades[1], 13u);
    EXPECT_EQ(trades[2], 9u);
    EXPECT_EQ(trades[3], 9u);

    // Garde anti-dérive : (kMChoisi, kBreakoutRemplaceReentry) est bien
    // l'argmax mesuré.
    int best = 0;
    for (int i = 1; i < 4; ++i)
        if (alphas[i] > alphas[best]) best = i;
    EXPECT_EQ(combos[best].m, kMChoisi);
    EXPECT_EQ(combos[best].remplace, kBreakoutRemplaceReentry);
}

// ─── 8s.3c — VERDICT structure : duels sur les pavages NON-CHOISIS ───────────
// Recoupements inter-fichiers de la chaîne : −9,9023/11 trades (canonique,
// SprintChainVsBaselineOosVerdictIsLocked) et −12,8332/11 trades (décalé,
// CandidateShiftedPavingOosVerdictIsLocked).
TEST(SignalFamiliesIntegration, StructureCanonicalPavingOosVerdictIsLocked) {
    const auto wChaine = WalkForward(cfgChaineV2(), SWINGBOT_QQQ_CSV,
                                     kIsCan, kOosCan, kStepCan).run();
    const auto wStruct = WalkForward(cfgStructure(kNChoisi), SWINGBOT_QQQ_CSV,
                                     kIsCan, kOosCan, kStepCan).run();
    ASSERT_EQ(wChaine.size(), 2u);
    ASSERT_EQ(wStruct.size(), 2u);

    std::cout << "  8s.3c — duel structure, pavage canonique (IS=700/OOS=400, non-choisi)\n";
    imprime("chaine v2        ", wChaine);
    imprime("chaine + structure", wStruct);

    EXPECT_NEAR(meanOosAlpha(wChaine), -9.9023, 1e-2);
    EXPECT_EQ(tradesOos(wChaine).size(), 11u);

    // Verdict figé le 2026-07-03 : INERTE — mesures identiques à la chaîne
    // (même alpha au 1e-4, mêmes 11 trades) : à N=20 la cassure du plus bas
    // de 20 jours n'arrive jamais avant le trailing % 0,03. « ≥ chaîne » est
    // vrai par ÉGALITÉ, pas par amélioration — rien à adopter.
    EXPECT_NEAR(meanOosAlpha(wStruct), -9.9023, 1e-2);
    EXPECT_EQ(tradesOos(wStruct).size(), 11u);
}

TEST(SignalFamiliesIntegration, StructureShiftedPavingOosVerdictIsLocked) {
    const auto wChaine = WalkForward(cfgChaineV2(), SWINGBOT_QQQ_CSV,
                                     kIsDec, kOosDec, kStepDec, kOffsetDec).run();
    const auto wStruct = WalkForward(cfgStructure(kNChoisi), SWINGBOT_QQQ_CSV,
                                     kIsDec, kOosDec, kStepDec, kOffsetDec).run();
    ASSERT_EQ(wChaine.size(), 3u);
    ASSERT_EQ(wStruct.size(), 3u);

    std::cout << "  8s.3c — duel structure, pavage decale (IS=500/OOS=400, offset=90, non-choisi)\n";
    imprime("chaine v2        ", wChaine);
    imprime("chaine + structure", wStruct);

    EXPECT_NEAR(meanOosAlpha(wChaine), -12.8332, 1e-2);
    EXPECT_EQ(tradesOos(wChaine).size(), 11u);

    // Verdict figé le 2026-07-03 : INERTE ici aussi (identique à la chaîne,
    // mêmes 11 trades) — le mécanisme ne change RIEN sur les trois pavages.
    EXPECT_NEAR(meanOosAlpha(wStruct), -12.8332, 1e-2);
    EXPECT_EQ(tradesOos(wStruct).size(), 11u);
}

// ─── 8s.3d — VERDICT breakout : duels sur les pavages NON-CHOISIS ────────────
TEST(SignalFamiliesIntegration, BreakoutCanonicalPavingOosVerdictIsLocked) {
    const auto wChaine = WalkForward(cfgChaineV2(), SWINGBOT_QQQ_CSV,
                                     kIsCan, kOosCan, kStepCan).run();
    const auto wBreak  = WalkForward(cfgBreakout(kMChoisi, kBreakoutRemplaceReentry),
                                     SWINGBOT_QQQ_CSV, kIsCan, kOosCan, kStepCan).run();
    ASSERT_EQ(wChaine.size(), 2u);
    ASSERT_EQ(wBreak.size(),  2u);

    std::cout << "  8s.3d — duel breakout, pavage canonique (IS=700/OOS=400, non-choisi)\n";
    imprime("chaine v2       ", wChaine);
    imprime("chaine + breakout", wBreak);

    EXPECT_NEAR(meanOosAlpha(wChaine), -9.9023, 1e-2);
    EXPECT_EQ(tradesOos(wChaine).size(), 11u);

    // Verdict figé le 2026-07-03 : PIRE que la chaîne sur ce pavage
    // non-choisi — −11,75 (9 trades) vs −9,90 (11), soit −1,85 pt : le
    // +0,19 pt du pavage de sélection était du biais de sélection (D36).
    const double alphaBreak = meanOosAlpha(wBreak);
    EXPECT_NEAR(alphaBreak, -11.7526, 1e-2);
    EXPECT_EQ(tradesOos(wBreak).size(), 9u);
    EXPECT_LT(alphaBreak, meanOosAlpha(wChaine));  // l'acceptation échoue ICI
    EXPECT_LT(alphaBreak, 0.0);                    // et aucun edge absolu
}

TEST(SignalFamiliesIntegration, BreakoutShiftedPavingOosVerdictIsLocked) {
    const auto wChaine = WalkForward(cfgChaineV2(), SWINGBOT_QQQ_CSV,
                                     kIsDec, kOosDec, kStepDec, kOffsetDec).run();
    const auto wBreak  = WalkForward(cfgBreakout(kMChoisi, kBreakoutRemplaceReentry),
                                     SWINGBOT_QQQ_CSV, kIsDec, kOosDec, kStepDec,
                                     kOffsetDec).run();
    ASSERT_EQ(wChaine.size(), 3u);
    ASSERT_EQ(wBreak.size(),  3u);

    std::cout << "  8s.3d — duel breakout, pavage decale (IS=500/OOS=400, offset=90, non-choisi)\n";
    imprime("chaine v2       ", wChaine);
    imprime("chaine + breakout", wBreak);

    EXPECT_NEAR(meanOosAlpha(wChaine), -12.8332, 1e-2);
    EXPECT_EQ(tradesOos(wChaine).size(), 11u);

    // Verdict figé le 2026-07-03 : PIRE que la chaîne ici aussi — −14,26
    // (10 trades) vs −12,83 (11), soit −1,43 pt. L'acceptation 8s.3
    // (« ≥ chaîne sur LES pavages non-choisis ») échoue sur les DEUX pavages
    // → verdict global « AUCUNE amélioration » (résultat VALIDE — on
    // consigne, on n'adopte pas). Conséquence : le gate de la COMBINAISON
    // structure+breakout est FERMÉ (chaque mécanisme devait être ≥ chaîne ;
    // le breakout échoue seul, la structure est inerte — rien à combiner).
    const double alphaBreak = meanOosAlpha(wBreak);
    EXPECT_NEAR(alphaBreak, -14.2640, 1e-2);
    EXPECT_EQ(tradesOos(wBreak).size(), 10u);
    EXPECT_LT(alphaBreak, meanOosAlpha(wChaine));  // l'acceptation échoue aussi ici
    EXPECT_LT(alphaBreak, 0.0);                    // et aucun edge absolu
}
