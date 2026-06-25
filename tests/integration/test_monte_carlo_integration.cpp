// ─── Tests d'intégration : Monte-Carlo sur les trades QQQ (item 7.3) ─────────
// Backteste la config prod sur QQQ, bootstrappe la séquence de trades obtenue
// (graine fixe) et vérifie la distribution CAGR/drawdown : percentiles finis,
// ordonnés, et p50 figés (golden). Acceptation : distribution, pas un chemin.
#include <gtest/gtest.h>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <string>
#include "backtest/BackTester.hpp"
#include "backtest/MonteCarlo.hpp"
#include "strategies/ProdConfig.hpp"

using namespace trading;

namespace {
// Jours depuis l'époque civile (algorithme days_from_civil), pour mesurer la
// durée observée du backtest à partir des dates d'équité.
long daysFromCivil(const std::string& date) {
    int y = 0, m = 0, d = 0;
    if (std::sscanf(date.c_str(), "%d-%d-%d", &y, &m, &d) != 3) return 0;
    y -= m <= 2;
    const long     era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153u * (m + (m > 2 ? -3 : 9)) + 2u) / 5u + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<long>(doe) - 719468;
}
} // namespace

TEST(MonteCarloIntegration, DistributionFromQqqProdBacktestTrades) {
    Backtester bt(prodSwingConfig(), SWINGBOT_QQQ_CSV, 10'000.0, 0.001);
    const auto bk = bt.run();
    ASSERT_FALSE(bk.trades.empty());
    ASSERT_GE(bk.equityDates.size(), 2u);

    const double years =
        (daysFromCivil(bk.equityDates.back()) -
         daysFromCivil(bk.equityDates.front())) / 365.25;
    ASSERT_GT(years, 0.0);

    MonteCarlo mc(10'000.0, /*seed=*/42, /*paths=*/2000);
    const auto r = mc.run(bk.trades, years);

    EXPECT_EQ(r.paths, 2000u);

    // Percentiles finis et ordonnés.
    for (double v : {r.cagrP5, r.cagrP50, r.cagrP95, r.ddP5, r.ddP50, r.ddP95})
        EXPECT_TRUE(std::isfinite(v));
    EXPECT_LE(r.cagrP5,  r.cagrP50);
    EXPECT_LE(r.cagrP50, r.cagrP95);
    EXPECT_LE(r.ddP5,    r.ddP50);
    EXPECT_LE(r.ddP50,   r.ddP95);

    std::cout << std::fixed << std::setprecision(4)
              << "  MONTE-CARLO (2000 chemins, graine 42) sur " << bk.trades.size()
              << " trades QQQ\n"
              << "  CAGR  p5/p50/p95 : " << r.cagrP5 << " / " << r.cagrP50
              << " / " << r.cagrP95 << " %\n"
              << "  DD    p5/p50/p95 : " << r.ddP5 << " / " << r.ddP50
              << " / " << r.ddP95 << " %\n";

    // Golden : p50 figés (graine 42, 2000 chemins, 6 trades prod → reproductible).
    // Toute dérive des trades du backtest (data, stratégie) ferait bouger ces
    // médianes ; le golden de backtest les attraperait d'abord.
    EXPECT_NEAR(r.cagrP50, 1.9039, 1e-3);
    EXPECT_NEAR(r.ddP50,   2.4668, 1e-3);
}
