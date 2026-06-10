#pragma once
#include "backtest/BackTester.hpp"
#include <vector>
#include <algorithm>
#include <cmath>
#include <random>
#include <cstdint>

namespace trading {

// ─── Monte-Carlo / bootstrap des trades (Sprint 7, item 7.3) ─────────────────
//
// POURQUOI. Un backtest produit UN seul chemin d'équité : le retour final et le
// drawdown observés sont un échantillon de taille 1. Le même edge, joué dans un
// autre ORDRE (ou avec d'autres tirages), aurait donné une autre courbe. Le
// bootstrap ré-échantillonne les rendements de trade AVEC REMISE pour construire
// des milliers de chemins alternatifs et en tirer une DISTRIBUTION : p5/p50/p95
// du retour et du drawdown, et la probabilité de finir en perte.
//
// Ce que ça répond : « le résultat du backtest est-il typique ou chanceux ? » et
// « quel est le pire drawdown plausible (p95) que je dois être prêt à encaisser ? ».
//
// HYPOTHÈSE (assumée et documentée) : les trades sont traités comme des
// rendements i.i.d. de taille de mise comparable. Cela ignore l'autocorrélation
// (un système de tendance enchaîne les gains en tendance) et le séquencement
// réel du sizing — c'est une mesure de la robustesse de l'EDGE DE TRADE, pas une
// reconstruction de la courbe d'équité exacte. Le block-bootstrap viendra si on a
// besoin de préserver les séries (backlog).

struct MonteCarloResult {
    int    samples           = 0;
    double p5ReturnPct       = 0.0;
    double p50ReturnPct      = 0.0;
    double p95ReturnPct      = 0.0;
    double p5MaxDrawdownPct  = 0.0;
    double p50MaxDrawdownPct = 0.0;
    double p95MaxDrawdownPct = 0.0;
    double probLossPct       = 0.0;   // % de chemins finissant sous le capital initial
};

class MonteCarlo {
public:
    // Rendements de trade RELATIFS À L'ÉQUITÉ (et non au seul montant de la
    // position) : `pnl$ / capital initial`. C'est la bonne grandeur à composer —
    // utiliser `pnlPct` (rendement sur position) surévaluerait massivement la
    // distribution, car le bot n'est investi qu'une fraction du temps et size à
    // 2 % de risque (la somme des pnl/capital ≈ le retour total du backtest).
    static std::vector<double> tradeReturns(const BacktestResult& r) {
        std::vector<double> out;
        out.reserve(r.trades.size());
        const double cap = r.initialCapital > 0 ? r.initialCapital : 1.0;
        for (const auto& t : r.trades) out.push_back(t.pnl / cap);
        return out;
    }

    // Ré-échantillonne `tradeReturns` AVEC REMISE, `nPaths` fois, chaque chemin
    // ayant autant de trades que l'original. Compose l'équité (départ 1.0),
    // mesure retour terminal et max drawdown par chemin, puis renvoie les
    // percentiles. Déterministe pour un seed donné.
    static MonteCarloResult bootstrapTrades(const std::vector<double>& tradeReturns,
                                            int nPaths,
                                            std::uint64_t seed = 42) {
        MonteCarloResult out;
        if (tradeReturns.empty() || nPaths <= 0) return out;

        const size_t k = tradeReturns.size();
        std::mt19937_64 rng(seed);
        std::uniform_int_distribution<size_t> pick(0, k - 1);

        std::vector<double> terminals; terminals.reserve(nPaths);
        std::vector<double> drawdowns; drawdowns.reserve(nPaths);
        int losses = 0;

        for (int p = 0; p < nPaths; ++p) {
            double equity = 1.0, peak = 1.0, maxDD = 0.0;
            for (size_t i = 0; i < k; ++i) {
                equity *= (1.0 + tradeReturns[pick(rng)]);
                if (equity > peak) peak = equity;
                double dd = peak > 0 ? (peak - equity) / peak : 0.0;
                if (dd > maxDD) maxDD = dd;
            }
            double ret = (equity - 1.0) * 100.0;
            terminals.push_back(ret);
            drawdowns.push_back(maxDD * 100.0);
            if (ret < 0.0) ++losses;
        }

        std::sort(terminals.begin(), terminals.end());
        std::sort(drawdowns.begin(), drawdowns.end());

        out.samples           = nPaths;
        out.p5ReturnPct       = percentile(terminals, 0.05);
        out.p50ReturnPct      = percentile(terminals, 0.50);
        out.p95ReturnPct      = percentile(terminals, 0.95);
        out.p5MaxDrawdownPct  = percentile(drawdowns, 0.05);
        out.p50MaxDrawdownPct = percentile(drawdowns, 0.50);
        out.p95MaxDrawdownPct = percentile(drawdowns, 0.95);
        out.probLossPct       = 100.0 * losses / nPaths;
        return out;
    }

private:
    // Percentile par interpolation linéaire sur un vecteur DÉJÀ trié.
    static double percentile(const std::vector<double>& sorted, double p) {
        if (sorted.empty()) return 0.0;
        if (sorted.size() == 1) return sorted.front();
        double idx = p * (sorted.size() - 1);
        size_t lo  = static_cast<size_t>(std::floor(idx));
        size_t hi  = static_cast<size_t>(std::ceil(idx));
        double frac = idx - lo;
        return sorted[lo] * (1.0 - frac) + sorted[hi] * frac;
    }
};

} // namespace trading
