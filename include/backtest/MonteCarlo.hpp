#pragma once
#include "brokers/PaperBroker.hpp"   // TradeRecord
#include <vector>
#include <random>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <iomanip>

namespace trading {

// ─── Monte-Carlo / bootstrap des trades (Sprint 7.3) ──────────────────────────
//
// Un backtest ne produit qu'UN chemin : une seule séquence de trades, donc un
// seul retour final et un seul drawdown. Ce chemin unique peut être chanceux.
// Le bootstrap rééchantillonne les trades AVEC REMISE (même nombre, ordre
// aléatoire) sur de nombreuses itérations et compose le résultat → une
// DISTRIBUTION de retours et de drawdowns. On en tire p5 / p50 / p95 : le
// retour médian, mais surtout le scénario défavorable (p5 du retour, p95 du
// drawdown) qu'un chemin unique masque.
//
// Graine fixée → entièrement reproductible (exigence d'acceptation 7.3).

struct MonteCarloResult {
    int      iterations = 0;
    unsigned seed       = 0;
    int      sampleSize = 0;     // nombre de trades par itération
    // Distribution du retour composé total (%)
    double   returnP5   = 0.0;
    double   returnP50  = 0.0;
    double   returnP95  = 0.0;
    // Distribution du drawdown maximal (%) — toujours ≥ 0
    double   drawdownP5  = 0.0;
    double   drawdownP50 = 0.0;
    double   drawdownP95 = 0.0;
};

class MonteCarloBootstrap {
public:
    // Percentile par interpolation linéaire (pure, testable isolément). q ∈
    // [0,100]. Copie triée → ne modifie pas l'appelant.
    static double percentile(std::vector<double> values, double q) {
        if (values.empty()) return 0.0;
        std::sort(values.begin(), values.end());
        if (values.size() == 1) return values.front();
        const double rank = (q / 100.0) * (values.size() - 1);
        const double lo   = std::floor(rank);
        const double hi   = std::ceil(rank);
        const double frac = rank - lo;
        return values[static_cast<size_t>(lo)] * (1.0 - frac)
             + values[static_cast<size_t>(hi)] * frac;
    }

    // Rééchantillonne `trades` avec remise sur `iterations` chemins ; renvoie la
    // distribution (p5/p50/p95) du retour composé et du drawdown maximal.
    MonteCarloResult run(const std::vector<TradeRecord>& trades,
                         int iterations, unsigned seed) const {
        MonteCarloResult mc;
        mc.iterations = iterations;
        mc.seed       = seed;
        mc.sampleSize = static_cast<int>(trades.size());
        if (trades.empty() || iterations <= 0) return mc;

        std::mt19937 rng(seed);
        std::uniform_int_distribution<size_t> pick(0, trades.size() - 1);

        std::vector<double> returns, drawdowns;
        returns.reserve(iterations);
        drawdowns.reserve(iterations);

        for (int it = 0; it < iterations; ++it) {
            double equity = 1.0, peak = 1.0, maxDd = 0.0;
            for (size_t k = 0; k < trades.size(); ++k) {
                const double r = trades[pick(rng)].pnlPct / 100.0;
                equity *= (1.0 + r);
                peak   = std::max(peak, equity);
                if (peak > 0.0)
                    maxDd = std::max(maxDd, (peak - equity) / peak * 100.0);
            }
            returns.push_back((equity - 1.0) * 100.0);
            drawdowns.push_back(maxDd);
        }

        mc.returnP5    = percentile(returns,   5.0);
        mc.returnP50   = percentile(returns,  50.0);
        mc.returnP95   = percentile(returns,  95.0);
        mc.drawdownP5  = percentile(drawdowns, 5.0);
        mc.drawdownP50 = percentile(drawdowns, 50.0);
        mc.drawdownP95 = percentile(drawdowns, 95.0);
        return mc;
    }

    void printReport(const MonteCarloResult& mc) const {
        std::cout << std::string(58, '=') << "\n"
                  << "  MONTE-CARLO — bootstrap des trades\n"
                  << "  " << mc.iterations << " iterations, "
                  << mc.sampleSize << " trades/iter, graine " << mc.seed << "\n"
                  << std::string(58, '=') << "\n"
                  << std::fixed << std::setprecision(2)
                  << "  Retour total %   p5=" << mc.returnP5
                  << "  p50=" << mc.returnP50
                  << "  p95=" << mc.returnP95 << "\n"
                  << "  Drawdown max %   p5=" << mc.drawdownP5
                  << "  p50=" << mc.drawdownP50
                  << "  p95=" << mc.drawdownP95 << "\n"
                  << std::string(58, '=') << "\n";
    }
};

} // namespace trading
