#pragma once
#include "backtest/BackTester.hpp"
#include <vector>
#include <utility>
#include <limits>

namespace trading {

// ─── Optimiseur de grille à sélection robuste (Sprint 7, item 7.2) ───────────
//
// POURQUOI. Choisir les paramètres qui maximisent une métrique sur l'historique
// est le piège classique du sur-ajustement : on trouve le « pic » chanceux d'une
// surface bruitée. Un edge réel se traduit par un PLATEAU — une zone où les
// paramètres voisins marchent AUSSI. Cet optimiseur balaie une grille 2D
// (deux axes entiers, ex. emaFast × emaSlow), évalue chaque cellule par le vrai
// moteur (`Backtester::runOn`) et expose DEUX choix :
//   - selectBest   : l'argmax brut (le pic) — à titre de comparaison ;
//   - selectRobust : l'argmax de la moyenne de voisinage 3×3 (le plateau).
// Si les deux diffèrent beaucoup, le pic est fragile.
//
// La grille produite est aussi une carte de sensibilité : voir comment le score
// varie le long de chaque axe dit si la stratégie est robuste ou au bord du vide.

class GridOptimizer {
public:
    struct ScoreCell {
        bool   valid = false;   // false = combinaison illégale (ex. emaFast≥emaSlow)
        double score = 0.0;     // métrique à maximiser (Sharpe par défaut)
    };

    struct Cell {
        int         emaFast = 0;
        int         emaSlow = 0;
        bool        valid   = false;
        double      sharpe  = 0.0;
        double      returnPct = 0.0;
        int         trades  = 0;
    };

    struct Result {
        std::vector<std::vector<Cell>> grid;   // [iFast][iSlow]
        Cell best;     // pic brut (max Sharpe)
        Cell robust;   // plateau (max moyenne de voisinage)
        std::pair<int,int> bestIdx   {-1,-1};
        std::pair<int,int> robustIdx {-1,-1};
    };

    GridOptimizer(SwingConfig base,
                  std::vector<Bar> bars,
                  double initialCapital = 10'000.0,
                  double commissionPct  = 0.001,
                  double slippagePct    = 0.0)
        : base_(std::move(base))
        , bars_(std::move(bars))
        , initialCapital_(initialCapital)
        , commissionPct_(commissionPct)
        , slippagePct_(slippagePct)
    {}

    // Balaie emaFast × emaSlow (les deux leviers de réactivité du croisement).
    // Score = Sharpe (risk-adjusted : moins sensible à un trade chanceux que le
    // retour brut). Cellule invalide si fast ≥ slow.
    Result optimizeEma(const std::vector<int>& fastVals,
                       const std::vector<int>& slowVals) {
        Result r;
        std::vector<std::vector<ScoreCell>> scores(
            fastVals.size(), std::vector<ScoreCell>(slowVals.size()));
        r.grid.assign(fastVals.size(), std::vector<Cell>(slowVals.size()));

        for (size_t i = 0; i < fastVals.size(); ++i) {
            for (size_t j = 0; j < slowVals.size(); ++j) {
                Cell c;
                c.emaFast = fastVals[i];
                c.emaSlow = slowVals[j];
                if (c.emaFast >= c.emaSlow) {   // croisement impossible
                    c.valid = false;
                    scores[i][j] = {false, 0.0};
                } else {
                    SwingConfig cfg = base_;
                    cfg.emaFast = c.emaFast;
                    cfg.emaSlow = c.emaSlow;
                    Backtester bt(cfg, "<mémoire>", initialCapital_,
                                  commissionPct_, slippagePct_);
                    auto res    = bt.runOn(bars_);
                    c.valid     = true;
                    c.sharpe    = res.sharpeRatio;
                    c.returnPct = res.totalReturnPct;
                    c.trades    = res.totalTrades;
                    scores[i][j] = {true, res.sharpeRatio};
                }
                r.grid[i][j] = c;
            }
        }

        r.bestIdx   = selectBest(scores);
        r.robustIdx = selectRobust(scores);
        if (r.bestIdx.first   >= 0) r.best   = r.grid[r.bestIdx.first][r.bestIdx.second];
        if (r.robustIdx.first >= 0) r.robust = r.grid[r.robustIdx.first][r.robustIdx.second];
        return r;
    }

    // ── Sélection (pure, testable isolément) ──────────────────────────────────

    // argmax brut du score sur les cellules valides ; {-1,-1} si aucune.
    static std::pair<int,int> selectBest(
            const std::vector<std::vector<ScoreCell>>& m) {
        std::pair<int,int> best{-1,-1};
        double bestScore = -std::numeric_limits<double>::infinity();
        for (size_t i = 0; i < m.size(); ++i)
            for (size_t j = 0; j < m[i].size(); ++j)
                if (m[i][j].valid && m[i][j].score > bestScore) {
                    bestScore = m[i][j].score;
                    best = {static_cast<int>(i), static_cast<int>(j)};
                }
        return best;
    }

    // argmax de la moyenne de voisinage 3×3 (cellule incluse) sur les voisins
    // valides — récompense les PLATEAUX. {-1,-1} si aucune cellule valide.
    static std::pair<int,int> selectRobust(
            const std::vector<std::vector<ScoreCell>>& m) {
        std::pair<int,int> best{-1,-1};
        double bestAvg = -std::numeric_limits<double>::infinity();
        for (size_t i = 0; i < m.size(); ++i) {
            for (size_t j = 0; j < m[i].size(); ++j) {
                if (!m[i][j].valid) continue;
                double sum = 0.0; int n = 0;
                for (int di = -1; di <= 1; ++di) {
                    for (int dj = -1; dj <= 1; ++dj) {
                        long ii = static_cast<long>(i) + di;
                        long jj = static_cast<long>(j) + dj;
                        if (ii < 0 || jj < 0 ||
                            ii >= static_cast<long>(m.size()) ||
                            jj >= static_cast<long>(m[ii].size())) continue;
                        if (!m[ii][jj].valid) continue;
                        sum += m[ii][jj].score; ++n;
                    }
                }
                double avg = (n > 0) ? sum / n : -std::numeric_limits<double>::infinity();
                if (avg > bestAvg) {
                    bestAvg = avg;
                    best = {static_cast<int>(i), static_cast<int>(j)};
                }
            }
        }
        return best;
    }

private:
    SwingConfig      base_;
    std::vector<Bar> bars_;
    double           initialCapital_;
    double           commissionPct_;
    double           slippagePct_;
};

} // namespace trading
