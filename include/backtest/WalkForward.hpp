#pragma once
#include "backtest/BackTester.hpp"
#include "brokers/CsvDataFeed.hpp"
#include "strategies/SwingStrategy.hpp"
#include <memory>
#include <string>
#include <vector>
#include <iostream>
#include <iomanip>

namespace trading {

// ─── WalkForward ──────────────────────────────────────────────────────────────
// Validation hors-échantillon (Sprint 7, item 7.1, D24). On ne fait AUCUNE
// confiance à un paramètre validé seulement « en arrière ». Le walk-forward
// rejoue le Backtester sur des fenêtres glissantes : une période in-sample (IS)
// suivie immédiatement d'une période out-of-sample (OOS) jamais vue par le
// réglage. Chaque fenêtre ré-amorce ses indicateurs (ReplayDataFeed::floorIdx),
// donc IS et OOS sont des backtests indépendants.
//
// Le verdict clé : un edge qui ne tient QU'EN IS (alpha IS positif, alpha OOS
// nul ou négatif) est explicitement signalé « sur-ajusté ». C'est l'outil qui
// jugera honnêtement toute modif de stratégie du Sprint 8.

struct WfWindow {
    size_t isStart = 0, isEnd = 0;     // fenêtre in-sample  [isStart, isEnd)
    size_t oosStart = 0, oosEnd = 0;   // fenêtre out-of-sample [oosStart, oosEnd)
    BacktestResult is;                 // résultat sur l'IS
    BacktestResult oos;                // résultat sur l'OOS
};

struct WfVerdict {
    bool   surajuste = false;          // edge présent en IS mais absent en OOS
    double isAlpha   = 0.0, oosAlpha  = 0.0;
    double isSharpe  = 0.0, oosSharpe = 0.0;
};

class WalkForward {
public:
    // isBars / oosBars : tailles des fenêtres IS et OOS (en barres) ; step : pas
    // de glissement entre deux fenêtres (par défaut = oosBars → OOS contiguës,
    // sans recouvrement). Capital/coûts : défauts du Backtester (cohérent golden).
    WalkForward(SwingConfig cfg, std::string csvPath,
                size_t isBars, size_t oosBars, size_t step = 0)
        : cfg_(std::move(cfg))
        , csvPath_(std::move(csvPath))
        , isBars_(isBars)
        , oosBars_(oosBars)
        , step_(step == 0 ? oosBars : step) {}

    // Construit et exécute les fenêtres glissantes. Vecteur vide si la série est
    // trop courte pour une seule fenêtre (isBars + oosBars > N).
    std::vector<WfWindow> run() const {
        std::vector<WfWindow> out;
        auto csv = std::make_shared<CsvDataFeed>(csvPath_);
        const size_t n = csv->allBars().size();
        const size_t span = isBars_ + oosBars_;
        if (span == 0 || n < span) return out;

        Backtester bt(cfg_, csvPath_);  // capital/coûts par défaut
        for (size_t s = 0; s + span <= n; s += step_) {
            WfWindow w;
            w.isStart  = s;
            w.isEnd    = s + isBars_;
            w.oosStart = w.isEnd;
            w.oosEnd   = w.isEnd + oosBars_;
            w.is  = bt.runRange(csv, w.isStart,  w.isEnd);
            w.oos = bt.runRange(csv, w.oosStart, w.oosEnd);
            out.push_back(std::move(w));
        }
        return out;
    }

    // Verdict d'une fenêtre : sur-ajusté si l'edge (alpha vs B&H) est positif en
    // IS mais s'évapore en OOS (≤ 0). C'est le test le plus dur : un réglage qui
    // « marche » en IS doit PROUVER qu'il généralise en OOS.
    static WfVerdict judge(const WfWindow& w) {
        WfVerdict v;
        v.isAlpha   = w.is.alpha;       v.oosAlpha  = w.oos.alpha;
        v.isSharpe  = w.is.sharpeRatio; v.oosSharpe = w.oos.sharpeRatio;
        v.surajuste = (w.is.alpha > 0.0 && w.oos.alpha <= 0.0);
        return v;
    }

    // Rapport console (français) : IS vs OOS par fenêtre + drapeau sur-ajusté.
    void printReport(const std::vector<WfWindow>& windows) const {
        std::cout << "\n  WALK-FORWARD — " << cfg_.symbol
                  << "  (IS=" << isBars_ << " / OOS=" << oosBars_
                  << " barres, pas=" << step_ << ")\n";
        std::cout << "  " << std::string(74, '-') << "\n";
        std::cout << "  " << std::left << std::setw(8) << "Fenetre"
                  << std::right
                  << std::setw(11) << "IS ret%"  << std::setw(11) << "IS alpha"
                  << std::setw(11) << "OOS ret%" << std::setw(11) << "OOS alpha"
                  << std::setw(10) << "OOS Shrp" << "  Verdict\n";
        std::cout << "  " << std::string(74, '-') << "\n";

        int idx = 0, surajustes = 0;
        for (const auto& w : windows) {
            const auto v = judge(w);
            if (v.surajuste) ++surajustes;
            std::cout << "  " << std::left << std::setw(8) << ("#" + std::to_string(idx++))
                      << std::right << std::fixed << std::setprecision(2)
                      << std::setw(11) << w.is.totalReturnPct
                      << std::setw(11) << w.is.alpha
                      << std::setw(11) << w.oos.totalReturnPct
                      << std::setw(11) << w.oos.alpha
                      << std::setw(10) << w.oos.sharpeRatio
                      << "  " << (v.surajuste ? "SUR-AJUSTE" : "ok") << "\n";
        }
        std::cout << "  " << std::string(74, '-') << "\n";
        std::cout << "  " << surajustes << "/" << windows.size()
                  << " fenetres sur-ajustees (edge IS qui ne tient pas en OOS)\n";
    }

private:
    SwingConfig cfg_;
    std::string csvPath_;
    size_t      isBars_;
    size_t      oosBars_;
    size_t      step_;
};

} // namespace trading
