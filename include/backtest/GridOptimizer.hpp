#pragma once
#include "backtest/BackTester.hpp"
#include <array>
#include <functional>
#include <string>
#include <vector>

namespace trading {

// ─── Optimiseur de grille à sélection robuste (Sprint 7.2) ───────────────────
// Balaye une grille de paramètres SwingConfig et mesure chaque combinaison sur
// une période IN-SAMPLE. La sélection ne retient PAS le pic isolé (meilleur
// score brut, symptôme classique de sur-ajustement) mais le PLATEAU : le point
// dont le voisinage (±1 pas de grille sur chaque paramètre) a le meilleur
// score moyen — un réglage dont les voisins s'effondrent ne généralisera pas.
// Le choix retenu est ensuite JUGÉ en out-of-sample (jamais sélectionné
// dessus) — le harnais walk-forward (7.1) fournit les bornes IS/OOS.

// Valeurs candidates par paramètre ; une dimension vide = la valeur de la
// config de base (le paramètre n'est pas balayé)
struct ParamGrid {
    std::vector<int>    emaFast;
    std::vector<int>    emaSlow;
    std::vector<double> rsiBuyMax;
    std::vector<double> rsiSellMin;
    std::vector<double> stopLossPct;
    std::vector<double> takeProfitPct;
};

class GridOptimizer {
public:
    static constexpr size_t kDims = 6;
    static constexpr std::array<const char*, kDims> kDimNames = {
        "emaFast", "emaSlow", "rsiBuyMax", "rsiSellMin",
        "stopLossPct", "takeProfitPct"};

    // Un point de la grille : la config candidate, ses coordonnées, son score
    // IS et son score robuste (moyenne du voisinage)
    struct Point {
        SwingConfig         config;
        std::vector<size_t> coord;          // une coordonnée par dimension
        bool                valid = true;   // combo cohérent (emaFast < emaSlow)
        double              score = 0.0;    // métrique IS (jamais OOS)
        double              robustScore = std::numeric_limits<double>::lowest();
    };

    struct Report {
        std::vector<Point>        points;
        size_t                    bestIndex = 0;  // par score ROBUSTE
        BacktestResult            bestIs;
        BacktestResult            bestOos;        // le verdict qui compte
        std::vector<double>       sensitivity;    // carte par paramètre
    };

    // Métrique de score IS — par défaut l'alpha net vs Buy & Hold (l'objectif
    // explicite de 6.4 : « faire de l'argent » = battre le B&H net de coûts)
    using Score = std::function<double(const BacktestResult&)>;

    GridOptimizer(
        SwingConfig        base,
        ParamGrid          grid,
        const std::string& csvPath,
        double             initialCapital = 10'000.0,
        double             commissionPct  = 0.001,
        Score              score = [](const BacktestResult& r) { return r.alpha; }
    )
        : base_(std::move(base))
        , grid_(std::move(grid))
        , csvPath_(csvPath)
        , initialCapital_(initialCapital)
        , commissionPct_(commissionPct)
        , score_(std::move(score))
    {}

    // ── Expansion pure de la grille (produit cartésien, testable seule) ──────
    static std::vector<Point> expand(const SwingConfig& base,
                                     const ParamGrid&   grid) {
        const auto dims = effectiveDims_(base, grid);
        std::array<size_t, kDims> sizes{};
        size_t total = 1;
        for (size_t d = 0; d < kDims; ++d) {
            sizes[d] = dims[d].size();
            total   *= sizes[d];
        }

        std::vector<Point> pts;
        pts.reserve(total);
        for (size_t linear = 0; linear < total; ++linear) {
            // Décomposition en coordonnées, dimension 0 la plus lente
            std::vector<size_t> coord(kDims);
            size_t rem = linear;
            for (size_t d = kDims; d-- > 0;) {
                coord[d] = rem % sizes[d];
                rem /= sizes[d];
            }

            Point p;
            p.coord  = coord;
            p.config = base;
            p.config.emaFast       = static_cast<int>(dims[0][coord[0]]);
            p.config.emaSlow       = static_cast<int>(dims[1][coord[1]]);
            p.config.rsiBuyMax     = dims[2][coord[2]];
            p.config.rsiSellMin    = dims[3][coord[3]];
            p.config.stopLossPct   = dims[4][coord[4]];
            p.config.takeProfitPct = dims[5][coord[5]];
            // Cohérence : le croisement EMA exige rapide < lente
            p.valid = p.config.emaFast < p.config.emaSlow;
            pts.push_back(std::move(p));
        }
        return pts;
    }

    // ── Score robuste : moyenne du score sur {le point + ses voisins valides}
    // (±1 pas sur exactement une dimension). Le pic isolé est ainsi dilué par
    // ses voisins faibles ; le plateau garde sa moyenne.
    static void computeRobustScores(std::vector<Point>& pts) {
        if (pts.empty()) return;
        const auto sizes   = sizesFromPoints_(pts);
        const auto strides = stridesFromSizes_(sizes);

        for (auto& p : pts) {
            if (!p.valid) {
                p.robustScore = std::numeric_limits<double>::lowest();
                continue;
            }
            double sum = p.score;
            int    n   = 1;
            for (size_t d = 0; d < kDims; ++d) {
                const size_t self = linearIndex_(p.coord, strides);
                if (p.coord[d] + 1 < sizes[d]) {
                    const Point& q = pts[self + strides[d]];
                    if (q.valid) { sum += q.score; ++n; }
                }
                if (p.coord[d] > 0) {
                    const Point& q = pts[self - strides[d]];
                    if (q.valid) { sum += q.score; ++n; }
                }
            }
            p.robustScore = sum / n;
        }
    }

    // ── Choix final : meilleur score ROBUSTE parmi les points valides ────────
    static size_t bestByRobustScore(const std::vector<Point>& pts) {
        size_t best = pts.size();
        for (size_t i = 0; i < pts.size(); ++i) {
            if (!pts[i].valid) continue;
            if (best == pts.size() || pts[i].robustScore > pts[best].robustScore)
                best = i;
        }
        return best;
    }

    // ── Carte de sensibilité : |Δscore| moyen par pas de grille, par
    // paramètre — un paramètre très sensible est un risque de sur-ajustement
    static std::vector<double> sensitivityMap(const std::vector<Point>& pts) {
        std::vector<double> sens(kDims, 0.0);
        if (pts.empty()) return sens;
        const auto sizes   = sizesFromPoints_(pts);
        const auto strides = stridesFromSizes_(sizes);

        for (size_t d = 0; d < kDims; ++d) {
            double acc = 0.0;
            int    n   = 0;
            for (const auto& p : pts) {
                if (!p.valid || p.coord[d] + 1 >= sizes[d]) continue;
                const Point& q = pts[linearIndex_(p.coord, strides) + strides[d]];
                if (!q.valid) continue;
                acc += std::abs(q.score - p.score);
                ++n;
            }
            sens[d] = n > 0 ? acc / n : 0.0;
        }
        return sens;
    }

    // ── Exécution : scores IS pour toute la grille, choix au plateau, puis
    // verdict OOS du choix (jamais l'inverse)
    Report optimize(size_t isBegin, size_t isEnd,
                    size_t oosBegin, size_t oosEnd) {
        Report rep;
        rep.points = expand(base_, grid_);
        for (auto& p : rep.points) {
            if (!p.valid) continue;
            Backtester bt(p.config, csvPath_, initialCapital_, commissionPct_);
            p.score = score_(bt.run(isBegin, isEnd));
        }
        computeRobustScores(rep.points);
        rep.sensitivity = sensitivityMap(rep.points);
        rep.bestIndex   = bestByRobustScore(rep.points);

        if (rep.bestIndex < rep.points.size()) {
            Backtester bt(rep.points[rep.bestIndex].config, csvPath_,
                          initialCapital_, commissionPct_);
            rep.bestIs  = bt.run(isBegin, isEnd);
            rep.bestOos = bt.run(oosBegin, oosEnd);
        }
        return rep;
    }

    // ── Rapport console : carte de sensibilité + choix + verdict OOS ─────────
    void printReport(const Report& rep) const {
        std::cout << "\n" << std::string(70, '=') << "\n"
                  << "  GRILLE DE PARAMETRES — " << rep.points.size()
                  << " combinaisons (selection au plateau)\n"
                  << std::string(70, '=') << "\n"
                  << "  Sensibilite (|dScore| moyen par pas de grille) :\n";
        for (size_t d = 0; d < kDims; ++d)
            if (rep.sensitivity.size() > d && rep.sensitivity[d] > 0.0)
                std::cout << "    " << std::left << std::setw(16) << kDimNames[d]
                          << std::fixed << std::setprecision(3)
                          << rep.sensitivity[d] << "\n";

        if (rep.bestIndex < rep.points.size()) {
            const auto& b = rep.points[rep.bestIndex];
            std::cout << "  Choix (plateau) : EMA " << b.config.emaFast << "/"
                      << b.config.emaSlow
                      << "  RSI achat<" << b.config.rsiBuyMax
                      << " vente>" << b.config.rsiSellMin
                      << "  SL " << b.config.stopLossPct * 100 << "%"
                      << "  TP " << b.config.takeProfitPct * 100 << "%\n"
                      << std::fixed << std::setprecision(2)
                      << "  Score IS (alpha) : " << b.score
                      << "  (robuste " << b.robustScore << ")\n"
                      << "  Verdict OOS      : retour "
                      << rep.bestOos.totalReturnPct
                      << " %, alpha " << rep.bestOos.alpha << " pts — "
                      << (rep.bestOos.beatsBuyHold
                              ? "BAT le Buy & Hold"
                              : "NE BAT PAS le Buy & Hold")
                      << "\n";
        }
        std::cout << std::string(70, '=') << "\n\n";
    }

private:
    SwingConfig base_;
    ParamGrid   grid_;
    std::string csvPath_;
    double      initialCapital_;
    double      commissionPct_;
    Score       score_;

    // Valeurs effectives par dimension (repli sur la config de base)
    static std::array<std::vector<double>, kDims> effectiveDims_(
        const SwingConfig& base, const ParamGrid& grid) {
        auto toD = [](const std::vector<int>& v) {
            return std::vector<double>(v.begin(), v.end());
        };
        std::array<std::vector<double>, kDims> dims = {
            grid.emaFast.empty()       ? std::vector<double>{double(base.emaFast)} : toD(grid.emaFast),
            grid.emaSlow.empty()       ? std::vector<double>{double(base.emaSlow)} : toD(grid.emaSlow),
            grid.rsiBuyMax.empty()     ? std::vector<double>{base.rsiBuyMax}       : grid.rsiBuyMax,
            grid.rsiSellMin.empty()    ? std::vector<double>{base.rsiSellMin}      : grid.rsiSellMin,
            grid.stopLossPct.empty()   ? std::vector<double>{base.stopLossPct}     : grid.stopLossPct,
            grid.takeProfitPct.empty() ? std::vector<double>{base.takeProfitPct}   : grid.takeProfitPct,
        };
        return dims;
    }

    static std::array<size_t, kDims> sizesFromPoints_(
        const std::vector<Point>& pts) {
        std::array<size_t, kDims> sizes{};
        for (const auto& p : pts)
            for (size_t d = 0; d < kDims; ++d)
                sizes[d] = std::max(sizes[d], p.coord[d] + 1);
        return sizes;
    }

    static std::array<size_t, kDims> stridesFromSizes_(
        const std::array<size_t, kDims>& sizes) {
        std::array<size_t, kDims> strides{};
        strides[kDims - 1] = 1;
        for (size_t d = kDims - 1; d-- > 0;)
            strides[d] = strides[d + 1] * sizes[d + 1];
        return strides;
    }

    static size_t linearIndex_(const std::vector<size_t>& coord,
                               const std::array<size_t, kDims>& strides) {
        size_t idx = 0;
        for (size_t d = 0; d < kDims; ++d) idx += coord[d] * strides[d];
        return idx;
    }
};

} // namespace trading
