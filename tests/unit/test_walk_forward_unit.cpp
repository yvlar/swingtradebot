// ============================================================
//  test_walk_forward_unit.cpp  —  Tests UNITAIRES
//  Cible : WalkForwardAnalyzer (Sprint 7.1, D24) — découpage
//  in-sample / out-of-sample, agrégation du rapport, et replay
//  d'une sous-période par le Backtester (sans fuite de barres
//  hors fenêtre).
// ============================================================
#include <gtest/gtest.h>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "backtest/WalkForward.hpp"

using namespace trading;
namespace fs = std::filesystem;

// ════════════════════════════════════════════════════════════
//  makeWindows : découpage pur des fenêtres IS / OOS
// ════════════════════════════════════════════════════════════

TEST(WalkForwardUnit, NoWindowWhenNotEnoughBars) {
    // 100 barres ne peuvent pas contenir IS 80 + OOS 30
    EXPECT_TRUE(WalkForwardAnalyzer::makeWindows(100, 80, 30).empty());
    // Tailles nulles : aucune fenêtre (configuration invalide)
    EXPECT_TRUE(WalkForwardAnalyzer::makeWindows(100, 0, 30).empty());
    EXPECT_TRUE(WalkForwardAnalyzer::makeWindows(100, 80, 0).empty());
}

TEST(WalkForwardUnit, SingleWindowWhenExactFit) {
    const auto w = WalkForwardAnalyzer::makeWindows(110, 80, 30);
    ASSERT_EQ(w.size(), 1u);
    EXPECT_EQ(w[0].isBegin,  0u);
    EXPECT_EQ(w[0].isEnd,    80u);
    EXPECT_EQ(w[0].oosBegin, 80u);
    EXPECT_EQ(w[0].oosEnd,   110u);
}

TEST(WalkForwardUnit, OosFollowsIsContiguously) {
    for (const auto& w : WalkForwardAnalyzer::makeWindows(1000, 200, 50)) {
        EXPECT_EQ(w.oosBegin, w.isEnd);
        EXPECT_EQ(w.isEnd  - w.isBegin,  200u);
        EXPECT_EQ(w.oosEnd - w.oosBegin, 50u);
    }
}

TEST(WalkForwardUnit, DefaultStepEqualsOosSizeForNonOverlappingOos) {
    // Pas par défaut = taille OOS : les fenêtres OOS pavent la série sans trou
    // ni recouvrement (chaque barre post-warmup jugée une seule fois en OOS)
    const auto w = WalkForwardAnalyzer::makeWindows(1000, 200, 50);
    ASSERT_GE(w.size(), 2u);
    for (size_t i = 1; i < w.size(); ++i) {
        EXPECT_EQ(w[i].isBegin, w[i-1].isBegin + 50u);
        EXPECT_EQ(w[i].oosBegin, w[i-1].oosEnd);
    }
    // La dernière fenêtre tient entièrement dans la série
    EXPECT_LE(w.back().oosEnd, 1000u);
}

TEST(WalkForwardUnit, CustomStepIsHonored) {
    const auto w = WalkForwardAnalyzer::makeWindows(1000, 200, 50, 100);
    ASSERT_GE(w.size(), 2u);
    EXPECT_EQ(w[1].isBegin, w[0].isBegin + 100u);
}

TEST(WalkForwardUnit, WindowCountMatchesArithmetic) {
    // 1858 barres, IS 504, OOS 126, pas 126 → 1 + (1858-630)/126 = 10 fenêtres
    EXPECT_EQ(WalkForwardAnalyzer::makeWindows(1858, 504, 126).size(), 10u);
}

// ════════════════════════════════════════════════════════════
//  aggregate : verdict « sur-ajusté » et moyennes IS vs OOS
// ════════════════════════════════════════════════════════════

namespace {

// Construit un résultat de fenêtre synthétique : seuls les champs consommés
// par l'agrégation sont remplis
WalkForwardWindowResult fenetre(bool edgeIs, bool edgeOos,
                                double alphaIs, double alphaOos) {
    WalkForwardWindowResult r;
    r.is.beatsBuyHold  = edgeIs;
    r.is.alpha         = alphaIs;
    r.oos.beatsBuyHold = edgeOos;
    r.oos.alpha        = alphaOos;
    return r;
}

} // namespace

TEST(WalkForwardUnit, AggregateCountsEdgesAndFlagsPerWindowOverfit) {
    std::vector<WalkForwardWindowResult> ws = {
        fenetre(true,  false,  5.0, -3.0),   // edge IS seulement → sur-ajusté
        fenetre(true,  true,   4.0,  2.0),   // edge des deux côtés
        fenetre(false, false, -8.0, -6.0),   // aucun edge
    };
    const auto rep = WalkForwardAnalyzer::aggregate(ws);

    EXPECT_EQ(rep.windowsWithEdgeIS,  2);
    EXPECT_EQ(rep.windowsWithEdgeOOS, 1);
    ASSERT_EQ(rep.windows.size(), 3u);
    EXPECT_TRUE (rep.windows[0].surAjuste);
    EXPECT_FALSE(rep.windows[1].surAjuste);
    EXPECT_FALSE(rep.windows[2].surAjuste);
    // L'edge tient au moins une fois en OOS → pas de signal global
    EXPECT_FALSE(rep.overfitSignal);

    EXPECT_NEAR(rep.meanIsAlpha,  (5.0 + 4.0 - 8.0) / 3.0, 1e-12);
    EXPECT_NEAR(rep.meanOosAlpha, (-3.0 + 2.0 - 6.0) / 3.0, 1e-12);
}

TEST(WalkForwardUnit, AggregateRaisesOverfitSignalWhenEdgeOnlyInSample) {
    // De l'edge en IS, jamais en OOS : c'est la définition du sur-ajustement
    std::vector<WalkForwardWindowResult> ws = {
        fenetre(true, false, 6.0, -2.0),
        fenetre(true, false, 3.0, -4.0),
    };
    const auto rep = WalkForwardAnalyzer::aggregate(ws);
    EXPECT_EQ(rep.windowsWithEdgeIS,  2);
    EXPECT_EQ(rep.windowsWithEdgeOOS, 0);
    EXPECT_TRUE(rep.overfitSignal);
}

TEST(WalkForwardUnit, AggregateNoSignalWhenNoEdgeAnywhere) {
    // Aucun edge nulle part : honnête, mais pas « sur-ajusté » pour autant
    std::vector<WalkForwardWindowResult> ws = {
        fenetre(false, false, -10.0, -12.0),
    };
    const auto rep = WalkForwardAnalyzer::aggregate(ws);
    EXPECT_FALSE(rep.overfitSignal);
}

TEST(WalkForwardUnit, AggregateOnEmptyInputIsEmptyAndSilent) {
    const auto rep = WalkForwardAnalyzer::aggregate({});
    EXPECT_TRUE(rep.windows.empty());
    EXPECT_EQ(rep.windowsWithEdgeIS,  0);
    EXPECT_EQ(rep.windowsWithEdgeOOS, 0);
    EXPECT_FALSE(rep.overfitSignal);
    EXPECT_DOUBLE_EQ(rep.meanIsAlpha,  0.0);
    EXPECT_DOUBLE_EQ(rep.meanOosAlpha, 0.0);
}

// ════════════════════════════════════════════════════════════
//  Backtester::run(begin, end) : replay d'une sous-période
//  sans fuite de barres hors fenêtre (socle du walk-forward)
// ════════════════════════════════════════════════════════════

// ── Fixture : un fichier CSV temporaire par test ──────────
class BacktesterSubPeriodUnit : public ::testing::Test {
protected:
    std::string path_;

    void SetUp() override {
        path_ = "unit_wf_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) + ".csv";
    }
    void TearDown() override {
        if (fs::exists(path_)) fs::remove(path_);
        if (fs::exists(path_ + ".sub")) fs::remove(path_ + ".sub");
    }

    // Série synthétique : tendance + oscillation, assez longue pour passer le
    // warmup (37 barres en config défaut) et générer des croisements EMA
    static std::vector<double> serieSynthetique(int n) {
        std::vector<double> v;
        for (int i = 0; i < n; ++i)
            v.push_back(100.0 + 0.15 * i + 8.0 * std::sin(i / 9.0));
        return v;
    }

    // Date civile valide et croissante : 2020-01-01 + i jours (calendrier réel
    // simplifié sur 28 jours/mois pour rester trivialement valide)
    static std::string dateAt(int i) {
        int y = 2020 + i / 336, m = 1 + (i % 336) / 28, d = 1 + i % 28;
        std::ostringstream s;
        s << y << "-" << (m < 10 ? "0" : "") << m
          << "-" << (d < 10 ? "0" : "") << d;
        return s.str();
    }

    static void writeSerie(const std::string& path,
                           const std::vector<double>& closes, int firstDay) {
        std::ofstream f(path);
        f << "Date,Open,High,Low,Close,Adj Close,Volume\n";
        for (size_t i = 0; i < closes.size(); ++i) {
            const double c = closes[i];
            f << dateAt(firstDay + static_cast<int>(i)) << ","
              << c - 0.5 << "," << c + 1.0 << "," << c - 1.0 << ","
              << c << "," << c << ",1000\n";
        }
    }
};

TEST_F(BacktesterSubPeriodUnit, FullRangeRunMatchesDefaultRun) {
    writeSerie(path_, serieSynthetique(200), 0);
    SwingConfig cfg;
    Backtester bt(cfg, path_);

    const auto full = bt.run();
    const auto sub  = bt.run(0, 200);
    EXPECT_DOUBLE_EQ(sub.finalValue, full.finalValue);
    EXPECT_EQ(sub.totalTrades, full.totalTrades);
    EXPECT_EQ(sub.equityCurve.size(), full.equityCurve.size());
    EXPECT_DOUBLE_EQ(sub.buyHoldReturnPct, full.buyHoldReturnPct);
}

TEST_F(BacktesterSubPeriodUnit, SubPeriodEquityCoversExactlyTheWindow) {
    writeSerie(path_, serieSynthetique(200), 0);
    SwingConfig cfg;
    Backtester bt(cfg, path_);

    const auto r = bt.run(50, 150);
    ASSERT_EQ(r.equityCurve.size(), 100u);
    EXPECT_EQ(r.equityDates.front(), dateAt(50));
    EXPECT_EQ(r.equityDates.back(),  dateAt(149));
}

TEST_F(BacktesterSubPeriodUnit, SubPeriodSeesNoBarBeforeWindowStart) {
    // Preuve d'étanchéité : rejouer [50, 150) sur le CSV complet doit donner
    // EXACTEMENT le même résultat qu'un backtest sur un CSV ne contenant QUE
    // les barres 50..149 — au centime près, trades compris. Toute fuite de
    // barres antérieures (seed SMA des EMA) ferait diverger les signaux.
    const auto closes = serieSynthetique(200);
    writeSerie(path_, closes, 0);
    std::vector<double> fenetre(closes.begin() + 50, closes.begin() + 150);
    writeSerie(path_ + ".sub", fenetre, 50);

    SwingConfig cfg;
    const auto surFenetre = Backtester(cfg, path_).run(50, 150);
    const auto surCsvSeul = Backtester(cfg, path_ + ".sub").run();

    EXPECT_DOUBLE_EQ(surFenetre.finalValue, surCsvSeul.finalValue);
    EXPECT_EQ(surFenetre.totalTrades, surCsvSeul.totalTrades);
    EXPECT_DOUBLE_EQ(surFenetre.buyHoldReturnPct, surCsvSeul.buyHoldReturnPct);
    ASSERT_EQ(surFenetre.equityCurve.size(), surCsvSeul.equityCurve.size());
    for (size_t i = 0; i < surFenetre.equityCurve.size(); ++i)
        EXPECT_DOUBLE_EQ(surFenetre.equityCurve[i], surCsvSeul.equityCurve[i]);
}

TEST_F(BacktesterSubPeriodUnit, DegenerateRangesYieldEmptyBacktest) {
    writeSerie(path_, serieSynthetique(60), 0);
    SwingConfig cfg;
    Backtester bt(cfg, path_);

    // begin == end : aucune barre rejouée, capital intact
    const auto vide = bt.run(30, 30);
    EXPECT_TRUE(vide.equityCurve.empty());
    EXPECT_EQ(vide.totalTrades, 0);
    EXPECT_DOUBLE_EQ(vide.finalValue, vide.initialCapital);

    // Bornes hors série : tronquées sans débordement
    const auto borne = bt.run(0, 10'000);
    EXPECT_EQ(borne.equityCurve.size(), 60u);
}
