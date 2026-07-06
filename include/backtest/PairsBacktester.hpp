#pragma once
#include "backtest/RotationBacktester.hpp"  // AlignedAxis, alignOnCommonDates (réutilisés)
#include "brokers/CsvDataFeed.hpp"
#include "brokers/PaperBroker.hpp"      // TradeRecord
#include "indicators/Indicators.hpp"    // SMA, RollingStdDev
#include <memory>
#include <string>
#include <vector>
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <limits>

namespace trading {

// ─── PairsBacktester ──────────────────────────────────────────────────────────
// 4e famille d'alpha (Sprint 12, décision utilisateur 11.4) : PAIRS-TRADING de
// valeur relative / MARKET-NEUTRAL. Les TROIS familles précédentes (trend-
// following mono-actif, rotation, mean-reversion) sont soldées sans edge OOS —
// toutes DIRECTIONNELLES sur un QQQ structurellement haussier (D48 : « acheter
// les creux hors tendance perd »). Le pairs-trading est la seule famille
// ORTHOGONALE : on ne parie pas sur la hausse d'un actif mais sur le RETOUR À LA
// MOYENNE de l'ÉCART entre deux actifs corrélés.
//
// Signal : z-score du spread log s = log(P0) − log(P1) sur une fenêtre glissante.
//  - à plat, z ≤ −entryK → LONG SPREAD (long P0 / short P1, poids +0,5 / −0,5) ;
//  - à plat, z ≥ +entryK → SHORT SPREAD (short P0 / long P1) ;
//  - en position, sortie au retour à la moyenne (long : z ≥ exitZ ; short :
//    z ≤ exitZ). La bande d'hystérésis [exitZ, entryK] évite le whipsaw.
//
// C'est un moteur SÉPARÉ (comme RotationBacktester) : il ne pilote NI TradingBot
// NI RiskManager NI la prod. La prod reste mono-symbole QQQ, paper, verrouillée ;
// SPY/IWM/MDY restent validation-only. Réutilise CsvDataFeed (total-return),
// alignOnCommonDates (axe commun 2 actifs), SMA + RollingStdDev (z du spread),
// TradeRecord (Monte-Carlo size-aware, D45) et MonteCarlo. Additif : aucun golden
// n'est touché.
//
// Conventions reprises du Backtester / RotationBacktester :
//  - anti look-ahead (B2) : décision au close[i], rendement gagné de i à i+1 ;
//  - ré-amorçage PAR fenêtre : le spread ET ses SMA/σ sont recalculés sur les
//    seules closes de la sous-fenêtre → aucune fuite d'historique (floorIdx) ;
//  - coûts de transaction (D22) : commission + slippage + demi-spread, PAR CÔTÉ
//    et par JAMBE (entrée = 2 côtés, sortie = 2 côtés) ;
//  - liquidation forcée en fin de fenêtre.
//
// SIMPLIFICATIONS ASSUMÉES (documentées, comme les défauts de slippage) :
//  - Sizing dollar-neutral 0,5 / 0,5 → exposition BRUTE = 1,0 (une convention
//    « spread unitaire » 1,0/1,0 = brut 2,0 doublerait rendement ET drawdown).
//    Invariant d'échelle pour le Sharpe ; n'affecte que les niveaux absolus figés.
//  - Rebalancement 50/50 IMPLICITE à chaque barre (constant-mix) : les jambes
//    dérivent en réalité ; le turnover intra-stint n'est PAS facturé (cohérent
//    avec la rotation qui ne facture qu'à la bascule).
//  - Coût d'emprunt / rebate du short IGNORÉ (quelques bps/an sur SPY/QQQ/IWM/MDY,
//    très liquides, mais non nul).
//  - β = 1 dollar-neutral ≠ beta-neutral : QQQ est plus haut-bêta que SPY, donc un
//    livre 0,5/0,5 porte un bêta net résiduel. β/hedge roulant = raffinement
//    additif ultérieur si un candidat émerge.

// Discipline & coûts (défauts miroir du Backtester/PaperBroker, D22). Pas d'enum
// de mode de hedge : β = 1 est codé en dur par le spread log — l'ajouter serait
// du code mort et une branche non testée (D33 impose de figer chaque champ).
struct PairsConfig {
    int    zWindow        = 20;       // fenêtre du z-score (SMA + σ du spread)
    double entryK         = 2.0;      // entrée si |z| ≥ entryK
    double exitZ          = 0.0;      // sortie au retour à la moyenne (z croise exitZ)
    double initialCapital = 10'000.0;
    double commissionPct  = 0.001;    // 0,1 % par côté
    double slippageBps    = 2.0;      // 2 bps par côté
    double halfSpreadBps  = 0.5;      // 0,5 bp par côté
};

struct PairsResult {
    double initialCapital = 0.0;
    double finalValue     = 0.0;
    double totalReturnPct = 0.0;     // rendement absolu net (critère PRIMAIRE market-neutral)
    // Références de CONTINUITÉ (attendues très négatives — PAS le critère : un
    // livre market-neutral ne bat pas un indice long) : on ne nomme PAS ces
    // champs « alphaVsBest » pour ne pas suggérer que battre un long est la barre.
    double leg0BuyHoldReturnPct   = 0.0;   // B&H de la jambe 0 sur la fenêtre
    double leg0BuyHoldDrawdownPct = 0.0;   // DD du B&H jambe 0 (clause DoD « DD réduit ≥ 50 % »)
    double basketBuyHoldReturnPct = 0.0;   // panier équipondéré 2 jambes (statique)
    double alphaVsLeg0BuyHold     = 0.0;   // totalReturn − leg0BuyHold
    double alphaVsBasketBuyHold   = 0.0;   // totalReturn − basket
    double maxDrawdownPct = 0.0;     // DD de la stratégie (à comparer à leg0BuyHoldDrawdownPct)
    double cagrPct        = 0.0;
    double calmarRatio    = 0.0;
    double sharpeRatio    = 0.0;     // le vrai critère de succès d'un market-neutral
    double sortinoRatio   = 0.0;
    double pctTimeInvested = 0.0;    // % de barres réellement en position (hors flat)
    int    roundTripCount  = 0;      // nombre d'allers-retours (un stint = un trade)
    std::vector<double>      equityCurve;   // une valeur par barre commune
    std::vector<std::string> equityDates;   // PARALLÈLE à equityCurve
    std::vector<TradeRecord> trades;        // un stint détenu = un trade
};

// ── Fonctions PURES (testables sans I/O) ─────────────────────────────────────

// Spread log s[i] = log(P0[i]) − log(P1[i]) sur les deux séries alignées. Garde
// de positivité (total-return closes > 0, mais on protège quand même) : un prix
// ≤ 0 rend le spread indéfini → on répète la dernière valeur valide (ou 0 au
// départ), ce qui neutralise la barre côté signal (σ locale l'absorbe).
inline std::vector<double> logSpread(const std::vector<double>& p0,
                                     const std::vector<double>& p1) {
    const size_t n = std::min(p0.size(), p1.size());
    std::vector<double> s(n, 0.0);
    for (size_t i = 0; i < n; ++i) {
        if (p0[i] > 0.0 && p1[i] > 0.0)
            s[i] = std::log(p0[i]) - std::log(p1[i]);
        else if (i > 0)
            s[i] = s[i - 1];
    }
    return s;
}

class PairsBacktester {
public:
    PairsBacktester(PairsConfig cfg, std::vector<std::string> csvPaths)
        : cfg_(std::move(cfg)), csvPaths_(std::move(csvPaths)) {
        std::vector<std::vector<Bar>> series;
        series.reserve(csvPaths_.size());
        for (const auto& p : csvPaths_)
            series.push_back(CsvDataFeed(p).allBars());
        axis_ = alignOnCommonDates(series);
    }

    // Seam de test : construit un moteur sur un axe déjà aligné (données
    // synthétiques en mémoire, sans CSV). Assertion forte : le pairs-trading
    // exige EXACTEMENT 2 actifs (un axe à 3+ actifs est une erreur d'appel).
    static PairsBacktester fromAxis(PairsConfig cfg, AlignedAxis axis) {
        PairsBacktester pb(std::move(cfg), std::vector<std::string>{});
        pb.axis_ = std::move(axis);
        return pb;
    }

    PairsResult run() const { return runRange(0, axis_.size()); }

    // Pairs-trade sur la sous-fenêtre [startIdx, endIdx) de l'axe commun. Le
    // spread ET ses SMA/σ sont RE-CALCULÉS sur les seules closes de la
    // sous-fenêtre (analogue floorIdx → anti-leak entre fenêtres).
    PairsResult runRange(size_t startIdx, size_t endIdx) const {
        return compute_(startIdx, endIdx);
    }

    const AlignedAxis& axis() const { return axis_; }

private:
    PairsConfig              cfg_;
    std::vector<std::string> csvPaths_;
    AlignedAxis              axis_;

    // Coût par côté (fraction) : commission + (slippage + demi-spread) en bps.
    static double perSideCost_(const PairsConfig& c) {
        return c.commissionPct + (c.slippageBps + c.halfSpreadBps) / 10'000.0;
    }

    // « YYYY-MM-DD » → jours civils (days_from_civil, Hinnant) ; -1 si invalide.
    static long pairsDaysFromCivil_(const std::string& date) {
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

    // Drawdown maximal (%) d'une série de prix sur [lo, hi).
    static double maxDrawdownOf_(const std::vector<double>& px, size_t lo, size_t hi) {
        double peak = 0.0, dd = 0.0;
        for (size_t i = lo; i < hi && i < px.size(); ++i) {
            peak = std::max(peak, px[i]);
            if (peak > 0.0) dd = std::max(dd, (peak - px[i]) / peak * 100.0);
        }
        return dd;
    }

    PairsResult compute_(size_t startIdx, size_t endIdx) const {
        PairsResult r;
        r.initialCapital = cfg_.initialCapital;
        r.finalValue     = cfg_.initialCapital;

        const size_t n = axis_.size();
        if (endIdx > n) endIdx = n;
        // Le pairs-trading exige EXACTEMENT 2 actifs ; sinon résultat neutre.
        if (startIdx >= endIdx || axis_.assets() != 2) return r;
        const size_t M = endIdx - startIdx;

        // Sous-séries de clôtures des deux jambes (ré-amorçage par fenêtre).
        std::vector<double> p0(axis_.close[0].begin() + static_cast<long>(startIdx),
                               axis_.close[0].begin() + static_cast<long>(endIdx));
        std::vector<double> p1(axis_.close[1].begin() + static_cast<long>(startIdx),
                               axis_.close[1].begin() + static_cast<long>(endIdx));

        // Spread log + z-score (SMA + σ glissants), calculés SUR LA SOUS-FENÊTRE.
        const std::vector<double> s   = logSpread(p0, p1);
        const std::vector<double> sma = SMA(cfg_.zWindow).compute(s);            // {} si M < zWindow
        const std::vector<double> sd  = RollingStdDev(cfg_.zWindow).compute(s);  // idem
        // Barre décidable : fin du warmup du z-score = zWindow − 1 (PAS smaPeriod−1).
        const size_t w = cfg_.zWindow > 0 ? static_cast<size_t>(cfg_.zWindow) - 1 : 0;

        const double c = perSideCost_(cfg_);
        double eq       = cfg_.initialCapital;
        int    pos      = 0;      // 0 = plat, +1 = long spread, -1 = short spread
        double entryEq  = 0.0;    // équité AVANT le coût d'entrée (pour le pnlPct du stint)
        size_t entryLoc = 0;
        size_t invested = 0;

        r.equityCurve.reserve(M);
        r.equityDates.reserve(M);

        auto closeStint = [&](size_t exitLoc, const char* reason) {
            // pnlPct = rendement PORTÉ AU PORTEFEUILLE sur la vie du stint (coûts
            // d'entrée ET de sortie inclus), dérivé de l'équité COMPOSÉE barre par
            // barre — JAMAIS d'un ratio de prix unique (faux pour 2 jambes : la
            // jambe short compose de façon path-dependante).
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
            // Le 0,5/0,5 est déjà dans pnlPct (rendement plein portefeuille) → 1,0.
            t.deployedFraction = 1.0;
            r.trades.push_back(std::move(t));
            ++r.roundTripCount;
        };

        for (size_t i = 0; i < M; ++i) {
            // Valorisation au marché AVANT toute décision (marks pré-liquidation)
            r.equityCurve.push_back(eq);
            r.equityDates.push_back(axis_.dates[startIdx + i]);
            if (i + 1 >= M) break;   // dernière barre : aucune séance suivante

            // z au close[i] : défini seulement si σ locale > 0 (warmup / série
            // trop courte → sd vide ou 0 → z indéfini → aucune décision).
            const bool zOk = !sd.empty() && sd[i] > 0.0 && !sma.empty();
            const double z = zOk ? (s[i] - sma[i]) / sd[i] : 0.0;

            if (pos == 0) {
                // À plat : ouverture si l'écart est extrême.
                if (zOk && z <= -cfg_.entryK) {
                    entryEq = eq; eq *= std::pow(1.0 - c, 2.0);  // long P0 / short P1
                    pos = +1; entryLoc = i;
                } else if (zOk && z >= cfg_.entryK) {
                    entryEq = eq; eq *= std::pow(1.0 - c, 2.0);  // short P0 / long P1
                    pos = -1; entryLoc = i;
                }
            } else {
                // En position : sortie au retour à la moyenne (côté-dépendant).
                const bool sortie = zOk && ((pos > 0 && z >= cfg_.exitZ)
                                         || (pos < 0 && z <= cfg_.exitZ));
                if (sortie) {
                    eq *= std::pow(1.0 - c, 2.0);   // coût de sortie (2 côtés)
                    closeStint(i, "reversion");
                    pos = 0;
                    // À plat pour cette barre (hystérésis : z ne peut pas être à la
                    // fois ≥ exitZ et ≤ −entryK) → aucun rendement gagné i→i+1.
                }
            }

            // Rendement dollar-neutral gagné de i à i+1 (pas de look-ahead).
            if (pos != 0 && p0[i] > 0.0 && p1[i] > 0.0) {
                const double r0 = p0[i + 1] / p0[i] - 1.0;
                const double r1 = p1[i + 1] / p1[i] - 1.0;
                eq *= (1.0 + pos * (0.5 * r0 - 0.5 * r1));
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

        // Références B&H sur la fenêtre depuis la fin du warmup du z-score.
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

        // Max drawdown de la stratégie (marks pré-liquidation, comme le Backtester)
        r.maxDrawdownPct = maxDrawdownOf_(r.equityCurve, 0, r.equityCurve.size());

        // Sharpe / Sortino : rendements journaliers à partir de la fin du warmup
        // (avant, l'équité est plate — tout cash — et n'ajouterait que des zéros).
        if (r.equityCurve.size() > w + 1) {
            std::vector<double> rets;
            for (size_t i = w + 1; i < r.equityCurve.size(); ++i)
                if (r.equityCurve[i - 1] > 0.0)
                    rets.push_back((r.equityCurve[i] - r.equityCurve[i - 1]) / r.equityCurve[i - 1]);
            if (!rets.empty()) {
                double mean = 0.0;
                for (double x : rets) mean += x;
                mean /= static_cast<double>(rets.size());
                double var = 0.0, downVar = 0.0;
                for (double x : rets) { var += (x - mean) * (x - mean); if (x < 0) downVar += x * x; }
                var     /= static_cast<double>(rets.size());
                downVar /= static_cast<double>(rets.size());
                const double stddev  = std::sqrt(var);
                const double downDev = std::sqrt(downVar);
                r.sharpeRatio  = stddev  > 0.0 ? (mean * 252.0) / (stddev  * std::sqrt(252.0)) : 0.0;
                r.sortinoRatio = downDev > 0.0 ? (mean * 252.0) / (downDev * std::sqrt(252.0))
                                               : (mean > 0.0 ? 999.0 : 0.0);
            }
        }

        // CAGR (durée réelle via les dates d'équité), Calmar.
        double years = 0.0;
        if (r.equityDates.size() >= 2) {
            const long d0 = pairsDaysFromCivil_(r.equityDates.front());
            const long d1 = pairsDaysFromCivil_(r.equityDates.back());
            if (d0 >= 0 && d1 > d0) years = static_cast<double>(d1 - d0) / 365.25;
        }
        if (years <= 0.0 && r.equityCurve.size() > 1)
            years = static_cast<double>(r.equityCurve.size()) / 252.0;
        if (years > 0.0 && cfg_.initialCapital > 0.0 && eq > 0.0)
            r.cagrPct = (std::pow(eq / cfg_.initialCapital, 1.0 / years) - 1.0) * 100.0;
        r.calmarRatio = r.maxDrawdownPct > 0.0 ? r.cagrPct / r.maxDrawdownPct
                                               : (r.cagrPct > 0.0 ? 999.0 : 0.0);

        // % du temps réellement en position (mesure anti-cash-drag, garde D47).
        if (M > 1)
            r.pctTimeInvested = std::min(100.0,
                100.0 * static_cast<double>(invested) / static_cast<double>(M - 1));

        return r;
    }
};

// ─── PairsWalkForward ─────────────────────────────────────────────────────────
// Pave l'axe commun en fenêtres IS/OOS contiguës (même mécanique que
// WalkForward / RotationWalkForward). Le pairs-trading n'AJUSTE aucun paramètre
// par fenêtre → l'IS est purement informatif ; les 3 pavages (canonique / fin /
// décalé) testent la ROBUSTESSE du verdict OOS sur des sous-périodes différentes.
struct PairWindow {
    size_t isStart = 0, isEnd = 0, oosStart = 0, oosEnd = 0;
    PairsResult is;
    PairsResult oos;
};

class PairsWalkForward {
public:
    PairsWalkForward(PairsConfig cfg, std::vector<std::string> csvPaths,
                     size_t isBars, size_t oosBars, size_t step = 0,
                     size_t offset = 0)
        : pbt_(std::move(cfg), std::move(csvPaths))
        , isBars_(isBars), oosBars_(oosBars)
        , step_(step == 0 ? oosBars : step), offset_(offset) {}

    std::vector<PairWindow> run() const {
        std::vector<PairWindow> out;
        const size_t n    = pbt_.axis().size();
        const size_t span = isBars_ + oosBars_;
        if (span == 0 || n < span) return out;
        for (size_t s = offset_; s + span <= n; s += step_) {
            PairWindow w;
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
    PairsBacktester pbt_;
    size_t isBars_, oosBars_, step_, offset_;
};

} // namespace trading
