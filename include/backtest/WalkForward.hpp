#pragma once
#include "backtest/BackTester.hpp"
#include <vector>
#include <string>
#include <iostream>
#include <iomanip>

namespace trading {

// ─── Walk-forward / split in-sample ↔ out-of-sample (Sprint 7.1, D24) ─────────
//
// « Aucune confiance dans un paramètre sans validation hors-échantillon. »
// QQQ.csv (~2019→2026) est quasi exclusivement haussier : un edge mesuré sur
// l'intégralité du fichier peut n'être qu'un sur-ajustement à ce régime. Le
// walk-forward découpe le CSV en fenêtres glissantes, chacune scindée en une
// période IN-SAMPLE (IS) suivie d'une période OUT-OF-SAMPLE (OOS). On rejoue le
// Backtester sur CHAQUE sous-période séparément (warmup en début de tranche,
// fenêtre auto-contenue grâce au ReplayDataFeed généralisé). Un edge qui ne
// tient qu'en IS — alpha positif en IS, négatif ou nul en OOS — est signalé
// « sur-ajusté » : c'est le verdict honnête que ce harnais doit produire.
//
// NB : sur une config FIXE, le walk-forward mesure la CONSTANCE de l'edge d'une
// période à l'autre. Couplé à l'optimiseur de grille (7.2), il devient un vrai
// walk-forward « optimise en IS, juge en OOS ».

struct WalkForwardWindow {
    std::string isStartDate, isEndDate;
    std::string oosStartDate, oosEndDate;
    BacktestResult is;        // performance sur la période in-sample
    BacktestResult oos;       // performance sur la période out-of-sample
    bool overfit = false;     // edge en IS mais pas en OOS → sur-ajusté
};

struct WalkForwardReport {
    std::vector<WalkForwardWindow> windows;
    int    overfitWindows = 0;  // nombre de fenêtres marquées sur-ajustées
    double avgIsAlpha     = 0.0;
    double avgOosAlpha     = 0.0;
    // Verdict global : l'edge généralise-t-il hors échantillon ? Vrai seulement
    // si l'alpha OOS moyen est positif ET aucune fenêtre n'est sur-ajustée.
    bool   generalizes    = false;
};

class WalkForwardValidator {
public:
    // Coûts par défaut alignés sur le Backtester (Sprint 6.2) : un walk-forward
    // doit juger l'edge net de coûts réalistes, jamais flatté.
    explicit WalkForwardValidator(
        SwingConfig config,
        double      initialCapital = 10'000.0,
        double      commissionPct  = 0.001,
        double      slippageBps    = 2.0,
        double      halfSpreadBps  = 0.5
    )
        : config_(std::move(config))
        , initialCapital_(initialCapital)
        , commissionPct_(commissionPct)
        , slippageBps_(slippageBps)
        , halfSpreadBps_(halfSpreadBps)
    {}

    // Prédicat de sur-ajustement (pur, testable isolément) : la stratégie
    // dégage un edge POSITIF net en IS (alpha > 0) mais ne le reproduit pas en
    // OOS (alpha <= 0). C'est exactement le piège que le walk-forward existe
    // pour détecter. Une stratégie qui perd déjà en IS n'est pas « sur-ajustée »
    // (elle ne capte aucun edge à reproduire) — elle est juste mauvaise.
    static bool isOverfit(const BacktestResult& is, const BacktestResult& oos) {
        return is.alpha > 0.0 && oos.alpha <= 0.0;
    }

    // Découpe les barres en fenêtres glissantes [IS | OOS] et rejoue le moteur
    // sur chaque sous-période. isBars / oosBars en nombre de barres (≈ jours de
    // bourse) ; stepBars = décalage entre deux fenêtres successives.
    WalkForwardReport run(const std::vector<Bar>& bars,
                          int isBars, int oosBars, int stepBars) const {
        WalkForwardReport report;
        if (isBars <= 0 || oosBars <= 0 || stepBars <= 0) return report;

        const size_t total = bars.size();
        double sumIs = 0.0, sumOos = 0.0;

        for (size_t start = 0;
             start + static_cast<size_t>(isBars) + static_cast<size_t>(oosBars) <= total;
             start += static_cast<size_t>(stepBars)) {

            const size_t isEnd  = start + static_cast<size_t>(isBars);
            const size_t oosEnd = isEnd + static_cast<size_t>(oosBars);

            std::vector<Bar> isSlice (bars.begin() + start, bars.begin() + isEnd);
            std::vector<Bar> oosSlice(bars.begin() + isEnd, bars.begin() + oosEnd);

            Backtester bt(config_, /*csvPath=*/"", initialCapital_,
                          commissionPct_, slippageBps_, halfSpreadBps_);

            WalkForwardWindow w;
            w.is  = bt.runOnBars(isSlice);
            w.oos = bt.runOnBars(oosSlice);
            w.isStartDate  = isSlice.front().date;
            w.isEndDate    = isSlice.back().date;
            w.oosStartDate = oosSlice.front().date;
            w.oosEndDate   = oosSlice.back().date;
            w.overfit      = isOverfit(w.is, w.oos);

            if (w.overfit) report.overfitWindows++;
            sumIs  += w.is.alpha;
            sumOos += w.oos.alpha;
            report.windows.push_back(std::move(w));
        }

        if (!report.windows.empty()) {
            report.avgIsAlpha  = sumIs  / report.windows.size();
            report.avgOosAlpha = sumOos / report.windows.size();
            report.generalizes = report.avgOosAlpha > 0.0 && report.overfitWindows == 0;
        }
        return report;
    }

    // ── Rapport console ─────────────────────────────────────────────────────
    void printReport(const WalkForwardReport& r) const {
        auto line = []() { std::cout << std::string(72, '=') << "\n"; };
        line();
        std::cout << "  WALK-FORWARD — IS vs OOS (" << r.windows.size() << " fenetres)\n";
        line();
        std::cout << "  " << std::left
                  << std::setw(24) << "Fenetre IS"
                  << std::setw(11) << "alpha IS"
                  << std::setw(24) << "Fenetre OOS"
                  << std::setw(11) << "alpha OOS"
                  << "Verdict\n";
        std::cout << std::string(72, '-') << "\n";
        for (const auto& w : r.windows) {
            std::cout << "  " << std::left
                      << std::setw(24) << (clip_(w.isStartDate) + ".." + clip_(w.isEndDate))
                      << std::right << std::fixed << std::setprecision(1)
                      << std::setw(9) << w.is.alpha << "  "
                      << std::left << std::setw(24)
                      << (clip_(w.oosStartDate) + ".." + clip_(w.oosEndDate))
                      << std::right << std::setw(9) << w.oos.alpha << "  "
                      << std::left << (w.overfit ? "SUR-AJUSTE" : "ok") << "\n";
        }
        std::cout << std::string(72, '-') << "\n";
        std::cout << std::fixed << std::setprecision(2)
                  << "  Alpha IS moyen : "  << r.avgIsAlpha  << " pts\n"
                  << "  Alpha OOS moyen : " << r.avgOosAlpha << " pts\n"
                  << "  Fenetres sur-ajustees : " << r.overfitWindows
                  << " / " << r.windows.size() << "\n"
                  << "  Verdict : " << (r.generalizes
                        ? "l'edge GENERALISE hors echantillon"
                        : "l'edge NE generalise PAS (sur-ajustement / pas d'edge)")
                  << "\n";
        line();
    }

private:
    static std::string clip_(const std::string& d) {
        return d.size() >= 7 ? d.substr(0, 7) : d;  // YYYY-MM
    }

    SwingConfig config_;
    double      initialCapital_;
    double      commissionPct_;
    double      slippageBps_;
    double      halfSpreadBps_;
};

} // namespace trading
