// ─── Tests d'intégration : backtest multi-actifs (Sprint 7.4, D29) ───────────
// Rejoue la config de production sur ≥ 3 actifs porteurs de dividendes (fixtures
// total-return SPY/IWM/MDY générées par tests/data/generate_fixtures.py) et
// vérifie le garde-fou de qualité D29 sur un CSV sans dividende (NODIV).
// Acceptation 7.4 : stratégie évaluée sur ≥ 3 actifs, dividendes comptés,
// garde-fou testé.
#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include "backtest/MultiAsset.hpp"
#include "strategies/ProdConfig.hpp"

using namespace trading;

namespace {

std::string fixture(const std::string& name) {
    return std::string(SWINGBOT_TEST_DATA_DIR) + "/" + name;
}

} // namespace

TEST(MultiAssetIntegration, ProdConfigEvaluatedOnThreeDividendAssets) {
    MultiAssetBacktest mab(prodSwingConfig(), 10'000.0, 0.001);
    std::ostringstream warn;
    auto r = mab.run({
        {"SPY", fixture("SPY_sample.csv")},
        {"IWM", fixture("IWM_sample.csv")},
        {"MDY", fixture("MDY_sample.csv")},
    }, warn);

    ASSERT_EQ(r.assets.size(), 3u);                 // ≥ 3 actifs
    EXPECT_TRUE(r.allHaveDividendInfo);             // dividendes comptés partout
    EXPECT_EQ(r.assetsWithoutDividends, 0);
    EXPECT_TRUE(warn.str().empty());                // aucun avertissement D29

    for (const auto& a : r.assets) {
        EXPECT_TRUE(a.hasDividendInfo);
        EXPECT_FALSE(a.result.equityCurve.empty()); // un backtest réel a tourné
    }
}

// Le garde-fou D29 DOIT se déclencher sur un CSV sans information de dividende.
TEST(MultiAssetIntegration, DataQualityGuardFiresOnNonAdjustedCsv) {
    MultiAssetBacktest mab(prodSwingConfig(), 10'000.0, 0.001);
    std::ostringstream warn;
    auto r = mab.run({
        {"SPY",   fixture("SPY_sample.csv")},
        {"NODIV", fixture("NODIV_sample.csv")},
    }, warn);

    EXPECT_EQ(r.assetsWithoutDividends, 1);
    EXPECT_FALSE(r.allHaveDividendInfo);
    EXPECT_NE(warn.str().find("NODIV"), std::string::npos);
}
