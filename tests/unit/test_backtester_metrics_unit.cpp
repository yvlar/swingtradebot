// ============================================================
//  test_backtester_metrics_unit.cpp  —  Tests UNITAIRES
//  Cible : Backtester::computeMetrics + ReplayDataFeed
//  Le golden (Integration.Backtester*) détecte une dérive globale ;
//  ces tests localisent la formule fautive sur données synthétiques.
// ============================================================
#include <gtest/gtest.h>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include "backtest/BackTester.hpp"

using namespace trading;
namespace fs = std::filesystem;

namespace {

// Le constructeur ne lit le CSV qu'au run() : un chemin fictif suffit
// pour tester computeMetrics directement
Backtester metricsOnly(double capital = 10'000.0) {
    return Backtester(SwingConfig{}, "non-utilise.csv", capital, 0.001);
}

std::vector<Bar> barsWithCloses(const std::vector<double>& closes) {
    std::vector<Bar> bars;
    for (double c : closes) {
        Bar b;
        b.close = c;
        bars.push_back(b);
    }
    return bars;
}

TradeRecord trade(double pnl, double pnlPct, int holdDays,
                  const std::string& exitReason = "signal") {
    TradeRecord t;
    t.pnl        = pnl;
    t.pnlPct     = pnlPct;
    t.holdDays   = holdDays;
    t.exitReason = exitReason;
    t.isWin      = pnl > 0;
    return t;
}

} // namespace

// ════════════════════════════════════════════════════════════
//  Retour total, Buy & Hold, alpha
// ════════════════════════════════════════════════════════════

TEST(BacktesterMetricsUnit, TotalReturnBuyHoldAndAlpha) {
    auto bt = metricsOnly(10'000.0);

    // Buy & Hold mesuré depuis la fin du warmup (barre d'indice 2)
    auto bars = barsWithCloses({50.0, 60.0, 100.0, 120.0, 150.0});
    auto r = bt.computeMetrics(11'000.0, bars, {}, {}, {}, /*warmup=*/2);

    EXPECT_DOUBLE_EQ(r.initialCapital, 10'000.0);
    EXPECT_DOUBLE_EQ(r.finalValue,     11'000.0);
    EXPECT_DOUBLE_EQ(r.totalReturnPct,   10.0);
    EXPECT_DOUBLE_EQ(r.buyHoldReturnPct, 50.0);   // (150-100)/100
    EXPECT_DOUBLE_EQ(r.alpha,           -40.0);
}

// ════════════════════════════════════════════════════════════
//  Max drawdown
// ════════════════════════════════════════════════════════════

TEST(BacktesterMetricsUnit, MaxDrawdownOnKnownCurve) {
    auto bt = metricsOnly();

    // Pic 120 → creux 90 : -25 % ; pic 130 → 110 : -15,4 % → max 25 %
    auto bars = barsWithCloses({100.0});
    auto r = bt.computeMetrics(10'000.0, bars,
                               {100.0, 120.0, 90.0, 130.0, 110.0},
                               {}, {}, /*warmup=*/0);

    EXPECT_DOUBLE_EQ(r.maxDrawdownPct, 25.0);
}

TEST(BacktesterMetricsUnit, MonotonicCurveHasZeroDrawdown) {
    auto bt = metricsOnly();
    auto r = bt.computeMetrics(10'000.0, barsWithCloses({100.0}),
                               {100.0, 105.0, 111.0, 120.0},
                               {}, {}, 0);
    EXPECT_DOUBLE_EQ(r.maxDrawdownPct, 0.0);
}

// ════════════════════════════════════════════════════════════
//  Sharpe & volatilité (rendements journaliers annualisés)
// ════════════════════════════════════════════════════════════

// Courbe {100, 110, 110} : rendements {0,10 ; 0} → moyenne 0,05,
// écart-type 0,05 → vol = 0,05×√252×100 ; Sharpe = 0,05×252/(0,05×√252) = √252
TEST(BacktesterMetricsUnit, SharpeAndVolatilityHandComputed) {
    auto bt = metricsOnly();
    auto r = bt.computeMetrics(10'000.0, barsWithCloses({100.0}),
                               {100.0, 110.0, 110.0},
                               {}, {}, /*warmup=*/0);

    EXPECT_NEAR(r.volatilityPct, 0.05 * std::sqrt(252.0) * 100.0, 1e-9);
    EXPECT_NEAR(r.sharpeRatio,   std::sqrt(252.0),                1e-9);
}

// Équité constante : écart-type nul → Sharpe 0 (pas de division par zéro)
TEST(BacktesterMetricsUnit, FlatEquityGivesZeroSharpeAndVolatility) {
    auto bt = metricsOnly();
    auto r = bt.computeMetrics(10'000.0, barsWithCloses({100.0}),
                               {100.0, 100.0, 100.0, 100.0},
                               {}, {}, 0);

    EXPECT_DOUBLE_EQ(r.sharpeRatio,   0.0);
    EXPECT_DOUBLE_EQ(r.volatilityPct, 0.0);
}

// ════════════════════════════════════════════════════════════
//  Statistiques des trades
// ════════════════════════════════════════════════════════════

TEST(BacktesterMetricsUnit, WinRateExpectancyAndAverages) {
    auto bt = metricsOnly();

    std::vector<TradeRecord> trades = {
        trade(+100.0, +10.0, 4),
        trade( +50.0,  +5.0, 2),
        trade( -50.0,  -5.0, 3),
    };
    auto r = bt.computeMetrics(10'000.0, barsWithCloses({100.0}),
                               {}, {}, trades, 0);

    EXPECT_EQ(r.totalTrades,   3);
    EXPECT_EQ(r.winningTrades, 2);
    EXPECT_EQ(r.losingTrades,  1);
    EXPECT_NEAR(r.winRate,      200.0 / 3.0, 1e-9);
    EXPECT_DOUBLE_EQ(r.avgWinPct,   7.5);     // (10+5)/2
    EXPECT_DOUBLE_EQ(r.avgLossPct, -5.0);
    EXPECT_DOUBLE_EQ(r.profitFactor, 3.0);    // 150/50
    EXPECT_NEAR(r.expectancy, 100.0 / 3.0, 1e-9);   // (150-50)/3
    EXPECT_DOUBLE_EQ(r.avgHoldDays, 3.0);
}

// Sentinelle figée : aucun trade perdant → profit factor 999
TEST(BacktesterMetricsUnit, ProfitFactorSentinelWhenNoLoss) {
    auto bt = metricsOnly();
    std::vector<TradeRecord> trades = {trade(+100.0, +10.0, 4)};
    auto r = bt.computeMetrics(10'000.0, barsWithCloses({100.0}),
                               {}, {}, trades, 0);
    EXPECT_DOUBLE_EQ(r.profitFactor, 999.0);
}

// Aucun trade : pas de division par zéro, stats à zéro (sauf la sentinelle)
TEST(BacktesterMetricsUnit, NoTradesProducesSafeDefaults) {
    auto bt = metricsOnly();
    auto r = bt.computeMetrics(10'000.0, barsWithCloses({100.0}),
                               {}, {}, {}, 0);

    EXPECT_EQ(r.totalTrades, 0);
    EXPECT_DOUBLE_EQ(r.winRate,    0.0);
    EXPECT_DOUBLE_EQ(r.expectancy, 0.0);
    EXPECT_DOUBLE_EQ(r.avgWinPct,  0.0);
    EXPECT_DOUBLE_EQ(r.avgLossPct, 0.0);
    EXPECT_DOUBLE_EQ(r.profitFactor, 999.0);
}

// Contrat implicite figé : la classification des raisons de sortie se fait
// par recherche de sous-chaînes FRANÇAISES — si la stratégie ou le
// RiskManager reformule ses raisons, ce décompte casse
TEST(BacktesterMetricsUnit, ExitReasonsClassifiedByFrenchSubstrings) {
    auto bt = metricsOnly();

    std::vector<TradeRecord> trades = {
        trade(+10.0, +1.0, 1, "stop-loss (-5.2%)"),
        trade(+10.0, +1.0, 1, "take-profit (+10.1%)"),
        trade(+10.0, +1.0, 1, "trailing-stop (-3.0%)"),
        trade(+10.0, +1.0, 1, "signal EMA/RSI"),
        trade(+10.0, +1.0, 1, "fin-backtest"),    // ni SL/TP/trailing → signal
    };
    auto r = bt.computeMetrics(10'000.0, barsWithCloses({100.0}),
                               {}, {}, trades, 0);

    EXPECT_EQ(r.stopLossCount,   1);
    EXPECT_EQ(r.takeProfitCount, 1);
    EXPECT_EQ(r.trailingCount,   1);
    EXPECT_EQ(r.signalCount,     2);
}

// La courbe d'équité et les trades sont recopiés tels quels dans le résultat
TEST(BacktesterMetricsUnit, EquityCurveAndTradesPassedThrough) {
    auto bt = metricsOnly();
    std::vector<TradeRecord> trades = {trade(+10.0, +1.0, 1)};
    auto r = bt.computeMetrics(10'000.0, barsWithCloses({100.0}),
                               {100.0, 101.0}, {"2024-01-02", "2024-01-03"},
                               trades, 0);

    ASSERT_EQ(r.equityCurve.size(), 2u);
    ASSERT_EQ(r.equityDates.size(), 2u);
    ASSERT_EQ(r.trades.size(), 1u);
    EXPECT_EQ(r.equityDates[1], "2024-01-03");
}

// ════════════════════════════════════════════════════════════
//  Métriques d'objectif (item 6.4) — CAGR, Sortino, Calmar,
//  % temps investi, verdict « bat le Buy & Hold »
// ════════════════════════════════════════════════════════════

// CAGR sur des dates réelles : 10 000 → 11 000 entre le 2020-01-01 et le
// 2022-01-01 (731 jours) → (1,1^(365,25/731) − 1) × 100
TEST(BacktesterMetricsUnit, CagrFromEquityDates) {
    auto bt = metricsOnly(10'000.0);
    auto r = bt.computeMetrics(11'000.0, barsWithCloses({100.0}),
                               {10'000.0, 11'000.0},
                               {"2020-01-01", "2022-01-01"},
                               {}, /*warmup=*/0);

    EXPECT_NEAR(r.cagrPct, 100.0 * (std::pow(1.1, 365.25 / 731.0) - 1.0), 1e-9);
}

// Sans dates exploitables : CAGR 0, pas de crash
TEST(BacktesterMetricsUnit, CagrZeroWithoutDates) {
    auto bt = metricsOnly(10'000.0);
    auto r = bt.computeMetrics(11'000.0, barsWithCloses({100.0}),
                               {}, {}, {}, 0);
    EXPECT_DOUBLE_EQ(r.cagrPct, 0.0);
}

// Courbe {100, 120, 108} : rendements {+0,20 ; −0,10} → moyenne 0,05,
// downside dev = √(0,1²/2) → Sortino = 0,05×252/(√0,005×√252)
TEST(BacktesterMetricsUnit, SortinoHandComputed) {
    auto bt = metricsOnly();
    auto r = bt.computeMetrics(10'000.0, barsWithCloses({100.0}),
                               {100.0, 120.0, 108.0},
                               {}, {}, /*warmup=*/0);

    EXPECT_NEAR(r.sortinoRatio,
                (0.05 * 252.0) / (std::sqrt(0.005) * std::sqrt(252.0)), 1e-9);
}

// Aucun rendement négatif : downside nulle → sentinelle 999 (cohérente avec
// profitFactor) quand la moyenne est positive
TEST(BacktesterMetricsUnit, SortinoSentinelWhenNoDownside) {
    auto bt = metricsOnly();
    auto r = bt.computeMetrics(10'000.0, barsWithCloses({100.0}),
                               {100.0, 110.0, 121.0},
                               {}, {}, 0);
    EXPECT_DOUBLE_EQ(r.sortinoRatio, 999.0);
}

// Calmar = CAGR / maxDD — sur 1 an exact (365,25 jours impossibles : on
// vérifie le ratio entre les deux champs déjà testés séparément)
TEST(BacktesterMetricsUnit, CalmarIsCagrOverMaxDrawdown) {
    auto bt = metricsOnly(10'000.0);
    // Pic 12 000 → creux 10 800 : maxDD 10 % ; capital final 11 000
    auto r = bt.computeMetrics(11'000.0, barsWithCloses({100.0}),
                               {10'000.0, 12'000.0, 10'800.0, 11'000.0},
                               {"2020-01-01", "2020-06-01", "2020-09-01", "2021-01-01"},
                               {}, 0);

    ASSERT_GT(r.maxDrawdownPct, 0.0);
    EXPECT_NEAR(r.calmarRatio, r.cagrPct / r.maxDrawdownPct, 1e-9);
}

// % temps investi = Σ holdDays / barres après warmup
TEST(BacktesterMetricsUnit, PctTimeInvestedFromHoldDays) {
    auto bt = metricsOnly();
    std::vector<TradeRecord> trades = {
        trade(+100.0, +10.0, 4), trade(+50.0, +5.0, 2), trade(-50.0, -5.0, 3),
    };
    // 20 points d'équité, warmup 2 → 18 barres tradables ; 9 jours en position
    std::vector<double> curve(20, 10'000.0);
    auto r = bt.computeMetrics(10'000.0, barsWithCloses({100.0}),
                               curve, {}, trades, /*warmup=*/2);

    EXPECT_DOUBLE_EQ(r.pctTimeInvested, 50.0);
}

// Verdict explicite : la stratégie bat-elle le Buy & Hold net de coûts ?
TEST(BacktesterMetricsUnit, BeatsBuyHoldVerdict) {
    auto bt = metricsOnly(10'000.0);
    auto bars = barsWithCloses({100.0, 100.0, 100.0, 105.0});  // B&H +5 % dès warmup 2

    auto perd  = bt.computeMetrics(10'200.0, bars, {}, {}, {}, 2);  // +2 % < +5 %
    auto gagne = bt.computeMetrics(11'000.0, bars, {}, {}, {}, 2);  // +10 % > +5 %

    EXPECT_FALSE(perd.beatsBuyHold);
    EXPECT_TRUE (gagne.beatsBuyHold);
}

// ════════════════════════════════════════════════════════════
//  ReplayDataFeed — fenêtre glissante bornée par le lookback
// ════════════════════════════════════════════════════════════

class ReplayDataFeedUnit : public ::testing::Test {
protected:
    std::string path_;

    void SetUp() override {
        path_ = "unit_replay_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) + ".csv";

        // 10 barres, closes 100..109
        std::ofstream f(path_);
        f << "Date,Open,High,Low,Close,Adj Close,Volume\n";
        for (int i = 0; i < 10; ++i)
            f << "2024-01-" << (i + 10) << ",100,101,99,"
              << (100.0 + i) << "," << (100.0 + i) << ",1000\n";
    }
    void TearDown() override {
        if (fs::exists(path_)) fs::remove(path_);
    }
};

// La fenêtre servie est clampée par le lookback, même si le bot demande plus :
// c'est ce qui fige le seed SMA des EMA (et donc les valeurs golden)
TEST_F(ReplayDataFeedUnit, WindowClampedByLookback) {
    auto csv = std::make_shared<CsvDataFeed>(path_);
    ReplayDataFeed feed(csv, /*lookback=*/3);
    feed.setCursor(5);

    auto r = feed.getBars("QQQ", 100);
    ASSERT_TRUE(r.ok());
    ASSERT_EQ(r.value().size(), 3u);
    EXPECT_DOUBLE_EQ(r.value().front().close, 103.0);
    EXPECT_DOUBLE_EQ(r.value().back().close,  105.0);   // barre du curseur
}

TEST_F(ReplayDataFeedUnit, SmallerRequestReturnsFewerBars) {
    auto csv = std::make_shared<CsvDataFeed>(path_);
    ReplayDataFeed feed(csv, /*lookback=*/5);
    feed.setCursor(9);

    auto r = feed.getBars("QQQ", 2);
    ASSERT_TRUE(r.ok());
    ASSERT_EQ(r.value().size(), 2u);
    EXPECT_DOUBLE_EQ(r.value().back().close, 109.0);
}

TEST_F(ReplayDataFeedUnit, MarketAlwaysOpenInReplay) {
    auto csv = std::make_shared<CsvDataFeed>(path_);
    ReplayDataFeed feed(csv, 3);
    EXPECT_TRUE(feed.isMarketOpen());
}

// ════════════════════════════════════════════════════════════
//  printReport — smoke test du rapport console
// ════════════════════════════════════════════════════════════

TEST(BacktesterMetricsUnit, PrintReportRendersAllSections) {
    auto bt = metricsOnly();
    std::vector<TradeRecord> trades = {
        trade(+100.0, +10.0, 4, "take-profit (+10%)"),
        trade( -50.0,  -5.0, 2, "stop-loss (-5%)"),
    };
    trades[0].buyDate  = "2024-01-02";
    trades[0].sellDate = "2024-01-08";
    auto r = bt.computeMetrics(10'050.0, barsWithCloses({100.0}),
                               {100.0, 101.0}, {"2024-01-02", "2024-01-03"},
                               trades, 0);

    testing::internal::CaptureStdout();
    bt.printReport(r);
    std::string out = testing::internal::GetCapturedStdout();

    EXPECT_NE(out.find("BACKTEST"),               std::string::npos);
    EXPECT_NE(out.find("PERFORMANCE GLOBALE"),    std::string::npos);
    EXPECT_NE(out.find("STATISTIQUES DES TRADES"),std::string::npos);
    EXPECT_NE(out.find("RAISONS DE SORTIE"),      std::string::npos);
    EXPECT_NE(out.find("2024-01-02"),             std::string::npos);
}
