#pragma once
#include "backtest/RotationBacktester.hpp"  // AlignedAxis, alignOnCommonDates (réutilisés)
#include "backtest/Cointegration.hpp"       // olsFit, egResidual, dfTStat
#include "brokers/CsvDataFeed.hpp"
#include "brokers/PaperBroker.hpp"      // TradeRecord
#include <string>
#include <vector>
#include <cmath>
#include <cstdio>
#include <algorithm>

namespace trading {

// ─── CointPairsBacktester ─────────────────────────────────────────────────────
// Réouverture Sprint 18 (piste §5.1) : pairs-trading RÉELLEMENT COINTÉGRÉ
// (Engle-Granger), là où le Sprint 12 utilisait un spread naïf β = 1 sans test
// de stationnarité — verdict D49 : « du bruit » (Sharpe OOS négatif partout).
// Deux corrections structurelles :
//  1. HEDGE RATIO ROULANT : β estimé par OLS causale des log-prix sur
//     `betaWindow` barres (étape 1 d'Engle-Granger) au lieu de β = 1 figé ;
//  2. GATE DE COINTÉGRATION : les ENTRÉES ne sont autorisées que si le résidu
//     e = log P0 − α − β·log P1 des `adfWindow` dernières barres passe le test
//     de Dickey-Fuller (t ≤ adfCritical, étape 2) ET β > 0 (une paire
//     anti-corrélée n'est pas un hedge) — quand la paire n'est PAS cointégrée,
//     on ne trade PAS (c'est exactement ce que le spread naïf ne savait pas).
//
// Signal : z-score du RÉSIDU (plus du spread brut) sur `zWindow` barres ;
// entrée à |z| ≥ entryK, sortie au retour à la moyenne (croisement de exitZ) —
// même hystérésis que PairsBacktester. β est FIGÉ À L'ENTRÉE pour la durée du
// stint (décision documentée : re-hedger chaque barre facturerait un churn non
// modélisé, le péché exact que la bande du VolScaled évite) ; poids des jambes
// w0 = 1/(1+β), w1 = β/(1+β) → exposition BRUTE 1,0, comparable au livre
// 0,5/0,5 du Sprint 12. Le z de SORTIE utilise les coefficients ROULANTS de
// l'instant courant (information de décision) ; si le fit devient indécidable
// en cours de stint, la position est simplement portée jusqu'à la prochaine
// sortie décidable (ou la liquidation forcée).
//
// C'est un moteur SÉPARÉ (offline) : il ne pilote NI TradingBot NI la prod.
// Réutilise CsvDataFeed (total-return), alignOnCommonDates, Cointegration.hpp,
// TradeRecord (Monte-Carlo size-aware, D45). Additif : aucun golden touché.
//
// Conventions reprises (identiques à PairsBacktester) :
//  - anti look-ahead (B2) : décision au close[i] (fit OLS + résidu + z sur les
//    seules données ≤ i), rendement gagné de i à i+1 ;
//  - ré-amorçage PAR fenêtre : logs, fits et z recalculés sur la sous-fenêtre ;
//  - coûts (D22) : commission + slippage + demi-spread PAR CÔTÉ et par JAMBE
//    (entrée = 2 côtés, sortie = 2 côtés) — conservateur : facturé sur le
//    notionnel PLEIN bien que l'exposition brute des jambes somme à 1,0 ;
//  - liquidation forcée en fin de fenêtre.
//
// AVERTISSEMENT WARMUP (D35) : première barre décidable =
// max(betaWindow, adfWindow) − 1 (≈ 125 barres aux défauts, et ~250 si
// betaWindow = 252) — dimensionner les fenêtres OOS LARGEMENT au-dessus.
// zWindow doit être ≤ adfWindow (le z est pris sur la queue du résidu).

// Discipline & coûts (défauts miroir du Backtester/PaperBroker, D22).
// adfCritical = valeur critique Engle-Granger FIGÉE (MacKinnon, 2 variables
// avec constante, 5 %) — pas de p-value calculée (simplification documentée
// dans Cointegration.hpp).
struct CointPairsConfig {
    int    betaWindow     = 126;      // OLS roulante du hedge ratio (log-prix)
    int    adfWindow      = 126;      // fenêtre du test DF sur le résidu
    double adfCritical    = -3.34;    // gate : cointégré si t ≤ adfCritical
    int    zWindow        = 20;       // z-score du résidu (queue de adfWindow)
    double entryK         = 2.0;      // entrée si |z| ≥ entryK
    double exitZ          = 0.0;      // sortie au croisement de exitZ
    double initialCapital = 10'000.0;
    double commissionPct  = 0.001;    // 0,1 % par côté
    double slippageBps    = 2.0;      // 2 bps par côté
    double halfSpreadBps  = 0.5;      // 0,5 bp par côté
};

struct CointPairsResult {
    double initialCapital = 0.0;
    double finalValue     = 0.0;
    double totalReturnPct = 0.0;     // rendement absolu net
    // Références de CONTINUITÉ (un market-neutral ne bat pas un indice long —
    // le critère PRIMAIRE est le Sharpe, comme au Sprint 12/D49).
    double leg0BuyHoldReturnPct   = 0.0;
    double leg0BuyHoldDrawdownPct = 0.0;
    double basketBuyHoldReturnPct = 0.0;
    double alphaVsLeg0BuyHold     = 0.0;
    double alphaVsBasketBuyHold   = 0.0;
    double maxDrawdownPct = 0.0;
    double cagrPct        = 0.0;
    double calmarRatio    = 0.0;
    double sharpeRatio    = 0.0;     // le vrai critère de succès d'un market-neutral
    double sortinoRatio   = 0.0;
    double pctTimeInvested = 0.0;
    // Garde d'ACTIVATION (D47) : part des barres décidables où le gate de
    // cointégration est OUVERT. S'il ne s'ouvre (presque) jamais, la famille
    // n'a pas été jugée — le verdict ne mesurerait que du cash drag.
    double pctBarsCointegrated = 0.0;
    int    roundTripCount  = 0;
    std::vector<double>      equityCurve;
    std::vector<std::string> equityDates;
    std::vector<TradeRecord> trades;
};

class CointPairsBacktester {
public:
    CointPairsBacktester(CointPairsConfig cfg, std::vector<std::string> csvPaths)
        : cfg_(std::move(cfg)), csvPaths_(std::move(csvPaths)) {
        std::vector<std::vector<Bar>> series;
        series.reserve(csvPaths_.size());
        for (const auto& p : csvPaths_)
            series.push_back(CsvDataFeed(p).allBars());
        axis_ = alignOnCommonDates(series);
    }

    // Seam de test : moteur sur un axe déjà aligné (2 séries synthétiques).
    static CointPairsBacktester fromAxis(CointPairsConfig cfg, AlignedAxis axis) {
        CointPairsBacktester pb(std::move(cfg), std::vector<std::string>{});
        pb.axis_ = std::move(axis);
        return pb;
    }

    CointPairsResult run() const { return runRange(0, axis_.size()); }

    CointPairsResult runRange(size_t startIdx, size_t endIdx) const {
        return compute_(startIdx, endIdx);
    }

    const AlignedAxis& axis() const { return axis_; }

private:
    CointPairsConfig         cfg_;
    std::vector<std::string> csvPaths_;
    AlignedAxis              axis_;

    static double perSideCost_(const CointPairsConfig& c) {
        return c.commissionPct + (c.slippageBps + c.halfSpreadBps) / 10'000.0;
    }

    static long cpDaysFromCivil_(const std::string& date) {
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

    // Log-prix avec garde de positivité (répète la dernière valeur valide).
    static std::vector<double> logPrices_(const std::vector<double>& p) {
        std::vector<double> lg(p.size(), 0.0);
        for (size_t i = 0; i < p.size(); ++i) {
            if (p[i] > 0.0) lg[i] = std::log(p[i]);
            else if (i > 0) lg[i] = lg[i - 1];
        }
        return lg;
    }

    CointPairsResult compute_(size_t startIdx, size_t endIdx) const {
        CointPairsResult r;
        r.initialCapital = cfg_.initialCapital;
        r.finalValue     = cfg_.initialCapital;

        const size_t n = axis_.size();
        if (endIdx > n) endIdx = n;
        // Le pairs-trading exige EXACTEMENT 2 actifs ; sinon résultat neutre.
        if (startIdx >= endIdx || axis_.assets() != 2) return r;
        // Fenêtres dégénérées → résultat neutre (zWindow doit tenir dans le résidu).
        if (cfg_.betaWindow < 2 || cfg_.adfWindow < 2 || cfg_.zWindow < 2
            || cfg_.zWindow > cfg_.adfWindow)
            return r;
        const size_t M = endIdx - startIdx;

        // Sous-séries des deux jambes (ré-amorçage par fenêtre).
        std::vector<double> p0(axis_.close[0].begin() + static_cast<long>(startIdx),
                               axis_.close[0].begin() + static_cast<long>(endIdx));
        std::vector<double> p1(axis_.close[1].begin() + static_cast<long>(startIdx),
                               axis_.close[1].begin() + static_cast<long>(endIdx));
        const std::vector<double> y = logPrices_(p0);   // régressé (jambe 0)
        const std::vector<double> x = logPrices_(p1);   // régresseur (jambe 1)

        // Première barre décidable : le fit OLS exige betaWindow barres ≤ i et le
        // résidu du test DF exige adfWindow barres ≤ i.
        const size_t w = static_cast<size_t>(
            std::max(cfg_.betaWindow, cfg_.adfWindow)) - 1;

        const double c = perSideCost_(cfg_);
        double eq       = cfg_.initialCapital;
        int    pos      = 0;      // 0 = plat, +1 = long spread, -1 = short spread
        double entryEq  = 0.0;
        size_t entryLoc = 0;
        double fw0 = 0.0, fw1 = 0.0;   // poids des jambes FIGÉS à l'entrée (β d'entrée)
        size_t invested = 0;
        size_t decidable = 0, coint = 0;

        r.equityCurve.reserve(M);
        r.equityDates.reserve(M);

        // pnlPct = rendement PORTEFEUILLE du stint (coûts inclus), dérivé de
        // l'équité COMPOSÉE barre à barre — jamais d'un ratio de prix (faux pour
        // 2 jambes, la jambe short compose de façon path-dépendante).
        auto closeStint = [&](size_t exitLoc, const char* reason) {
            TradeRecord t;
            t.buyDate    = axis_.dates[startIdx + entryLoc];
            t.sellDate   = axis_.dates[startIdx + exitLoc];
            t.buyPrice   = p0[entryLoc];   // indicatif (jambe 0)
            t.sellPrice  = p0[exitLoc];
            t.shares     = 0;
            t.pnlPct     = entryEq > 0.0 ? (eq / entryEq - 1.0) * 100.0 : 0.0;
            t.pnl        = eq - entryEq;
            t.holdDays   = static_cast<int>(exitLoc - entryLoc);
            t.exitReason = reason;
            t.isWin      = t.pnlPct > 0.0;
            t.deployedFraction = 1.0;
            r.trades.push_back(std::move(t));
            ++r.roundTripCount;
        };

        for (size_t i = 0; i < M; ++i) {
            // Valorisation au marché AVANT toute décision (marks pré-liquidation).
            r.equityCurve.push_back(eq);
            r.equityDates.push_back(axis_.dates[startIdx + i]);
            if (i + 1 >= M) break;   // dernière barre : aucune séance suivante

            // Décision au close[i] : fit OLS roulant → résidu → test DF → z-score,
            // le tout sur les seules données ≤ i (anti look-ahead).
            bool   gateOk = false, zOk = false;
            double z = 0.0, beta = 0.0;
            if (i >= w) {
                ++decidable;
                const OlsFit fit = olsFit(y, x, i,
                                          static_cast<size_t>(cfg_.betaWindow));
                if (fit.ok) {
                    beta = fit.beta;
                    const size_t lo = i + 1 - static_cast<size_t>(cfg_.adfWindow);
                    const std::vector<double> e = egResidual(y, x, fit, lo, i + 1);
                    const double t = dfTStat(e);
                    gateOk = fit.beta > 0.0 && t <= cfg_.adfCritical;
                    if (gateOk) ++coint;

                    // z-score du résidu sur la QUEUE zWindow (σ population).
                    const size_t zw = static_cast<size_t>(cfg_.zWindow);
                    if (e.size() >= zw) {
                        double mz = 0.0;
                        for (size_t k = e.size() - zw; k < e.size(); ++k) mz += e[k];
                        mz /= static_cast<double>(zw);
                        double vz = 0.0;
                        for (size_t k = e.size() - zw; k < e.size(); ++k)
                            vz += (e[k] - mz) * (e[k] - mz);
                        vz /= static_cast<double>(zw);
                        const double sz = std::sqrt(vz);
                        if (sz > 0.0) { z = (e.back() - mz) / sz; zOk = true; }
                    }
                }
            }

            if (pos == 0) {
                // À plat : ouverture SEULEMENT si la paire est cointégrée (gate)
                // ET l'écart extrême. β figé à l'entrée → poids w0/w1 constants.
                if (gateOk && zOk && z <= -cfg_.entryK) {
                    entryEq = eq; eq *= std::pow(1.0 - c, 2.0);  // long P0 / short β·P1
                    pos = +1; entryLoc = i;
                    fw0 = 1.0 / (1.0 + beta); fw1 = beta / (1.0 + beta);
                } else if (gateOk && zOk && z >= cfg_.entryK) {
                    entryEq = eq; eq *= std::pow(1.0 - c, 2.0);  // short P0 / long β·P1
                    pos = -1; entryLoc = i;
                    fw0 = 1.0 / (1.0 + beta); fw1 = beta / (1.0 + beta);
                }
            } else {
                // En position : sortie au retour à la moyenne (côté-dépendant),
                // jugée sur le z ROULANT courant. Pas de gate à la sortie.
                const bool sortie = zOk && ((pos > 0 && z >= cfg_.exitZ)
                                         || (pos < 0 && z <= cfg_.exitZ));
                if (sortie) {
                    eq *= std::pow(1.0 - c, 2.0);   // coût de sortie (2 côtés)
                    closeStint(i, "reversion");
                    pos = 0;
                }
            }

            // Rendement du livre hedgé gagné de i à i+1 (β d'ENTRÉE figé).
            if (pos != 0 && p0[i] > 0.0 && p1[i] > 0.0) {
                const double r0 = p0[i + 1] / p0[i] - 1.0;
                const double r1 = p1[i + 1] / p1[i] - 1.0;
                eq *= (1.0 + pos * (fw0 * r0 - fw1 * r1));
                ++invested;
            }
        }

        // Liquidation forcée du stint ouvert au dernier close.
        if (pos != 0) {
            eq *= std::pow(1.0 - c, 2.0);
            closeStint(M - 1, "fin");
        }

        r.finalValue     = eq;
        r.totalReturnPct = (eq - cfg_.initialCapital) / cfg_.initialCapital * 100.0;

        // Références B&H sur la fenêtre depuis la fin du warmup.
        if (M > w + 1) {
            if (p0[w] > 0.0) {
                r.leg0BuyHoldReturnPct   = (p0[M - 1] / p0[w] - 1.0) * 100.0;
                r.leg0BuyHoldDrawdownPct = maxDrawdownOf_(p0, w, M);
            }
            const double ratio0 = p0[w] > 0.0 ? p0[M - 1] / p0[w] : 1.0;
            const double ratio1 = p1[w] > 0.0 ? p1[M - 1] / p1[w] : 1.0;
            r.basketBuyHoldReturnPct = (0.5 * ratio0 + 0.5 * ratio1 - 1.0) * 100.0;
        }
        r.alphaVsLeg0BuyHold   = r.totalReturnPct - r.leg0BuyHoldReturnPct;
        r.alphaVsBasketBuyHold = r.totalReturnPct - r.basketBuyHoldReturnPct;

        r.maxDrawdownPct = maxDrawdownOf_(r.equityCurve, 0, r.equityCurve.size());

        // Sharpe / Sortino : rendements journaliers depuis la fin du warmup.
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
            const long d0 = cpDaysFromCivil_(r.equityDates.front());
            const long d1 = cpDaysFromCivil_(r.equityDates.back());
            if (d0 >= 0 && d1 > d0) years = static_cast<double>(d1 - d0) / 365.25;
        }
        if (years <= 0.0 && r.equityCurve.size() > 1)
            years = static_cast<double>(r.equityCurve.size()) / 252.0;
        if (years > 0.0 && cfg_.initialCapital > 0.0 && eq > 0.0)
            r.cagrPct = (std::pow(eq / cfg_.initialCapital, 1.0 / years) - 1.0) * 100.0;
        r.calmarRatio = r.maxDrawdownPct > 0.0 ? r.cagrPct / r.maxDrawdownPct
                                               : (r.cagrPct > 0.0 ? 999.0 : 0.0);

        // % du temps en position + part des barres décidables cointégrées (D47).
        if (M > 1)
            r.pctTimeInvested = std::min(100.0,
                100.0 * static_cast<double>(invested) / static_cast<double>(M - 1));
        if (decidable > 0)
            r.pctBarsCointegrated =
                100.0 * static_cast<double>(coint) / static_cast<double>(decidable);

        return r;
    }
};

// ─── CointPairsWalkForward ────────────────────────────────────────────────────
// Pave l'axe commun en fenêtres IS/OOS contiguës (même mécanique que
// PairsWalkForward). Aucun paramètre ajusté par fenêtre → l'IS est purement
// informatif ; les 3 pavages testent la ROBUSTESSE du verdict OOS.
struct CointPairWindow {
    size_t isStart = 0, isEnd = 0, oosStart = 0, oosEnd = 0;
    CointPairsResult is;
    CointPairsResult oos;
};

class CointPairsWalkForward {
public:
    CointPairsWalkForward(CointPairsConfig cfg, std::vector<std::string> csvPaths,
                          size_t isBars, size_t oosBars, size_t step = 0,
                          size_t offset = 0)
        : pbt_(std::move(cfg), std::move(csvPaths))
        , isBars_(isBars), oosBars_(oosBars)
        , step_(step == 0 ? oosBars : step), offset_(offset) {}

    std::vector<CointPairWindow> run() const {
        std::vector<CointPairWindow> out;
        const size_t n    = pbt_.axis().size();
        const size_t span = isBars_ + oosBars_;
        if (span == 0 || n < span) return out;
        for (size_t s = offset_; s + span <= n; s += step_) {
            CointPairWindow w;
            w.isStart  = s;           w.isEnd  = s + isBars_;
            w.oosStart = w.isEnd;     w.oosEnd = w.isEnd + oosBars_;
            w.is  = pbt_.runRange(w.isStart,  w.isEnd);
            w.oos = pbt_.runRange(w.oosStart, w.oosEnd);
            out.push_back(std::move(w));
        }
        return out;
    }

    const AlignedAxis& axis() const { return pbt_.axis(); }

private:
    CointPairsBacktester pbt_;
    size_t isBars_, oosBars_, step_, offset_;
};

} // namespace trading
