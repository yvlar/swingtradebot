// ─── Tests d'intégration : walk-forward IS/OOS sur QQQ (item 7.1, D24) ───────
// Rejoue le Backtester sur des fenêtres glissantes du QQQ.csv total-return et
// produit un rapport IS vs OOS par fenêtre. Acceptation : ≥ 2 fenêtres, des
// métriques finies de part et d'autre, et le verdict « sur-ajusté » calculé.
#include <gtest/gtest.h>
#include <cmath>
#include "backtest/WalkForward.hpp"
#include "strategies/ProdConfig.hpp"

using namespace trading;

namespace {
// Paramètres figés pour un nombre de fenêtres stable sur 1790 barres :
// IS=700, OOS=300, pas=300 → s = 0, 300, 600 (s+1000 ≤ 1790) = 3 fenêtres.
constexpr size_t IS_BARS  = 700;
constexpr size_t OOS_BARS = 300;
constexpr size_t STEP     = 300;
} // namespace

TEST(WalkForwardIntegration, ReportsIsVsOosPerWindowOnQqq) {
    WalkForward wf(prodSwingConfig(), SWINGBOT_QQQ_CSV, IS_BARS, OOS_BARS, STEP);
    const auto windows = wf.run();

    // Nombre de fenêtres verrouillé : une dérive du dataset le ferait bouger.
    ASSERT_EQ(windows.size(), 3u);

    for (const auto& w : windows) {
        // Bornes cohérentes et contiguës IS → OOS.
        EXPECT_EQ(w.isEnd, w.oosStart);
        EXPECT_EQ(w.isEnd - w.isStart,   IS_BARS);
        EXPECT_EQ(w.oosEnd - w.oosStart, OOS_BARS);

        // Métriques finies de part et d'autre (pas de NaN/inf).
        EXPECT_TRUE(std::isfinite(w.is.totalReturnPct));
        EXPECT_TRUE(std::isfinite(w.is.alpha));
        EXPECT_TRUE(std::isfinite(w.oos.totalReturnPct));
        EXPECT_TRUE(std::isfinite(w.oos.alpha));
        EXPECT_TRUE(std::isfinite(w.oos.sharpeRatio));

        // Le verdict est calculable sur chaque fenêtre (ne lève pas).
        const auto v = WalkForward::judge(w);
        EXPECT_EQ(v.isAlpha,  w.is.alpha);
        EXPECT_EQ(v.oosAlpha, w.oos.alpha);
    }

    // Artefact d'acceptation : la table IS vs OOS (visible avec --output-on-failure
    // ou en cas d'échec ; sert de preuve que le harnais tourne sur QQQ réel).
    wf.printReport(windows);
}
