// ============================================================
//  test_mean_reversion_unit.cpp  —  Tests UNITAIRES
//  Famille MEAN-REVERSION (Sprint 10, mode StrategyMode::MeanReversion)
//  Prémisse INVERSE du trend-following : acheter la faiblesse (survente),
//  sortir au retour à la moyenne. Prouve que le mode diverge réellement du
//  trend-following sur les MÊMES barres, et que le filtre de régime gate les
//  entrées MR. Le mode par défaut (TrendFollow) reste couvert par
//  test_swing_strategy_unit.cpp (goldens byte-identiques).
// ============================================================
#include <gtest/gtest.h>
#include <vector>
#include "strategies/SwingStrategy.hpp"

using namespace trading;

namespace {

    Bar make_bar(int i, double close, long vol = 100000L) {
        return {"2024-" + std::to_string(i), close, close + 0.5, close - 0.5, close, vol};
    }

    // Baisse monotone → RSI → 0 (survente) et prix SOUS ses moyennes.
    std::vector<Bar> strong_downtrend(int n = 80) {
        std::vector<Bar> bars;
        for (int i = 0; i < n; ++i)
            bars.push_back(make_bar(i, 300.0 - i * 2.0));
        return bars;
    }

    // Hausse monotone → RSI → 100 (suracheté / au-dessus du seuil de sortie).
    std::vector<Bar> strong_uptrend(int n = 80) {
        std::vector<Bar> bars;
        for (int i = 0; i < n; ++i)
            bars.push_back(make_bar(i, 100.0 + i * 2.0));
        return bars;
    }

    // Config mean-reversion, filtre de régime DÉSACTIVÉ (smaTrendPeriod = 1) :
    // isole la logique RSI d'entrée/sortie (base de comparaison OOS du filtre).
    SwingConfig mrConfigNoRegime() {
        SwingConfig cfg;
        cfg.mode           = StrategyMode::MeanReversion;
        cfg.mrRsiEntryMax  = 30.0;
        cfg.mrRsiExitMin   = 55.0;
        cfg.smaTrendPeriod = 1;   // filtre de régime désactivé
        return cfg;
    }

    // ── Variante z-score / Bollinger (Sprint 11, item 11.1) ───────────────────
    // Config MR avec l'entrée z-score active : achat quand la clôture perce la
    // bande basse (z ≤ −k). `regime` = smaTrendPeriod (1 = filtre désactivé).
    SwingConfig mrZScoreConfig(double k = 1.5, int regime = 1) {
        SwingConfig cfg;
        cfg.mode           = StrategyMode::MeanReversion;
        cfg.mrBandPeriod   = 20;
        cfg.mrBandEntryK   = k;
        cfg.mrBandExitZ    = 0.0;   // sortie au retour à la moyenne
        cfg.smaTrendPeriod = regime;
        return cfg;
    }

    // Tendance haussière (100 → 139) puis creux marqué à la dernière barre : la
    // clôture finale (110) perce nettement la bande basse de Bollinger(20)
    // — z très négatif — tout en restant, sans filtre de régime, éligible à
    // l'achat contrarian z-score.
    std::vector<Bar> uptrend_then_dip() {
        std::vector<Bar> bars;
        for (int i = 0; i < 40; ++i) bars.push_back(make_bar(i, 100.0 + i));
        bars.push_back(make_bar(40, 110.0));
        return bars;
    }

} // namespace

// ════════════════════════════════════════════════════════════
//  Valeurs par défaut de la config MR
// ════════════════════════════════════════════════════════════
TEST(MeanReversionUnit, DefaultConfigValues) {
    SwingConfig cfg;
    EXPECT_EQ(cfg.mode, StrategyMode::TrendFollow);   // défaut = comportement historique
    EXPECT_DOUBLE_EQ(cfg.mrRsiEntryMax, 30.0);
    EXPECT_DOUBLE_EQ(cfg.mrRsiExitMin,  55.0);
}

// ════════════════════════════════════════════════════════════
//  Entrée contrarian : acheter la survente
// ════════════════════════════════════════════════════════════
// Sur une baisse (RSI en survente), le mode MeanReversion ACHÈTE le creux…
TEST(MeanReversionUnit, OversoldTriggersBuy) {
    auto strat = SwingStrategy::create(mrConfigNoRegime());
    Signal s = strat->evaluate(strong_downtrend());
    EXPECT_EQ(s.type, SignalType::BUY);
    EXPECT_NE(s.reason.find("Mean-reversion"), std::string::npos);
}

// …alors que le trend-following NE PREND PAS d'entrée sur les MÊMES barres
// (aucun croisement haussier, prix sous les EMA) → HOLD. Preuve que la famille
// diverge réellement (elle n'est pas une reparamétrisation du trend-following).
TEST(MeanReversionUnit, TrendFollowDoesNotBuyTheSameOversoldDip) {
    SwingConfig tf = mrConfigNoRegime();
    tf.mode = StrategyMode::TrendFollow;   // même série, mode historique
    auto strat = SwingStrategy::create(tf);
    Signal s = strat->evaluate(strong_downtrend());
    EXPECT_NE(s.type, SignalType::BUY);    // le trend-following reste à l'écart
}

// ════════════════════════════════════════════════════════════
//  Sortie au retour à la moyenne
// ════════════════════════════════════════════════════════════
// RSI revenu au-dessus du seuil de sortie (hausse) → SELL « retour à la moyenne ».
TEST(MeanReversionUnit, ReversionToMeanTriggersSell) {
    auto strat = SwingStrategy::create(mrConfigNoRegime());
    Signal s = strat->evaluate(strong_uptrend());
    EXPECT_EQ(s.type, SignalType::SELL);
    EXPECT_NE(s.reason.find("retour à la moyenne"), std::string::npos);
}

// ════════════════════════════════════════════════════════════
//  Le filtre de régime gate les entrées MR
// ════════════════════════════════════════════════════════════
// Régime ACTIF (smaTrendPeriod = 20) : sur une baisse le prix est SOUS la SMA
// → régime baissier → l'entrée MR est BLOQUÉE malgré la survente (on n'achète
// le creux QUE dans une tendance haussière). Les sorties restent possibles.
TEST(MeanReversionUnit, RegimeDownBlocksMeanReversionEntry) {
    SwingConfig cfg = mrConfigNoRegime();
    cfg.smaTrendPeriod = 20;   // filtre de régime réactivé
    auto strat = SwingStrategy::create(cfg);
    Signal s = strat->evaluate(strong_downtrend());
    EXPECT_NE(s.type, SignalType::BUY);   // survente mais régime baissier → pas d'achat
}

// ════════════════════════════════════════════════════════════
//  Variante z-score / Bollinger (Sprint 11, item 11.1)
//  L'entrée achète la faiblesse de PRIX (clôture sous la bande
//  basse), moins couplée au RSI que le 1er jet MR (Sprint 10).
// ════════════════════════════════════════════════════════════

// Défauts : la variante z-score est DÉSACTIVÉE (aucun effet tant qu'on ne la
// câble pas → goldens byte-identiques).
TEST(MeanReversionUnit, ZScoreDefaultsDisabled) {
    SwingConfig cfg;
    EXPECT_EQ(cfg.mrBandPeriod, 0);
    EXPECT_DOUBLE_EQ(cfg.mrBandEntryK, 0.0);
    EXPECT_DOUBLE_EQ(cfg.mrBandExitZ,  0.0);
}

// Sans filtre de régime, un creux qui perce la bande basse déclenche l'ACHAT
// z-score (raison explicite « z-score »).
TEST(MeanReversionUnit, ZScoreEntryBuysBelowLowerBand) {
    auto strat = SwingStrategy::create(mrZScoreConfig(1.5, /*regime=*/1));
    Signal s = strat->evaluate(uptrend_then_dip());
    EXPECT_EQ(s.type, SignalType::BUY);
    EXPECT_NE(s.reason.find("z-score"), std::string::npos);
}

// Le trend-following NE PREND PAS d'entrée sur les mêmes barres (creux sous les
// EMA, aucun croisement haussier) → preuve que la variante diverge réellement.
TEST(MeanReversionUnit, ZScoreEntryDivergesFromTrendFollow) {
    SwingConfig tf = mrZScoreConfig(1.5, 1);
    tf.mode = StrategyMode::TrendFollow;
    auto strat = SwingStrategy::create(tf);
    Signal s = strat->evaluate(uptrend_then_dip());
    EXPECT_NE(s.type, SignalType::BUY);
}

// Une hausse monotone laisse la clôture bien AU-DESSUS de sa bande-moyenne
// (z ≥ 0) → SELL « retour à la moyenne » (la sortie n'est jamais gatée par le
// régime).
TEST(MeanReversionUnit, ZScoreExitSellsOnReturnToMean) {
    auto strat = SwingStrategy::create(mrZScoreConfig(1.5, 1));
    Signal s = strat->evaluate(strong_uptrend());
    EXPECT_EQ(s.type, SignalType::SELL);
    EXPECT_NE(s.reason.find("retour à la moyenne"), std::string::npos);
}

// Le filtre de régime gate l'entrée z-score comme il gate l'entrée RSI : au
// creux le prix passe sous la SMA de régime → régime baissier → achat BLOQUÉ
// (c'est le levier « retrait du filtre SMA200 » : regime OFF autorise l'entrée
// que regime ON refuse ici).
TEST(MeanReversionUnit, ZScoreRegimeDownBlocksEntry) {
    auto stratOn  = SwingStrategy::create(mrZScoreConfig(1.5, /*regime=*/20));
    Signal on  = stratOn->evaluate(uptrend_then_dip());
    EXPECT_NE(on.type, SignalType::BUY);   // régime baissier au creux → pas d'achat

    auto stratOff = SwingStrategy::create(mrZScoreConfig(1.5, /*regime=*/1));
    Signal off = stratOff->evaluate(uptrend_then_dip());
    EXPECT_EQ(off.type, SignalType::BUY);  // régime désactivé → l'achat passe
}
