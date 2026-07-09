#pragma once
#include "backtest/RotationBacktester.hpp"     // AlignedAxis, alignOnCommonDates (réutilisés)
#include "backtest/VolScaledBacktester.hpp"    // mmTargetWeight, mmShouldRebalance (réutilisées)
#include "brokers/CsvDataFeed.hpp"
#include "brokers/PaperBroker.hpp"      // TradeRecord
#include <string>
#include <vector>
#include <cmath>
#include <cstdio>
#include <algorithm>

namespace trading {

// ─── VixScaledBacktester ──────────────────────────────────────────────────────
// Réouverture Sprint 18 (piste §5.2), variante à VOLATILITÉ IMPLICITE : le
// scaling continu Moreira-Muir piloté par le NIVEAU du VIX au lieu de la vol
// réalisée. Le VIX EST déjà une volatilité annualisée en points de % → le poids
// est décidable dès la PREMIÈRE barre (aucun warmup, contrairement au
// VolScaled dont la vol réalisée exige volLookback − 1 barres, et au binaire
// VixRegime dont la médiane exigeait refLookback − 1 barres) :
//     w*[i] = min(maxWeight, targetVixPct / vix[i]).
//
// DEUX séries alignées (QQQ tradé, VIX signal) : on réutilise `alignOnCommonDates`
// / `AlignedAxis` exactement comme VixRegimeBacktester. **Seule la série 0 (QQQ)
// est TRADÉE** ; la série 1 (VIX) n'est qu'un SIGNAL — le VIX n'est pas un actif
// investissable. Moteur SÉPARÉ (offline, ne touche NI la prod NI aucun golden).
// Réutilise mmTargetWeight / mmShouldRebalance (VolScaledBacktester) — même
// formule de poids et même bande anti-churn, coût proportionnel à |Δw| (D22).
//
// Conventions reprises (identiques à VolScaled/VixRegime) :
//  - anti look-ahead (B2, D37) : décision au close[i] (VIX ≤ i), rendement QQQ
//    gagné de i à i+1 ;
//  - ré-amorçage PAR fenêtre (trivial ici : le signal est le NIVEAU du VIX,
//    aucune statistique glissante à ré-amorcer) ;
//  - coûts par côté pondérés par |Δw| ; liquidation forcée en fin de fenêtre.

// Discipline & coûts (défauts miroir du Backtester/PaperBroker, D22). Pas de
// volLookback : le niveau du VIX EST la vol annualisée (D33 — aucun champ mort).
struct VixScaledConfig {
    double targetVixPct   = 15.0;      // cible de vol ANNUALISÉE (%) : w* = cible/VIX
    double maxWeight      = 1.0;       // plafond d'exposition (1,0 = pas de levier)
    double rebalanceBand  = 0.05;      // rebalancer seulement si |w* − w| > bande
    double initialCapital = 10'000.0;
    double commissionPct  = 0.001;     // 0,1 % par côté
    double slippageBps    = 2.0;       // 2 bps par côté
    double halfSpreadBps  = 0.5;       // 0,5 bp par côté
};

struct VixScaledResult {
    double initialCapital = 0.0;
    double finalValue     = 0.0;
    double totalReturnPct = 0.0;
    // Références Buy & Hold de l'actif TRADÉ (QQQ) sur la fenêtre. Directionnel
    // → jugé sur l'ALPHA vs B&H net de coûts (repli Sprint 8 : Sharpe ≥ B&H ET
    // DD réduit ≥ 50 %).
    double buyHoldPct            = 0.0;
    double buyHoldMaxDrawdownPct = 0.0;
    double buyHoldSharpe         = 0.0;
    double alphaVsBuyHold        = 0.0;
    double maxDrawdownPct = 0.0;
    double cagrPct        = 0.0;
    double calmarRatio    = 0.0;
    double sharpeRatio    = 0.0;
    double sortinoRatio   = 0.0;
    double pctTimeInvested = 0.0;
    double avgWeight       = 0.0;    // poids moyen détenu (cash drag continu)
    double turnover        = 0.0;    // Σ|Δw| (churn total)
    int    rebalanceCount  = 0;      // rebalancements effectifs (bande franchie)
    std::vector<double>      equityCurve;
    std::vector<std::string> equityDates;
    std::vector<TradeRecord> trades;
};

class VixScaledBacktester {
public:
    // csvPaths = { actif TRADÉ (QQQ), indice SIGNAL (VIX) }. Aligne les deux
    // séries sur leurs dates communes (le VIX ~1990 déborde QQQ ~1999 → borné).
    VixScaledBacktester(VixScaledConfig cfg, std::vector<std::string> csvPaths)
        : cfg_(std::move(cfg)), csvPaths_(std::move(csvPaths)) {
        std::vector<std::vector<Bar>> series;
        series.reserve(csvPaths_.size());
        for (const auto& p : csvPaths_)
            series.push_back(CsvDataFeed(p).allBars());
        axis_ = alignOnCommonDates(series);
    }

    // Seam de test : moteur sur un axe déjà aligné (2 séries synthétiques).
    static VixScaledBacktester fromAxis(VixScaledConfig cfg, AlignedAxis axis) {
        VixScaledBacktester v(std::move(cfg), std::vector<std::string>{});
        v.axis_ = std::move(axis);
        return v;
    }

    VixScaledResult run() const { return runRange(0, axis_.size()); }

    VixScaledResult runRange(size_t startIdx, size_t endIdx) const {
        return compute_(startIdx, endIdx);
    }

    const AlignedAxis& axis() const { return axis_; }

private:
    VixScaledConfig          cfg_;
    std::vector<std::string> csvPaths_;
    AlignedAxis              axis_;

    static double perSideCost_(const VixScaledConfig& c) {
        return c.commissionPct + (c.slippageBps + c.halfSpreadBps) / 10'000.0;
    }

    static long vxsDaysFromCivil_(const std::string& date) {
        int y = 0, m = 0, d = 0;
        if (date.size() < 10
            || std::sscanf(date.c_str(), "%d-%d-%d", &y, &m, &d) != 3
            || m < 1 || m > 12 || d < 1 || d > 31)
            return -1;
        y -= m <= 2;
        const long     era = (y >= 0 ? y : y - 399) / 400;
        const unsigned yoe = static_cast<unsigned>(y - era * 400);
        const unsigned doy = (153u * (m + (m > 2 ? -3 : 9)) + 2u) / 5u + d - 1;
        const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return era * 146097 + static_cast<long>(doe) - 719468;
    }

    static double maxDrawdownOf_(const std::vector<double>& px, size_t lo, size_t hi) {
        double peak = 0.0, dd = 0.0;
        for (size_t i = lo; i < hi && i < px.size(); ++i) {
            peak = std::max(peak, px[i]);
            if (peak > 0.0) dd = std::max(dd, (peak - px[i]) / peak * 100.0);
        }
        return dd;
    }

    static void sharpeSortino_(const std::vector<double>& rets,
                               double& sharpe, double& sortino) {
        sharpe = 0.0; sortino = 0.0;
        if (rets.empty()) return;
        double mean = 0.0;
        for (double x : rets) mean += x;
        mean /= static_cast<double>(rets.size());
        double var = 0.0, downVar = 0.0;
        for (double x : rets) { var += (x - mean) * (x - mean); if (x < 0) downVar += x * x; }
        var     /= static_cast<double>(rets.size());
        downVar /= static_cast<double>(rets.size());
        const double stddev  = std::sqrt(var);
        const double downDev = std::sqrt(downVar);
        sharpe  = stddev  > 0.0 ? (mean * 252.0) / (stddev  * std::sqrt(252.0)) : 0.0;
        sortino = downDev > 0.0 ? (mean * 252.0) / (downDev * std::sqrt(252.0))
                                : (mean > 0.0 ? 999.0 : 0.0);
    }

    VixScaledResult compute_(size_t startIdx, size_t endIdx) const {
        VixScaledResult r;
        r.initialCapital = cfg_.initialCapital;
        r.finalValue     = cfg_.initialCapital;

        const size_t n = axis_.size();
        if (endIdx > n) endIdx = n;
        // Exige EXACTEMENT 2 séries (QQQ tradé + VIX signal) ; sinon résultat neutre.
        if (startIdx >= endIdx || axis_.assets() != 2) return r;
        const size_t M = endIdx - startIdx;

        // Série 0 = actif TRADÉ (QQQ), série 1 = SIGNAL (VIX).
        std::vector<double> px (axis_.close[0].begin() + static_cast<long>(startIdx),
                                axis_.close[0].begin() + static_cast<long>(endIdx));
        std::vector<double> vix(axis_.close[1].begin() + static_cast<long>(startIdx),
                                axis_.close[1].begin() + static_cast<long>(endIdx));

        // Aucun warmup : le VIX est déjà une vol annualisée, décidable dès i = 0.
        const size_t w = 0;

        const double c = perSideCost_(cfg_);
        double eq       = cfg_.initialCapital;
        double wHeld    = 0.0;
        double entryEq  = 0.0;
        size_t entryLoc = 0;
        size_t invested = 0;
        double weightSum = 0.0;

        r.equityCurve.reserve(M);
        r.equityDates.reserve(M);

        // pnlPct = rendement PORTEFEUILLE du stint (coûts inclus, poids composé
        // barre à barre) → deployedFraction = 1,0 (D45, même sémantique que
        // VolScaledBacktester / PairsBacktester).
        auto closeStint = [&](size_t exitLoc, const char* reason) {
            TradeRecord t;
            t.buyDate    = axis_.dates[startIdx + entryLoc];
            t.sellDate   = axis_.dates[startIdx + exitLoc];
            t.buyPrice   = px[entryLoc];
            t.sellPrice  = px[exitLoc];
            t.shares     = 0;
            t.pnlPct     = entryEq > 0.0 ? (eq / entryEq - 1.0) * 100.0 : 0.0;
            t.pnl        = eq - entryEq;
            t.holdDays   = static_cast<int>(exitLoc - entryLoc);
            t.exitReason = reason;
            t.isWin      = t.pnlPct > 0.0;
            t.deployedFraction = 1.0;
            r.trades.push_back(std::move(t));
        };

        for (size_t i = 0; i < M; ++i) {
            r.equityCurve.push_back(eq);
            r.equityDates.push_back(axis_.dates[startIdx + i]);
            if (i + 1 >= M) break;

            // Poids cible au close[i] : le niveau du VIX est déjà en % annualisés
            // → même formule pure que VolScaled (mmTargetWeight). VIX ≤ 0
            // (donnée dégénérée) → poids 0 par la garde de la fonction.
            const double wTarget = mmTargetWeight(vix[i], cfg_.targetVixPct,
                                                  cfg_.maxWeight);

            // Bande anti-churn + coût ∝ |Δw| ; chaque coût appartient à
            // EXACTEMENT un stint (cf. VolScaledBacktester).
            if (mmShouldRebalance(wHeld, wTarget, cfg_.rebalanceBand)) {
                const double delta = std::fabs(wTarget - wHeld);
                if (wHeld > 0.0) {
                    eq *= (1.0 - c * delta);
                    closeStint(i, wTarget > 0.0 ? "rebal" : "cash");
                    if (wTarget > 0.0) { entryLoc = i; entryEq = eq; }
                } else {
                    entryEq = eq;
                    eq *= (1.0 - c * delta);
                    entryLoc = i;
                }
                r.turnover += delta;
                ++r.rebalanceCount;
                wHeld = wTarget;
            }

            if (wHeld > 0.0) {
                const double ret = px[i] > 0.0 ? px[i + 1] / px[i] - 1.0 : 0.0;
                eq *= (1.0 + wHeld * ret);   // QQQ gagné de i à i+1 (pas de look-ahead)
                ++invested;
                weightSum += wHeld;
            }
        }

        // Liquidation forcée du stint ouvert au dernier close (coût ∝ poids).
        if (wHeld > 0.0) {
            eq *= (1.0 - c * wHeld);
            r.turnover += wHeld;
            closeStint(M - 1, "fin");
        }

        r.finalValue     = eq;
        r.totalReturnPct = (eq - cfg_.initialCapital) / cfg_.initialCapital * 100.0;

        // Références B&H de QQQ sur la fenêtre (w = 0 : pas de warmup).
        if (M > w + 1 && px[w] > 0.0) {
            r.buyHoldPct            = (px[M - 1] / px[w] - 1.0) * 100.0;
            r.buyHoldMaxDrawdownPct = maxDrawdownOf_(px, w, M);
            std::vector<double> bhRets;
            bhRets.reserve(M - w);
            for (size_t i = w + 1; i < M; ++i)
                if (px[i - 1] > 0.0) bhRets.push_back(px[i] / px[i - 1] - 1.0);
            double bhSortino = 0.0;
            sharpeSortino_(bhRets, r.buyHoldSharpe, bhSortino);
        }
        r.alphaVsBuyHold = r.totalReturnPct - r.buyHoldPct;

        r.maxDrawdownPct = maxDrawdownOf_(r.equityCurve, 0, r.equityCurve.size());

        if (r.equityCurve.size() > w + 1) {
            std::vector<double> rets;
            for (size_t i = w + 1; i < r.equityCurve.size(); ++i)
                if (r.equityCurve[i - 1] > 0.0)
                    rets.push_back((r.equityCurve[i] - r.equityCurve[i - 1]) / r.equityCurve[i - 1]);
            sharpeSortino_(rets, r.sharpeRatio, r.sortinoRatio);
        }

        double years = 0.0;
        if (r.equityDates.size() >= 2) {
            const long d0 = vxsDaysFromCivil_(r.equityDates.front());
            const long d1 = vxsDaysFromCivil_(r.equityDates.back());
            if (d0 >= 0 && d1 > d0) years = static_cast<double>(d1 - d0) / 365.25;
        }
        if (years <= 0.0 && r.equityCurve.size() > 1)
            years = static_cast<double>(r.equityCurve.size()) / 252.0;
        if (years > 0.0 && cfg_.initialCapital > 0.0 && eq > 0.0)
            r.cagrPct = (std::pow(eq / cfg_.initialCapital, 1.0 / years) - 1.0) * 100.0;
        r.calmarRatio = r.maxDrawdownPct > 0.0 ? r.cagrPct / r.maxDrawdownPct
                                               : (r.cagrPct > 0.0 ? 999.0 : 0.0);

        if (M > 1) {
            r.pctTimeInvested = std::min(100.0,
                100.0 * static_cast<double>(invested) / static_cast<double>(M - 1));
            r.avgWeight = weightSum / static_cast<double>(M - 1);
        }

        return r;
    }
};

// ─── VixScaledWalkForward ─────────────────────────────────────────────────────
// Pave l'axe commun (QQQ∩VIX) en fenêtres IS/OOS contiguës (même mécanique que
// VolScaled/VixRegime). Aucun paramètre ajusté par fenêtre ; les 3 pavages
// testent la ROBUSTESSE du verdict OOS.
struct VixScaledWindow {
    size_t isStart = 0, isEnd = 0, oosStart = 0, oosEnd = 0;
    VixScaledResult is;
    VixScaledResult oos;
};

class VixScaledWalkForward {
public:
    VixScaledWalkForward(VixScaledConfig cfg, std::vector<std::string> csvPaths,
                         size_t isBars, size_t oosBars, size_t step = 0,
                         size_t offset = 0)
        : vbt_(std::move(cfg), std::move(csvPaths))
        , isBars_(isBars), oosBars_(oosBars)
        , step_(step == 0 ? oosBars : step), offset_(offset) {}

    std::vector<VixScaledWindow> run() const {
        std::vector<VixScaledWindow> out;
        const size_t n    = vbt_.axis().size();
        const size_t span = isBars_ + oosBars_;
        if (span == 0 || n < span) return out;
        for (size_t s = offset_; s + span <= n; s += step_) {
            VixScaledWindow w;
            w.isStart  = s;           w.isEnd  = s + isBars_;
            w.oosStart = w.isEnd;     w.oosEnd = w.isEnd + oosBars_;
            w.is  = vbt_.runRange(w.isStart,  w.isEnd);
            w.oos = vbt_.runRange(w.oosStart, w.oosEnd);
            out.push_back(std::move(w));
        }
        return out;
    }

    const AlignedAxis& axis() const { return vbt_.axis(); }

private:
    VixScaledBacktester vbt_;
    size_t isBars_, oosBars_, step_, offset_;
};

} // namespace trading
