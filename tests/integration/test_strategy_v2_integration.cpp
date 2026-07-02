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

// ── Pavage FIN (Sprint 8-bis, item 8b.3, D34) ────────────────────────────────
// Les verdicts 8.x reposent sur 2 fenêtres OOS — fragile statistiquement (D34 :
// un verdict OOS sans trades ne juge que le temps en cash). Ce pavage fin donne
// 4 fenêtres OOS contiguës (s = 0, 300, 600, 900 sur 1790 barres).
// OOS=300 et non l'exemple littéral de la ROADMAP (250) : chaque fenêtre
// ré-amorce ses indicateurs avec un warmup local de ~201 barres (SMA200,
// BackTester.hpp) — avec 250 il ne resterait que ~49 barres tradables par OOS
// (verdict quasi pur cash drag, le piège D34 justement) ; avec 300 on en a ~99.
// Décision utilisateur d'ouverture du Sprint 8-bis (2026-07-02).
constexpr size_t kIsFin   = 500;
constexpr size_t kOosFin  = 300;
constexpr size_t kStepFin = 300;

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
    // Flags nés APRÈS 8.1 : explicitement à leur valeur « comportement 8.1 »
    // (les défauts ont bougé depuis — c'est tout l'objet de ce snapshot).
    c.rsiSellOnlyIfRegimeDown = false;   // item 8.4
    c.regimeReentry           = false;   // item 8.5
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

// cfg83() = chaîne retenue après l'item 8.3 : entrée sur la force (plafond
// RSI d'achat désactivé — acceptation OOS satisfaite, adopté en défaut).
SwingConfig cfg83() {
    SwingConfig c = cfg82();
    c.rsiBuyMax = 100.0;
    return c;
}

// cfg84() = chaîne retenue après l'item 8.4 : vente RSI gatée par le régime
// (acceptation OOS satisfaite, adopté en défaut).
SwingConfig cfg84() {
    SwingConfig c = cfg83();
    c.rsiSellOnlyIfRegimeDown = true;
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
    // Goldens re-figés le 2026-07-02 (B2, exécution à l'open i+1)
    EXPECT_NEAR(espV, 12.0572, 1e-2);
    EXPECT_NEAR(alphaV, -13.8959, 1e-2);   // alpha ~inchangé vs base (−14,1015)
}

// Verdict OOS de l'item 8.4 (vente RSI gatée par le régime, D26/T2).
// Acceptation (définie à ce sprint, cohérente avec 8.2/8.5) : « gain moyen
// des gagnants ↑ et alpha net OOS ≥ base, sans dégrader le facteur de
// profit ». Base = cfg83 (chaîne : 8.1 + 8.2 + 8.3 retenus) ; variante =
// cfg83 + rsiSellOnlyIfRegimeDown.
//
// VERDICT MESURÉ (figé) : ACCEPTATION SATISFAITE sur les 3 critères —
// gain moyen des gagnants 2,46 → 4,19 %, facteur de profit 1,06 → 1,84,
// alpha OOS −14,12 → −13,11 (+1,01 pt), à nombre de trades constant (5).
// Ne plus vendre la force en tendance confirmée laisse courir les gagnants
// (l'effet que 8.2 seul ne pouvait pas produire faute de trades).
// Défaut adopté : rsiSellOnlyIfRegimeDown = true.
TEST(StrategyV2Integration, RsiSellRegimeGateOosVerdictIsLocked) {
    SwingConfig base    = cfg83();
    SwingConfig variant = cfg83(); variant.rsiSellOnlyIfRegimeDown = true;

    const auto wb = WalkForward(base,    SWINGBOT_QQQ_CSV, kIs, kOos, kStep).run();
    const auto wv = WalkForward(variant, SWINGBOT_QQQ_CSV, kIs, kOos, kStep).run();
    ASSERT_EQ(wb.size(), 2u);
    ASSERT_EQ(wv.size(), 2u);

    const auto tb = tradesOos(wb);
    const auto tv = tradesOos(wv);
    const double gainB  = gainMoyenGagnants(tb), gainV = gainMoyenGagnants(tv);
    const double pfB    = facteurProfit(tb),     pfV   = facteurProfit(tv);
    const double alphaB = meanOosAlpha(wb),      alphaV = meanOosAlpha(wv);

    std::cout << std::fixed << std::setprecision(4)
              << "  ITEM 8.4 — VENTE RSI GATEE PAR LE REGIME (trades OOS pooles)\n"
              << "    trades              : base " << tb.size() << " / variante " << tv.size() << "\n"
              << "    gain moyen gagnants : base " << gainB << " % / variante " << gainV << " %\n"
              << "    facteur de profit   : base " << pfB   << " / variante "   << pfV   << "\n"
              << "    alpha OOS moyen     : base " << alphaB << " / variante "  << alphaV << "\n";

    for (const auto& x : wv) EXPECT_TRUE(std::isfinite(x.oos.alpha));

    EXPECT_EQ(tb.size(), 5u);
    EXPECT_EQ(tv.size(), 5u);
    EXPECT_GT(gainV, gainB);               // gain moyen des gagnants ↑
    // Goldens re-figés le 2026-07-02 (B2, exécution à l'open i+1)
    EXPECT_NEAR(gainV, 5.3494, 1e-2);      // golden de mesure
    EXPECT_GE(pfV, pfB);                   // facteur de profit non dégradé
    EXPECT_NEAR(pfV, 2.2147, 1e-2);
    EXPECT_GE(alphaV, alphaB);             // alpha net OOS ≥ base
    EXPECT_NEAR(alphaV, -12.5272, 1e-2);
    EXPECT_LT(alphaV, 0.0);                // ne bat toujours pas le B&H
}

// Verdict OOS de l'item 8.5 (re-entrée sur régime, T4 — cash drag).
// Acceptation ROADMAP : « % de temps investi ↑, alpha net ↑ ». Base = cfg84
// (chaîne : 8.1 + 8.2 + 8.3 + 8.4 retenus) ; variante = cfg84 + regimeReentry.
//
// VERDICT MESURÉ (figé) : ACCEPTATION SATISFAITE sur les 2 critères —
// % temps investi 30,65 → 54,02, alpha OOS −13,11 → −10,03 (+3,08 pts),
// trades 5 → 11. La re-entrée est le plus gros contributeur d'alpha du
// sprint : rester avec la tendance rapporte plus que la seule qualité des
// sorties. Défaut adopté : regimeReentry = true.
TEST(StrategyV2Integration, RegimeReentryOosVerdictIsLocked) {
    SwingConfig base    = cfg84();
    SwingConfig variant = cfg84(); variant.regimeReentry = true;

    const auto wb = WalkForward(base,    SWINGBOT_QQQ_CSV, kIs, kOos, kStep).run();
    const auto wv = WalkForward(variant, SWINGBOT_QQQ_CSV, kIs, kOos, kStep).run();
    ASSERT_EQ(wb.size(), 2u);
    ASSERT_EQ(wv.size(), 2u);

    const auto tb = tradesOos(wb);
    const auto tv = tradesOos(wv);
    const double expoB  = moyenneOos(wb, &BacktestResult::pctTimeInvested);
    const double expoV  = moyenneOos(wv, &BacktestResult::pctTimeInvested);
    const double alphaB = meanOosAlpha(wb), alphaV = meanOosAlpha(wv);

    std::cout << std::fixed << std::setprecision(4)
              << "  ITEM 8.5 — RE-ENTREE SUR REGIME (trades OOS pooles, 2 fenetres QQQ)\n"
              << "    trades          : base " << tb.size() << " / variante " << tv.size() << "\n"
              << "    % temps investi : base " << expoB << " / variante " << expoV << "\n"
              << "    alpha OOS moyen : base " << alphaB << " / variante " << alphaV << "\n";

    for (const auto& x : wv) EXPECT_TRUE(std::isfinite(x.oos.alpha));

    EXPECT_EQ(tb.size(),  5u);
    EXPECT_EQ(tv.size(), 11u);             // trades ↑
    EXPECT_GT(expoV, expoB);               // % temps investi ↑
    EXPECT_NEAR(expoV, 54.0201, 1e-2);     // golden de mesure
    EXPECT_GT(alphaV, alphaB);             // alpha net ↑
    // Golden re-figé le 2026-07-02 (B2, exécution à l'open i+1)
    EXPECT_NEAR(alphaV, -9.9023, 1e-2);
    EXPECT_LT(alphaV, 0.0);                // ne bat toujours pas le B&H
}

// ─── Verdict de SPRINT : chaîne 8.1→8.5 complète vs état 8.1 ─────────────────
// Le juge de paix du Sprint 8 : la stratégie retenue (défauts actuels = cfg85)
// comparée à l'état de départ (cfg81), mêmes fenêtres OOS. Alimente la DoD du
// sprint : « bat le B&H net de coûts en OOS, OU le sous-performe de moins de
// 5 pts avec un drawdown réduit d'au moins 50 % — sinon pas d'edge démontré,
// pas de déploiement ». Inclut la RÉ-EXAMINATION promise de 8.2 : TP off
// jugé sur la chaîne finale, maintenant que l'échantillon porte des trades.
//
// VERDICT DE SPRINT MESURÉ (figé) : le sprint a produit +4,07 pts d'alpha OOS
// (−14,10 → −10,03), temps investi 0 → 54,02 %, 11 trades OOS de qualité
// (gain moyen des gagnants +4,46 %, facteur de profit 3,71, espérance
// +77,93 $). Ré-exam 8.2 : TP off VALIDÉ positivement sur la chaîne finale
// (−10,03 vs −10,20 avec TP 10 % — laisser courir rapporte aussi en OOS).
// MAIS l'alpha reste NÉGATIF et sous-performe le B&H de plus de 5 pts →
// **DoD non atteinte : pas d'edge démontré, pas de déploiement** (résultat
// valide — on ne met jamais d'argent réel sur un edge non prouvé).
TEST(StrategyV2Integration, SprintChainVsBaselineOosVerdictIsLocked) {
    SwingConfig chain = cfg84(); chain.regimeReentry = true;   // = cfg85 retenue
    SwingConfig start = cfg81();
    SwingConfig chainTp = chain; chainTp.takeProfitPct = 0.10; // ré-exam 8.2

    const auto wc = WalkForward(chain,   SWINGBOT_QQQ_CSV, kIs, kOos, kStep).run();
    const auto ws = WalkForward(start,   SWINGBOT_QQQ_CSV, kIs, kOos, kStep).run();
    const auto wt = WalkForward(chainTp, SWINGBOT_QQQ_CSV, kIs, kOos, kStep).run();
    ASSERT_EQ(wc.size(), 2u);
    ASSERT_EQ(ws.size(), 2u);
    ASSERT_EQ(wt.size(), 2u);

    const auto tc = tradesOos(wc);
    const double alphaC = meanOosAlpha(wc), alphaS = meanOosAlpha(ws);
    const double alphaT = meanOosAlpha(wt);
    const double expoC  = moyenneOos(wc, &BacktestResult::pctTimeInvested);
    const double expoS  = moyenneOos(ws, &BacktestResult::pctTimeInvested);
    const double ddC    = moyenneOos(wc, &BacktestResult::maxDrawdownPct);
    const double ddS    = moyenneOos(ws, &BacktestResult::maxDrawdownPct);

    std::cout << std::fixed << std::setprecision(4)
              << "  SPRINT 8 — CHAINE COMPLETE vs ETAT 8.1 (2 fenetres OOS QQQ)\n"
              << "    alpha OOS moyen : 8.1 " << alphaS << " -> chaine " << alphaC
              << " (delta " << (alphaC - alphaS) << ")\n"
              << "    % temps investi : 8.1 " << expoS << " -> chaine " << expoC << "\n"
              << "    max DD OOS moyen: 8.1 " << ddS << " -> chaine " << ddC << "\n"
              << "    trades OOS      : chaine " << tc.size()
              << " (gain moyen gagnants " << gainMoyenGagnants(tc)
              << " %, PF " << facteurProfit(tc)
              << ", esperance " << esperanceParTrade(tc) << " $)\n"
              << "    re-exam 8.2     : chaine TP off " << alphaC
              << " vs chaine TP 10% " << alphaT << "\n";

    for (const auto& x : wc) EXPECT_TRUE(std::isfinite(x.oos.alpha));

    // Progression du sprint (chaîne > état 8.1), valeurs figées.
    // Goldens re-figés le 2026-07-02 (B2, exécution à l'open i+1) : le verdict
    // qualitatif est INCHANGÉ (chaîne > 8.1, DoD non atteinte).
    EXPECT_GT(alphaC, alphaS);
    EXPECT_NEAR(alphaC, -9.9023, 1e-2);
    EXPECT_NEAR(alphaS, -14.1015, 1e-2);
    EXPECT_GT(expoC, expoS);
    EXPECT_NEAR(expoC, 54.0201, 1e-2);
    EXPECT_EQ(tc.size(), 11u);
    EXPECT_NEAR(gainMoyenGagnants(tc),  5.6792, 1e-2);
    EXPECT_NEAR(facteurProfit(tc),      3.2203, 1e-2);
    EXPECT_NEAR(esperanceParTrade(tc), 80.2136, 1e-2);

    // Ré-exam 8.2 sur la chaîne finale : TP off ≥ TP 10 % (adoption confirmée).
    EXPECT_GE(alphaC, alphaT);
    EXPECT_NEAR(alphaT, -10.1140, 1e-2);

    // DoD du Sprint 8 : NON atteinte — alpha OOS négatif et sous-performance
    // > 5 pts vs B&H → pas d'edge démontré, pas de déploiement. Si ce verrou
    // casse « dans le bon sens » un jour, re-dérouler la DoD complète (drawdown
    // −50 %…) avant tout déploiement.
    EXPECT_LT(alphaC, -5.0);
}

// ─── Sprint 8-bis, item 8b.3 : pavage walk-forward FIN ───────────────────────

// Structure du pavage fin : au moins 4 fenêtres OOS contiguës de tailles
// exactes — la condition pour que les verdicts fins soient comparables entre
// eux et avec le pavage 2-fenêtres.
TEST(StrategyV2Integration, FinePavingProducesAtLeastFourOosWindows) {
    SwingConfig chain = cfg84(); chain.regimeReentry = true;   // chaîne v2

    const auto w = WalkForward(chain, SWINGBOT_QQQ_CSV, kIsFin, kOosFin, kStepFin).run();

    ASSERT_GE(w.size(), 4u);
    for (size_t i = 0; i < w.size(); ++i) {
        EXPECT_EQ(w[i].isEnd,   w[i].oosStart);                // IS colle a l'OOS
        EXPECT_EQ(w[i].isEnd  - w[i].isStart,  kIsFin);
        EXPECT_EQ(w[i].oosEnd - w[i].oosStart, kOosFin);
        if (i > 0)                                             // OOS contigus
            EXPECT_EQ(w[i].oosStart, w[i - 1].oosStart + kStepFin);
    }
}

// Verdict de CHAÎNE re-verrouillé sur le pavage fin (item 8b.3). Même
// comparaison que SprintChainVsBaselineOosVerdictIsLocked (chaîne v2 vs état
// 8.1) mais jugée sur 4 fenêtres OOS au lieu de 2 — toute INVERSION de
// conclusion vs le pavage 2-fenêtres est précisément ce que ce test doit
// révéler et figer.
//
// VERDICT MESURÉ (figé) : AUCUNE inversion — le pavage fin CONFIRME le
// pavage 2-fenêtres sur toute la ligne :
//   - la chaîne bat l'état 8.1 : alpha OOS −7,15 vs −9,05 (delta +1,91,
//     contre +4,07 sur 2 fenêtres — l'amélioration tient, plus modeste) ;
//   - l'alpha reste NÉGATIF et sous −5 pts → DoD toujours non atteinte,
//     pas d'edge, pas de déploiement ;
//   - l'échantillon D34 est enfin étoffé : 13 trades OOS poolés pour la
//     chaîne (gagnants +7,41 %, PF 1,98, espérance +62,52 $)… mais l'état
//     8.1 reste à ZÉRO trade OOS même sur 4 fenêtres — son alpha n'est
//     toujours que du cash drag, le pavage fin ne l'a pas « réveillé ».
TEST(StrategyV2Integration, SprintChainFinePavingOosVerdictIsLocked) {
    SwingConfig chain = cfg84(); chain.regimeReentry = true;   // = cfg85 retenue
    SwingConfig start = cfg81();

    const auto wc = WalkForward(chain, SWINGBOT_QQQ_CSV, kIsFin, kOosFin, kStepFin).run();
    const auto ws = WalkForward(start, SWINGBOT_QQQ_CSV, kIsFin, kOosFin, kStepFin).run();
    ASSERT_GE(wc.size(), 4u);
    ASSERT_EQ(ws.size(), wc.size());

    const auto tc = tradesOos(wc);
    const auto ts = tradesOos(ws);
    const double alphaC = meanOosAlpha(wc), alphaS = meanOosAlpha(ws);
    const double expoC  = moyenneOos(wc, &BacktestResult::pctTimeInvested);
    const double expoS  = moyenneOos(ws, &BacktestResult::pctTimeInvested);

    std::cout << std::fixed << std::setprecision(4)
              << "  ITEM 8b.3 — PAVAGE FIN (" << wc.size() << " fenetres OOS QQQ, IS="
              << kIsFin << "/OOS=" << kOosFin << ")\n"
              << "    alpha OOS moyen : 8.1 " << alphaS << " -> chaine " << alphaC
              << " (delta " << (alphaC - alphaS) << ")\n"
              << "    % temps investi : 8.1 " << expoS << " -> chaine " << expoC << "\n"
              << "    trades OOS      : 8.1 " << ts.size() << " -> chaine " << tc.size()
              << " (gain moyen gagnants " << gainMoyenGagnants(tc)
              << " %, PF " << facteurProfit(tc)
              << ", esperance " << esperanceParTrade(tc) << " $)\n";

    for (const auto& x : wc) EXPECT_TRUE(std::isfinite(x.oos.alpha));
    for (const auto& x : ws) EXPECT_TRUE(std::isfinite(x.oos.alpha));

    // La chaîne bat toujours l'état 8.1 sur le pavage fin (pas d'inversion).
    // Goldens re-figés le 2026-07-02 (B2, exécution à l'open i+1) : aucune
    // inversion de verdict là non plus.
    EXPECT_GT(alphaC, alphaS);
    EXPECT_NEAR(alphaC, -6.8794, 1e-2);    // golden de mesure (4 fenetres)
    EXPECT_NEAR(alphaS, -9.0527, 1e-2);
    EXPECT_NEAR(expoC,  74.7475, 1e-2);
    EXPECT_EQ(tc.size(), 13u);             // garde-fou D34 : échantillon non vide
    EXPECT_EQ(ts.size(),  0u);             // 8.1 : toujours 0 trade OOS (cash drag)
    EXPECT_NEAR(gainMoyenGagnants(tc),  8.0393, 1e-2);
    EXPECT_NEAR(facteurProfit(tc),      2.0571, 1e-2);
    EXPECT_NEAR(esperanceParTrade(tc), 70.6659, 1e-2);
    // DoD toujours NON atteinte sur le pavage fin : alpha négatif, > 5 pts
    // sous le B&H → pas d'edge démontré, pas de déploiement.
    EXPECT_LT(alphaC, -5.0);
}
