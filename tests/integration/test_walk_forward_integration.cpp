// ─── Tests d'intégration : WalkForwardAnalyzer (Sprint 7.1, D24) ─────────────
// Walk-forward complet sur QQQ.csv : IS 504 barres (~2 ans) / OOS 126 barres
// (~6 mois), pas = OOS (fenêtres OOS jointives, chaque barre jugée une fois).
// Les valeurs figées ci-dessous sont des GOLDENS : toute dérive du découpage,
// du replay de sous-période ou des métriques doit faire échouer ces tests.
// Lecture du résultat (2026-06-11) : l'« edge OOS » de certaines fenêtres
// vient surtout du cash (stratégie à plat pendant le marché baissier 2022,
// alpha positif par défaut de participation) — pas d'un edge exploitable.
#include <gtest/gtest.h>
#include "backtest/WalkForward.hpp"
#include "strategies/ProdConfig.hpp"

using namespace trading;

namespace {

WalkForwardReport runWalkForward(const SwingConfig& cfg) {
    WalkForwardAnalyzer wf(cfg, SWINGBOT_QQQ_CSV, /*isBars=*/504,
                           /*oosBars=*/126);
    return wf.run();
}

} // namespace

TEST(WalkForwardIntegration, WindowLayoutOnQqqCsv) {
    // 1858 barres → 1 + (1858 - 630) / 126 = 10 fenêtres
    const auto rep = runWalkForward(SwingConfig{});
    ASSERT_EQ(rep.windows.size(), 10u);

    for (const auto& w : rep.windows) {
        // Chaque sous-backtest couvre exactement sa fenêtre
        EXPECT_EQ(w.is.equityCurve.size(),  504u);
        EXPECT_EQ(w.oos.equityCurve.size(), 126u);
        // L'OOS suit l'IS sans trou
        EXPECT_EQ(w.window.oosBegin, w.window.isEnd);
    }
    // Fenêtres OOS jointives (pas par défaut = taille OOS)
    for (size_t i = 1; i < rep.windows.size(); ++i)
        EXPECT_EQ(rep.windows[i].window.oosBegin,
                  rep.windows[i - 1].window.oosEnd);
}

// ─── Golden walk-forward, config DÉFAUT (figé le 2026-06-11, Sprint 7.1) ─────
TEST(WalkForwardIntegration, GoldenDefaultConfigVerdictOnQqqCsv) {
    const auto rep = runWalkForward(SwingConfig{});
    ASSERT_EQ(rep.windows.size(), 10u);

    // L'edge (battre le B&H net de coûts) apparaît en IS sur 3 fenêtres et en
    // OOS sur 5 — il est CONFIRMÉ quelque part en OOS, donc pas de signal
    // global de sur-ajustement ; mais 2 fenêtres sont individuellement
    // sur-ajustées (edge IS évaporé en OOS)
    EXPECT_EQ(rep.windowsWithEdgeIS,  3);
    EXPECT_EQ(rep.windowsWithEdgeOOS, 5);
    EXPECT_FALSE(rep.overfitSignal);
    const int surAjustees = static_cast<int>(std::count_if(
        rep.windows.begin(), rep.windows.end(),
        [](const WalkForwardWindowResult& w) { return w.surAjuste; }));
    EXPECT_EQ(surAjustees, 2);

    // Alpha moyen : très négatif en IS (fenêtres haussières ratées), proche
    // de zéro en OOS (le cash « gagne » pendant le baissier 2022)
    EXPECT_NEAR(rep.meanIsAlpha,  -32.86, 0.01);
    EXPECT_NEAR(rep.meanOosAlpha, -1.95,  0.01);
}

// ─── Golden walk-forward, config PROD (figé le 2026-06-11, Sprint 7.1) ───────
TEST(WalkForwardIntegration, GoldenProdConfigVerdictOnQqqCsv) {
    const auto rep = runWalkForward(prodSwingConfig());
    ASSERT_EQ(rep.windows.size(), 10u);

    EXPECT_EQ(rep.windowsWithEdgeIS,  4);
    EXPECT_EQ(rep.windowsWithEdgeOOS, 5);
    EXPECT_FALSE(rep.overfitSignal);

    EXPECT_NEAR(rep.meanIsAlpha,  -27.65, 0.01);
    EXPECT_NEAR(rep.meanOosAlpha, -1.82,  0.01);

    // Même hors-échantillon, l'alpha moyen reste négatif : AUCUN edge
    // démontré — la refonte de stratégie (Sprint 8) sera jugée là-dessus
    EXPECT_LT(rep.meanOosAlpha, 0.0);
}

// Le rapport console s'affiche sans erreur (lisible dans la sortie ctest)
TEST(WalkForwardIntegration, PrintsPerWindowReport) {
    SwingConfig cfg;
    WalkForwardAnalyzer wf(cfg, SWINGBOT_QQQ_CSV, 504, 126);
    const auto rep = wf.run();
    ASSERT_NO_THROW(wf.printReport(rep));
}
