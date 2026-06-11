// ─── Tests d'intégration : multi-actifs total-return (Sprint 7.4, D29) ───────
// La stratégie est évaluée sur ≥ 3 actifs (SPY, IWM, MDY + QQQ ré-exporté) avec
// des séries TOTAL-RETURN réelles : chaque CSV de data/ porte des dividendes
// (Adj Close ≠ Close), exportés par tools/export_total_return_csv.py et figés
// dans le dépôt. Les valeurs ci-dessous sont des GOLDENS sur ces fichiers.
// Enseignements consignés (2026-06-11) :
//   - le B&H QQQ dividendes compris vaut +262,08 % (vs +238,55 % sur le
//     QQQ.csv historique sans dividendes) — D29 quantifié ;
//   - aucun edge sur aucun des 4 actifs (la config prod fait au mieux
//     +12,94 % sur SPY contre un B&H de +171,40 %).
#include <gtest/gtest.h>
#include <string>
#include "backtest/BackTester.hpp"
#include "strategies/ProdConfig.hpp"

using namespace trading;

namespace {

std::string dataCsv(const std::string& name) {
    return std::string(SWINGBOT_DATA_DIR) + "/" + name;
}

BacktestResult runOn(const std::string& csv, const SwingConfig& cfg) {
    Backtester bt(cfg, csv, 10'000.0, 0.001);
    return bt.run();
}

} // namespace

// Garde-fou D29 sur les données réelles : les exports total-return portent
// bien les dividendes ; le QQQ.csv historique, lui, n'en porte AUCUN
TEST(MultiAssetIntegration, TotalReturnCsvsCarryDividendInfo) {
    for (const auto& f : {"SPY.csv", "IWM.csv", "MDY.csv", "QQQ_TR.csv"}) {
        CsvDataFeed feed(dataCsv(f));
        EXPECT_TRUE(feed.hasDividendInfo()) << f;
        EXPECT_EQ(feed.size(), 1790u) << f;
    }
}

TEST(MultiAssetIntegration, LegacyQqqCsvTriggersTheD29Warning) {
    testing::internal::CaptureStderr();
    CsvDataFeed feed(SWINGBOT_QQQ_CSV);
    const std::string err = testing::internal::GetCapturedStderr();
    EXPECT_FALSE(feed.hasDividendInfo());
    EXPECT_NE(err.find("Adj Close"), std::string::npos);
}

// Dividendes comptés : le B&H QQQ total-return DÉPASSE le B&H du QQQ.csv
// historique (+262,08 % vs +238,55 %) — l'alpha réel est encore pire que
// mesuré jusqu'ici, exactement ce que D29 annonçait
TEST(MultiAssetIntegration, QqqBuyHoldIsHigherWithDividends) {
    const auto r = runOn(dataCsv("QQQ_TR.csv"), SwingConfig{});
    EXPECT_NEAR(r.buyHoldReturnPct, 262.0807, 0.01);
    EXPECT_GT(r.buyHoldReturnPct, 238.5544);
}

// ─── Goldens multi-actifs, config PROD (figés le 2026-06-11, Sprint 7.4) ─────
TEST(MultiAssetIntegration, GoldenProdConfigAcrossAssets) {
    struct Attendu { const char* csv; double ret; int trades; };
    // Aucun actif ne bat son Buy & Hold — verrouillé ligne à ligne
    const Attendu attendus[] = {
        {"SPY.csv",    12.9365, 21},
        {"IWM.csv",     2.6139, 12},
        {"MDY.csv",     3.7098, 25},
        {"QQQ_TR.csv",  4.2053, 23},
    };
    for (const auto& a : attendus) {
        const auto r = runOn(dataCsv(a.csv), prodSwingConfig());
        EXPECT_NEAR(r.totalReturnPct, a.ret, 1e-3) << a.csv;
        EXPECT_EQ(r.totalTrades, a.trades)         << a.csv;
        EXPECT_FALSE(r.beatsBuyHold)               << a.csv;
    }
}

// ─── Goldens multi-actifs, config DÉFAUT (figés le 2026-06-11, Sprint 7.4) ───
TEST(MultiAssetIntegration, GoldenDefaultConfigAcrossAssets) {
    struct Attendu { const char* csv; double ret; int trades; };
    const Attendu attendus[] = {
        {"SPY.csv",    1.7576,  4},
        {"IWM.csv",    3.6229, 14},
        {"MDY.csv",    0.7696, 13},
        {"QQQ_TR.csv", 4.8591,  6},
    };
    for (const auto& a : attendus) {
        const auto r = runOn(dataCsv(a.csv), SwingConfig{});
        EXPECT_NEAR(r.totalReturnPct, a.ret, 1e-3) << a.csv;
        EXPECT_EQ(r.totalTrades, a.trades)         << a.csv;
        EXPECT_FALSE(r.beatsBuyHold)               << a.csv;
    }
}

// Rapport côte à côte (lisible dans la sortie ctest) : la conclusion
// multi-actifs en un coup d'œil
TEST(MultiAssetIntegration, PrintsSideBySideSummary) {
    std::cout << "  Actif        [prod ret%]  [B&H ret%]  [trades]\n";
    for (const auto& f : {"SPY.csv", "IWM.csv", "MDY.csv", "QQQ_TR.csv"}) {
        const auto r = runOn(dataCsv(f), prodSwingConfig());
        std::ostringstream s;
        s << "  " << std::left << std::setw(13) << f
          << std::right << std::fixed << std::setprecision(2)
          << std::setw(11) << r.totalReturnPct
          << std::setw(12) << r.buyHoldReturnPct
          << std::setw(10) << r.totalTrades;
        std::cout << s.str() << "\n";
        EXPECT_GT(r.buyHoldReturnPct, 0.0) << f;
    }
}
