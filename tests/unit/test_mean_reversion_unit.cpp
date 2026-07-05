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
