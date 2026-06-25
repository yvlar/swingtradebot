// ============================================================
//  test_walk_forward_unit.cpp  —  Tests UNITAIRES
//  Cible : WalkForward (backtest/WalkForward.hpp) + le seam runRange /
//  ReplayDataFeed::floorIdx (BackTester.hpp) qui l'alimente.
// ============================================================
#include <gtest/gtest.h>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include "backtest/WalkForward.hpp"
#include "backtest/BackTester.hpp"
#include "brokers/CsvDataFeed.hpp"

using namespace trading;
namespace fs = std::filesystem;

namespace {
// Dates synthétiques valides, strictement croissantes et triables
// lexicographiquement (zéro-padding) : année tous les 360 jours, mois tous les
// 30, jour tous les 1. daysSinceEpoch_ les accepte (mois 1-12, jour 1-30).
std::string synthDate(int i) {
    const int year  = 2000 + i / 360;
    const int month = 1 + (i / 30) % 12;
    const int day   = 1 + i % 30;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", year, month, day);
    return buf;
}
} // namespace

// ── Fixture : un CSV synthétique de N barres ──────────────
class WalkForwardUnit : public ::testing::Test {
protected:
    std::string path_;

    void SetUp() override {
        path_ = "unit_wf_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) + ".csv";
    }
    void TearDown() override {
        if (fs::exists(path_)) fs::remove(path_);
    }

    // N barres, close oscillant doucement autour d'une tendance haussière
    // (positif partout, quelques croisements EMA). Adj Close == Close (les
    // valeurs de prix ne jouent pas ici — on teste le fenêtrage, pas l'edge).
    void writeSeries(int n) {
        std::ofstream f(path_);
        f << "Date,Open,High,Low,Close,Adj Close,Volume\n";
        for (int i = 0; i < n; ++i) {
            const double c = 100.0 + 0.1 * i + (i % 20);  // > 0, oscillant
            f << synthDate(i) << "," << c << "," << (c + 1) << ","
              << (c - 1) << "," << c << "," << c << ",1000\n";
        }
    }
};

// ════════════════════════════════════════════════════════════
//  Les fenêtres pavent la série aux bons index
// ════════════════════════════════════════════════════════════
TEST_F(WalkForwardUnit, WindowsTileTheSeries) {
    writeSeries(200);
    WalkForward wf(SwingConfig{}, path_, /*isBars=*/80, /*oosBars=*/40, /*step=*/40);
    const auto w = wf.run();

    ASSERT_EQ(w.size(), 3u);
    // Fenêtre 0
    EXPECT_EQ(w[0].isStart,  0u);   EXPECT_EQ(w[0].isEnd,  80u);
    EXPECT_EQ(w[0].oosStart, 80u);  EXPECT_EQ(w[0].oosEnd, 120u);
    // Fenêtre 1 (glissée de step=40)
    EXPECT_EQ(w[1].isStart,  40u);  EXPECT_EQ(w[1].isEnd,  120u);
    EXPECT_EQ(w[1].oosStart, 120u); EXPECT_EQ(w[1].oosEnd, 160u);
    // Fenêtre 2
    EXPECT_EQ(w[2].isStart,  80u);  EXPECT_EQ(w[2].isEnd,  160u);
    EXPECT_EQ(w[2].oosStart, 160u); EXPECT_EQ(w[2].oosEnd, 200u);
}

TEST_F(WalkForwardUnit, EmptyWhenSeriesTooShortForOneWindow) {
    writeSeries(100);
    WalkForward wf(SwingConfig{}, path_, /*isBars=*/80, /*oosBars=*/40);
    EXPECT_TRUE(wf.run().empty());  // isBars + oosBars = 120 > 100
}

// ════════════════════════════════════════════════════════════
//  Verrou de sûreté golden : runRange sur toute la série == run()
// ════════════════════════════════════════════════════════════
TEST_F(WalkForwardUnit, RunRangeEqualsFullRunOnFullWindow) {
    writeSeries(120);
    auto csv = std::make_shared<CsvDataFeed>(path_);
    const size_t n = csv->allBars().size();

    Backtester bt(SwingConfig{}, path_);
    const auto full  = bt.run();
    const auto range = bt.runRange(csv, 0, n);

    EXPECT_DOUBLE_EQ(range.finalValue,     full.finalValue);
    EXPECT_DOUBLE_EQ(range.totalReturnPct, full.totalReturnPct);
    EXPECT_EQ(range.totalTrades,           full.totalTrades);
    EXPECT_EQ(range.equityCurve.size(),    full.equityCurve.size());
}

// ════════════════════════════════════════════════════════════
//  Le verdict sur-ajusté : edge en IS qui ne tient pas en OOS
// ════════════════════════════════════════════════════════════
TEST_F(WalkForwardUnit, OverfitFlaggedWhenEdgeOnlyInSample) {
    WfWindow w;
    w.is.alpha  = +5.0;   // edge positif en IS
    w.oos.alpha = -2.0;   // disparu en OOS
    EXPECT_TRUE(WalkForward::judge(w).surajuste);

    w.oos.alpha = +3.0;   // edge qui tient en OOS
    EXPECT_FALSE(WalkForward::judge(w).surajuste);

    // Pas d'edge en IS → pas « sur-ajusté » (rien à sur-ajuster)
    w.is.alpha  = -1.0;  w.oos.alpha = -3.0;
    EXPECT_FALSE(WalkForward::judge(w).surajuste);
}

// ════════════════════════════════════════════════════════════
//  Isolation par fenêtre : le feed ne sert jamais avant floorIdx
// ════════════════════════════════════════════════════════════
TEST_F(WalkForwardUnit, ReplayFeedNeverServesBeforeFloor) {
    writeSeries(10);
    auto csv = std::make_shared<CsvDataFeed>(path_);

    // floorIdx = 4 : même avec un grand lookback, la fenêtre commence à l'index 4
    ReplayDataFeed feed(csv, /*lookback=*/100, /*floorIdx=*/4);
    feed.setCursor(8);

    auto r = feed.getBars("QQQ", 100);
    ASSERT_TRUE(r.ok());
    ASSERT_EQ(r.value().size(), 5u);   // index 4,5,6,7,8
    EXPECT_DOUBLE_EQ(r.value().front().close, csv->allBars()[4].close);
    EXPECT_DOUBLE_EQ(r.value().back().close,  csv->allBars()[8].close);
}

TEST_F(WalkForwardUnit, FloorZeroReproducesUnboundedReplay) {
    writeSeries(10);
    auto csv = std::make_shared<CsvDataFeed>(path_);

    // floorIdx par défaut (0) = ancien comportement : lookback seul borne.
    ReplayDataFeed feed(csv, /*lookback=*/3);
    feed.setCursor(5);
    auto r = feed.getBars("QQQ", 100);
    ASSERT_TRUE(r.ok());
    ASSERT_EQ(r.value().size(), 3u);   // index 3,4,5
    EXPECT_DOUBLE_EQ(r.value().back().close, csv->allBars()[5].close);
}
