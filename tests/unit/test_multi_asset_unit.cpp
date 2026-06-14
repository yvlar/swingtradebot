// ============================================================
//  test_multi_asset_unit.cpp  —  Tests UNITAIRES
//  Cible : garde-fou de qualité des données (CsvDataFeed::hasDividendInfo)
//          + agrégation du MultiAssetBacktest (Sprint 7.4, D29)
//  CSV temporaires synthétiques ; aucune dépendance aux fixtures de prod.
// ============================================================
#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "backtest/MultiAsset.hpp"

using namespace trading;
namespace fs = std::filesystem;

namespace {

// Écrit un petit CSV Yahoo. Si `withDividend` est faux, Adj Close == Close
// (symptôme D29) ; sinon Adj Close = Close * 0,9 (dividendes).
std::string writeCsv(const std::string& tag, bool withDividend, int rows = 12) {
    std::string path = "unit_ma_" + tag + "_" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()) + ".csv";
    std::ofstream f(path);
    f << "Date,Open,High,Low,Close,Adj Close,Volume\n";
    for (int i = 0; i < rows; ++i) {
        double close = 100.0 + i;
        double adj   = withDividend ? close * 0.9 : close;
        char date[32];
        std::snprintf(date, sizeof(date), "2024-01-%02d", i + 1);
        f << date << "," << close << "," << close << "," << close << ","
          << close << "," << adj << ",1000\n";
    }
    return path;
}

} // namespace

// ════════════════════════════════════════════════════════════
//  Garde-fou D29 : détection de l'information de dividende
// ════════════════════════════════════════════════════════════

TEST(MultiAssetUnit, DividendInfoDetectedWhenAdjCloseDiffers) {
    auto path = writeCsv("div", /*withDividend=*/true);
    CsvDataFeed feed(path);
    EXPECT_TRUE(feed.hasDividendInfo());
    fs::remove(path);
}

TEST(MultiAssetUnit, GuardFiresWhenAdjCloseEqualsCloseEverywhere) {
    auto path = writeCsv("nodiv", /*withDividend=*/false);
    CsvDataFeed feed(path);
    EXPECT_FALSE(feed.hasDividendInfo());  // c'est le cas D29 de QQQ.csv
    fs::remove(path);
}

// ════════════════════════════════════════════════════════════
//  MultiAssetBacktest : agrégation + avertissement
// ════════════════════════════════════════════════════════════

TEST(MultiAssetUnit, CountsAssetsWithoutDividendsAndWarns) {
    auto spy   = writeCsv("spy",   /*withDividend=*/true);
    auto nodiv = writeCsv("nodiv", /*withDividend=*/false);

    MultiAssetBacktest mab(SwingConfig{});
    std::ostringstream warn;
    auto r = mab.run({{"SPY", spy}, {"NODIV", nodiv}}, warn);

    ASSERT_EQ(r.assets.size(), 2u);
    EXPECT_EQ(r.assetsWithoutDividends, 1);
    EXPECT_FALSE(r.allHaveDividendInfo);
    // L'avertissement D29 doit nommer l'actif fautif.
    EXPECT_NE(warn.str().find("NODIV"), std::string::npos);
    EXPECT_NE(warn.str().find("D29"),   std::string::npos);

    fs::remove(spy);
    fs::remove(nodiv);
}

TEST(MultiAssetUnit, AllHaveDividendInfoWhenEveryAssetIsAdjusted) {
    auto a = writeCsv("a", true);
    auto b = writeCsv("b", true);

    MultiAssetBacktest mab(SwingConfig{});
    std::ostringstream warn;
    auto r = mab.run({{"A", a}, {"B", b}}, warn);

    EXPECT_EQ(r.assetsWithoutDividends, 0);
    EXPECT_TRUE(r.allHaveDividendInfo);
    EXPECT_TRUE(warn.str().empty());  // aucun avertissement quand tout est ajusté

    fs::remove(a);
    fs::remove(b);
}
