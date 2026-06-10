#pragma once
#include "backtest/BackTester.hpp"
#include <vector>
#include <string>
#include <numeric>

namespace trading {

// ─── Harnais de validation hors-échantillon (Sprint 7, item 7.1) ─────────────
//
// POURQUOI. Un backtest sur tout l'historique mesure la performance IN-SAMPLE :
// si on a choisi les paramètres en regardant ce même historique, le résultat est
// optimiste (sur-ajustement). « Faire de l'argent » exige une preuve OUT-OF-SAMPLE :
// la stratégie tient-elle sur des données qu'elle n'a pas servi à se calibrer ?
//
// Ce harnais ne fait QUE découper le dataset et rejouer le VRAI moteur
// (`Backtester::runOn`) sur chaque tranche. Il ne touche pas à la stratégie.
// L'optimiseur (item 7.2) viendra choisir des paramètres sur la tranche IS et les
// évaluer sur la tranche OOS ; ici, on pose l'infrastructure et un premier verdict :
// la config FIXE se comporte-t-elle pareil sur les périodes récentes (OOS) que sur
// les anciennes (IS) ?

// Une tranche évaluée du dataset
struct Fold {
    std::string    label;        // ex. "IS", "OOS", "WF#3"
    std::string    startDate;
    std::string    endDate;
    BacktestResult result;
};

struct WalkForwardResult {
    std::vector<Fold> folds;

    // Agrégats sur les seules tranches OOS (celles qui comptent pour le verdict)
    double oosMeanReturnPct  = 0.0;
    double oosMeanCagrPct    = 0.0;
    double oosWorstReturnPct = 0.0;
    int    oosBeatBuyHold    = 0;   // nb de tranches OOS qui battent le B&H
    int    oosCount          = 0;
};

class WalkForward {
public:
    WalkForward(SwingConfig config,
                std::string csvPath,
                double initialCapital = 10'000.0,
                double commissionPct  = 0.001,
                double slippagePct    = 0.0)
        : config_(std::move(config))
        , initialCapital_(initialCapital)
        , commissionPct_(commissionPct)
        , slippagePct_(slippagePct)
    {
        CsvDataFeed feed(csvPath);
        bars_ = feed.allBars();
    }

    // Construction directe depuis des barres (tests, multi-actifs à venir)
    WalkForward(SwingConfig config,
                std::vector<Bar> bars,
                double initialCapital = 10'000.0,
                double commissionPct  = 0.001,
                double slippagePct    = 0.0)
        : config_(std::move(config))
        , initialCapital_(initialCapital)
        , commissionPct_(commissionPct)
        , slippagePct_(slippagePct)
        , bars_(std::move(bars))
    {}

    // ── Split ancré IS / OOS ──────────────────────────────────────────────────
    // Les `isFraction` premières barres = in-sample, le reste = out-of-sample.
    // Chaque tranche est un backtest complet (warmup compris) sur SA fenêtre.
    WalkForwardResult anchoredSplit(double isFraction = 0.7) const {
        WalkForwardResult out;
        if (bars_.size() < 4) return out;

        size_t cut = static_cast<size_t>(bars_.size() * isFraction);
        cut = std::max<size_t>(2, std::min(cut, bars_.size() - 2));

        std::vector<Bar> is(bars_.begin(), bars_.begin() + cut);
        std::vector<Bar> oos(bars_.begin() + cut, bars_.end());

        out.folds.push_back(makeFold("IS",  is));
        out.folds.push_back(makeFold("OOS", oos));
        aggregateOos(out);
        return out;
    }

    // ── Walk-forward roulant ──────────────────────────────────────────────────
    // Découpe l'historique en `nSegments` tranches OOS consécutives de taille
    // égale et rejoue la config sur chacune. C'est la version « fenêtres
    // glissantes » : un edge réel doit tenir sur PLUSIEURS régimes, pas sur un
    // seul morceau chanceux. (L'optimiseur de l'item 7.2 calibrera sur la tranche
    // précédente avant d'évaluer chaque tranche — ici la config est fixe.)
    WalkForwardResult rolling(int nSegments) const {
        WalkForwardResult out;
        if (nSegments < 1 || bars_.size() < static_cast<size_t>(nSegments) * 4)
            return out;

        size_t seg = bars_.size() / nSegments;
        for (int k = 0; k < nSegments; ++k) {
            size_t a = k * seg;
            size_t b = (k == nSegments - 1) ? bars_.size() : (k + 1) * seg;
            std::vector<Bar> window(bars_.begin() + a, bars_.begin() + b);
            out.folds.push_back(makeFold("WF#" + std::to_string(k + 1), window));
        }
        aggregateOos(out);   // en walk-forward fixe, toutes les tranches comptent
        return out;
    }

    const std::vector<Bar>& bars() const { return bars_; }

private:
    SwingConfig      config_;
    double           initialCapital_;
    double           commissionPct_;
    double           slippagePct_;
    std::vector<Bar> bars_;

    Fold makeFold(const std::string& label, const std::vector<Bar>& window) const {
        Backtester bt(config_, "<mémoire>", initialCapital_, commissionPct_, slippagePct_);
        Fold f;
        f.label     = label;
        f.startDate = window.empty() ? "" : window.front().date;
        f.endDate   = window.empty() ? "" : window.back().date;
        f.result    = bt.runOn(window);
        return f;
    }

    // N'agrège que les tranches OOS (label commençant par "OOS" ou "WF")
    static void aggregateOos(WalkForwardResult& out) {
        double sumRet = 0, sumCagr = 0, worst = 0;
        bool first = true;
        for (const auto& f : out.folds) {
            bool isOos = f.label.rfind("OOS", 0) == 0 || f.label.rfind("WF", 0) == 0;
            if (!isOos) continue;
            out.oosCount++;
            sumRet  += f.result.totalReturnPct;
            sumCagr += f.result.cagrPct;
            if (f.result.beatsBuyHold) out.oosBeatBuyHold++;
            if (first || f.result.totalReturnPct < worst) {
                worst = f.result.totalReturnPct;
                first = false;
            }
        }
        if (out.oosCount > 0) {
            out.oosMeanReturnPct  = sumRet  / out.oosCount;
            out.oosMeanCagrPct    = sumCagr / out.oosCount;
            out.oosWorstReturnPct = worst;
        }
    }
};

} // namespace trading
