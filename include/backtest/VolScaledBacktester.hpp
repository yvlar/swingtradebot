#pragma once
#include "backtest/VolRegimeBacktester.hpp"    // closeReturns, realizedVol (réutilisées)
#include "brokers/CsvDataFeed.hpp"
#include "brokers/PaperBroker.hpp"      // TradeRecord
#include <string>
#include <vector>
#include <cmath>
#include <cstdio>
#include <algorithm>

namespace trading {

// ─── VolScaledBacktester ──────────────────────────────────────────────────────
// Réouverture de la recherche d'edge (Sprint 18, décision utilisateur (r)) —
// piste §5.2 de CONCLUSION_RECHERCHE_EDGE.md : SCALING CONTINU de l'exposition
// selon la volatilité (« volatility-managed portfolios », Moreira & Muir 2017).
// Le filtre BINAIRE long/cash des Sprints 13-14 avait un Sharpe OOS positif mais
// SOUS le buy & hold (D50/D51 : le cash drag coûte plus en rendement manqué que
// la réduction de risque ne rapporte). Ici on ne sort jamais brutalement : le
// poids investi est CONTINU, w*[i] = min(maxWeight, cible / vol annualisée), et
// on ne rebalance qu'au franchissement d'une BANDE (anti-churn) avec un coût
// PROPORTIONNEL au notionnel tradé |Δw| — sans quoi le verdict serait malhonnête
// (un rebalancement journalier gratuit surestimerait l'edge, D22).
//
// C'est un moteur SÉPARÉ (comme VolRegime/Pairs/Rotation) : il ne pilote NI
// TradingBot NI RiskManager NI la prod. La prod reste mono-symbole QQQ, paper,
// verrouillée. Réutilise CsvDataFeed (total-return), closeReturns + realizedVol
// (VolRegimeBacktester), TradeRecord (Monte-Carlo size-aware, D45) et MonteCarlo.
// Additif : aucun golden existant n'est touché.
//
// Conventions reprises (identiques à VolRegime/Rotation/Pairs) :
//  - anti look-ahead (B2, D37) : décision au close[i] (vol calculée sur les
//    seules données ≤ i), rendement gagné de i à i+1 ;
//  - ré-amorçage PAR fenêtre (floorIdx) : vol réalisée recalculée sur les seules
//    barres de la sous-fenêtre → aucune fuite d'historique ;
//  - coûts (D22) : commission + slippage + demi-spread PAR CÔTÉ, ici pondérés
//    par la fraction de notionnel effectivement tradée (|Δw| au rebalancement) ;
//  - liquidation forcée en fin de fenêtre (coût ∝ poids détenu).
//
// AVERTISSEMENT WARMUP (D35) : première barre décidable = volLookback − 1 (la
// vol réalisée doit être valide) — BEAUCOUP plus court que le binaire (qui
// exigeait en plus refLookback barres de médiane). Avant le warmup, et si la vol
// est nulle (série dégénérée), le poids est 0 (« inconnu ≠ calme », convention
// filtre SMA200) — le binaire, lui, considérait vol nulle = calme extrême.

// Discipline & coûts (défauts miroir du Backtester/PaperBroker, D22). La CIBLE
// est ABSOLUE (vol annualisée en %) : c'est le cœur de Moreira-Muir — pas de
// médiane de référence auto-normalisante comme le binaire.
struct VolScaledConfig {
    int    volLookback     = 20;       // fenêtre de la vol réalisée (σ des rendements)
    double targetVolAnnPct = 15.0;     // cible de vol ANNUALISÉE (%) : w* = cible/vol
    double maxWeight       = 1.0;      // plafond d'exposition (1,0 = pas de levier)
    double rebalanceBand   = 0.05;     // rebalancer seulement si |w* − w| > bande
    double initialCapital  = 10'000.0;
    double commissionPct   = 0.001;    // 0,1 % par côté
    double slippageBps     = 2.0;      // 2 bps par côté
    double halfSpreadBps   = 0.5;      // 0,5 bp par côté
};

struct VolScaledResult {
    double initialCapital = 0.0;
    double finalValue     = 0.0;
    double totalReturnPct = 0.0;
    // Références Buy & Hold de l'actif sur la fenêtre (depuis la fin du warmup).
    // Le scaling est DIRECTIONNEL → jugé sur l'ALPHA vs B&H net de coûts (D23),
    // avec la clause de repli Sprint 8 (Sharpe ≥ B&H ET DD réduit ≥ 50 %).
    double buyHoldPct            = 0.0;
    double buyHoldMaxDrawdownPct = 0.0;
    double buyHoldSharpe         = 0.0;
    double alphaVsBuyHold        = 0.0;   // totalReturn − buyHold
    double maxDrawdownPct = 0.0;
    double cagrPct        = 0.0;
    double calmarRatio    = 0.0;
    double sharpeRatio    = 0.0;
    double sortinoRatio   = 0.0;
    double pctTimeInvested = 0.0;    // % de barres avec poids > 0
    double avgWeight       = 0.0;    // poids moyen détenu (mesure du cash drag continu)
    double turnover        = 0.0;    // Σ|Δw| sur la fenêtre (churn total)
    int    rebalanceCount  = 0;      // nombre de rebalancements effectifs (bande franchie)
    std::vector<double>      equityCurve;   // une valeur par barre de la fenêtre
    std::vector<std::string> equityDates;   // PARALLÈLE à equityCurve
    std::vector<TradeRecord> trades;        // un stint à poids constant = un trade
};

// ── Fonctions PURES (testables sans I/O) ─────────────────────────────────────

// Poids cible Moreira-Muir : w* = min(maxWeight, cible / vol annualisée). Une vol
// nulle ou négative (warmup, série dégénérée) donne un poids 0 — « inconnu ≠
// calme » : on n'investit pas sur une vol qu'on ne sait pas mesurer.
inline double mmTargetWeight(double volAnnPct, double targetVolAnnPct,
                             double maxWeight) {
    if (volAnnPct <= 0.0 || targetVolAnnPct <= 0.0 || maxWeight <= 0.0) return 0.0;
    return std::min(maxWeight, targetVolAnnPct / volAnnPct);
}

// Bande anti-churn : ne rebalancer que si l'écart au poids cible excède la bande.
// (Un rebalancement journalier au moindre bruit de vol facturerait des coûts en
// continu et fausserait la comparaison au B&H — D22.)
inline bool mmShouldRebalance(double wHeld, double wTarget, double band) {
    return std::fabs(wTarget - wHeld) > band;
}

// Vol annualisée (%) depuis la vol réalisée journalière (σ des rendements) :
// volAnn = rv · √252 · 100 — même unité que le VIX (points de %).
inline double mmAnnualizedVolPct(double dailyVol) {
    return dailyVol * std::sqrt(252.0) * 100.0;
}

class VolScaledBacktester {
public:
    // Charge UNE série (mono-actif : QQQ en prod, SPY/IWM/MDY en validation-only).
    // cppcheck-suppress passedByValue ; copie unique au chargement
    VolScaledBacktester(VolScaledConfig cfg, std::string csvPath)
        : cfg_(std::move(cfg)) {
        const auto bars = CsvDataFeed(csvPath).allBars();
        dates_.reserve(bars.size());
        closes_.reserve(bars.size());
        for (const auto& b : bars) { dates_.push_back(b.date); closes_.push_back(b.close); }
    }

    // Seam de test : moteur sur une série synthétique en mémoire (sans CSV),
    // l'analogue de VolRegimeBacktester::fromSeries.
    static VolScaledBacktester fromSeries(VolScaledConfig cfg,
                                          std::vector<std::string> dates,
                                          std::vector<double> closes) {
        VolScaledBacktester v(std::move(cfg));
        v.dates_  = std::move(dates);
        v.closes_ = std::move(closes);
        return v;
    }

    VolScaledResult run() const { return runRange(0, closes_.size()); }

    // Backtest sur la sous-fenêtre [startIdx, endIdx). Vol réalisée RE-CALCULÉE
    // sur les seules closes de la sous-fenêtre (anti-leak, floorIdx).
    VolScaledResult runRange(size_t startIdx, size_t endIdx) const {
        return compute_(startIdx, endIdx);
    }

    size_t size() const { return closes_.size(); }
    const std::vector<std::string>& dates() const { return dates_; }

private:
    explicit VolScaledBacktester(VolScaledConfig cfg) : cfg_(std::move(cfg)) {}

    VolScaledConfig          cfg_;
    std::vector<std::string> dates_;
    std::vector<double>      closes_;

    // Coût par côté (fraction) : commission + (slippage + demi-spread) en bps.
    static double perSideCost_(const VolScaledConfig& c) {
        return c.commissionPct + (c.slippageBps + c.halfSpreadBps) / 10'000.0;
    }

    // « YYYY-MM-DD » → jours civils (days_from_civil, Hinnant) ; -1 si invalide.
    static long vsDaysFromCivil_(const std::string& date) {
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

    // Drawdown maximal (%) d'une série sur [lo, hi).
    static double maxDrawdownOf_(const std::vector<double>& px, size_t lo, size_t hi) {
        double peak = 0.0, dd = 0.0;
        for (size_t i = lo; i < hi && i < px.size(); ++i) {
            peak = std::max(peak, px[i]);
            if (peak > 0.0) dd = std::max(dd, (peak - px[i]) / peak * 100.0);
        }
        return dd;
    }

    // Sharpe / Sortino annualisés (252) d'une série de rendements journaliers.
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

    VolScaledResult compute_(size_t startIdx, size_t endIdx) const {
        VolScaledResult r;
        r.initialCapital = cfg_.initialCapital;
        r.finalValue     = cfg_.initialCapital;

        const size_t n = closes_.size();
        if (endIdx > n) endIdx = n;
        if (startIdx >= endIdx) return r;
        const size_t M = endIdx - startIdx;

        // Sous-série de clôtures (ré-amorçage par fenêtre).
        std::vector<double> px(closes_.begin() + static_cast<long>(startIdx),
                               closes_.begin() + static_cast<long>(endIdx));

        // Vol réalisée calculée SUR LA SOUS-FENÊTRE (réutilise VolRegimeBacktester).
        const std::vector<double> rv = realizedVol(px, cfg_.volLookback);
        // Première barre décidable : vol réalisée valide dès volLookback − 1
        // (convention RollingStdDev, identique au binaire).
        const size_t w = cfg_.volLookback > 0
                             ? static_cast<size_t>(cfg_.volLookback) - 1 : 0;

        const double c = perSideCost_(cfg_);
        double eq       = cfg_.initialCapital;
        double wHeld    = 0.0;    // poids investi courant (0 = cash)
        double entryEq  = 0.0;    // équité AVANT le coût d'entrée du stint courant
        size_t entryLoc = 0;
        size_t invested = 0;
        double weightSum = 0.0;   // Σ des poids détenus (pour avgWeight)

        r.equityCurve.reserve(M);
        r.equityDates.reserve(M);

        // pnlPct = rendement PORTEFEUILLE du stint (coûts d'entrée ET de sortie
        // inclus, le poids w est DÉJÀ composé dedans barre à barre) → la
        // deployedFraction du Monte-Carlo size-aware (D45) vaut 1,0 — même
        // sémantique que PairsBacktester (jamais un ratio de prix × w, qui serait
        // faux : la composition (1 + w·r) est path-dépendante).
        auto closeStint = [&](size_t exitLoc, const char* reason) {
            TradeRecord t;
            t.buyDate    = dates_[startIdx + entryLoc];
            t.sellDate   = dates_[startIdx + exitLoc];
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
            // Valorisation au marché AVANT toute décision (marks pré-liquidation).
            r.equityCurve.push_back(eq);
            r.equityDates.push_back(dates_[startIdx + i]);
            if (i + 1 >= M) break;   // dernière barre : aucune séance suivante

            // Poids cible au close[i] : décidable après le warmup, sur les seules
            // données ≤ i. Avant le warmup (ou vol nulle) → 0 (inconnu ≠ calme).
            const double wTarget = i >= w
                ? mmTargetWeight(mmAnnualizedVolPct(rv[i]),
                                 cfg_.targetVolAnnPct, cfg_.maxWeight)
                : 0.0;

            // Rebalancement seulement au franchissement de la bande. Coût
            // PROPORTIONNEL au notionnel tradé : eq × (1 − c·|Δw|). Chaque coût
            // appartient à EXACTEMENT un stint (Σ pnl des trades = chemin d'équité) :
            //  - entrée depuis le cash → coût porté par le NOUVEAU stint (entryEq
            //    photographié AVANT le coût, comme PairsBacktester) ;
            //  - ajustement / retour au cash → coût porté par le stint SORTANT.
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
                eq *= (1.0 + wHeld * ret);   // pas de look-ahead : gagné de i à i+1
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

        // Références B&H depuis la fin du warmup (rendement, DD, Sharpe).
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

        // Max drawdown de la stratégie (marks pré-liquidation, comme le Backtester).
        r.maxDrawdownPct = maxDrawdownOf_(r.equityCurve, 0, r.equityCurve.size());

        // Sharpe / Sortino stratégie : rendements journaliers depuis la fin du warmup.
        if (r.equityCurve.size() > w + 1) {
            std::vector<double> rets;
            for (size_t i = w + 1; i < r.equityCurve.size(); ++i)
                if (r.equityCurve[i - 1] > 0.0)
                    rets.push_back((r.equityCurve[i] - r.equityCurve[i - 1]) / r.equityCurve[i - 1]);
            sharpeSortino_(rets, r.sharpeRatio, r.sortinoRatio);
        }

        // CAGR (durée réelle via les dates d'équité), Calmar.
        double years = 0.0;
        if (r.equityDates.size() >= 2) {
            const long d0 = vsDaysFromCivil_(r.equityDates.front());
            const long d1 = vsDaysFromCivil_(r.equityDates.back());
            if (d0 >= 0 && d1 > d0) years = static_cast<double>(d1 - d0) / 365.25;
        }
        if (years <= 0.0 && r.equityCurve.size() > 1)
            years = static_cast<double>(r.equityCurve.size()) / 252.0;
        if (years > 0.0 && cfg_.initialCapital > 0.0 && eq > 0.0)
            r.cagrPct = (std::pow(eq / cfg_.initialCapital, 1.0 / years) - 1.0) * 100.0;
        r.calmarRatio = r.maxDrawdownPct > 0.0 ? r.cagrPct / r.maxDrawdownPct
                                               : (r.cagrPct > 0.0 ? 999.0 : 0.0);

        // % du temps avec poids > 0 et poids moyen détenu (cash drag continu).
        if (M > 1) {
            r.pctTimeInvested = std::min(100.0,
                100.0 * static_cast<double>(invested) / static_cast<double>(M - 1));
            r.avgWeight = weightSum / static_cast<double>(M - 1);
        }

        return r;
    }
};

// ─── VolScaledWalkForward ─────────────────────────────────────────────────────
// Pave la série mono-actif en fenêtres IS/OOS contiguës (même mécanique que
// VolRegimeWalkForward). Le scaling n'AJUSTE aucun paramètre par fenêtre →
// l'IS est purement informatif ; les 3 pavages (canonique / fin / décalé)
// testent la ROBUSTESSE du verdict OOS.
struct VolScaledWindow {
    size_t isStart = 0, isEnd = 0, oosStart = 0, oosEnd = 0;
    VolScaledResult is;
    VolScaledResult oos;
};

class VolScaledWalkForward {
public:
    VolScaledWalkForward(VolScaledConfig cfg, std::string csvPath,
                         size_t isBars, size_t oosBars, size_t step = 0,
                         size_t offset = 0)
        : vbt_(std::move(cfg), std::move(csvPath))
        , isBars_(isBars), oosBars_(oosBars)
        , step_(step == 0 ? oosBars : step), offset_(offset) {}

    std::vector<VolScaledWindow> run() const {
        std::vector<VolScaledWindow> out;
        const size_t n    = vbt_.size();
        const size_t span = isBars_ + oosBars_;
        if (span == 0 || n < span) return out;
        for (size_t s = offset_; s + span <= n; s += step_) {
            VolScaledWindow w;
            w.isStart  = s;           w.isEnd  = s + isBars_;
            w.oosStart = w.isEnd;     w.oosEnd = w.isEnd + oosBars_;
            w.is  = vbt_.runRange(w.isStart,  w.isEnd);
            w.oos = vbt_.runRange(w.oosStart, w.oosEnd);
            out.push_back(std::move(w));
        }
        return out;
    }

    size_t size() const { return vbt_.size(); }

private:
    VolScaledBacktester vbt_;
    size_t isBars_, oosBars_, step_, offset_;
};

} // namespace trading
