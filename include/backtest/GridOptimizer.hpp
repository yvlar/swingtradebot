#pragma once
#include "strategies/SwingStrategy.hpp"
#include <algorithm>
#include <functional>
#include <string>
#include <vector>
#include <cstddef>
#include <iostream>
#include <iomanip>

namespace trading {

// ─── GridOptimizer ────────────────────────────────────────────────────────────
// Optimiseur de grille de paramètres (Sprint 7, item 7.2). Les paramètres de
// SwingConfig sont des nombres magiques ; cet outil balaie un produit cartésien
// (emaFast × emaSlow × rsiBuyMax × rsiSellMin × SL × TP × smaTrendPeriod ×
// trailingStopPct — les deux derniers ajoutés au Sprint 8-bis, item 8b.1 : ce
// sont les axes qui PILOTENT la chaîne v2) et produit une CARTE DE SENSIBILITÉ.
//
// PRINCIPE ANTI-SUR-AJUSTEMENT : on ne retient JAMAIS le pic isolé (un réglage
// qui brille sur une seule cellule est probablement un artefact). On retient un
// PLATEAU — un point dont le VOISINAGE (±1 cran sur chaque axe) est lui aussi
// bon. La métrique objectif est fournie par l'appelant : en pratique le Sharpe
// OOS moyen du walk-forward (item 7.1), jamais l'in-sample. Un filtre dur
// alpha > 0 écarte les plateaux qui ne battent même pas… eux-mêmes (pas d'edge).

struct GridScore {
    double metric   = 0.0;   // métrique à maximiser (ex. Sharpe OOS)
    double drawdown = 0.0;   // pour le départage (plus bas = mieux)
    double alpha    = 0.0;   // filtre dur : un edge doit avoir alpha > 0
};

// Évalue une config et renvoie son score. Injecté → testable sans backtest.
using ObjectiveFn = std::function<GridScore(const SwingConfig&)>;

struct GridPoint {
    SwingConfig cfg;
    GridScore   score;
    double      neighborhoodAvg = 0.0;  // moyenne de la métrique sur le voisinage
};

struct GridSelection {
    GridPoint point;
    bool      passedAlphaFilter = false;  // false = aucun point n'a alpha > 0
};

class GridOptimizer {
public:
    // Les deux derniers axes (Sprint 8-bis, 8b.1) sont optionnels : un vecteur
    // vide est normalisé en SINGLETON pris sur la config de base → les appels
    // historiques à 6 axes balaient exactement comme avant.
    GridOptimizer(std::vector<int>    emaFast,
                  std::vector<int>    emaSlow,
                  std::vector<double> rsiBuyMax,
                  std::vector<double> rsiSellMin,
                  std::vector<double> stopLoss,
                  std::vector<double> takeProfit,
                  ObjectiveFn         objective,
                  SwingConfig         base = SwingConfig{},
                  std::vector<int>    smaTrendPeriod  = {},
                  std::vector<double> trailingStopPct = {})
        : emaFast_(std::move(emaFast))
        , emaSlow_(std::move(emaSlow))
        , rsiBuyMax_(std::move(rsiBuyMax))
        , rsiSellMin_(std::move(rsiSellMin))
        , stopLoss_(std::move(stopLoss))
        , takeProfit_(std::move(takeProfit))
        , smaTrendPeriod_(std::move(smaTrendPeriod))
        , trailingStopPct_(std::move(trailingStopPct))
        , objective_(std::move(objective))
        , base_(base) {
        if (smaTrendPeriod_.empty())  smaTrendPeriod_  = {base_.smaTrendPeriod};
        if (trailingStopPct_.empty()) trailingStopPct_ = {base_.trailingStopPct};
    }

    // Énumère le produit cartésien complet, évalue chaque combo et calcule la
    // moyenne de voisinage (la carte de sensibilité).
    std::vector<GridPoint> evaluate() const {
        const std::vector<size_t> dims = dims_();
        size_t total = 1;
        for (size_t d : dims) total *= (d == 0 ? 1 : d);

        std::vector<GridPoint> pts;
        pts.reserve(total);
        for (size_t lin = 0; lin < total; ++lin) {
            const auto idx = toMultiIndex_(lin, dims);
            GridPoint p;
            p.cfg                 = base_;
            p.cfg.emaFast         = emaFast_[idx[0]];
            p.cfg.emaSlow         = emaSlow_[idx[1]];
            p.cfg.rsiBuyMax       = rsiBuyMax_[idx[2]];
            p.cfg.rsiSellMin      = rsiSellMin_[idx[3]];
            p.cfg.stopLossPct     = stopLoss_[idx[4]];
            p.cfg.takeProfitPct   = takeProfit_[idx[5]];
            p.cfg.smaTrendPeriod  = smaTrendPeriod_[idx[6]];
            p.cfg.trailingStopPct = trailingStopPct_[idx[7]];
            p.score               = objective_(p.cfg);
            pts.push_back(std::move(p));
        }

        // Moyenne de voisinage : self + voisins ±1 cran sur chaque axe.
        for (size_t lin = 0; lin < total; ++lin) {
            const auto idx = toMultiIndex_(lin, dims);
            double sum = pts[lin].score.metric;
            int    cnt = 1;
            for (size_t ax = 0; ax < dims.size(); ++ax) {
                if (idx[ax] > 0) {
                    auto n = idx; --n[ax];
                    sum += pts[toLinear_(n, dims)].score.metric; ++cnt;
                }
                if (idx[ax] + 1 < dims[ax]) {
                    auto n = idx; ++n[ax];
                    sum += pts[toLinear_(n, dims)].score.metric; ++cnt;
                }
            }
            pts[lin].neighborhoodAvg = sum / cnt;
        }
        return pts;
    }

    // Sélection robuste : parmi les points qui PASSENT le filtre alpha > 0,
    // celui de plus forte moyenne de voisinage ; départage sur le drawdown le
    // plus bas. Si aucun point ne passe le filtre, on renvoie quand même le
    // meilleur plateau (passedAlphaFilter = false) — à l'appelant de décider
    // qu'il n'y a pas d'edge.
    static GridSelection selectRobustPlateau(const std::vector<GridPoint>& pts) {
        GridSelection sel;
        bool haveEligible = false;
        for (const auto& p : pts) {
            const bool eligible = p.score.alpha > 0.0;
            if (eligible && !haveEligible) {           // 1er éligible
                sel.point = p; sel.passedAlphaFilter = true; haveEligible = true;
                continue;
            }
            if (eligible && better_(p, sel.point)) sel.point = p;
        }
        if (haveEligible) return sel;

        // Repli : aucun alpha > 0 → meilleur plateau quand même, non validé.
        for (const auto& p : pts) {
            if (!sel.passedAlphaFilter && &p == &pts.front()) { sel.point = p; continue; }
            if (better_(p, sel.point)) sel.point = p;
        }
        sel.passedAlphaFilter = false;
        return sel;
    }

    // ── Sensibilité par axe (Sprint 8-bis, gate de l'item 8b.4) ──────────────
    // Pour chaque axe : moyenne, sur toutes les « lignes » de la grille (toutes
    // les coordonnées des AUTRES axes fixées), de l'écart max−min de la métrique
    // le long de cet axe. Un axe singleton vaut 0 (rien ne varie). L'axe le plus
    // sensible est celui dont dépend le plus la performance — c'est lui qui
    // décide si le trailing adaptatif ATR (8b.4) mérite d'être exploré.
    std::vector<double> axisSensitivities(const std::vector<GridPoint>& pts) const {
        const std::vector<size_t> dims = dims_();
        std::vector<double> sens(dims.size(), 0.0);
        for (size_t ax = 0; ax < dims.size(); ++ax) {
            if (dims[ax] < 2) continue;                 // singleton → 0
            double somme = 0.0; size_t lignes = 0;
            for (size_t lin = 0; lin < pts.size(); ++lin) {
                auto idx = toMultiIndex_(lin, dims);
                if (idx[ax] != 0) continue;             // une ligne = idx[ax]==0
                double lo = pts[lin].score.metric, hi = lo;
                for (size_t j = 1; j < dims[ax]; ++j) {
                    idx[ax] = j;
                    const double m = pts[toLinear_(idx, dims)].score.metric;
                    lo = std::min(lo, m); hi = std::max(hi, m);
                }
                somme += hi - lo; ++lignes;
            }
            sens[ax] = lignes ? somme / static_cast<double>(lignes) : 0.0;
        }
        return sens;
    }

    // Nom de chaque axe, dans l'ordre du constructeur (pour les rapports).
    static const char* axisName(size_t ax) {
        static const char* noms[] = {
            "emaFast", "emaSlow", "rsiBuyMax", "rsiSellMin",
            "stopLossPct", "takeProfitPct", "smaTrendPeriod", "trailingStopPct"
        };
        return ax < 8 ? noms[ax] : "?";
    }

    // Carte de sensibilité (français) : chaque combo, sa métrique et sa moyenne
    // de voisinage, avec le point retenu marqué ; puis le classement de
    // sensibilité par axe (gate 8b.4).
    void printSensitivityMap(const std::vector<GridPoint>& pts) const {
        const auto sel = selectRobustPlateau(pts);
        std::cout << "\n  CARTE DE SENSIBILITE — " << pts.size() << " combinaisons\n";
        std::cout << "  " << std::string(86, '-') << "\n";
        std::cout << "  " << std::left
                  << std::setw(8) << "emaF" << std::setw(8) << "emaS"
                  << std::setw(9) << "rsiBuy" << std::setw(9) << "rsiSell"
                  << std::setw(7) << "SL" << std::setw(7) << "TP"
                  << std::setw(7) << "smaT" << std::setw(7) << "trail"
                  << std::right << std::setw(10) << "metric"
                  << std::setw(11) << "voisinage" << "\n";
        std::cout << "  " << std::string(86, '-') << "\n";
        for (const auto& p : pts) {
            const bool chosen = sameCfg_(p.cfg, sel.point.cfg);
            std::cout << "  " << std::left
                      << std::setw(8) << p.cfg.emaFast
                      << std::setw(8) << p.cfg.emaSlow
                      << std::setw(9) << p.cfg.rsiBuyMax
                      << std::setw(9) << p.cfg.rsiSellMin
                      << std::setw(7) << p.cfg.stopLossPct
                      << std::setw(7) << p.cfg.takeProfitPct
                      << std::setw(7) << p.cfg.smaTrendPeriod
                      << std::setw(7) << p.cfg.trailingStopPct
                      << std::right << std::fixed << std::setprecision(3)
                      << std::setw(10) << p.score.metric
                      << std::setw(11) << p.neighborhoodAvg
                      << (chosen ? "  <= PLATEAU" : "") << "\n";
        }
        std::cout << "  " << std::string(86, '-') << "\n";
        std::cout << "  Retenu : " << (sel.passedAlphaFilter
                  ? "plateau robuste (alpha > 0)"
                  : "AUCUN edge (alpha <= 0 partout) — ne pas deployer") << "\n";

        // Classement de sensibilité par axe (axes balayés uniquement).
        const auto sens = axisSensitivities(pts);
        size_t plusSensible = 0;
        bool   unAxeBalaye  = false;
        std::cout << "  Sensibilite par axe (ecart moyen max-min de la metrique) :\n";
        for (size_t ax = 0; ax < sens.size(); ++ax) {
            if (dims_()[ax] < 2) continue;              // singleton : muet
            unAxeBalaye = true;
            std::cout << "    " << std::left << std::setw(16) << axisName(ax)
                      << std::right << std::fixed << std::setprecision(4)
                      << sens[ax] << "\n";
            if (sens[ax] > sens[plusSensible]) plusSensible = ax;
        }
        if (unAxeBalaye)
            std::cout << "  Axe le plus sensible : " << axisName(plusSensible) << "\n";
    }

private:
    std::vector<int>    emaFast_, emaSlow_;
    std::vector<double> rsiBuyMax_, rsiSellMin_, stopLoss_, takeProfit_;
    std::vector<int>    smaTrendPeriod_;
    std::vector<double> trailingStopPct_;
    ObjectiveFn         objective_;
    SwingConfig         base_;

    // Tailles des 8 axes, dans l'ordre du constructeur.
    std::vector<size_t> dims_() const {
        return { emaFast_.size(), emaSlow_.size(), rsiBuyMax_.size(),
                 rsiSellMin_.size(), stopLoss_.size(), takeProfit_.size(),
                 smaTrendPeriod_.size(), trailingStopPct_.size() };
    }

    // Meilleur = plus forte moyenne de voisinage ; départage drawdown plus bas.
    static bool better_(const GridPoint& a, const GridPoint& b) {
        if (a.neighborhoodAvg != b.neighborhoodAvg)
            return a.neighborhoodAvg > b.neighborhoodAvg;
        return a.score.drawdown < b.score.drawdown;
    }
    static bool sameCfg_(const SwingConfig& a, const SwingConfig& b) {
        return a.emaFast == b.emaFast && a.emaSlow == b.emaSlow
            && a.rsiBuyMax == b.rsiBuyMax && a.rsiSellMin == b.rsiSellMin
            && a.stopLossPct == b.stopLossPct && a.takeProfitPct == b.takeProfitPct
            && a.smaTrendPeriod == b.smaTrendPeriod
            && a.trailingStopPct == b.trailingStopPct;
    }
    // Conversions index linéaire <-> multi-index (mixed-radix, row-major).
    static std::vector<size_t> toMultiIndex_(size_t lin, const std::vector<size_t>& dims) {
        std::vector<size_t> idx(dims.size(), 0);
        for (size_t k = dims.size(); k-- > 0; ) {
            const size_t d = dims[k] == 0 ? 1 : dims[k];
            idx[k] = lin % d;
            lin   /= d;
        }
        return idx;
    }
    static size_t toLinear_(const std::vector<size_t>& idx, const std::vector<size_t>& dims) {
        size_t lin = 0;
        for (size_t k = 0; k < dims.size(); ++k) {
            const size_t d = dims[k] == 0 ? 1 : dims[k];
            lin = lin * d + idx[k];
        }
        return lin;
    }
};

} // namespace trading
