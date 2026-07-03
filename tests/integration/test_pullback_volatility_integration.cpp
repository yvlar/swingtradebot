// ─── Tests d'intégration : verdicts OOS de la 3e famille de signaux (Sprint 8-sexies) ─
// Item 8y.3. Trois axes sont soldés (paramètres — 8-ter, trailing — 8-quater,
// structure/breakout — 8-quinquies) : la recherche d'edge continue par la piste
// JAMAIS testée — acheter la FAIBLESSE dans la tendance (item 8y.1,
// entryPullbackRsiMax : l'inverse exact du breakout 8s.2) et filtrer les
// entrées par la VOLATILITÉ (item 8y.2, entryMaxAtrPct : ATR(14)/clôture).
// Protocole (modèle test_signal_families_integration.cpp, leçon 8-ter : tout
// gagnant est jugé sur des fenêtres NON-CHOISIES) :
//   1. mini-grilles CHOISIES sur le pavage FIN (500/300) — RSI ∈ {30, 40, 50}
//      × {remplace, s'ajoute} pour le pullback (mêmes hypothèses de
//      cohabitation avec la re-entrée 8.5 que le breakout : masquage PARTIEL
//      anticipé, pas sous-ensemble — le pullback peut tirer SOUS les EMAs) ;
//      seuil ∈ {0,010, 0,015, 0,025} pour le filtre ATR appliqué à la chaîne
//      (cran bas 0,010 ajouté par décision utilisateur d'ouverture 2026-07-03
//      pour réduire le risque d'inertie totale D41 si les fenêtres sont calmes) ;
//   2. duels chaîne v2 vs chaîne + mécanisme (réglage choisi) sur les pavages
//      CANONIQUE (700/400) et DÉCALÉ (500/400, offset 90) — que les
//      mini-grilles n'ont pas vus. Acceptation ROADMAP : alpha OOS ≥ chaîne
//      sur ces pavages-là, sinon « pas d'amélioration » (résultat VALIDE) ;
//   3. la COMBINAISON des deux mécanismes n'est jugée que si chacun est ≥
//      chaîne sur les deux pavages non-choisis (gate 8y.3).
// Discipline : configs EXPLICITES champ par champ (D33), nombre de trades OOS
// poolés figé (D34 — et c'est lui qui distingue « inerte » d'« équivalent »,
// leçon D41), mesures ET argmax des mini-grilles figés (garde anti-dérive,
// leçon 8-quater), deltas chiffrés consignés. Aucun de ces verrous n'autorise
// à lui seul une adoption (gate 8y.4 = décision utilisateur).
// VERDICT GLOBAL MESURÉ (figé le 2026-07-03) : PREMIÈRE AMÉLIORATION ROBUSTE
// depuis que le protocole existe —
//   • pullback : RSI ≤ 40 « s'ajoute » (argmax fin, +0,33 pt) fait MIEUX que
//     la chaîne sur les DEUX pavages non-choisis : canonique −9,49 vs −9,90
//     (+0,42 pt, 12 trades vs 11) et décalé −11,32 vs −12,83 (+1,52 pt).
//     L'acceptation (≥ chaîne sur les deux pavages non-choisis) PASSE — pour
//     la première fois (le breakout 8s.3 et le trailing ATR 8q.2 avaient
//     échoué ici). ATTENTION : l'alpha reste NÉGATIF partout (aucun edge
//     absolu, la DoD « battre le B&H » n'est PAS atteinte) — l'adoption
//     éventuelle comme défaut est une décision utilisateur (gate 8y.4).
//   • filtre ATR : seuil 0,015 (argmax fin, +0,40 pt) NE GÉNÉRALISE PAS —
//     canonique −11,53 vs −9,90 (−1,63 pt), décalé −13,33 vs −12,83
//     (−0,50 pt) : biais de sélection (D36), même verdict que le breakout.
//   • combinaison : gate FERMÉ (le filtre ATR échoue seul — la règle exige
//     que CHAQUE mécanisme soit ≥ chaîne sur les deux pavages non-choisis).
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
//   • pullback : RSI ≤ 40 en variante « S'AJOUTE » (−6,5491 vs chaîne
//     −6,8794, +0,33 pt) — l'INVERSE du breakout 8s.3 (où « s'ajoute » était
//     inerte) : le pullback n'est PAS un sous-ensemble de la re-entrée, il
//     tire sous les EMAs (mêmes 13 trades que la chaîne mais alphas
//     différents = chemins d'entrée réellement modifiés, D41 vérifiée par
//     les deltas, pas par les comptes). « Remplace » DÉGRADE (−8,08/−8,95 à
//     seuil 30/40) : le creux seul entre trop tard dans le cycle.
//   • filtre ATR : seuil 0,015 (−6,4838, +0,40 pt, 6 trades vs 13 — le
//     filtre est ACTIF, il bloque réellement des entrées) ; 0,010 sur-bloque
//     (1 seul trade, −8,32), 0,025 est quasi transparent (12 trades).
constexpr double kRsiChoisi = 40.0;                // entrée pullback (seuil RSI)
constexpr bool   kPullbackRemplaceReentry = false; // variante retenue : « s'ajoute »
constexpr double kAtrChoisi = 0.015;               // filtre volatilité (seuil ATR/close)

// ── Config de la chaîne v2, EXPLICITE champ par champ (règle D33) ────────────
// Snapshot volontairement dupliqué des autres fichiers de verdict : une mesure
// historique ne doit pas bouger si les défauts de SwingConfig évoluent (en
// particulier si 8y.4 adopte un des mécanismes comme défaut).
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
    c.entryPullbackRsiMax     = 0.0;     // 8y.1 : entrée pullback désactivée
    c.entryMaxAtrPct          = 0.0;     // 8y.2 : filtre volatilité désactivé
    return c;
}

// Chaîne + entrée pullback. Deux hypothèses de cohabitation avec la
// re-entrée 8.5 (mêmes que le breakout 8s.3, décision utilisateur
// 2026-07-03) :
//   • « s'ajoute »  : regimeReentry reste true — masquage PARTIEL attendu
//     (la re-entrée couvre déjà « prix > EMAs » ; le pullback n'ajoute que
//     les creux SOUS les EMAs) ;
//   • « remplace »  : regimeReentry=false — l'entrée hors-croisement exige
//     le creux (RSI ≤ seuil), pas le seul état « régime up + prix > EMAs ».
SwingConfig cfgPullback(double rsiMax, bool remplaceReentry) {
    SwingConfig c = cfgChaineV2();
    c.entryPullbackRsiMax = rsiMax;
    c.regimeReentry       = !remplaceReentry;
    return c;
}

// Chaîne + filtre de volatilité : seul entryMaxAtrPct diffère (toutes les
// entrées de la chaîne — croisement et re-entrée — sont soumises au filtre).
SwingConfig cfgAtrFilter(double maxAtrPct) {
    SwingConfig c = cfgChaineV2();
    c.entryMaxAtrPct = maxAtrPct;
    return c;
}

double meanOosAlpha(const std::vector<WfWindow>& w) {
    if (w.empty()) return 0.0;
    double s = 0.0;
    for (const auto& x : w) s += x.oos.alpha;
    return s / static_cast<double>(w.size());
}

// Trades OOS POOLÉS sur toutes les fenêtres (D34 : chaque verdict fige aussi
// son NOMBRE de trades — un alpha OOS sans trades ne juge que le cash drag,
// et mêmes alphas + mêmes trades = mécanisme INERTE, D41).
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

// ─── 8y.3a — Mini-grille pullback RSI ∈ {30,40,50} × {remplace, s'ajoute} ────
// C'est le pavage de CHOIX : le réglage retenu est l'argmax de l'alpha OOS
// moyen ici. Les six mesures ET l'argmax sont figés — si un changement de
// moteur déplace la sélection, ce verrou le dit AVANT que les duels ne
// deviennent silencieusement incomparables. Recoupement inter-fichiers : la
// chaîne rend −6,8794 / 13 trades sur ce pavage
// (SprintChainFinePavingOosVerdictIsLocked).
TEST(PullbackVolatilityIntegration, PullbackMiniGridFinePavingSelectionIsLocked) {
    const auto wChaine = WalkForward(cfgChaineV2(), SWINGBOT_QQQ_CSV,
                                     kIsFin, kOosFin, kStepFin).run();
    ASSERT_EQ(wChaine.size(), 4u);
    std::cout << "  8y.3a — mini-grille pullback (pavage fin IS=500/OOS=300, 4 fenetres)\n";
    imprime("chaine v2", wChaine);
    EXPECT_NEAR(meanOosAlpha(wChaine), -6.8794, 1e-2);
    EXPECT_EQ(tradesOos(wChaine).size(), 13u);

    struct Combo { double rsiMax; bool remplace; const char* nom; };
    const Combo combos[] = {
        {30.0, false, "RSI<=30 s'ajoute "},
        {40.0, false, "RSI<=40 s'ajoute "},
        {50.0, false, "RSI<=50 s'ajoute "},
        {30.0, true,  "RSI<=30 remplace "},
        {40.0, true,  "RSI<=40 remplace "},
        {50.0, true,  "RSI<=50 remplace "},
    };
    double alphas[6] = {0, 0, 0, 0, 0, 0};
    size_t trades[6] = {0, 0, 0, 0, 0, 0};
    for (int i = 0; i < 6; ++i) {
        const auto w = WalkForward(cfgPullback(combos[i].rsiMax, combos[i].remplace),
                                   SWINGBOT_QQQ_CSV, kIsFin, kOosFin, kStepFin).run();
        ASSERT_EQ(w.size(), 4u);
        alphas[i] = meanOosAlpha(w);
        trades[i] = tradesOos(w).size();
        EXPECT_TRUE(std::isfinite(alphas[i]));
        std::cout << std::fixed << std::setprecision(4)
                  << "  " << combos[i].nom << ": alpha OOS moyen " << alphas[i]
                  << " pt, " << trades[i] << " trades OOS pooles\n";
    }

    // Mesures figées le 2026-07-03. « S'ajoute » n'est PAS inerte (contraste
    // avec le breakout 8s.3) : à seuil 30/40 les COMPTES égalent la chaîne
    // (13) mais les alphas diffèrent — mêmes nombres, autres trades (entrées
    // avancées au creux) ; à seuil 50 le pullback sur-achète (19 trades,
    // −7,29, dégrade). « Remplace » dégrade partout sauf 50 (−6,93 < chaîne) :
    // sans la re-entrée, attendre le creux fait rater le gros de la tendance.
    EXPECT_NEAR(alphas[0], -6.5713, 1e-2);
    EXPECT_NEAR(alphas[1], -6.5491, 1e-2);
    EXPECT_NEAR(alphas[2], -7.2903, 1e-2);
    EXPECT_NEAR(alphas[3], -8.0750, 1e-2);
    EXPECT_NEAR(alphas[4], -8.9510, 1e-2);
    EXPECT_NEAR(alphas[5], -6.9295, 1e-2);
    EXPECT_EQ(trades[0], 13u);
    EXPECT_EQ(trades[1], 13u);
    EXPECT_EQ(trades[2], 19u);
    EXPECT_EQ(trades[3], 6u);
    EXPECT_EQ(trades[4], 8u);
    EXPECT_EQ(trades[5], 15u);

    // Garde anti-dérive : (kRsiChoisi, kPullbackRemplaceReentry) est bien
    // l'argmax mesuré (premier des ex æquo si la sélection est plate —
    // verrouillé pour que les duels restent comparables, leçon D41).
    int best = 0;
    for (int i = 1; i < 6; ++i)
        if (alphas[i] > alphas[best]) best = i;
    EXPECT_DOUBLE_EQ(combos[best].rsiMax, kRsiChoisi);
    EXPECT_EQ(combos[best].remplace, kPullbackRemplaceReentry);
}

// ─── 8y.3b — Mini-grille filtre ATR ∈ {0,010, 0,015, 0,025} sur la chaîne ────
// Le filtre PEUT être inerte si les fenêtres OOS sont calmes (D41) : chaque
// ligne est COMPARÉE à la chaîne — mêmes alphas ET mêmes comptes de trades =
// inertie, à documenter dans ce verrou même. Le compte de trades est la
// mesure d'ACTIVATION (moins de trades que la chaîne = des entrées ont été
// réellement bloquées).
TEST(PullbackVolatilityIntegration, AtrFilterMiniGridFinePavingSelectionIsLocked) {
    const auto wChaine = WalkForward(cfgChaineV2(), SWINGBOT_QQQ_CSV,
                                     kIsFin, kOosFin, kStepFin).run();
    ASSERT_EQ(wChaine.size(), 4u);
    std::cout << "  8y.3b — mini-grille filtre ATR (pavage fin IS=500/OOS=300, 4 fenetres)\n";
    imprime("chaine v2", wChaine);
    EXPECT_NEAR(meanOosAlpha(wChaine), -6.8794, 1e-2);
    EXPECT_EQ(tradesOos(wChaine).size(), 13u);

    const double seuils[] = {0.010, 0.015, 0.025};
    double alphas[3] = {0, 0, 0};
    size_t trades[3] = {0, 0, 0};
    for (int i = 0; i < 3; ++i) {
        const auto w = WalkForward(cfgAtrFilter(seuils[i]), SWINGBOT_QQQ_CSV,
                                   kIsFin, kOosFin, kStepFin).run();
        ASSERT_EQ(w.size(), 4u);
        alphas[i] = meanOosAlpha(w);
        trades[i] = tradesOos(w).size();
        EXPECT_TRUE(std::isfinite(alphas[i]));
        std::cout << std::fixed << std::setprecision(4)
                  << "  ATR<=" << seuils[i] << " : alpha OOS moyen " << alphas[i]
                  << " pt, " << trades[i] << " trades OOS pooles\n";
    }

    // Mesures figées le 2026-07-03. Le filtre est ACTIF sur les trois crans
    // (D41 : comptes de trades ≠ chaîne partout — pas d'inertie à documenter,
    // le cran bas ajouté à l'ouverture n'était finalement pas nécessaire à
    // l'activation). 0,010 SUR-bloque (1 seul trade OOS : quasi tout le
    // pavage est au-dessus de 1 % d'ATR — cash drag pur, −8,32) ; 0,015
    // garde 6 trades et fait mieux que la chaîne (+0,40 pt) ; 0,025 est
    // quasi transparent (12 trades sur 13, −6,57).
    EXPECT_NEAR(alphas[0], -8.3209, 1e-2);
    EXPECT_NEAR(alphas[1], -6.4838, 1e-2);
    EXPECT_NEAR(alphas[2], -6.5651, 1e-2);
    EXPECT_EQ(trades[0], 1u);
    EXPECT_EQ(trades[1], 6u);
    EXPECT_EQ(trades[2], 12u);

    // Garde anti-dérive : kAtrChoisi est bien l'argmax mesuré (premier des
    // ex æquo si la sélection est plate).
    int best = 0;
    for (int i = 1; i < 3; ++i)
        if (alphas[i] > alphas[best]) best = i;
    EXPECT_DOUBLE_EQ(seuils[best], kAtrChoisi);
}

// ─── 8y.3c — VERDICT pullback : duels sur les pavages NON-CHOISIS ────────────
// Recoupements inter-fichiers de la chaîne : −9,9023/11 trades (canonique,
// SprintChainVsBaselineOosVerdictIsLocked) et −12,8332/11 trades (décalé,
// CandidateShiftedPavingOosVerdictIsLocked).
TEST(PullbackVolatilityIntegration, PullbackCanonicalPavingOosVerdictIsLocked) {
    const auto wChaine = WalkForward(cfgChaineV2(), SWINGBOT_QQQ_CSV,
                                     kIsCan, kOosCan, kStepCan).run();
    const auto wPull   = WalkForward(cfgPullback(kRsiChoisi, kPullbackRemplaceReentry),
                                     SWINGBOT_QQQ_CSV, kIsCan, kOosCan, kStepCan).run();
    ASSERT_EQ(wChaine.size(), 2u);
    ASSERT_EQ(wPull.size(),   2u);

    std::cout << "  8y.3c — duel pullback, pavage canonique (IS=700/OOS=400, non-choisi)\n";
    imprime("chaine v2       ", wChaine);
    imprime("chaine + pullback", wPull);

    EXPECT_NEAR(meanOosAlpha(wChaine), -9.9023, 1e-2);
    EXPECT_EQ(tradesOos(wChaine).size(), 11u);

    // Verdict figé le 2026-07-03 : MIEUX que la chaîne sur ce pavage
    // non-choisi — −9,49 (12 trades) vs −9,90 (11), soit +0,42 pt :
    // l'acceptation « ≥ chaîne » PASSE ici (1er des 2 pavages requis).
    // L'alpha reste négatif : amélioration relative, pas edge absolu.
    const double alphaPull = meanOosAlpha(wPull);
    EXPECT_NEAR(alphaPull, -9.4861, 1e-2);
    EXPECT_EQ(tradesOos(wPull).size(), 12u);
    EXPECT_GT(alphaPull, meanOosAlpha(wChaine));   // l'acceptation passe ICI
    EXPECT_LT(alphaPull, 0.0);                     // mais aucun edge absolu
}

TEST(PullbackVolatilityIntegration, PullbackShiftedPavingOosVerdictIsLocked) {
    const auto wChaine = WalkForward(cfgChaineV2(), SWINGBOT_QQQ_CSV,
                                     kIsDec, kOosDec, kStepDec, kOffsetDec).run();
    const auto wPull   = WalkForward(cfgPullback(kRsiChoisi, kPullbackRemplaceReentry),
                                     SWINGBOT_QQQ_CSV, kIsDec, kOosDec, kStepDec,
                                     kOffsetDec).run();
    ASSERT_EQ(wChaine.size(), 3u);
    ASSERT_EQ(wPull.size(),   3u);

    std::cout << "  8y.3c — duel pullback, pavage decale (IS=500/OOS=400, offset=90, non-choisi)\n";
    imprime("chaine v2       ", wChaine);
    imprime("chaine + pullback", wPull);

    EXPECT_NEAR(meanOosAlpha(wChaine), -12.8332, 1e-2);
    EXPECT_EQ(tradesOos(wChaine).size(), 11u);

    // Verdict figé le 2026-07-03 : MIEUX que la chaîne ici aussi — −11,32
    // (12 trades) vs −12,83 (11), soit +1,52 pt. L'acceptation 8y.3
    // (« ≥ chaîne sur LES pavages non-choisis ») PASSE sur les DEUX pavages
    // → PREMIÈRE amélioration robuste du protocole (fin +0,33, canonique
    // +0,42, décalé +1,52 — le signe ne s'inverse sur aucun pavage, contraste
    // avec le candidat 8-ter et le breakout 8s.3). L'alpha reste négatif
    // partout : PAS d'edge absolu, la décision d'adoption appartient au gate
    // 8y.4 (décision utilisateur).
    const double alphaPull = meanOosAlpha(wPull);
    EXPECT_NEAR(alphaPull, -11.3159, 1e-2);
    EXPECT_EQ(tradesOos(wPull).size(), 12u);
    EXPECT_GT(alphaPull, meanOosAlpha(wChaine));   // l'acceptation passe AUSSI ici
    EXPECT_LT(alphaPull, 0.0);                     // mais aucun edge absolu
}

// ─── 8y.3d — VERDICT filtre ATR : duels sur les pavages NON-CHOISIS ──────────
TEST(PullbackVolatilityIntegration, AtrFilterCanonicalPavingOosVerdictIsLocked) {
    const auto wChaine = WalkForward(cfgChaineV2(), SWINGBOT_QQQ_CSV,
                                     kIsCan, kOosCan, kStepCan).run();
    const auto wAtr    = WalkForward(cfgAtrFilter(kAtrChoisi), SWINGBOT_QQQ_CSV,
                                     kIsCan, kOosCan, kStepCan).run();
    ASSERT_EQ(wChaine.size(), 2u);
    ASSERT_EQ(wAtr.size(),    2u);

    std::cout << "  8y.3d — duel filtre ATR, pavage canonique (IS=700/OOS=400, non-choisi)\n";
    imprime("chaine v2        ", wChaine);
    imprime("chaine + filtre ATR", wAtr);

    EXPECT_NEAR(meanOosAlpha(wChaine), -9.9023, 1e-2);
    EXPECT_EQ(tradesOos(wChaine).size(), 11u);

    // Verdict figé le 2026-07-03 : PIRE que la chaîne sur ce pavage
    // non-choisi — −11,53 (6 trades) vs −9,90 (11), soit −1,63 pt : le
    // +0,40 pt du pavage de sélection était du biais de sélection (D36),
    // même mécanisme d'échec que le breakout 8s.3. Le filtre est bien ACTIF
    // (6 trades vs 11 — il bloque), mais il bloque les MAUVAISES entrées
    // sur ce pavage.
    const double alphaAtr = meanOosAlpha(wAtr);
    EXPECT_NEAR(alphaAtr, -11.5294, 1e-2);
    EXPECT_EQ(tradesOos(wAtr).size(), 6u);
    EXPECT_LT(alphaAtr, meanOosAlpha(wChaine));    // l'acceptation échoue ICI
    EXPECT_LT(alphaAtr, 0.0);                      // et aucun edge absolu
}

TEST(PullbackVolatilityIntegration, AtrFilterShiftedPavingOosVerdictIsLocked) {
    const auto wChaine = WalkForward(cfgChaineV2(), SWINGBOT_QQQ_CSV,
                                     kIsDec, kOosDec, kStepDec, kOffsetDec).run();
    const auto wAtr    = WalkForward(cfgAtrFilter(kAtrChoisi), SWINGBOT_QQQ_CSV,
                                     kIsDec, kOosDec, kStepDec, kOffsetDec).run();
    ASSERT_EQ(wChaine.size(), 3u);
    ASSERT_EQ(wAtr.size(),    3u);

    std::cout << "  8y.3d — duel filtre ATR, pavage decale (IS=500/OOS=400, offset=90, non-choisi)\n";
    imprime("chaine v2        ", wChaine);
    imprime("chaine + filtre ATR", wAtr);

    EXPECT_NEAR(meanOosAlpha(wChaine), -12.8332, 1e-2);
    EXPECT_EQ(tradesOos(wChaine).size(), 11u);

    // Verdict figé le 2026-07-03 : PIRE que la chaîne ici aussi — −13,33
    // (9 trades) vs −12,83 (11), soit −0,50 pt. L'acceptation échoue sur les
    // DEUX pavages non-choisis → verdict filtre ATR « PAS d'amélioration »
    // (résultat VALIDE — on consigne, on n'adopte pas). Conséquence : le
    // gate de la COMBINAISON pullback+filtre est FERMÉ (chaque mécanisme
    // devait être ≥ chaîne sur les deux pavages ; le filtre échoue seul).
    // Le pullback, lui, passe SEUL — son sort relève du gate 8y.4.
    const double alphaAtr = meanOosAlpha(wAtr);
    EXPECT_NEAR(alphaAtr, -13.3341, 1e-2);
    EXPECT_EQ(tradesOos(wAtr).size(), 9u);
    EXPECT_LT(alphaAtr, meanOosAlpha(wChaine));    // l'acceptation échoue aussi ici
    EXPECT_LT(alphaAtr, 0.0);                      // et aucun edge absolu
}
