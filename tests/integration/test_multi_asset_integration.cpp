// ─── Tests d'intégration : multi-actifs total-return (item 7.4, D29) ─────────
// La stratégie ne doit pas être validée sur un seul actif (QQQ) ni sur un seul
// régime. Ce test la fait tourner sur ≥ 3 actifs réels (SPY, IWM, MDY) servis
// en série total-return (Adj Close ≠ Close, dividendes réinvestis), et vérifie
// le garde-fou de qualité de données (auditTotalReturnCsv) qui détecte un export
// sans dividendes (symptôme D29). Chemins CSV injectés par CMake.
#include <gtest/gtest.h>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>
#include "backtest/BackTester.hpp"
#include "backtest/DataQuality.hpp"
#include "strategies/ProdConfig.hpp"

using namespace trading;
namespace fs = std::filesystem;

namespace {
struct Asset { std::string nom; std::string csv; };

std::vector<Asset> actifs() {
    return {
        {"SPY", SWINGBOT_SPY_CSV},
        {"IWM", SWINGBOT_IWM_CSV},
        {"MDY", SWINGBOT_MDY_CSV},
    };
}
} // namespace

// ════════════════════════════════════════════════════════════
//  La stratégie tourne sur ≥ 3 actifs sans casser
// ════════════════════════════════════════════════════════════
TEST(MultiAssetIntegration, StrategyEvaluatedOnAtLeastThreeAssets) {
    const auto liste = actifs();
    ASSERT_GE(liste.size(), 3u);

    std::cout << "  Multi-actifs (config prod, total-return)\n"
              << "  " << std::left << std::setw(6) << "Actif"
              << std::right << std::setw(12) << "Retour%"
              << std::setw(12) << "B&H%" << std::setw(12) << "Alpha"
              << std::setw(10) << "Trades" << std::setw(10) << "CAGR%" << "\n";

    for (const auto& a : liste) {
        Backtester bt(prodSwingConfig(), a.csv, 10'000.0, 0.001);
        const auto r = bt.run();

        // Le backtest produit des métriques finies et une courbe d'équité.
        EXPECT_GT(r.equityCurve.size(), 0u) << a.nom;
        EXPECT_TRUE(std::isfinite(r.totalReturnPct))   << a.nom;
        EXPECT_TRUE(std::isfinite(r.buyHoldReturnPct))  << a.nom;
        EXPECT_TRUE(std::isfinite(r.cagrPct))           << a.nom;
        EXPECT_GE(r.totalTrades, 0)                     << a.nom;

        std::cout << "  " << std::left << std::setw(6) << a.nom
                  << std::right << std::fixed << std::setprecision(2)
                  << std::setw(12) << r.totalReturnPct
                  << std::setw(12) << r.buyHoldReturnPct
                  << std::setw(12) << r.alpha
                  << std::setw(10) << r.totalTrades
                  << std::setw(10) << r.cagrPct << "\n";
    }
}

// ════════════════════════════════════════════════════════════
//  Les dividendes sont bel et bien comptés (vs le QQQ.csv d'avant, D29)
// ════════════════════════════════════════════════════════════
TEST(MultiAssetIntegration, DividendsCountedOnTotalReturnAssets) {
    // Chaque actif réel doit porter de l'information de dividende :
    // Adj Close ≠ Close sur au moins une partie des lignes.
    for (const auto& a : actifs()) {
        const auto q = auditTotalReturnCsv(a.csv);
        EXPECT_GT(q.rows, 0u) << a.nom;
        EXPECT_FALSE(q.suspectNoDividends)
            << a.nom << " : Adj Close == Close partout (export sans dividende ?)";
        EXPECT_LT(q.adjEqualsCloseRows, q.rows)
            << a.nom << " : aucune ligne ajustée";
    }

    // QQQ ré-exporté en total-return (item 7.4) n'est plus suspect non plus.
    const auto qqq = auditTotalReturnCsv(SWINGBOT_QQQ_CSV);
    EXPECT_GT(qqq.rows, 0u);
    EXPECT_FALSE(qqq.suspectNoDividends)
        << "QQQ.csv devrait être total-return après le ré-export (D29)";
}

// ════════════════════════════════════════════════════════════
//  Le garde-fou lève bien le drapeau sur un CSV sans dividende
// ════════════════════════════════════════════════════════════
TEST(MultiAssetIntegration, GuardFlagsSyntheticNoDividendCsv) {
    // Reconstitue le symptôme D29 dans un fichier temporaire : Adj == Close.
    const std::string path = "integ_dq_nodiv_" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()) + ".csv";
    {
        std::ofstream f(path);
        f << "Date,Open,High,Low,Close,Adj Close,Volume\n"
          << "2024-01-02,100,101,99,100.50,100.50,1000\n"
          << "2024-01-03,100.50,102,100,101.20,101.20,1100\n"
          << "2024-01-04,101.20,103,101,102.00,102.00,1200\n";
    }
    const auto r = auditTotalReturnCsv(path);
    EXPECT_TRUE(r.suspectNoDividends);
    EXPECT_EQ(r.rows, r.adjEqualsCloseRows);
    fs::remove(path);
}
