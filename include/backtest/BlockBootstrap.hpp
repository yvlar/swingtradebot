#pragma once
#include "backtest/MonteCarlo.hpp"   // McResult + TradeRecord (convention D45)
#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

namespace trading {

// ─── BlockBootstrapMonteCarlo — bootstrap PAR BLOCS (Sprint 23, item 23.7) ───
// COMPLÉMENT du Monte-Carlo IID historique (MonteCarlo.hpp, INCHANGÉ — ses
// goldens restent la référence) : le tirage IID détruit l'autocorrélation de
// la séquence de trades, or les pertes arrivent en GRAPPES (régimes). Le
// moving-block bootstrap ré-échantillonne des blocs de `blockSize` trades
// CONSÉCUTIFS (ordre interne conservé) avec remise → les grappes de
// gains/pertes survivent dans les chemins simulés, la queue de drawdown est
// moins optimiste que l'IID quand les pertes sont autocorrélées.
//
// Reproductible : un seul std::mt19937 semé une fois (défaut 42), tirages en
// ordre fixe — mêmes percentiles à chaque exécution. Mêmes conventions que le
// MC historique : rendements portés au portefeuille (deployedFraction ×
// pnlPct, D45), CAGR annualisé sur la durée observée, percentiles par rang le
// plus proche.
class BlockBootstrapMonteCarlo {
public:
    BlockBootstrapMonteCarlo(double initialCapital, size_t blockSize,
                             unsigned seed = 42, size_t paths = 1000)
        : initialCapital_(initialCapital)
        , blockSize_(std::max<size_t>(1, blockSize))
        , seed_(seed)
        , paths_(paths) {}

    McResult run(const std::vector<TradeRecord>& trades,
                 double observedYears) const {
        McResult r;
        if (trades.empty() || paths_ == 0) return r;

        std::vector<double> rets;
        rets.reserve(trades.size());
        for (const auto& t : trades)
            rets.push_back(t.deployedFraction * t.pnlPct / 100.0);

        const size_t n = rets.size();
        // blockSize ≥ n : chaque bloc est LA séquence entière — tous les
        // chemins sont identiques à l'observé (dégénéré mais bien défini).
        const size_t bs = std::min(blockSize_, n);
        // Départs possibles d'un bloc COMPLET : [0, n − bs] (l'ordre interne
        // du bloc est conservé, jamais de bloc tronqué en fin de série).
        std::mt19937 rng(seed_);
        std::uniform_int_distribution<size_t> pickStart(0, n - bs);

        std::vector<double> cagrs, drawdowns;
        cagrs.reserve(paths_);
        drawdowns.reserve(paths_);

        for (size_t p = 0; p < paths_; ++p) {
            double equity = initialCapital_;
            double peak   = initialCapital_;
            double maxDd  = 0.0;
            size_t drawn  = 0;
            while (drawn < n) {
                const size_t start = pickStart(rng);
                // Concatène le bloc [start, start+bs), tronqué seulement pour
                // caler la longueur TOTALE du chemin sur n trades.
                for (size_t k = 0; k < bs && drawn < n; ++k, ++drawn) {
                    equity *= (1.0 + rets[start + k]);
                    peak    = std::max(peak, equity);
                    if (peak > 0.0)
                        maxDd = std::max(maxDd, (peak - equity) / peak * 100.0);
                }
            }
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
    size_t   blockSize_;
    unsigned seed_;
    size_t   paths_;

    // Percentile par rang le plus proche — MÊME convention que le MC
    // historique (MonteCarlo.hpp, méthode privée : dupliquée à l'identique
    // plutôt que de toucher au fichier verrouillé par les goldens).
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
