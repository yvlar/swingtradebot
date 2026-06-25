#pragma once
#include "brokers/PaperBroker.hpp"   // TradeRecord
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

namespace trading {

// ─── MonteCarlo ───────────────────────────────────────────────────────────────
// Bootstrap des trades (Sprint 7, item 7.3). Un seul chemin de backtest cache
// la VARIABILITÉ : le même edge (ou la même absence d'edge) aurait pu produire
// un drawdown bien pire selon l'ORDRE des trades. On ré-échantillonne donc la
// séquence de P&L observée AVEC REMISE et on reconstruit des milliers de chemins
// d'équité → distribution du CAGR et du drawdown (p5 / p50 / p95), pas un point.
//
// Reproductible : un seul std::mt19937 semé une fois (graine par défaut 42),
// tous les tirages dans un ordre fixe → mêmes percentiles à chaque exécution.

struct McResult {
    double cagrP5  = 0.0, cagrP50 = 0.0, cagrP95 = 0.0;  // CAGR % (p5/p50/p95)
    double ddP5    = 0.0, ddP50   = 0.0, ddP95   = 0.0;  // max drawdown % (idem)
    size_t paths   = 0;                                  // chemins simulés
};

class MonteCarlo {
public:
    MonteCarlo(double initialCapital, unsigned seed = 42, size_t paths = 1000)
        : initialCapital_(initialCapital), seed_(seed), paths_(paths) {}

    // Bootstrappe la séquence de P&L de `trades`. `observedYears` est la durée
    // calendaire du backtest observé (l'ordre est mélangé, on perd les dates ;
    // on fige donc le dénominateur d'annualisation à la période réelle). Vecteur
    // vide ou durée ≤ 0 → résultat nul.
    McResult run(const std::vector<TradeRecord>& trades,
                 double observedYears) const {
        McResult r;
        if (trades.empty() || paths_ == 0) return r;

        // Rendements multiplicatifs par trade observés.
        std::vector<double> rets;
        rets.reserve(trades.size());
        for (const auto& t : trades) rets.push_back(t.pnlPct / 100.0);

        std::mt19937 rng(seed_);
        std::uniform_int_distribution<size_t> pick(0, rets.size() - 1);

        std::vector<double> cagrs, drawdowns;
        cagrs.reserve(paths_);
        drawdowns.reserve(paths_);

        for (size_t p = 0; p < paths_; ++p) {
            double equity = initialCapital_;
            double peak   = initialCapital_;
            double maxDd  = 0.0;
            for (size_t k = 0; k < rets.size(); ++k) {
                equity *= (1.0 + rets[pick(rng)]);
                peak    = std::max(peak, equity);
                if (peak > 0.0)
                    maxDd = std::max(maxDd, (peak - equity) / peak * 100.0);
            }
            // CAGR sur le dénominateur d'années observé (fixe, comparable au
            // CAGR de tête du backtest).
            double cagr = 0.0;
            if (observedYears > 0.0 && initialCapital_ > 0.0 && equity > 0.0)
                cagr = (std::pow(equity / initialCapital_, 1.0 / observedYears)
                        - 1.0) * 100.0;
            cagrs.push_back(cagr);
            drawdowns.push_back(maxDd);
        }

        r.paths   = paths_;
        r.cagrP5  = percentile_(cagrs,      5.0);
        r.cagrP50 = percentile_(cagrs,     50.0);
        r.cagrP95 = percentile_(cagrs,     95.0);
        r.ddP5    = percentile_(drawdowns,  5.0);
        r.ddP50   = percentile_(drawdowns, 50.0);
        r.ddP95   = percentile_(drawdowns, 95.0);
        return r;
    }

private:
    double   initialCapital_;
    unsigned seed_;
    size_t   paths_;

    // Percentile par rang le plus proche (sans interpolation → déterministe).
    static double percentile_(std::vector<double>& v, double p) {
        if (v.empty()) return 0.0;
        std::sort(v.begin(), v.end());
        long idx = std::lround(p / 100.0 * static_cast<double>(v.size() - 1));
        if (idx < 0) idx = 0;
        if (idx >= static_cast<long>(v.size())) idx = static_cast<long>(v.size()) - 1;
        return v[static_cast<size_t>(idx)];
    }
};

} // namespace trading
