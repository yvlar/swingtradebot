// ─── Tests d'intégration : verdict OUT-OF-SAMPLE de la famille MEAN-REVERSION ──
// Sprint 10 (item 10.2). Première famille de signal RÉELLEMENT différente jugée
// après que les deux axes de recherche d'alpha (chaîne mono-actif trend-following
// et rotation multi-actifs) ont été soldés sans edge. On juge la famille
// contrarian EXACTEMENT comme toutes les précédentes : en OOS via le walk-forward
// (Sprint 7, WalkForward.hpp), JAMAIS sur le plein échantillon (leçon 8.1),
// config construite champ par champ (D33), trades OOS POOLÉS avant tout ratio
// (D34), fenêtres OOS largement au-dessus du warmup local ~201 barres (D35).
//
// Le harnais prouve l'edge OU son absence ; les deux sont des résultats valides.
// Un alpha OOS ≤ 0 partout = branche « sinon » : pas d'adoption, prod reste paper.
#include <gtest/gtest.h>
#include <cmath>
#include <iostream>
#include <iomanip>
#include "backtest/WalkForward.hpp"

using namespace trading;

namespace {

// Pavage canonique (identique aux verdicts 8.x) : IS=700 / OOS=400 / pas=400 →
// 2 fenêtres OOS contiguës sur 1790 barres QQQ (~199 tradables par OOS après le
// warmup SMA200).
constexpr size_t kIs   = 700;
constexpr size_t kOos  = 400;
constexpr size_t kStep = 400;

// Pavage FIN (8b.3, D34) : IS=500 / OOS=300 / pas=300 → 4 fenêtres OOS (~99
// tradables chacune). OOS=300 > warmup local ~201 (D35).
constexpr size_t kIsFin   = 500;
constexpr size_t kOosFin  = 300;
constexpr size_t kStepFin = 300;

// ── Config de la chaîne MEAN-REVERSION (D33 : champ par champ) ────────────────
// mode contrarian, entrée sur survente RSI ≤ 30 dans une tendance haussière
// confirmée (SMA200), sortie au retour à la moyenne RSI ≥ 55. Stops/trailing du
// RiskManager identiques à la chaîne (SL 5 %, trailing 3 %, minHold 3). Les flags
// propres au trend-following (rsiBuyMax, rsiSellMin, rsiSellOnlyIfRegimeDown,
// regimeReentry, entry*) sont sans effet en mode MeanReversion (evaluate retourne
// depuis le bloc MR) — laissés à des valeurs neutres explicites.
SwingConfig mrChain() {
    SwingConfig c;
    c.symbol          = "QQQ";
    c.mode            = StrategyMode::MeanReversion;
    c.emaFast         = 9;
    c.emaSlow         = 21;
    c.rsiPeriod       = 14;
    c.mrRsiEntryMax   = 30.0;
    c.mrRsiExitMin    = 55.0;
    c.stopLossPct     = 0.05;
    c.takeProfitPct   = 0.0;
    c.trailingStopPct = 0.03;
    c.riskPerTradePct = 0.02;
    c.minHoldDays     = 3;
    c.smaTrendPeriod  = 200;   // filtre de régime ACTIF : acheter le creux en tendance
    return c;
}

double meanOosAlpha(const std::vector<WfWindow>& w) {
    if (w.empty()) return 0.0;
    double s = 0.0;
    for (const auto& x : w) s += x.oos.alpha;
    return s / static_cast<double>(w.size());
}

size_t nbTradesOos(const std::vector<WfWindow>& w) {
    size_t n = 0;
    for (const auto& x : w) n += x.oos.trades.size();
    return n;
}

// meanOosAlpha d'une config MR (entry/exit donnés) sur le pavage fin QQQ.
double sweepAlpha(double entryMax, double exitMin) {
    SwingConfig c = mrChain();
    c.mrRsiEntryMax = entryMax;
    c.mrRsiExitMin  = exitMin;
    const auto w = WalkForward(c, SWINGBOT_QQQ_CSV, kIsFin, kOosFin, kStepFin).run();
    return meanOosAlpha(w);
}

} // namespace

// La chaîne MR et le trend-following produisent le MÊME pavage de fenêtres (le
// tiling ne dépend que de isBars/oosBars/step/N, pas du mode) — condition d'une
// comparaison honnête.
TEST(MeanReversionOosIntegration, MeanReversionAndTrendFollowShareWindowTiling) {
    SwingConfig mr = mrChain();
    SwingConfig tf = mrChain(); tf.mode = StrategyMode::TrendFollow;

    const auto wm = WalkForward(mr, SWINGBOT_QQQ_CSV, kIs, kOos, kStep).run();
    const auto wt = WalkForward(tf, SWINGBOT_QQQ_CSV, kIs, kOos, kStep).run();

    ASSERT_EQ(wm.size(), 2u);
    ASSERT_EQ(wt.size(), wm.size());
    for (size_t i = 0; i < wm.size(); ++i) {
        EXPECT_EQ(wm[i].oosStart, wt[i].oosStart);
        EXPECT_EQ(wm[i].oosEnd,   wt[i].oosEnd);
    }
}

// Verdict OOS verrouillé de la chaîne MR (QQQ, pavages canonique + fin).
TEST(MeanReversionOosIntegration, MeanReversionChainOosVerdictIsLocked) {
    const auto wc = WalkForward(mrChain(), SWINGBOT_QQQ_CSV, kIs,    kOos,    kStep   ).run();
    const auto wf = WalkForward(mrChain(), SWINGBOT_QQQ_CSV, kIsFin, kOosFin, kStepFin).run();
    ASSERT_EQ(wc.size(), 2u);
    ASSERT_EQ(wf.size(), 4u);

    const double alphaCanon = meanOosAlpha(wc);
    const double alphaFin    = meanOosAlpha(wf);

    std::cout << std::fixed << std::setprecision(4)
              << "  MEAN-REVERSION — alpha OOS moyen (pts vs B&H)\n"
              << "    canonique (2 fen.) : " << alphaCanon
              << "  | trades OOS pooles : " << nbTradesOos(wc) << "\n"
              << "    fin       (4 fen.) : " << alphaFin
              << "  | trades OOS pooles : " << nbTradesOos(wf) << "\n";

    for (const auto& x : wc) EXPECT_TRUE(std::isfinite(x.oos.alpha));
    for (const auto& x : wf) EXPECT_TRUE(std::isfinite(x.oos.alpha));

    // VERDICT 10.2 (figé). La famille MEAN-REVERSION ne bat PAS le Buy & Hold en
    // OOS : alpha négatif sur les deux pavages. De plus la chaîne MR (entrée
    // RSI ≤ 30 EN régime haussier + SMA200) déclenche à peine — 1 seul trade OOS
    // poolé : le verdict est très majoritairement du cash drag (le piège D34, un
    // OOS quasi sans trades ne juge que le temps hors marché). Aucun edge, pas
    // d'adoption ; la prod reste paper.
    EXPECT_NEAR(alphaCanon, -13.1092, 1e-2);
    EXPECT_NEAR(alphaFin,    -8.5565, 1e-2);
    EXPECT_EQ(nbTradesOos(wc), 1u);
    EXPECT_EQ(nbTradesOos(wf), 1u);
    EXPECT_LT(alphaCanon, 0.0);   // ne bat pas le B&H
    EXPECT_LT(alphaFin,   0.0);
}

// Le filtre de régime aide-t-il la famille MR ? (régime ON vs OFF, pavage fin).
TEST(MeanReversionOosIntegration, MeanReversionRegimeFilterOosVerdictIsLocked) {
    SwingConfig on  = mrChain();
    SwingConfig off = mrChain(); off.smaTrendPeriod = 1;   // filtre désactivé

    const auto won  = WalkForward(on,  SWINGBOT_QQQ_CSV, kIsFin, kOosFin, kStepFin).run();
    const auto woff = WalkForward(off, SWINGBOT_QQQ_CSV, kIsFin, kOosFin, kStepFin).run();
    ASSERT_EQ(won.size(),  4u);
    ASSERT_EQ(woff.size(), 4u);

    const double aOn  = meanOosAlpha(won);
    const double aOff = meanOosAlpha(woff);

    std::cout << std::fixed << std::setprecision(4)
              << "  MEAN-REVERSION — filtre de regime (pavage fin QQQ)\n"
              << "    regime ON  (SMA200) : " << aOn  << " | trades " << nbTradesOos(won)  << "\n"
              << "    regime OFF (off)    : " << aOff << " | trades " << nbTradesOos(woff) << "\n";

    // VERDICT (figé). Comme pour la chaîne trend-following (8.1), le filtre de
    // régime rend l'alpha OOS « moins mauvais » (−8,56 > −13,02) mais au prix
    // d'un échantillon quasi vide (1 trade vs 6) : le régime coupe la plupart des
    // achats de creux. Les deux restent négatifs — aucun edge.
    EXPECT_NEAR(aOn,   -8.5565, 1e-2);
    EXPECT_NEAR(aOff, -13.0185, 1e-2);
    EXPECT_EQ(nbTradesOos(won),  1u);
    EXPECT_EQ(nbTradesOos(woff), 6u);
    EXPECT_GT(aOn, aOff);   // le régime aide en OOS…
    EXPECT_LT(aOn, 0.0);    // …mais ne bat toujours pas le B&H
}

// Cohérence multi-actifs (SPY/IWM/MDY, pavage fin) : la famille MR est-elle un
// artefact QQQ ? (8b.2 pour la chaîne trend-following).
TEST(MeanReversionOosIntegration, MeanReversionMultiAssetOosVerdictIsLocked) {
    struct A { const char* nom; const char* csv; double alphaAttendu; size_t tradesAttendus; };
    // Verdict figé : alpha OOS NÉGATIF sur les trois actifs (la famille MR n'est
    // pas un artefact QQQ — elle ne bat le B&H nulle part), échantillons minces.
    const A actifs[] = {
        {"SPY", SWINGBOT_SPY_CSV, -6.9344, 1u},
        {"IWM", SWINGBOT_IWM_CSV, -4.3451, 0u},
        {"MDY", SWINGBOT_MDY_CSV, -2.7945, 2u},
    };
    std::cout << std::fixed << std::setprecision(4)
              << "  MEAN-REVERSION — multi-actifs (pavage fin)\n";
    for (const auto& a : actifs) {
        SwingConfig c = mrChain(); c.symbol = a.nom;
        const auto w = WalkForward(c, a.csv, kIsFin, kOosFin, kStepFin).run();
        ASSERT_FALSE(w.empty());
        const double alpha = meanOosAlpha(w);
        std::cout << "    " << a.nom << " : " << alpha
                  << " | fenetres " << w.size()
                  << " | trades OOS pooles " << nbTradesOos(w) << "\n";
        for (const auto& x : w) EXPECT_TRUE(std::isfinite(x.oos.alpha));
        EXPECT_NEAR(alpha, a.alphaAttendu, 1e-2);
        EXPECT_EQ(nbTradesOos(w), a.tradesAttendus);
        EXPECT_LT(alpha, 0.0);   // ne bat le B&H sur aucun actif
    }
}

// Balayage de seuils MR (candidat éventuel pour la confirmation 10.3, GATÉE) :
// entryMax ∈ {25,30,35} × exitMin ∈ {50,55,60}, jugé sur le pavage fin QQQ. On
// rapporte le MEILLEUR alpha OOS et s'il existe un candidat (> 0). Un grid winner
// n'est PAS un edge (D36/D43) — juste une hypothèse à confirmer hors-protocole.
TEST(MeanReversionOosIntegration, MeanReversionThresholdSweepBestOosIsLocked) {
    const double entrees[] = {25.0, 30.0, 35.0};
    const double sorties[] = {50.0, 55.0, 60.0};
    double best = -1e9; double bestEntry = 0, bestExit = 0;
    std::cout << std::fixed << std::setprecision(4)
              << "  MEAN-REVERSION — balayage de seuils (pavage fin QQQ)\n";
    for (double e : entrees) for (double x : sorties) {
        const double a = sweepAlpha(e, x);
        std::cout << "    entry<=" << e << " / exit>=" << x << " : " << a << "\n";
        if (a > best) { best = a; bestEntry = e; bestExit = x; }
    }
    std::cout << "    MEILLEUR : entry<=" << bestEntry << " / exit>=" << bestExit
              << " -> alpha OOS " << best
              << (best > 0.0 ? "  (CANDIDAT > 0)" : "  (aucun candidat)") << "\n";

    // VERDICT 10.2 (figé) : AUCUN seuil MR ne produit d'alpha OOS positif — le
    // meilleur (entry ≤ 30 / exit ≥ 60) reste à −8,38 pts. Le gate de la
    // confirmation hors-protocole (10.3) reste donc FERMÉ : rien à confirmer.
    EXPECT_NEAR(best, -8.3849, 1e-2);
    EXPECT_DOUBLE_EQ(bestEntry, 30.0);
    EXPECT_DOUBLE_EQ(bestExit,  60.0);
    EXPECT_FALSE(best > 0.0);   // pas de candidat -> pas de confirmation 10.3
}

// ════════════════════════════════════════════════════════════════════════════
//  VARIANTE Z-SCORE / BOLLINGER (Sprint 11, item 11.2, décision 10.4 = a)
//  Le 1er jet MR (RSI ≤ 30 EN régime) ne tradait qu'1 fois en OOS (D47) : la
//  FAMILLE n'était pas jugée, seul ce réglage restrictif l'était. La variante
//  z-score achète la faiblesse de PRIX (clôture sous la bande basse de
//  Bollinger), moins couplée au régime → un échantillon de trades RÉEL. On la
//  juge EXACTEMENT comme la chaîne RSI : OOS, pavages canonique + fin, config
//  champ par champ, trades POOLÉS, régime ON/OFF, balayage, multi-actifs.
// ════════════════════════════════════════════════════════════════════════════
namespace {

// Chaîne MR z-score (D33 : champ par champ). Bande Bollinger(20), entrée sous
// −2 σ, sortie au retour à la moyenne (z ≥ 0). Stops/trailing identiques à la
// chaîne RSI. Filtre de régime SMA200 ACTIF (acheter le creux DANS la tendance).
SwingConfig mrZChain() {
    SwingConfig c;
    c.symbol          = "QQQ";
    c.mode            = StrategyMode::MeanReversion;
    c.emaFast         = 9;
    c.emaSlow         = 21;
    c.rsiPeriod       = 14;
    c.mrBandPeriod    = 20;
    c.mrBandEntryK    = 2.0;
    c.mrBandExitZ     = 0.0;
    c.stopLossPct     = 0.05;
    c.takeProfitPct   = 0.0;
    c.trailingStopPct = 0.03;
    c.riskPerTradePct = 0.02;
    c.minHoldDays     = 3;
    c.smaTrendPeriod  = 200;
    return c;
}

double zSweepAlpha(int period, double k) {
    SwingConfig c = mrZChain();
    c.mrBandPeriod = period;
    c.mrBandEntryK = k;
    const auto w = WalkForward(c, SWINGBOT_QQQ_CSV, kIsFin, kOosFin, kStepFin).run();
    return meanOosAlpha(w);
}

} // namespace

// Verdict OOS verrouillé de la chaîne z-score (QQQ, pavages canonique + fin).
// POINT CENTRAL (D47) : contrairement au 1er jet RSI (1 trade OOS), la variante
// z-score TRADE réellement (3 canon / 4 fin) → la famille est enfin jugée, pas
// la rareté du signal. Et malgré ça : AUCUN edge (alpha négatif partout).
TEST(MeanReversionOosIntegration, ZScoreChainOosVerdictIsLocked) {
    const auto wc = WalkForward(mrZChain(), SWINGBOT_QQQ_CSV, kIs,    kOos,    kStep   ).run();
    const auto wf = WalkForward(mrZChain(), SWINGBOT_QQQ_CSV, kIsFin, kOosFin, kStepFin).run();
    ASSERT_EQ(wc.size(), 2u);
    ASSERT_EQ(wf.size(), 4u);

    const double alphaCanon = meanOosAlpha(wc);
    const double alphaFin    = meanOosAlpha(wf);

    std::cout << std::fixed << std::setprecision(4)
              << "  MEAN-REVERSION Z-SCORE — alpha OOS moyen (pts vs B&H)\n"
              << "    canonique (2 fen.) : " << alphaCanon
              << "  | trades OOS pooles : " << nbTradesOos(wc) << "\n"
              << "    fin       (4 fen.) : " << alphaFin
              << "  | trades OOS pooles : " << nbTradesOos(wf) << "\n";

    for (const auto& x : wc) EXPECT_TRUE(std::isfinite(x.oos.alpha));
    for (const auto& x : wf) EXPECT_TRUE(std::isfinite(x.oos.alpha));

    // VERDICT 11.2 (figé). La variante z-score TRADE (3/4 trades OOS vs 1 pour le
    // 1er jet RSI — la famille est enfin exercée, D47 levé) mais ne bat PAS le
    // Buy & Hold : alpha négatif sur les deux pavages. Aucun edge, pas
    // d'adoption ; la prod reste paper.
    EXPECT_NEAR(alphaCanon, -15.3453, 1e-2);
    EXPECT_NEAR(alphaFin,    -9.8749, 1e-2);
    EXPECT_EQ(nbTradesOos(wc), 3u);
    EXPECT_EQ(nbTradesOos(wf), 4u);
    EXPECT_GT(nbTradesOos(wc), 1u);   // D47 : la famille TRADE réellement…
    EXPECT_GT(nbTradesOos(wf), 1u);
    EXPECT_LT(alphaCanon, 0.0);       // …mais ne bat pas le B&H
    EXPECT_LT(alphaFin,   0.0);
}

// Le filtre de régime aide-t-il la variante z-score ? (ON vs OFF, pavage fin).
// Levier « retrait du filtre SMA200 » de la décision 10.4 : OFF trade BEAUCOUP
// plus (23 vs 4) mais l'alpha empire — le régime aide, sans donner d'edge.
TEST(MeanReversionOosIntegration, ZScoreRegimeFilterOosVerdictIsLocked) {
    SwingConfig on  = mrZChain();
    SwingConfig off = mrZChain(); off.smaTrendPeriod = 1;   // filtre désactivé

    const auto won  = WalkForward(on,  SWINGBOT_QQQ_CSV, kIsFin, kOosFin, kStepFin).run();
    const auto woff = WalkForward(off, SWINGBOT_QQQ_CSV, kIsFin, kOosFin, kStepFin).run();
    ASSERT_EQ(won.size(),  4u);
    ASSERT_EQ(woff.size(), 4u);

    const double aOn  = meanOosAlpha(won);
    const double aOff = meanOosAlpha(woff);

    std::cout << std::fixed << std::setprecision(4)
              << "  MEAN-REVERSION Z-SCORE — filtre de regime (pavage fin QQQ)\n"
              << "    regime ON  (SMA200) : " << aOn  << " | trades " << nbTradesOos(won)  << "\n"
              << "    regime OFF (off)    : " << aOff << " | trades " << nbTradesOos(woff) << "\n";

    // VERDICT (figé). Le retrait du filtre fait TRADER bien plus (4 → 23) — c'est
    // le levier « moins restrictif » de la décision 10.4 — mais dégrade l'alpha
    // (−9,87 → −15,68) : acheter les creux hors tendance perd de l'argent. Les
    // deux restent négatifs — aucun edge.
    EXPECT_NEAR(aOn,   -9.8749,  1e-2);
    EXPECT_NEAR(aOff, -15.6789,  1e-2);
    EXPECT_EQ(nbTradesOos(won),   4u);
    EXPECT_EQ(nbTradesOos(woff), 23u);
    EXPECT_GT(nbTradesOos(woff), nbTradesOos(won));  // OFF trade beaucoup plus…
    EXPECT_GT(aOn, aOff);   // …mais le régime aide l'alpha
    EXPECT_LT(aOn, 0.0);    // …et rien ne bat le B&H
}

// Cohérence multi-actifs (SPY/IWM/MDY, pavage fin) : la variante z-score est-elle
// un artefact QQQ ? Verdict figé : alpha OOS NÉGATIF sur les trois actifs.
TEST(MeanReversionOosIntegration, ZScoreMultiAssetOosVerdictIsLocked) {
    struct A { const char* nom; const char* csv; double alphaAttendu; size_t tradesAttendus; };
    const A actifs[] = {
        {"SPY", SWINGBOT_SPY_CSV, -7.7777, 3u},
        {"IWM", SWINGBOT_IWM_CSV, -5.1394, 2u},
        {"MDY", SWINGBOT_MDY_CSV, -3.3581, 5u},
    };
    std::cout << std::fixed << std::setprecision(4)
              << "  MEAN-REVERSION Z-SCORE — multi-actifs (pavage fin)\n";
    for (const auto& a : actifs) {
        SwingConfig c = mrZChain(); c.symbol = a.nom;
        const auto w = WalkForward(c, a.csv, kIsFin, kOosFin, kStepFin).run();
        ASSERT_FALSE(w.empty());
        const double alpha = meanOosAlpha(w);
        std::cout << "    " << a.nom << " : " << alpha
                  << " | fenetres " << w.size()
                  << " | trades OOS pooles " << nbTradesOos(w) << "\n";
        for (const auto& x : w) EXPECT_TRUE(std::isfinite(x.oos.alpha));
        EXPECT_NEAR(alpha, a.alphaAttendu, 1e-2);
        EXPECT_EQ(nbTradesOos(w), a.tradesAttendus);
        EXPECT_LT(alpha, 0.0);   // ne bat le B&H sur aucun actif
    }
}

// Balayage période × k (candidat éventuel pour la confirmation 11.3, GATÉE) :
// mrBandPeriod ∈ {10,20} × mrBandEntryK ∈ {1.5,2.0,2.5}, pavage fin QQQ. On
// rapporte le MEILLEUR alpha OOS et s'il existe un candidat (> 0).
TEST(MeanReversionOosIntegration, ZScoreThresholdSweepBestOosIsLocked) {
    const int    periodes[] = {10, 20};
    const double coefs[]    = {1.5, 2.0, 2.5};
    double best = -1e9; int bestP = 0; double bestK = 0.0;
    std::cout << std::fixed << std::setprecision(4)
              << "  MEAN-REVERSION Z-SCORE — balayage periode x k (pavage fin QQQ)\n";
    for (int p : periodes) for (double k : coefs) {
        const double a = zSweepAlpha(p, k);
        std::cout << "    period=" << p << " / k=" << k << " : " << a << "\n";
        if (a > best) { best = a; bestP = p; bestK = k; }
    }
    std::cout << "    MEILLEUR : period=" << bestP << " / k=" << bestK
              << " -> alpha OOS " << best
              << (best > 0.0 ? "  (CANDIDAT > 0)" : "  (aucun candidat)") << "\n";

    // VERDICT 11.2 (figé) : AUCUN réglage z-score ne produit d'alpha OOS positif —
    // le meilleur (period 10 / k 2,5) reste à −9,15 pts (et sur seulement 3
    // trades : le piège D47 guette même le « meilleur » ; les réglages qui
    // TRADENT le plus, k=1,5, sont eux aussi négatifs, −9,25 à −9,41). Le gate de
    // la confirmation hors-protocole (11.3) reste donc FERMÉ : rien à confirmer.
    EXPECT_NEAR(best, -9.1496, 1e-2);
    EXPECT_EQ(bestP, 10);
    EXPECT_DOUBLE_EQ(bestK, 2.5);
    EXPECT_FALSE(best > 0.0);   // pas de candidat -> pas de confirmation 11.3
}
