#pragma once
#include "backtest/BackTester.hpp"
#include "brokers/CsvDataFeed.hpp"
#include <string>
#include <vector>
#include <iostream>
#include <iomanip>

namespace trading {

// ─── Backtest multi-actifs + garde-fou total-return (Sprint 7.4, D29) ─────────
//
// Un edge validé sur un SEUL actif (QQQ) et un SEUL régime (~2019→2026, quasi
// 100 % haussier) n'est pas un edge démontré (T6). Ce harnais rejoue la même
// config sur PLUSIEURS CSV (SPY, IWM, MDY…) pour mesurer la robustesse
// inter-actifs. Il intègre le garde-fou de qualité des données D29 : tout CSV
// dont `Adj Close == Close` sur toutes les lignes (aucun dividende) est
// signalé, car son Buy & Hold — et donc l'alpha — est sous-estimé.

struct AssetSpec {
    std::string symbol;
    std::string csvPath;
};

struct AssetResult {
    std::string    symbol;
    BacktestResult result;
    bool           hasDividendInfo = false;  // false ⇒ données total-return tronquées (D29)
};

struct MultiAssetReport {
    std::vector<AssetResult> assets;
    int  assetsWithoutDividends = 0;  // combien d'actifs déclenchent le garde-fou D29
    bool allHaveDividendInfo    = false;
};

class MultiAssetBacktest {
public:
    explicit MultiAssetBacktest(
        SwingConfig config,
        double      initialCapital = 10'000.0,
        double      commissionPct  = 0.001,
        double      slippageBps    = 2.0,
        double      halfSpreadBps  = 0.5
    )
        : config_(std::move(config))
        , initialCapital_(initialCapital)
        , commissionPct_(commissionPct)
        , slippageBps_(slippageBps)
        , halfSpreadBps_(halfSpreadBps)
    {}

    // Rejoue la config sur chaque actif. Le `symbol` de la config est aligné sur
    // l'actif courant pour que le rapport soit lisible. Les avertissements de
    // qualité de données (D29) sont émis sur `warn` (stderr par défaut).
    MultiAssetReport run(const std::vector<AssetSpec>& specs,
                         std::ostream& warn = std::cerr) const {
        MultiAssetReport report;
        for (const auto& spec : specs) {
            auto feed = std::make_shared<CsvDataFeed>(spec.csvPath);

            AssetResult ar;
            ar.symbol          = spec.symbol;
            ar.hasDividendInfo = feed->hasDividendInfo();

            if (!ar.hasDividendInfo) {
                report.assetsWithoutDividends++;
                warn << "[ATTENTION qualite donnees D29] " << spec.symbol
                     << " : Adj Close == Close sur toutes les lignes — aucun "
                        "dividende. Buy & Hold et alpha SOUS-ESTIMES.\n";
            }

            SwingConfig cfg = config_;
            cfg.symbol = spec.symbol;
            Backtester bt(cfg, /*csvPath=*/"", initialCapital_,
                          commissionPct_, slippageBps_, halfSpreadBps_);
            ar.result = bt.runOnBars(feed->allBars());

            report.assets.push_back(std::move(ar));
        }
        report.allHaveDividendInfo =
            !report.assets.empty() && report.assetsWithoutDividends == 0;
        return report;
    }

    void printReport(const MultiAssetReport& r) const {
        std::cout << std::string(72, '=') << "\n"
                  << "  BACKTEST MULTI-ACTIFS — " << r.assets.size() << " actifs\n"
                  << std::string(72, '=') << "\n";
        std::cout << "  " << std::left
                  << std::setw(8)  << "Actif"
                  << std::setw(12) << "Retour %"
                  << std::setw(12) << "B&H %"
                  << std::setw(12) << "Alpha pts"
                  << std::setw(9)  << "Sharpe"
                  << "Dividendes\n";
        std::cout << std::string(72, '-') << "\n";
        for (const auto& a : r.assets) {
            std::cout << "  " << std::left
                      << std::setw(8) << a.symbol
                      << std::right << std::fixed << std::setprecision(2)
                      << std::setw(10) << a.result.totalReturnPct << "  "
                      << std::setw(10) << a.result.buyHoldReturnPct << "  "
                      << std::setw(10) << a.result.alpha << "  "
                      << std::setw(7)  << a.result.sharpeRatio << "  "
                      << std::left << (a.hasDividendInfo ? "oui" : "NON (D29)")
                      << "\n";
        }
        std::cout << std::string(72, '-') << "\n"
                  << "  Actifs sans dividende (D29) : " << r.assetsWithoutDividends
                  << " / " << r.assets.size() << "\n"
                  << std::string(72, '=') << "\n";
    }

private:
    SwingConfig config_;
    double      initialCapital_;
    double      commissionPct_;
    double      slippageBps_;
    double      halfSpreadBps_;
};

} // namespace trading
