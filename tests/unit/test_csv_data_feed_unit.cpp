// ============================================================
//  test_csv_data_feed_unit.cpp  —  Tests UNITAIRES
//  Cible : CsvDataFeed — parsing Yahoo Finance et fenêtrage
//  (getBarsUpTo pilote la fenêtre glissante du backtester)
// ============================================================
#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include "brokers/CsvDataFeed.hpp"
#include "../support/TempPath.hpp"

using namespace trading;
namespace fs = std::filesystem;

// ── Fixture : un fichier CSV temporaire par test ──────────
class CsvDataFeedUnit : public ::testing::Test {
protected:
    std::string path_;

    void SetUp() override {
        // Nom unique PID+compteur+tick (D59) — même motif que les fixtures SQLite.
        path_ = swingbot_test::uniqueTempName("unit_csv_", ".csv");
    }
    void TearDown() override {
        if (fs::exists(path_)) fs::remove(path_);
    }

    void writeCsv(const std::string& content) {
        std::ofstream f(path_);
        f << content;
    }

    // En-tête Yahoo Finance + N jours consécutifs de janvier 2024,
    // close = 100 + i (volume = 1000 + i)
    void writeStandardCsv(int days) {
        std::ostringstream s;
        s << "Date,Open,High,Low,Close,Adj Close,Volume\n";
        for (int i = 0; i < days; ++i) {
            s << "2024-01-" << (i + 10) << ","          // jours 10..N+9 : tri stable
              << (99.0 + i) << "," << (101.0 + i) << "," << (98.0 + i) << ","
              << (100.0 + i) << "," << (100.0 + i) << "," << (1000 + i) << "\n";
        }
        writeCsv(s.str());
    }
};

// ════════════════════════════════════════════════════════════
//  Chargement et parsing
// ════════════════════════════════════════════════════════════

TEST_F(CsvDataFeedUnit, LoadsValidCsvWithAllFields) {
    writeCsv("Date,Open,High,Low,Close,Adj Close,Volume\n"
             "2024-01-02,403.25,404.10,401.50,402.80,402.80,45123400\n");

    CsvDataFeed feed(path_);
    ASSERT_EQ(feed.size(), 1u);
    const Bar& b = feed.allBars().front();
    EXPECT_EQ(b.date, "2024-01-02");
    EXPECT_DOUBLE_EQ(b.open,  403.25);
    EXPECT_DOUBLE_EQ(b.high,  404.10);
    EXPECT_DOUBLE_EQ(b.low,   401.50);
    EXPECT_DOUBLE_EQ(b.close, 402.80);
    EXPECT_EQ(b.volume, 45'123'400L);
}

// Rendement TOTAL (Sprint 6.3, D7) : la colonne utilisée est Adj Close —
// dividendes et splits intégrés — pour la stratégie ET le Buy & Hold
// (inverse l'ancien verrou UsesCloseNotAdjClose, rouge sur l'ancien code)
TEST_F(CsvDataFeedUnit, UsesAdjCloseForTotalReturn) {
    writeCsv("Date,Open,High,Low,Close,Adj Close,Volume\n"
             "2024-01-02,100,101,99,100.50,95.25,1000\n");

    CsvDataFeed feed(path_);
    ASSERT_EQ(feed.size(), 1u);
    EXPECT_DOUBLE_EQ(feed.allBars().front().close, 95.25);
}

// La série reste COHÉRENTE : open/high/low sont mis à l'échelle par le
// facteur d'ajustement (Adj Close / Close) — sinon des extrêmes non ajustés
// côtoieraient un close ajusté (faux ranges, faux stops)
TEST_F(CsvDataFeedUnit, OhlcScaledByAdjustmentFactor) {
    writeCsv("Date,Open,High,Low,Close,Adj Close,Volume\n"
             "2024-01-02,100,101,99,100.50,95.25,1000\n");

    CsvDataFeed feed(path_);
    ASSERT_EQ(feed.size(), 1u);
    const Bar& b = feed.allBars().front();
    const double f = 95.25 / 100.50;
    EXPECT_NEAR(b.open, 100.0 * f, 1e-9);
    EXPECT_NEAR(b.high, 101.0 * f, 1e-9);
    EXPECT_NEAR(b.low,   99.0 * f, 1e-9);
    EXPECT_EQ(b.volume, 1000L);
}

// Adj Close == Close (cas du QQQ.csv actuel, exporté sans ajustement) :
// aucune barre ne change — c'est ce qui justifie le golden inchangé (D29)
TEST_F(CsvDataFeedUnit, NoChangeWhenAdjCloseEqualsClose) {
    writeCsv("Date,Open,High,Low,Close,Adj Close,Volume\n"
             "2024-01-02,403.25,404.10,401.50,402.80,402.80,45123400\n");

    CsvDataFeed feed(path_);
    const Bar& b = feed.allBars().front();
    EXPECT_DOUBLE_EQ(b.open,  403.25);
    EXPECT_DOUBLE_EQ(b.high,  404.10);
    EXPECT_DOUBLE_EQ(b.low,   401.50);
    EXPECT_DOUBLE_EQ(b.close, 402.80);
}

TEST_F(CsvDataFeedUnit, ThrowsOnMissingFile) {
    EXPECT_THROW(CsvDataFeed("fichier_inexistant_xyz.csv"), std::runtime_error);
}

// Un CSV sans aucune ligne de données exploitable doit échouer fort,
// pas produire un backtest silencieusement vide
TEST_F(CsvDataFeedUnit, ThrowsWhenNoUsableData) {
    writeCsv("Date,Open,High,Low,Close,Adj Close,Volume\n");
    EXPECT_THROW({ CsvDataFeed feed(path_); }, std::runtime_error);
}

// La première ligne est toujours traitée comme en-tête
TEST_F(CsvDataFeedUnit, SkipsHeaderLine) {
    writeStandardCsv(3);
    CsvDataFeed feed(path_);
    EXPECT_EQ(feed.size(), 3u);
    EXPECT_EQ(feed.allBars().front().date, "2024-01-10");
}

// Lignes Yahoo avec "null" (jours fériés) ou champs vides → ignorées
TEST_F(CsvDataFeedUnit, SkipsNullAndEmptyValueRows) {
    writeCsv("Date,Open,High,Low,Close,Adj Close,Volume\n"
             "2024-01-02,100,101,99,100,100,1000\n"
             "2024-01-03,null,null,null,null,null,null\n"
             "2024-01-04,100,,99,100,100,1000\n"
             "2024-01-05,102,103,101,102,102,1200\n");

    CsvDataFeed feed(path_);
    ASSERT_EQ(feed.size(), 2u);
    EXPECT_EQ(feed.allBars()[0].date, "2024-01-02");
    EXPECT_EQ(feed.allBars()[1].date, "2024-01-05");
}

TEST_F(CsvDataFeedUnit, SkipsMalformedCommentAndShortRows) {
    writeCsv("Date,Open,High,Low,Close,Adj Close,Volume\n"
             "# commentaire\n"
             "\n"
             "2024-01-02,abc,101,99,100,100,1000\n"      // non numérique
             "2024-01-03,100,101\n"                       // < 6 colonnes
             "2024-01-04,100,101,99,100,100,1000\n");

    CsvDataFeed feed(path_);
    ASSERT_EQ(feed.size(), 1u);
    EXPECT_EQ(feed.allBars().front().date, "2024-01-04");
}

// 6 colonnes seulement (pas de Volume) → volume par défaut 0
TEST_F(CsvDataFeedUnit, MissingVolumeColumnDefaultsToZero) {
    writeCsv("Date,Open,High,Low,Close,Adj Close\n"
             "2024-01-02,100,101,99,100,100\n");

    CsvDataFeed feed(path_);
    ASSERT_EQ(feed.size(), 1u);
    EXPECT_EQ(feed.allBars().front().volume, 0L);
}

TEST_F(CsvDataFeedUnit, TrimsWhitespaceAroundTokens) {
    writeCsv("Date,Open,High,Low,Close,Adj Close,Volume\n"
             " 2024-01-02 , 100 , 101 , 99 , 100.5 , 100.5 , 1000 \r\n");

    CsvDataFeed feed(path_);
    ASSERT_EQ(feed.size(), 1u);
    EXPECT_EQ(feed.allBars().front().date, "2024-01-02");
    EXPECT_DOUBLE_EQ(feed.allBars().front().close, 100.5);
}

// Un CSV non trié doit être remis en ordre chronologique
TEST_F(CsvDataFeedUnit, SortsRowsByAscendingDate) {
    writeCsv("Date,Open,High,Low,Close,Adj Close,Volume\n"
             "2024-01-05,103,104,102,103,103,1000\n"
             "2024-01-02,100,101,99,100,100,1000\n"
             "2024-01-03,101,102,100,101,101,1000\n");

    CsvDataFeed feed(path_);
    ASSERT_EQ(feed.size(), 3u);
    EXPECT_EQ(feed.allBars()[0].date, "2024-01-02");
    EXPECT_EQ(feed.allBars()[1].date, "2024-01-03");
    EXPECT_EQ(feed.allBars()[2].date, "2024-01-05");
}

// ════════════════════════════════════════════════════════════
//  getBars / getBarsUpTo / priceAt — fenêtrage
// ════════════════════════════════════════════════════════════

TEST_F(CsvDataFeedUnit, GetBarsReturnsLastNDays) {
    writeStandardCsv(10);
    CsvDataFeed feed(path_);

    auto r = feed.getBars("QQQ", 3);
    ASSERT_TRUE(r.ok());                    // données locales : jamais Err
    ASSERT_EQ(r.value().size(), 3u);
    EXPECT_DOUBLE_EQ(r.value().back().close, 109.0);   // dernière barre
    EXPECT_DOUBLE_EQ(r.value().front().close, 107.0);
}

TEST_F(CsvDataFeedUnit, GetBarsMoreThanAvailableReturnsAll) {
    writeStandardCsv(5);
    CsvDataFeed feed(path_);

    auto r = feed.getBars("QQQ", 100);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value().size(), 5u);
}

// Bornes de la fenêtre glissante du backtester
TEST_F(CsvDataFeedUnit, GetBarsUpToWindowBoundaries) {
    writeStandardCsv(10);                   // closes 100..109
    CsvDataFeed feed(path_);

    // index 4, lookback 3 → barres 2,3,4 (la barre du curseur incluse)
    auto w = feed.getBarsUpTo(4, 3);
    ASSERT_EQ(w.size(), 3u);
    EXPECT_DOUBLE_EQ(w.front().close, 102.0);
    EXPECT_DOUBLE_EQ(w.back().close,  104.0);

    // index 0 → une seule barre, lookback clampé au début
    auto first = feed.getBarsUpTo(0, 5);
    ASSERT_EQ(first.size(), 1u);
    EXPECT_DOUBLE_EQ(first.front().close, 100.0);

    // index hors limites → vide
    EXPECT_TRUE(feed.getBarsUpTo(10, 3).empty());
}

TEST_F(CsvDataFeedUnit, PriceAtReturnsCloseOrZeroOutOfRange) {
    writeStandardCsv(3);
    CsvDataFeed feed(path_);

    EXPECT_DOUBLE_EQ(feed.priceAt(1), 101.0);
    EXPECT_DOUBLE_EQ(feed.priceAt(99), 0.0);
}

TEST_F(CsvDataFeedUnit, MarketIsAlwaysOpenInBacktest) {
    writeStandardCsv(1);
    CsvDataFeed feed(path_);
    EXPECT_TRUE(feed.isMarketOpen());
}
