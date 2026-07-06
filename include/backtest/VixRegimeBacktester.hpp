#pragma once
#include "backtest/RotationBacktester.hpp"    // AlignedAxis, alignOnCommonDates (réutilisés)
#include "backtest/VolRegimeBacktester.hpp"    // trailingMedian (réutilisée)
#include "brokers/CsvDataFeed.hpp"
#include "brokers/PaperBroker.hpp"      // TradeRecord
#include <string>
#include <vector>
#include <cmath>
#include <cstdio>
#include <algorithm>

namespace trading {

// ─── VixRegimeBacktester ──────────────────────────────────────────────────────
// 5e famille d'alpha, variante à données EXTERNES (Sprint 14, décision utilisateur
// (c'), backlog D50) : régime de volatilité IMPLICITE. Identique au VolRegime du
// Sprint 13, MAIS le signal de régime n'est plus la vol RÉALISÉE de l'actif tradé
// (rétrospective, dérivée du prix lui-même) — c'est le niveau du VIX (volatilité
// IMPLICITE, anticipatrice, exogène). Hypothèse : « quand le marché price une vol
// élevée, réduire l'exposition ». Filtre binaire long/cash :
//  - régime CALME (VIX ≤ thresholdMult × sa propre médiane glissante refLookback) → LONG QQQ ;
//  - régime AGITÉ (VIX au-dessus) ou INCONNU (warmup) → CASH.
//
// DEUX séries alignées (QQQ tradé, VIX signal) : on réutilise `alignOnCommonDates`
// / `AlignedAxis` exactement comme PairsBacktester. **Seule la série 0 (QQQ) est
// TRADÉE** ; la série 1 (VIX) n'est qu'un SIGNAL — le VIX n'est pas un actif
// investissable et n'est jamais acheté. Moteur SÉPARÉ (offline, ne touche NI la
// prod NI aucun golden). Réutilise `trailingMedian` (VolRegimeBacktester),
// `alignOnCommonDates` (RotationBacktester), `TradeRecord`, `MonteCarlo`,
// `CsvDataFeed`.
//
// Conventions reprises (identiques à VolRegime/Rotation/Pairs) :
//  - anti look-ahead (B2, D37) : décision au close[i] (VIX + médiane ≤ i),
//    rendement QQQ gagné de i à i+1 ;
//  - ré-amorçage PAR fenêtre (la médiane du VIX est recalculée sur la sous-fenêtre) ;
//  - coûts par côté (D22) ; liquidation forcée en fin de fenêtre.
//
// Warmup = refLookback − 1 seulement (le VIX est DÉJÀ un niveau de vol : pas de
// fenêtre de vol réalisée à amorcer, contrairement au VolRegime dont le warmup
// était volLookback + refLookback − 2).

// Discipline & coûts (défauts miroir du Backtester/PaperBroker, D22). Pas de
// volLookback : le niveau du VIX EST le signal (D33 — pas de champ non exercé).
struct VixRegimeConfig {
    int    refLookback    = 126;      // fenêtre de la médiane de référence du VIX (~6 mois)
    double thresholdMult  = 1.0;      // calme si VIX ≤ thresholdMult × médiane
    double initialCapital = 10'000.0;
    double commissionPct  = 0.001;    // 0,1 % par côté
    double slippageBps    = 2.0;      // 2 bps par côté
    double halfSpreadBps  = 0.5;      // 0,5 bp par côté
};

struct VixRegimeResult {
    double initialCapital = 0.0;
    double finalValue     = 0.0;
    double totalReturnPct = 0.0;
    // Références Buy & Hold de l'actif TRADÉ (QQQ) sur la fenêtre (depuis le warmup).
    double buyHoldPct            = 0.0;
    double buyHoldMaxDrawdownPct = 0.0;
    double buyHoldSharpe         = 0.0;
    double alphaVsBuyHold        = 0.0;   // totalReturn − buyHold (sanity D23)
    double maxDrawdownPct = 0.0;
    double cagrPct        = 0.0;
    double calmarRatio    = 0.0;
    double sharpeRatio    = 0.0;     // critère PRIMAIRE : Sharpe stratégie vs buyHoldSharpe
    double sortinoRatio   = 0.0;
    double pctTimeInvested = 0.0;
    int    switchCount     = 0;
    std::vector<double>      equityCurve;
    std::vector<std::string> equityDates;
    std::vector<TradeRecord> trades;
};

class VixRegimeBacktester {
public:
    // csvPaths = { actif TRADÉ (QQQ), indice SIGNAL (VIX) }. Aligne les deux séries
    // sur leurs dates communes (le VIX ~1990 déborde QQQ ~1999 → borné par QQQ).
    VixRegimeBacktester(VixRegimeConfig cfg, std::vector<std::string> csvPaths)
        : cfg_(std::move(cfg)), csvPaths_(std::move(csvPaths)) {
        std::vector<std::vector<Bar>> series;
        series.reserve(csvPaths_.size());
        for (const auto& p : csvPaths_)
            series.push_back(CsvDataFeed(p).allBars());
        axis_ = alignOnCommonDates(series);
    }

    // Seam de test : moteur sur un axe déjà aligné (2 séries synthétiques).
    static VixRegimeBacktester fromAxis(VixRegimeConfig cfg, AlignedAxis axis) {
        VixRegimeBacktester v(std::move(cfg), std::vector<std::string>{});
        v.axis_ = std::move(axis);
        return v;
    }

    VixRegimeResult run() const { return runRange(0, axis_.size()); }

    VixRegimeResult runRange(size_t startIdx, size_t endIdx) const {
        return compute_(startIdx, endIdx);
    }

    const AlignedAxis& axis() const { return axis_; }

private:
    VixRegimeConfig          cfg_;
    std::vector<std::string> csvPaths_;
    AlignedAxis              axis_;

    static double perSideCost_(const VixRegimeConfig& c) {
        return c.commissionPct + (c.slippageBps + c.halfSpreadBps) / 10'000.0;
    }

    static long vixDaysFromCivil_(const std::string& date) {
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

    VixRegimeResult compute_(size_t startIdx, size_t endIdx) const {
        VixRegimeResult r;
        r.initialCapital = cfg_.initialCapital;
        r.finalValue     = cfg_.initialCapital;

        const size_t n = axis_.size();
        if (endIdx > n) endIdx = n;
        // Exige EXACTEMENT 2 séries (QQQ tradé + VIX signal) ; sinon résultat neutre.
        if (startIdx >= endIdx || axis_.assets() != 2) return r;
        const size_t M = endIdx - startIdx;

        // Série 0 = actif TRADÉ (QQQ), série 1 = SIGNAL (VIX). Ré-amorçage par fenêtre.
        std::vector<double> px (axis_.close[0].begin() + static_cast<long>(startIdx),
                                axis_.close[0].begin() + static_cast<long>(endIdx));
        std::vector<double> vix(axis_.close[1].begin() + static_cast<long>(startIdx),
                                axis_.close[1].begin() + static_cast<long>(endIdx));

        // Médiane glissante du VIX sur la sous-fenêtre (référence auto-normalisante).
        const std::vector<double> med = trailingMedian(vix, cfg_.refLookback);
        const size_t w = cfg_.refLookback > 0 ? static_cast<size_t>(cfg_.refLookback) - 1 : 0;

        const double c = perSideCost_(cfg_);
        double eq       = cfg_.initialCapital;
        bool   held     = false;
        size_t entryLoc = 0;
        size_t invested = 0;

        r.equityCurve.reserve(M);
        r.equityDates.reserve(M);

        auto closeStint = [&](size_t exitLoc, const char* reason) {
            const double gross = px[entryLoc] > 0.0 ? px[exitLoc] / px[entryLoc] : 1.0;
            const double net   = gross * std::pow(1.0 - c, 2.0);
            TradeRecord t;
            t.buyDate    = axis_.dates[startIdx + entryLoc];
            t.sellDate   = axis_.dates[startIdx + exitLoc];
            t.buyPrice   = px[entryLoc];
            t.sellPrice  = px[exitLoc];
            t.shares     = 0;
            t.pnlPct     = (net - 1.0) * 100.0;
            t.pnl        = cfg_.initialCapital * (net - 1.0);
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

            // Régime au close[i] : décidable après le warmup. Calme si le VIX est ≤
            // thresholdMult × sa médiane de référence (données ≤ i). Inconnu → CASH.
            bool target = false;
            if (i >= w)
                target = vix[i] <= cfg_.thresholdMult * med[i];

            if (target != held) {
                ++r.switchCount;
                const int sides = (held ? 1 : 0) + (target ? 1 : 0);
                eq *= std::pow(1.0 - c, static_cast<double>(sides));
                if (held)  closeStint(i, "regime");
                if (target) entryLoc = i;
            }
            held = target;

            if (held) {
                const double ratio = px[i] > 0.0 ? px[i + 1] / px[i] : 1.0;
                eq *= ratio;   // QQQ gagné de i à i+1 (pas de look-ahead)
                ++invested;
            }
        }

        if (held) {
            eq *= (1.0 - c);
            closeStint(M - 1, "fin");
        }

        r.finalValue     = eq;
        r.totalReturnPct = (eq - cfg_.initialCapital) / cfg_.initialCapital * 100.0;

        // Références B&H de QQQ depuis la fin du warmup.
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
            const long d0 = vixDaysFromCivil_(r.equityDates.front());
            const long d1 = vixDaysFromCivil_(r.equityDates.back());
            if (d0 >= 0 && d1 > d0) years = static_cast<double>(d1 - d0) / 365.25;
        }
        if (years <= 0.0 && r.equityCurve.size() > 1)
            years = static_cast<double>(r.equityCurve.size()) / 252.0;
        if (years > 0.0 && cfg_.initialCapital > 0.0 && eq > 0.0)
            r.cagrPct = (std::pow(eq / cfg_.initialCapital, 1.0 / years) - 1.0) * 100.0;
        r.calmarRatio = r.maxDrawdownPct > 0.0 ? r.cagrPct / r.maxDrawdownPct
                                               : (r.cagrPct > 0.0 ? 999.0 : 0.0);

        if (M > 1)
            r.pctTimeInvested = std::min(100.0,
                100.0 * static_cast<double>(invested) / static_cast<double>(M - 1));

        return r;
    }
};

// ─── VixRegimeWalkForward ─────────────────────────────────────────────────────
// Pave l'axe commun (QQQ∩VIX) en fenêtres IS/OOS contiguës (même mécanique que
// Rotation/Pairs/VolRegime). Aucun paramètre ajusté par fenêtre ; les 3 pavages
// (canonique / fin / décalé) testent la ROBUSTESSE du verdict OOS.
struct VixWindow {
    size_t isStart = 0, isEnd = 0, oosStart = 0, oosEnd = 0;
    VixRegimeResult is;
    VixRegimeResult oos;
};

class VixRegimeWalkForward {
public:
    VixRegimeWalkForward(VixRegimeConfig cfg, std::vector<std::string> csvPaths,
                         size_t isBars, size_t oosBars, size_t step = 0,
                         size_t offset = 0)
        : vbt_(std::move(cfg), std::move(csvPaths))
        , isBars_(isBars), oosBars_(oosBars)
        , step_(step == 0 ? oosBars : step), offset_(offset) {}

    std::vector<VixWindow> run() const {
        std::vector<VixWindow> out;
        const size_t n    = vbt_.axis().size();
        const size_t span = isBars_ + oosBars_;
        if (span == 0 || n < span) return out;
        for (size_t s = offset_; s + span <= n; s += step_) {
            VixWindow w;
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
    VixRegimeBacktester vbt_;
    size_t isBars_, oosBars_, step_, offset_;
};

} // namespace trading
