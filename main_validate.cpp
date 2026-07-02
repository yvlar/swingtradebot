// ─── main_validate.cpp ───────────────────────────────────────────────────────
// Exécutable du HARNAIS DE VALIDATION (Sprint 7). Imprime, pour la stratégie
// telle qu'elle est câblée en production (ProdConfig.hpp) :
//   1. le backtest de référence sur QQQ (total-return),
//   2. le rapport walk-forward IS/OOS (item 7.1),
//   3. la carte de sensibilité de grille jugée en OOS (item 7.2),
//   4. la distribution Monte-Carlo CAGR/drawdown (item 7.3),
//   5. l'audit qualité des données multi-actifs (item 7.4, D29).
//
// Sortie humaine, non testée par un golden (les tests d'intégration verrouillent
// les chiffres) ; c'est l'outil à lancer pour INSPECTER l'edge — ou son absence.
//
// Chemins CSV injectés par CMake (SWINGBOT_*_CSV). Build : cible `validate`.
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "backtest/BackTester.hpp"
#include "backtest/WalkForward.hpp"
#include "backtest/GridOptimizer.hpp"
#include "backtest/MonteCarlo.hpp"
#include "backtest/DataQuality.hpp"
#include "strategies/ProdConfig.hpp"

using namespace trading;

namespace {

// Durée calendaire (années) entre deux dates « YYYY-MM-DD ».
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

void titre(const std::string& t) {
    std::cout << "\n" << std::string(60, '#') << "\n## " << t << "\n"
              << std::string(60, '#') << "\n";
}

} // namespace

int main() {
    const std::string qqq = SWINGBOT_QQQ_CSV;
    const SwingConfig cfg = prodSwingConfig();

    // 1. Backtest de référence ────────────────────────────────────────────────
    titre("1. BACKTEST DE REFERENCE — QQQ total-return (config prod)");
    Backtester bt(cfg, qqq, 10'000.0, 0.001);
    const auto bk = bt.run();
    bt.printReport(bk);

    // 2. Walk-forward IS/OOS ──────────────────────────────────────────────────
    titre("2. WALK-FORWARD (in-sample vs out-of-sample)");
    WalkForward wf(cfg, qqq, /*IS=*/700, /*OOS=*/300, /*pas=*/300);
    const auto windows = wf.run();
    wf.printReport(windows);

    // 2-bis. Walk-forward FIN (Sprint 8-bis, item 8b.3, D34) ──────────────────
    // 4 fenêtres OOS au lieu de 2 : OOS=300 (et non 250) pour laisser ~99
    // barres tradables après le warmup local de ~201 barres (SMA200).
    titre("2-bis. WALK-FORWARD FIN (IS=500/OOS=300, 4 fenetres — item 8b.3)");
    WalkForward wfFin(cfg, qqq, /*IS=*/500, /*OOS=*/300, /*pas=*/300);
    wfFin.printReport(wfFin.run());

    // 3. Optimiseur de grille jugé en OOS ─────────────────────────────────────
    // Grille PLEINE sur les axes qui pilotent la chaîne v2 (Sprint 8-bis,
    // item 8b.1) : emaFast × emaSlow × smaTrendPeriod × trailingStopPct =
    // 81 combos, jugés sur le pavage FIN (4 fenêtres OOS, item 8b.3). Les
    // axes morts de l'ancienne stratégie (rsiBuyMax, TP) restent en singleton
    // à leur valeur de chaîne. Hors timeout : le verrou CI est la grille
    // réduite de test_grid_optimizer_integration.cpp.
    titre("3. CARTE DE SENSIBILITE (grille chaine v2, Sharpe OOS, pavage fin)");
    auto objectifOos = [&](const SwingConfig& c) -> GridScore {
        WalkForward w(c, qqq, 500, 300, 300);
        const auto ws = w.run();
        GridScore s;
        if (ws.empty()) return s;
        double sh = 0, al = 0, dd = 0;
        for (const auto& x : ws) { sh += x.oos.sharpeRatio; al += x.oos.alpha; dd += x.oos.maxDrawdownPct; }
        const double k = static_cast<double>(ws.size());
        s.metric = sh / k; s.alpha = al / k; s.drawdown = dd / k;
        return s;
    };
    GridOptimizer opt({5, 9, 13}, {21, 34, 50}, {100}, {70}, {0.05}, {0.0},
                      objectifOos, cfg,
                      /*smaTrend*/ {150, 200, 250},
                      /*trailing*/ {0.03, 0.05, 0.08});
    opt.printSensitivityMap(opt.evaluate());

    // 4. Monte-Carlo des trades ───────────────────────────────────────────────
    titre("4. MONTE-CARLO (bootstrap des trades, graine 42)");
    double years = 0.0;
    if (bk.equityDates.size() >= 2)
        years = (daysFromCivil(bk.equityDates.back()) -
                 daysFromCivil(bk.equityDates.front())) / 365.25;
    const auto mc = MonteCarlo(10'000.0, 42, 2000).run(bk.trades, years);
    std::cout << std::fixed << std::setprecision(2)
              << "  " << bk.trades.size() << " trades, " << mc.paths
              << " chemins, " << years << " ans\n"
              << "  CAGR     p5=" << mc.cagrP5 << "%  p50=" << mc.cagrP50
              << "%  p95=" << mc.cagrP95 << "%\n"
              << "  Drawdown p5=" << mc.ddP5 << "%  p50=" << mc.ddP50
              << "%  p95=" << mc.ddP95 << "%\n";

    // 5. Qualité des données multi-actifs ─────────────────────────────────────
    titre("5. QUALITE DES DONNEES (total-return, D29)");
    const std::vector<std::pair<std::string, std::string>> actifs = {
        {"QQQ", qqq}, {"SPY", SWINGBOT_SPY_CSV},
        {"IWM", SWINGBOT_IWM_CSV}, {"MDY", SWINGBOT_MDY_CSV},
    };
    for (const auto& a : actifs) {
        const auto q = auditTotalReturnCsv(a.second);
        std::cout << "  " << std::left << std::setw(5) << a.first
                  << q.adjEqualsCloseRows << "/" << q.rows
                  << " lignes Adj==Close  "
                  << (q.suspectNoDividends ? "SUSPECT (sans dividende ?)"
                                           : "ok (total-return)") << "\n";
    }

    // 6. Walk-forward multi-actifs (Sprint 8-bis, item 8b.2) ──────────────────
    // La chaîne v2 ET le candidat de la grille 8b.1 (smaT=250, trail=0,05),
    // jugés par actif sur le pavage fin — un edge qui ne tient que sur QQQ
    // est un artefact.
    titre("6. WALK-FORWARD MULTI-ACTIFS (chaine v2 et candidat 8b.1, pavage fin)");
    SwingConfig candidat = cfg;
    candidat.smaTrendPeriod  = 250;
    candidat.trailingStopPct = 0.05;
    const std::vector<std::pair<std::string, SwingConfig>> variantes = {
        {"chaine v2       ", cfg}, {"candidat 8b.1   ", candidat},
    };
    std::cout << "  " << std::left << std::setw(20) << "Config"
              << std::setw(7) << "Actif"
              << std::right << std::setw(14) << "alpha OOS moy"
              << std::setw(12) << "trades OOS" << "\n";
    for (const auto& v : variantes) {
        for (const auto& a : actifs) {
            SwingConfig c = v.second;
            c.symbol = a.first;
            const auto ws = WalkForward(c, a.second, 500, 300, 300).run();
            double alpha = 0; size_t trades = 0;
            for (const auto& w : ws) { alpha += w.oos.alpha; trades += w.oos.trades.size(); }
            if (!ws.empty()) alpha /= static_cast<double>(ws.size());
            std::cout << "  " << std::left << std::setw(20) << v.first
                      << std::setw(7) << a.first
                      << std::right << std::fixed << std::setprecision(4)
                      << std::setw(14) << alpha
                      << std::setw(12) << trades << "\n";
        }
    }

    std::cout << "\n";
    return 0;
}
