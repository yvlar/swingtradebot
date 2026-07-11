// ============================================================
//  test_data_quality_unit.cpp  —  Tests UNITAIRES
//  Cible : auditTotalReturnCsv (backtest/DataQuality.hpp)
//  Garde-fou D29 : détecter un CSV où Adj Close == Close partout
//  (export sans ajustement = dividendes/splits ignorés).
// ============================================================
#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include "backtest/DataQuality.hpp"
#include "../support/TempPath.hpp"

using namespace trading;
namespace fs = std::filesystem;

// ── Fixture : un fichier CSV temporaire par test ──────────
class DataQualityUnit : public ::testing::Test {
protected:
    std::string path_;

    void SetUp() override {
        // Nom unique PID+compteur+tick (D59) — même motif que les fixtures SQLite.
        path_ = swingbot_test::uniqueTempName("unit_dq_", ".csv");
    }
    void TearDown() override {
        if (fs::exists(path_)) fs::remove(path_);
    }
    void writeCsv(const std::string& content) {
        std::ofstream f(path_);
        f << content;
    }
};

// ════════════════════════════════════════════════════════════
//  Le cas D29 : Adj Close == Close sur TOUTES les lignes
// ════════════════════════════════════════════════════════════

TEST_F(DataQualityUnit, FlagsCsvWhereAdjCloseEqualsCloseEverywhere) {
    // Reproduit le symptôme D29 (l'ancien QQQ.csv) : aucun dividende.
    writeCsv("Date,Open,High,Low,Close,Adj Close,Volume\n"
             "2024-01-02,403.25,404.10,401.50,402.80,402.80,45123400\n"
             "2024-01-03,402.00,405.00,401.00,404.50,404.50,40000000\n"
             "2024-01-04,404.50,406.00,403.00,405.10,405.10,38000000\n");

    const auto r = auditTotalReturnCsv(path_);
    EXPECT_EQ(r.rows,               3u);
    EXPECT_EQ(r.adjEqualsCloseRows, 3u);
    EXPECT_TRUE(r.suspectNoDividends);
}

TEST_F(DataQualityUnit, DoesNotFlagWhenDividendsPresent) {
    // Une seule ligne avec Adj Close ≠ Close suffit à lever le drapeau.
    writeCsv("Date,Open,High,Low,Close,Adj Close,Volume\n"
             "2024-01-02,403.25,404.10,401.50,402.80,402.80,45123400\n"
             "2024-01-03,402.00,405.00,401.00,404.50,400.10,40000000\n"   // dividende
             "2024-01-04,404.50,406.00,403.00,405.10,405.10,38000000\n");

    const auto r = auditTotalReturnCsv(path_);
    EXPECT_EQ(r.rows,               3u);
    EXPECT_EQ(r.adjEqualsCloseRows, 2u);
    EXPECT_FALSE(r.suspectNoDividends);
}

TEST_F(DataQualityUnit, CountsRowsAndMismatchesExactly) {
    // 4 lignes, 2 avec dividende → adjEqualsCloseRows == 2.
    writeCsv("Date,Open,High,Low,Close,Adj Close,Volume\n"
             "2024-01-02,403.25,404.10,401.50,402.80,400.00,45123400\n"   // ≠
             "2024-01-03,402.00,405.00,401.00,404.50,404.50,40000000\n"   // ==
             "2024-01-04,404.50,406.00,403.00,405.10,401.00,38000000\n"   // ≠
             "2024-01-05,405.10,407.00,404.00,406.20,406.20,35000000\n"); // ==

    const auto r = auditTotalReturnCsv(path_);
    EXPECT_EQ(r.rows,               4u);
    EXPECT_EQ(r.adjEqualsCloseRows, 2u);
    EXPECT_FALSE(r.suspectNoDividends);
}

TEST_F(DataQualityUnit, EmptyOrHeaderOnlyCsvIsNotFlagged) {
    // Pas de faux positif sur un fichier vide / en-tête seul (rows == 0).
    writeCsv("Date,Open,High,Low,Close,Adj Close,Volume\n");

    const auto r = auditTotalReturnCsv(path_);
    EXPECT_EQ(r.rows, 0u);
    EXPECT_FALSE(r.suspectNoDividends);
}

TEST_F(DataQualityUnit, MissingFileIsNotFlagged) {
    // Fichier absent → rapport vide, aucun drapeau (pas de crash).
    const auto r = auditTotalReturnCsv("fichier_inexistant_dq.csv");
    EXPECT_EQ(r.rows, 0u);
    EXPECT_FALSE(r.suspectNoDividends);
}
