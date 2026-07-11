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
#include "../support/TempPath.hpp"

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
//  Métriques d'objectif (Sprint 6.4, D23) : CAGR, Sortino,
//  Calmar, % temps investi, verdict vs Buy & Hold
// ════════════════════════════════════════════════════════════

// 10 000 → 14 641 sur exactement 4 ans (2020-01-01 → 2024-01-01, 1461 jours
// civils / 365,25) : CAGR = 1,4641^(1/4) − 1 = 10 % tout rond
TEST(BacktesterMetricsUnit, CagrComputedFromEquityDates) {
    auto bt = metricsOnly(10'000.0);
    auto r = bt.computeMetrics(14'641.0, barsWithCloses({100.0}),
                               {10'000.0, 14'641.0},
                               {"2020-01-01", "2024-01-01"},
                               {}, /*warmup=*/0);

    EXPECT_NEAR(r.cagrPct, 10.0, 1e-9);
}

// Sans dates exploitables, repli sur les barres : N points ≈ N/252 années.
// 252 points et +10 % → CAGR = 10 %
TEST(BacktesterMetricsUnit, CagrFallsBackToTradingDaysWithoutDates) {
    auto bt = metricsOnly(10'000.0);
    std::vector<double> curve(252, 10'000.0);
    curve.back() = 11'000.0;
    auto r = bt.computeMetrics(11'000.0, barsWithCloses({100.0}),
                               curve, {}, {}, 0);

    EXPECT_NEAR(r.cagrPct, 10.0, 1e-9);
}

// Courbe {100, 120, 108} : rendements {+0,20 ; −0,10}, moyenne 0,05.
// Déviation basse (cible 0, sur N=2 rendements) = √(0,1²/2) = 0,1/√2.
// Sortino annualisé = moyenne×252 / (dev_basse×√252) = 0,05×√252/(0,1/√2)
TEST(BacktesterMetricsUnit, SortinoUsesOnlyDownsideDeviation) {
    auto bt = metricsOnly();
    auto r = bt.computeMetrics(10'000.0, barsWithCloses({100.0}),
                               {100.0, 120.0, 108.0},
                               {}, {}, /*warmup=*/0);

    const double devBasse = 0.1 / std::sqrt(2.0);
    EXPECT_NEAR(r.sortinoRatio, 0.05 * std::sqrt(252.0) / devBasse, 1e-9);
}

// Aucun rendement négatif : déviation basse nulle → sentinelle 999 (comme le
// profit factor) si la moyenne est positive ; courbe plate → 0
TEST(BacktesterMetricsUnit, SortinoSentinelWhenNoDownside) {
    auto bt = metricsOnly();
    auto up = bt.computeMetrics(10'000.0, barsWithCloses({100.0}),
                                {100.0, 110.0, 121.0}, {}, {}, 0);
    EXPECT_DOUBLE_EQ(up.sortinoRatio, 999.0);

    auto flat = bt.computeMetrics(10'000.0, barsWithCloses({100.0}),
                                  {100.0, 100.0, 100.0}, {}, {}, 0);
    EXPECT_DOUBLE_EQ(flat.sortinoRatio, 0.0);
}

// Calmar = CAGR / max drawdown : CAGR 10 % (4 ans exacts, cf. ci-dessus) et
// drawdown 25 % (pic 12 000 → creux 9 000) → 0,4
TEST(BacktesterMetricsUnit, CalmarFromCagrAndMaxDrawdown) {
    auto bt = metricsOnly(10'000.0);
    auto r = bt.computeMetrics(14'641.0, barsWithCloses({100.0}),
                               {10'000.0, 12'000.0, 9'000.0, 13'000.0, 14'641.0},
                               {"2020-01-01", "", "", "", "2024-01-01"},
                               {}, 0);

    EXPECT_NEAR(r.maxDrawdownPct, 25.0, 1e-9);
    EXPECT_NEAR(r.calmarRatio,    10.0 / 25.0, 1e-9);
}

// Drawdown nul et CAGR positif → sentinelle 999 (pas de division par zéro)
TEST(BacktesterMetricsUnit, CalmarSentinelWhenNoDrawdown) {
    auto bt = metricsOnly(10'000.0);
    auto r = bt.computeMetrics(14'641.0, barsWithCloses({100.0}),
                               {10'000.0, 14'641.0},
                               {"2020-01-01", "2024-01-01"},
                               {}, 0);
    EXPECT_DOUBLE_EQ(r.calmarRatio, 999.0);
}

// 9 jours en position (4+2+3) sur 18 barres post-warmup (20 − 2) → 50 %
TEST(BacktesterMetricsUnit, PctTimeInvestedFromHoldDays) {
    auto bt = metricsOnly();
    std::vector<TradeRecord> trades = {
        trade(+10.0, +1.0, 4),
        trade(+10.0, +1.0, 2),
        trade(-10.0, -1.0, 3),
    };
    std::vector<double> curve(20, 10'000.0);
    auto r = bt.computeMetrics(10'000.0, barsWithCloses({100.0}),
                               curve, {}, trades, /*warmup=*/2);

    EXPECT_DOUBLE_EQ(r.pctTimeInvested, 50.0);
}

// Verdict explicite « bat-on le Buy & Hold net de coûts ? » (D23) :
// B&H = 10 % (100 → 110 depuis la fin du warmup)
TEST(BacktesterMetricsUnit, BeatsBuyHoldVerdictIsExplicit) {
    auto bt = metricsOnly(10'000.0);
    auto bars = barsWithCloses({100.0, 105.0, 110.0});

    auto gagne = bt.computeMetrics(11'500.0, bars, {}, {}, {}, /*warmup=*/0);
    EXPECT_DOUBLE_EQ(gagne.buyHoldReturnPct, 10.0);
    EXPECT_TRUE(gagne.beatsBuyHold);    // +15 % > +10 %

    auto perd = bt.computeMetrics(10'500.0, bars, {}, {}, {}, 0);
    EXPECT_FALSE(perd.beatsBuyHold);    // +5 % < +10 %
}

// Le rapport console tranche désormais la question (D23)
TEST(BacktesterMetricsUnit, PrintReportShowsObjectiveMetricsAndVerdict) {
    auto bt = metricsOnly(10'000.0);
    auto r = bt.computeMetrics(10'500.0, barsWithCloses({100.0, 105.0, 110.0}),
                               {10'000.0, 10'200.0, 10'500.0},
                               {"2024-01-02", "2024-01-03", "2024-01-04"},
                               {}, 0);

    testing::internal::CaptureStdout();
    bt.printReport(r);
    std::string out = testing::internal::GetCapturedStdout();

    EXPECT_NE(out.find("OBJECTIF"),       std::string::npos);
    EXPECT_NE(out.find("CAGR"),           std::string::npos);
    EXPECT_NE(out.find("Sortino"),        std::string::npos);
    EXPECT_NE(out.find("Calmar"),         std::string::npos);
    EXPECT_NE(out.find("Temps investi"),  std::string::npos);
    EXPECT_NE(out.find("NE BAT PAS"),     std::string::npos);
}

// ════════════════════════════════════════════════════════════
//  ReplayDataFeed — fenêtre glissante bornée par le lookback
// ════════════════════════════════════════════════════════════

class ReplayDataFeedUnit : public ::testing::Test {
protected:
    std::string path_;

    void SetUp() override {
        // Nom unique PID+compteur+tick (D59) — même motif que les fixtures SQLite.
        path_ = swingbot_test::uniqueTempName("unit_replay_", ".csv");

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
