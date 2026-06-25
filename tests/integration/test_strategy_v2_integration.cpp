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
#include "strategies/ProdConfig.hpp"

using namespace trading;

namespace {

// Fenêtres dimensionnées pour la SMA200 : l'OOS doit rester tradable après le
// warmup de ~201 barres. IS=700 / OOS=400 / pas=400 → 2 fenêtres OOS contiguës
// sur les 1790 barres de QQQ (≈ 199 barres tradables par OOS).
constexpr size_t kIs   = 700;
constexpr size_t kOos  = 400;
constexpr size_t kStep = 400;

double meanOosAlpha(const std::vector<WfWindow>& w) {
    if (w.empty()) return 0.0;
    double s = 0.0;
    for (const auto& x : w) s += x.oos.alpha;
    return s / static_cast<double>(w.size());
}

} // namespace

// La régime-config et la base produisent le MÊME pavage de fenêtres (mêmes
// bornes IS/OOS) — condition d'une comparaison honnête.
TEST(StrategyV2Integration, RegimeAndBaselineShareWindowTiling) {
    SwingConfig regime = prodSwingConfig();        // smaTrendPeriod = 200
    SwingConfig base   = prodSwingConfig(); base.smaTrendPeriod = 1;  // filtre off

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
    SwingConfig regime = prodSwingConfig();
    SwingConfig base   = prodSwingConfig(); base.smaTrendPeriod = 1;

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
