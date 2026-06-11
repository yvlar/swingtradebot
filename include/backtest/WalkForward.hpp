#pragma once
#include "backtest/BackTester.hpp"
#include <string>
#include <vector>

namespace trading {

// ─── Walk-forward IS / OOS (Sprint 7.1, D24) ─────────────────────────────────
// « Aucune confiance dans un paramètre sans validation hors-échantillon. »
// La série est découpée en fenêtres glissantes [IS | OOS] contiguës : la
// stratégie est mesurée sur la période in-sample (celle qu'un optimiseur
// verrait — Sprint 7.2) PUIS sur la période out-of-sample qui la suit
// immédiatement (celle qu'elle n'a jamais « vue »). Un edge qui ne tient
// qu'en IS est explicitement signalé « sur-ajusté ».
//
// Définition de l'edge (héritée de 6.4, D23) : battre le Buy & Hold net de
// coûts sur la période considérée (BacktestResult::beatsBuyHold).

// Une fenêtre : in-sample [isBegin, isEnd) puis out-of-sample [oosBegin, oosEnd),
// avec oosBegin == isEnd (l'OOS suit l'IS sans trou, comme en réel)
struct WalkForwardWindow {
    size_t isBegin  = 0;
    size_t isEnd    = 0;
    size_t oosBegin = 0;
    size_t oosEnd   = 0;
};

struct WalkForwardWindowResult {
    WalkForwardWindow window;
    BacktestResult    is;
    BacktestResult    oos;
    // Sur-ajustement par fenêtre : l'edge existe en IS mais disparaît en OOS
    bool surAjuste = false;
};

struct WalkForwardReport {
    std::vector<WalkForwardWindowResult> windows;
    int    windowsWithEdgeIS  = 0;
    int    windowsWithEdgeOOS = 0;
    double meanIsAlpha    = 0.0;   // alpha moyen vs B&H, fenêtres IS
    double meanOosAlpha   = 0.0;   // alpha moyen vs B&H, fenêtres OOS
    double meanIsSharpe   = 0.0;
    double meanOosSharpe  = 0.0;
    double meanIsSortino  = 0.0;
    double meanOosSortino = 0.0;
    // Signal global : de l'edge en IS, JAMAIS confirmé en OOS → tout réglage
    // « validé » sur cette base serait du sur-ajustement
    bool overfitSignal = false;
};

class WalkForwardAnalyzer {
public:
    // isBars/oosBars : tailles des fenêtres en barres ; stepBars : avance entre
    // deux fenêtres (0 = défaut = oosBars, pour des OOS jointifs sans
    // recouvrement — chaque barre n'est jugée qu'une fois en OOS)
    WalkForwardAnalyzer(
        SwingConfig        config,
        const std::string& csvPath,
        size_t             isBars,
        size_t             oosBars,
        size_t             stepBars       = 0,
        double             initialCapital = 10'000.0,
        double             commissionPct  = 0.001
    )
        : config_(std::move(config))
        , csvPath_(csvPath)
        , isBars_(isBars)
        , oosBars_(oosBars)
        , stepBars_(stepBars)
        , initialCapital_(initialCapital)
        , commissionPct_(commissionPct)
    {}

    // ── Découpage pur des fenêtres (testable sans CSV) ───────────────────────
    static std::vector<WalkForwardWindow> makeWindows(
        size_t totalBars, size_t isBars, size_t oosBars, size_t stepBars = 0)
    {
        std::vector<WalkForwardWindow> windows;
        if (isBars == 0 || oosBars == 0) return windows;
        const size_t step = stepBars > 0 ? stepBars : oosBars;

        for (size_t begin = 0; begin + isBars + oosBars <= totalBars;
             begin += step) {
            WalkForwardWindow w;
            w.isBegin  = begin;
            w.isEnd    = begin + isBars;
            w.oosBegin = w.isEnd;
            w.oosEnd   = w.isEnd + oosBars;
            windows.push_back(w);
        }
        return windows;
    }

    // ── Agrégation pure des résultats (testable sans CSV) ────────────────────
    static WalkForwardReport aggregate(std::vector<WalkForwardWindowResult> ws) {
        WalkForwardReport rep;
        rep.windows = std::move(ws);
        if (rep.windows.empty()) return rep;

        for (auto& w : rep.windows) {
            if (w.is.beatsBuyHold)  rep.windowsWithEdgeIS++;
            if (w.oos.beatsBuyHold) rep.windowsWithEdgeOOS++;
            w.surAjuste = w.is.beatsBuyHold && !w.oos.beatsBuyHold;

            rep.meanIsAlpha    += w.is.alpha;
            rep.meanOosAlpha   += w.oos.alpha;
            rep.meanIsSharpe   += w.is.sharpeRatio;
            rep.meanOosSharpe  += w.oos.sharpeRatio;
            rep.meanIsSortino  += w.is.sortinoRatio;
            rep.meanOosSortino += w.oos.sortinoRatio;
        }
        const double n = static_cast<double>(rep.windows.size());
        rep.meanIsAlpha    /= n;
        rep.meanOosAlpha   /= n;
        rep.meanIsSharpe   /= n;
        rep.meanOosSharpe  /= n;
        rep.meanIsSortino  /= n;
        rep.meanOosSortino /= n;

        // Edge présent en IS mais jamais confirmé hors-échantillon
        rep.overfitSignal = rep.windowsWithEdgeIS > 0
                         && rep.windowsWithEdgeOOS == 0;
        return rep;
    }

    // ── Exécution : un backtest IS + un backtest OOS par fenêtre ─────────────
    WalkForwardReport run() {
        const size_t totalBars = CsvDataFeed(csvPath_).size();
        std::vector<WalkForwardWindowResult> results;

        for (const auto& w : makeWindows(totalBars, isBars_, oosBars_, stepBars_)) {
            Backtester bt(config_, csvPath_, initialCapital_, commissionPct_);
            WalkForwardWindowResult r;
            r.window = w;
            r.is     = bt.run(w.isBegin,  w.isEnd);
            r.oos    = bt.run(w.oosBegin, w.oosEnd);
            results.push_back(std::move(r));
        }
        return aggregate(std::move(results));
    }

    // ── Rapport console : IS vs OOS par fenêtre + verdict ────────────────────
    void printReport(const WalkForwardReport& rep) const {
        auto ligne = []() { std::cout << std::string(78, '-') << "\n"; };
        std::cout << "\n" << std::string(78, '=') << "\n"
                  << "  WALK-FORWARD — " << config_.symbol
                  << "  (IS " << isBars_ << " barres / OOS " << oosBars_
                  << " barres)\n" << std::string(78, '=') << "\n";
        std::cout << "  " << std::left
                  << std::setw(12) << "Fenetre"
                  << std::setw(11) << "Ret IS%"  << std::setw(11) << "Ret OOS%"
                  << std::setw(11) << "Alpha IS" << std::setw(11) << "Alpha OOS"
                  << std::setw(9)  << "Sharpe"   << "Verdict\n";
        ligne();
        for (size_t i = 0; i < rep.windows.size(); ++i) {
            const auto& w = rep.windows[i];
            std::ostringstream sharpe;
            sharpe << std::fixed << std::setprecision(2)
                   << w.is.sharpeRatio << "/" << w.oos.sharpeRatio;
            std::cout << "  " << std::left << std::setw(12)
                      << ("#" + std::to_string(i + 1))
                      << std::fixed << std::setprecision(2)
                      << std::setw(11) << w.is.totalReturnPct
                      << std::setw(11) << w.oos.totalReturnPct
                      << std::setw(11) << w.is.alpha
                      << std::setw(11) << w.oos.alpha
                      << std::setw(9)  << sharpe.str()
                      << (w.surAjuste ? "SUR-AJUSTE"
                          : (w.oos.beatsBuyHold ? "edge OOS" : "pas d'edge"))
                      << "\n";
        }
        ligne();
        std::cout << std::fixed << std::setprecision(2)
                  << "  Fenetres avec edge : IS " << rep.windowsWithEdgeIS
                  << "/" << rep.windows.size()
                  << ", OOS " << rep.windowsWithEdgeOOS
                  << "/" << rep.windows.size() << "\n"
                  << "  Alpha moyen        : IS " << rep.meanIsAlpha
                  << " pts, OOS " << rep.meanOosAlpha << " pts\n"
                  << "  Verdict global     : "
                  << (rep.overfitSignal
                          ? "SUR-AJUSTE (edge IS jamais confirme en OOS)"
                          : (rep.windowsWithEdgeOOS > 0
                                 ? "edge present en OOS sur certaines fenetres"
                                 : "aucun edge demontre (ni IS ni OOS)"))
                  << "\n" << std::string(78, '=') << "\n\n";
    }

private:
    SwingConfig config_;
    std::string csvPath_;
    size_t      isBars_;
    size_t      oosBars_;
    size_t      stepBars_;
    double      initialCapital_;
    double      commissionPct_;
};

} // namespace trading
