#pragma once
#include "backtest/BackTester.hpp"
#include <vector>
#include <map>
#include <functional>
#include <iostream>
#include <iomanip>

namespace trading {

// ─── Optimiseur de grille à sélection ROBUSTE (Sprint 7.2) ────────────────────
//
// Les paramètres de SwingConfig sont aujourd'hui des nombres magiques. Cet
// optimiseur balaie une grille (produit cartésien) et produit une CARTE DE
// SENSIBILITÉ. Le piège classique de l'optimisation est de retenir le PIC isolé
// (sur-ajusté : un voisin et la performance s'effondre). On retient au contraire
// un PLATEAU : le point dont le VOISINAGE est globalement bon. Un plateau
// généralise ; un pic se brise hors échantillon.
//
// Discipline (acceptation 7.2) : la grille est balayée EN IS (in-sample) ; le
// point retenu est ensuite jugé EN OOS (7.1), jamais en IS.

struct ParamGrid {
    std::vector<int>    emaFast;
    std::vector<int>    emaSlow;
    std::vector<double> rsiBuyMax;
    std::vector<double> rsiSellMin;
    std::vector<double> stopLossPct;
    std::vector<double> takeProfitPct;

    // Tailles des 6 axes (un axe vide compte pour 1 : on garde la valeur de base)
    std::vector<size_t> axisSizes() const {
        return {
            std::max<size_t>(1, emaFast.size()),
            std::max<size_t>(1, emaSlow.size()),
            std::max<size_t>(1, rsiBuyMax.size()),
            std::max<size_t>(1, rsiSellMin.size()),
            std::max<size_t>(1, stopLossPct.size()),
            std::max<size_t>(1, takeProfitPct.size()),
        };
    }
};

struct GridPoint {
    SwingConfig         config;
    BacktestResult      result;
    double              score = 0.0;   // critère à maximiser (alpha par défaut)
    std::vector<size_t> index;         // coordonnée 6-D dans la grille
};

class GridOptimizer {
public:
    // Le score à maximiser : par défaut l'alpha net (retour − Buy & Hold). Le
    // Sharpe ou le Calmar sont des alternatives raisonnables.
    using Scorer = std::function<double(const BacktestResult&)>;

    explicit GridOptimizer(
        SwingConfig baseConfig,
        double      initialCapital = 10'000.0,
        double      commissionPct  = 0.001,
        double      slippageBps    = 2.0,
        double      halfSpreadBps  = 0.5,
        Scorer      scorer         = [](const BacktestResult& r) { return r.alpha; }
    )
        : base_(std::move(baseConfig))
        , initialCapital_(initialCapital)
        , commissionPct_(commissionPct)
        , slippageBps_(slippageBps)
        , halfSpreadBps_(halfSpreadBps)
        , scorer_(std::move(scorer))
    {}

    // Balaie la grille EN IS et retourne la carte de sensibilité complète.
    std::vector<GridPoint> search(const std::vector<Bar>& isBars,
                                  const ParamGrid& grid) const {
        const auto fast  = axis_(grid.emaFast,       base_.emaFast);
        const auto slow  = axis_(grid.emaSlow,       base_.emaSlow);
        const auto bmax  = axis_(grid.rsiBuyMax,     base_.rsiBuyMax);
        const auto smin  = axis_(grid.rsiSellMin,    base_.rsiSellMin);
        const auto sl    = axis_(grid.stopLossPct,   base_.stopLossPct);
        const auto tp    = axis_(grid.takeProfitPct, base_.takeProfitPct);

        std::vector<GridPoint> map;
        for (size_t i0 = 0; i0 < fast.size(); ++i0)
        for (size_t i1 = 0; i1 < slow.size(); ++i1)
        for (size_t i2 = 0; i2 < bmax.size(); ++i2)
        for (size_t i3 = 0; i3 < smin.size(); ++i3)
        for (size_t i4 = 0; i4 < sl.size();   ++i4)
        for (size_t i5 = 0; i5 < tp.size();   ++i5) {
            SwingConfig cfg   = base_;
            cfg.emaFast       = fast[i0];
            cfg.emaSlow       = slow[i1];
            cfg.rsiBuyMax     = bmax[i2];
            cfg.rsiSellMin    = smin[i3];
            cfg.stopLossPct   = sl[i4];
            cfg.takeProfitPct = tp[i5];

            Backtester bt(cfg, /*csvPath=*/"", initialCapital_,
                          commissionPct_, slippageBps_, halfSpreadBps_);
            GridPoint p;
            p.config = cfg;
            p.result = bt.runOnBars(isBars);
            p.score  = scorer_(p.result);
            p.index  = {i0, i1, i2, i3, i4, i5};
            map.push_back(std::move(p));
        }
        return map;
    }

    // Sélection ROBUSTE (pure, testable isolément) : retient le point dont le
    // VOISINAGE (lui-même + chaque axe ±1) a la meilleure moyenne de score, et
    // non le pic isolé. Tranche les égalités par le premier point rencontré.
    static GridPoint selectRobust(const std::vector<GridPoint>& map,
                                  const std::vector<size_t>& axisSizes) {
        if (map.empty()) return {};

        // Index 6-D → score, pour retrouver les voisins en O(1).
        std::map<std::vector<size_t>, double> byIndex;
        for (const auto& p : map) byIndex[p.index] = p.score;

        const GridPoint* best = nullptr;
        double bestNeighbourhood = 0.0;
        for (const auto& p : map) {
            double sum = p.score;
            int    cnt = 1;
            for (size_t a = 0; a < p.index.size() && a < axisSizes.size(); ++a) {
                for (int delta : {-1, 1}) {
                    const long ni = static_cast<long>(p.index[a]) + delta;
                    if (ni < 0 || ni >= static_cast<long>(axisSizes[a])) continue;
                    auto idx = p.index;
                    idx[a] = static_cast<size_t>(ni);
                    auto it = byIndex.find(idx);
                    if (it != byIndex.end()) { sum += it->second; ++cnt; }
                }
            }
            const double avg = sum / cnt;
            if (best == nullptr || avg > bestNeighbourhood) {
                bestNeighbourhood = avg;
                best = &p;
            }
        }
        return *best;
    }

    // Confort : balaie en IS et retourne directement le choix robuste.
    GridPoint optimize(const std::vector<Bar>& isBars, const ParamGrid& grid) const {
        return selectRobust(search(isBars, grid), grid.axisSizes());
    }

private:
    template <typename T>
    static std::vector<T> axis_(const std::vector<T>& values, T fallback) {
        return values.empty() ? std::vector<T>{fallback} : values;
    }

    SwingConfig base_;
    double      initialCapital_;
    double      commissionPct_;
    double      slippageBps_;
    double      halfSpreadBps_;
    Scorer      scorer_;
};

} // namespace trading
