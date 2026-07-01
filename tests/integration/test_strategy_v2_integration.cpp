// ─── Tests d'intégration : verdict OUT-OF-SAMPLE de la stratégie v2 ──────────
// Sprint 8, item 8.1 (filtre de régime SMA200, D26). On ne juge JAMAIS une modif
// de stratégie sur le plein échantillon (in-sample) : on la juge en OOS via le
// walk-forward (Sprint 7, WalkForward.hpp). Ce test compare, sur les MÊMES
// fenêtres, la config avec filtre de régime (smaTrendPeriod=200) à la base sans
// filtre (smaTrendPeriod=1 = filtre désactivé) et VERROUILLE le verdict mesuré.
//
// Acceptation ROADMAP 8.1 : « alpha net OOS > version actuelle ». La vérité
// mesurée ici est ce qui compte — y compris si elle est « pas d'amélioration ».
// Le harnais prouve l'edge OU son absence ; les deux sont des résultats valides.
#include <gtest/gtest.h>
#include <cmath>
#include <iostream>
#include "backtest/WalkForward.hpp"

using namespace trading;

namespace {

// Fenêtres dimensionnées pour la SMA200 : l'OOS doit rester tradable après le
// warmup de ~201 barres. IS=700 / OOS=400 / pas=400 → 2 fenêtres OOS contiguës
// sur les 1790 barres de QQQ (≈ 199 barres tradables par OOS).
constexpr size_t kIs   = 700;
constexpr size_t kOos  = 400;
constexpr size_t kStep = 400;

// ── Configs de référence des verdicts (préparation 8.2-8.5) ──────────────────
// Chaque verrou de verdict construit sa config EXPLICITEMENT, champ par champ :
// un verdict est une mesure HISTORIQUE, il ne doit pas bouger quand les défauts
// de SwingConfig évoluent (les items 8.2-8.5 adoptent leurs réglages comme
// nouveaux défauts au fil du sprint). cfg81() = l'état exact de la stratégie à
// la livraison de l'item 8.1 (défauts d'alors, D30 : prod ≡ défaut).
SwingConfig cfg81() {
    SwingConfig c;
    c.symbol          = "QQQ";
    c.emaFast         = 9;
    c.emaSlow         = 21;
    c.rsiPeriod       = 14;
    c.rsiBuyMax       = 55.0;
    c.rsiSellMin      = 70.0;
    c.stopLossPct     = 0.05;
    c.takeProfitPct   = 0.10;
    c.trailingStopPct = 0.03;
    c.riskPerTradePct = 0.02;
    c.minHoldDays     = 3;
    c.smaTrendPeriod  = 200;
    return c;
}

// cfg82() = chaîne retenue après l'item 8.2 : take-profit désactivé (adopté
// comme retrait d'une contrainte morte — delta strictement nul, voir le
// verdict 8.2 ci-dessous).
SwingConfig cfg82() {
    SwingConfig c = cfg81();
    c.takeProfitPct = 0.0;
    return c;
}

double meanOosAlpha(const std::vector<WfWindow>& w) {
    if (w.empty()) return 0.0;
    double s = 0.0;
    for (const auto& x : w) s += x.oos.alpha;
    return s / static_cast<double>(w.size());
}

// ── Agrégats sur trades OOS POOLÉS (préparation 8.2-8.5) ─────────────────────
// Les fenêtres OOS individuelles portent trop peu de trades (1-3) pour des
// ratios par fenêtre : profitFactor dégénère en sentinelle 999 sans perdant,
// avgWinPct vaut 0 sans gagnant. On agrège donc les trades de TOUTES les
// fenêtres OOS avant de calculer gain moyen / facteur de profit / espérance.
std::vector<TradeRecord> tradesOos(const std::vector<WfWindow>& w) {
    std::vector<TradeRecord> out;
    for (const auto& x : w)
        out.insert(out.end(), x.oos.trades.begin(), x.oos.trades.end());
    return out;
}

// Gain moyen (%) des trades gagnants ; 0 si aucun gagnant.
double gainMoyenGagnants(const std::vector<TradeRecord>& t) {
    double s = 0.0; int n = 0;
    for (const auto& tr : t) if (tr.isWin) { s += tr.pnlPct; ++n; }
    return n ? s / n : 0.0;
}

// Facteur de profit = Σ gains / Σ |pertes| ; sentinelle 999 si aucune perte.
double facteurProfit(const std::vector<TradeRecord>& t) {
    double gains = 0.0, pertes = 0.0;
    for (const auto& tr : t) (tr.pnl >= 0 ? gains : pertes) += tr.pnl;
    if (pertes == 0.0) return gains > 0.0 ? 999.0 : 0.0;
    return gains / -pertes;
}

// Espérance par trade en dollars ; 0 si aucun trade.
double esperanceParTrade(const std::vector<TradeRecord>& t) {
    if (t.empty()) return 0.0;
    double s = 0.0;
    for (const auto& tr : t) s += tr.pnl;
    return s / static_cast<double>(t.size());
}

// Moyenne d'un champ de BacktestResult sur les fenêtres OOS (alpha,
// pctTimeInvested…).
double moyenneOos(const std::vector<WfWindow>& w, double BacktestResult::*champ) {
    if (w.empty()) return 0.0;
    double s = 0.0;
    for (const auto& x : w) s += x.oos.*champ;
    return s / static_cast<double>(w.size());
}

} // namespace

// La régime-config et la base produisent le MÊME pavage de fenêtres (mêmes
// bornes IS/OOS) — condition d'une comparaison honnête.
TEST(StrategyV2Integration, RegimeAndBaselineShareWindowTiling) {
    SwingConfig regime = cfg81();                  // smaTrendPeriod = 200
    SwingConfig base   = cfg81(); base.smaTrendPeriod = 1;  // filtre off

    const auto wr = WalkForward(regime, SWINGBOT_QQQ_CSV, kIs, kOos, kStep).run();
    const auto wb = WalkForward(base,   SWINGBOT_QQQ_CSV, kIs, kOos, kStep).run();

    ASSERT_EQ(wr.size(), 2u);
    ASSERT_EQ(wb.size(), wr.size());
    for (size_t i = 0; i < wr.size(); ++i) {
        EXPECT_EQ(wr[i].oosStart, wb[i].oosStart);
        EXPECT_EQ(wr[i].oosEnd,   wb[i].oosEnd);
        EXPECT_EQ(wr[i].isEnd - wr[i].isStart, kIs);
        EXPECT_EQ(wr[i].oosEnd - wr[i].oosStart, kOos);
    }
}

// Verdict OOS verrouillé (item 8.1). Mesure l'alpha OOS moyen du filtre de régime
// et de la base, et fige la relation. RÉSULTAT HONNÊTE : sur QQQ (quasi
// exclusivement haussier 2019-2026), le filtre de régime n'AMÉLIORE PAS l'alpha
// OOS — il reste plus souvent hors marché dans un actif qui monte. C'est le point
// de départ chiffré des items 8.2-8.5 (laisser courir, entrer sur la force,
// réduire le cash drag), pas une raison de déployer.
TEST(StrategyV2Integration, RegimeFilterOosVerdictIsLocked) {
    SwingConfig regime = cfg81();
    SwingConfig base   = cfg81(); base.smaTrendPeriod = 1;

    const auto wr = WalkForward(regime, SWINGBOT_QQQ_CSV, kIs, kOos, kStep).run();
    const auto wb = WalkForward(base,   SWINGBOT_QQQ_CSV, kIs, kOos, kStep).run();
    ASSERT_EQ(wr.size(), 2u);
    ASSERT_EQ(wb.size(), 2u);

    const double regimeOos = meanOosAlpha(wr);
    const double baseOos   = meanOosAlpha(wb);

    std::cout << std::fixed << std::setprecision(4)
              << "  ALPHA OOS MOYEN (pts vs B&H, 2 fenetres QQQ)\n"
              << "    regime (SMA200) : " << regimeOos << "\n"
              << "    base   (off)    : " << baseOos   << "\n"
              << "    delta regime-base : " << (regimeOos - baseOos) << "\n";

    // Métriques finies (pas de NaN/inf).
    for (const auto& x : wr) EXPECT_TRUE(std::isfinite(x.oos.alpha));
    for (const auto& x : wb) EXPECT_TRUE(std::isfinite(x.oos.alpha));

    // VERDICT 8.1 (figé). Le filtre de régime AMÉLIORE l'alpha OOS vs la base :
    // −14,10 pts > −16,09 pts (+1,99 pt). Acceptation ROADMAP 8.1 satisfaite
    // (« alpha net OOS > version actuelle ») — alors même que le plein échantillon
    // semblait DÉGRADÉ : c'est précisément pourquoi on juge en OOS, pas en IS.
    // Les deux restent NÉGATIFS : aucune ne bat le Buy & Hold → pas d'edge, pas de
    // déploiement. La suite (8.2-8.5) doit transformer ce « moins mauvais » en alpha.
    EXPECT_GT(regimeOos, baseOos);                 // le filtre aide en OOS
    EXPECT_NEAR(regimeOos, -14.1015, 1e-2);        // golden de mesure
    EXPECT_NEAR(baseOos,   -16.0889, 1e-2);
    EXPECT_LT(regimeOos, 0.0);                      // ne bat toujours pas le B&H
}

// Verdict OOS de l'item 8.2 (take-profit désactivé, D26/T1). Acceptation
// ROADMAP : « gain moyen des gagnants ↑ sans dégrader le profit factor net ».
// Base = cfg81 (chaîne cumulative : 8.1 retenu) ; variante = cfg81 + TP off.
//
// VERDICT MESURÉ (figé) : les 2 fenêtres OOS ne portent AUCUN trade sous la
// config 8.1 — l'alpha −14,10 pts est du pur cash drag (jamais en position).
// Gain des gagnants et facteur de profit sont donc INDÉCIDABLES sur cet
// échantillon, et TP off est un delta STRICTEMENT NUL (le take-profit ne se
// déclenche nulle part : 0 TP aussi sur le plein échantillon — golden).
// Décision (politique d'adoption du sprint) : contrainte MORTE retirée
// (défaut takeProfitPct → 0), ré-examinée sur la chaîne finale en fin de
// sprint quand 8.3/8.5 auront rendu l'échantillon de trades non vide.
TEST(StrategyV2Integration, TakeProfitOffOosVerdictIsLocked) {
    SwingConfig base    = cfg81();
    SwingConfig variant = cfg81(); variant.takeProfitPct = 0.0;  // TP désactivé

    const auto wb = WalkForward(base,    SWINGBOT_QQQ_CSV, kIs, kOos, kStep).run();
    const auto wv = WalkForward(variant, SWINGBOT_QQQ_CSV, kIs, kOos, kStep).run();
    ASSERT_EQ(wb.size(), 2u);
    ASSERT_EQ(wv.size(), 2u);

    const auto tb = tradesOos(wb);
    const auto tv = tradesOos(wv);
    const double alphaB = meanOosAlpha(wb), alphaV = meanOosAlpha(wv);

    std::cout << std::fixed << std::setprecision(4)
              << "  ITEM 8.2 — TAKE-PROFIT OFF (trades OOS pooles, 2 fenetres QQQ)\n"
              << "    trades          : base " << tb.size() << " / variante " << tv.size() << "\n"
              << "    alpha OOS moyen : base " << alphaB << " / variante "  << alphaV << "\n";

    for (const auto& x : wv) EXPECT_TRUE(std::isfinite(x.oos.alpha));

    // Vérité mesurée : zéro trade OOS de part et d'autre → TP off ne change
    // RIEN en OOS (equity au bit près). Si un de ces verrous casse un jour,
    // l'échantillon est devenu non vide : re-juger 8.2 pour de vrai.
    EXPECT_EQ(tb.size(), 0u);
    EXPECT_EQ(tv.size(), 0u);
    EXPECT_DOUBLE_EQ(alphaV, alphaB);
    EXPECT_NEAR(alphaB, -14.1015, 1e-2);   // cohérent avec le verdict 8.1
}

// Verdict OOS de l'item 8.3 (entrée sur la force, D26/T3). Acceptation
// ROADMAP : « nombre de trades et exposition ↑, expectancy nette ≥ 0 ».
// Base = cfg82 (chaîne : 8.1 + 8.2 retenus) ; variante = cfg82 + plafond
// RSI d'achat désactivé (rsiBuyMax = 100).
//
// VERDICT MESURÉ (figé) : ACCEPTATION SATISFAITE sur les 3 critères —
// trades OOS 0 → 5, exposition 0 → 22,11 %, espérance +2,97 $/trade.
// L'alpha OOS reste ~inchangé (−14,10 → −14,12) : entrer sur la force met
// ENFIN la stratégie en position en OOS sans détruire de valeur — le gain
// d'alpha est attendu de 8.5 (rester investi), pas de l'entrée seule.
// Défaut adopté : rsiBuyMax → 100 (plafond désactivé).
TEST(StrategyV2Integration, RsiEntryCapOffOosVerdictIsLocked) {
    SwingConfig base    = cfg82();
    SwingConfig variant = cfg82(); variant.rsiBuyMax = 100.0;  // plafond off

    const auto wb = WalkForward(base,    SWINGBOT_QQQ_CSV, kIs, kOos, kStep).run();
    const auto wv = WalkForward(variant, SWINGBOT_QQQ_CSV, kIs, kOos, kStep).run();
    ASSERT_EQ(wb.size(), 2u);
    ASSERT_EQ(wv.size(), 2u);

    const auto tb = tradesOos(wb);
    const auto tv = tradesOos(wv);
    const double expoB  = moyenneOos(wb, &BacktestResult::pctTimeInvested);
    const double expoV  = moyenneOos(wv, &BacktestResult::pctTimeInvested);
    const double espV   = esperanceParTrade(tv);
    const double alphaB = meanOosAlpha(wb), alphaV = meanOosAlpha(wv);

    std::cout << std::fixed << std::setprecision(4)
              << "  ITEM 8.3 — PLAFOND RSI ACHAT OFF (trades OOS pooles, 2 fenetres QQQ)\n"
              << "    trades              : base " << tb.size() << " / variante " << tv.size() << "\n"
              << "    % temps investi     : base " << expoB << " / variante " << expoV << "\n"
              << "    esperance/trade ($) : variante " << espV << "\n"
              << "    alpha OOS moyen     : base " << alphaB << " / variante " << alphaV << "\n";

    for (const auto& x : wv) EXPECT_TRUE(std::isfinite(x.oos.alpha));

    EXPECT_EQ(tb.size(), 0u);              // la base ne trade toujours pas en OOS
    EXPECT_EQ(tv.size(), 5u);              // trades ↑ (0 → 5)
    EXPECT_GT(expoV, expoB);               // exposition ↑
    EXPECT_NEAR(expoV, 22.1106, 1e-2);     // golden de mesure
    EXPECT_GE(espV, 0.0);                  // expectancy nette ≥ 0
    EXPECT_NEAR(espV, 2.9667, 1e-2);
    EXPECT_NEAR(alphaV, -14.1234, 1e-2);   // alpha ~inchangé vs base (−14,1015)
}
