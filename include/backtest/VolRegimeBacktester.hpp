#pragma once
#include "brokers/CsvDataFeed.hpp"
#include "brokers/PaperBroker.hpp"      // TradeRecord
#include "indicators/Indicators.hpp"    // RollingStdDev
#include <string>
#include <vector>
#include <cmath>
#include <cstdio>
#include <algorithm>

namespace trading {

// ─── VolRegimeBacktester ──────────────────────────────────────────────────────
// 5e famille d'alpha (Sprint 13, décision utilisateur (c) — famille NEUVE au-delà
// des signaux de prix simples). Les QUATRE familles précédentes (trend-following
// mono-actif, rotation multi-actifs, mean-reversion, pairs-trading) sont soldées
// sans edge OOS. Ici on ne parie NI sur la direction du prix NI sur le retour à la
// moyenne d'un écart : on module l'EXPOSITION selon un RÉGIME DE VOLATILITÉ
// (hypothèse « volatility-managed portfolios », Moreira & Muir 2017 — réduire
// l'exposition quand la volatilité récente est élevée améliorerait le rendement
// ajusté du risque).
//
// Signal : volatilité RÉALISÉE (écart-type glissant des rendements journaliers sur
// `volLookback` barres), COMPARÉE à sa propre médiane glissante sur `refLookback`
// barres (référence « normale » auto-normalisante, sans échelle → comparable entre
// actifs et entre fenêtres). Filtre binaire long/cash :
//  - régime CALME (vol réalisée ≤ thresholdMult × médiane de référence) → LONG ;
//  - régime AGITÉ (vol au-dessus) ou INCONNU (warmup) → CASH.
// Premier jet DISCRET (long/cash) et non un scaling continu w = cible/réalisée :
// le scaling continu introduirait un champ `targetVol`/plafond de levier qui
// serait du code mort tant que le moteur est binaire (D33 impose de figer chaque
// champ) ; il est réservé à un sprint ultérieur (backlog), tout comme une variante
// à volatilité IMPLICITE (VIX/VXN — aucune donnée dans le dépôt aujourd'hui).
//
// C'est un moteur SÉPARÉ (comme RotationBacktester / PairsBacktester) : il ne
// pilote NI TradingBot NI RiskManager NI la prod. La prod reste mono-symbole QQQ,
// paper, verrouillée. Réutilise CsvDataFeed (total-return), RollingStdDev (vol
// réalisée), TradeRecord (Monte-Carlo size-aware, D45) et MonteCarlo. Additif :
// aucun golden existant n'est touché.
//
// Conventions reprises du Backtester / RotationBacktester :
//  - anti look-ahead (B2, D37) : décision au close[i] (vol + médiane calculées sur
//    les seules données ≤ i), rendement gagné de i à i+1 ;
//  - ré-amorçage PAR fenêtre (floorIdx) : vol réalisée ET médiane recalculées sur
//    les seules barres de la sous-fenêtre → aucune fuite d'historique ;
//  - coûts de transaction (D22) : commission + slippage + demi-spread, PAR CÔTÉ ;
//  - liquidation forcée en fin de fenêtre.
//
// AVERTISSEMENT WARMUP (D35) : le warmup vaut volLookback + refLookback − 2 barres
// (la vol réalisée doit être valide ET la médiane de référence pleine) — bien plus
// que le z-score du pairs-trading. Toute fenêtre OOS doit être dimensionnée
// LARGEMENT au-dessus (sinon on ne mesure que du cash drag, piège D34/D35).

// Discipline & coûts (défauts miroir du Backtester/PaperBroker, D22). Pas de champ
// de scaling continu : le moteur est binaire long/cash (D33 — un champ non exercé
// serait du code mort et une branche non testée).
struct VolRegimeConfig {
    int    volLookback    = 20;       // fenêtre de la vol réalisée (σ des rendements)
    int    refLookback    = 126;      // fenêtre de la médiane de référence (~6 mois)
    double thresholdMult  = 1.0;      // calme si vol ≤ thresholdMult × médiane
    double initialCapital = 10'000.0;
    double commissionPct  = 0.001;    // 0,1 % par côté
    double slippageBps    = 2.0;      // 2 bps par côté
    double halfSpreadBps  = 0.5;      // 0,5 bp par côté
};

struct VolRegimeResult {
    double initialCapital = 0.0;
    double finalValue     = 0.0;
    double totalReturnPct = 0.0;
    // Références Buy & Hold de l'actif sur la fenêtre (depuis la fin du warmup). Le
    // filtre long/cash est DIRECTIONNEL → comparable au B&H sur rendement ET Sharpe.
    double buyHoldPct            = 0.0;   // B&H de l'actif sur la fenêtre
    double buyHoldMaxDrawdownPct = 0.0;   // DD du B&H (clause DoD « DD réduit ≥ 50 % »)
    double buyHoldSharpe         = 0.0;   // Sharpe du B&H (référence du critère ajusté du risque)
    double alphaVsBuyHold        = 0.0;   // totalReturn − buyHold (sanity D23 : « faire de l'argent »)
    double maxDrawdownPct = 0.0;     // DD de la stratégie (à comparer à buyHoldMaxDrawdownPct)
    double cagrPct        = 0.0;
    double calmarRatio    = 0.0;
    double sharpeRatio    = 0.0;     // critère PRIMAIRE : Sharpe stratégie vs buyHoldSharpe
    double sortinoRatio   = 0.0;
    double pctTimeInvested = 0.0;    // % de barres réellement long (mesure du cash drag T4)
    int    switchCount     = 0;      // nombre de bascules régime (calme⇄agité)
    std::vector<double>      equityCurve;   // une valeur par barre de la fenêtre
    std::vector<std::string> equityDates;   // PARALLÈLE à equityCurve
    std::vector<TradeRecord> trades;        // un stint long = un trade (cash exclu)
};

// ── Fonctions PURES (testables sans I/O) ─────────────────────────────────────

// Rendements simples barre à barre : ret[0] = 0, ret[i] = close[i]/close[i-1] − 1.
// Garde de positivité (close total-return > 0, mais on protège quand même).
inline std::vector<double> closeReturns(const std::vector<double>& closes) {
    std::vector<double> ret(closes.size(), 0.0);
    for (size_t i = 1; i < closes.size(); ++i)
        ret[i] = closes[i - 1] > 0.0 ? closes[i] / closes[i - 1] - 1.0 : 0.0;
    return ret;
}

// Volatilité réalisée : écart-type glissant (population) des rendements sur
// `volLookback` barres. rv[i] utilise les seuls rendements ≤ i (causal). Warmup :
// 0 jusqu'à l'indice volLookback−1 (convention RollingStdDev), donc RV vide ⇒
// vecteur de zéros de la même longueur que `closes` (pour un accès homogène).
inline std::vector<double> realizedVol(const std::vector<double>& closes,
                                       int volLookback) {
    if (volLookback <= 0) return std::vector<double>(closes.size(), 0.0);
    const std::vector<double> ret = closeReturns(closes);
    std::vector<double> rv = RollingStdDev(volLookback).compute(ret);  // {} si trop court
    if (rv.size() != closes.size()) rv.assign(closes.size(), 0.0);
    return rv;
}

// Médiane glissante CAUSALE sur `window` barres : out[i] = médiane de
// v[i−window+1 .. i] pour i ≥ window−1, sinon 0 (fenêtre incomplète = warmup).
// Fenêtre paire → moyenne des deux valeurs centrales. Aucun usage de v[j > i].
inline std::vector<double> trailingMedian(const std::vector<double>& v, int window) {
    std::vector<double> out(v.size(), 0.0);
    if (window <= 0) return out;
    const size_t win = static_cast<size_t>(window);
    for (size_t i = 0; i < v.size(); ++i) {
        if (i + 1 < win) continue;   // fenêtre incomplète
        std::vector<double> w(v.begin() + static_cast<long>(i + 1 - win),
                              v.begin() + static_cast<long>(i + 1));
        std::sort(w.begin(), w.end());
        const size_t m = w.size() / 2;
        out[i] = (w.size() % 2 == 1) ? w[m] : 0.5 * (w[m - 1] + w[m]);
    }
    return out;
}

class VolRegimeBacktester {
public:
    // Charge UNE série (mono-actif : QQQ en prod, SPY/IWM/MDY en validation-only).
    VolRegimeBacktester(VolRegimeConfig cfg, std::string csvPath)
        : cfg_(std::move(cfg)) {
        const auto bars = CsvDataFeed(csvPath).allBars();
        dates_.reserve(bars.size());
        closes_.reserve(bars.size());
        for (const auto& b : bars) { dates_.push_back(b.date); closes_.push_back(b.close); }
    }

    // Seam de test : construit un moteur sur une série synthétique en mémoire (sans
    // CSV), l'analogue mono-actif de PairsBacktester::fromAxis.
    static VolRegimeBacktester fromSeries(VolRegimeConfig cfg,
                                          std::vector<std::string> dates,
                                          std::vector<double> closes) {
        VolRegimeBacktester v(std::move(cfg));
        v.dates_  = std::move(dates);
        v.closes_ = std::move(closes);
        return v;
    }

    VolRegimeResult run() const { return runRange(0, closes_.size()); }

    // Backtest sur la sous-fenêtre [startIdx, endIdx). Vol réalisée ET médiane
    // RE-CALCULÉES sur les seules closes de la sous-fenêtre (anti-leak, floorIdx).
    VolRegimeResult runRange(size_t startIdx, size_t endIdx) const {
        return compute_(startIdx, endIdx);
    }

    size_t size() const { return closes_.size(); }
    const std::vector<std::string>& dates() const { return dates_; }

private:
    explicit VolRegimeBacktester(VolRegimeConfig cfg) : cfg_(std::move(cfg)) {}

    VolRegimeConfig          cfg_;
    std::vector<std::string> dates_;
    std::vector<double>      closes_;

    // Coût par côté (fraction) : commission + (slippage + demi-spread) en bps.
    static double perSideCost_(const VolRegimeConfig& c) {
        return c.commissionPct + (c.slippageBps + c.halfSpreadBps) / 10'000.0;
    }

    // Première barre DÉCIDABLE : la vol réalisée est valide dès volLookback−1 et la
    // médiane de référence est pleine dès refLookback−1 valeurs de vol valides →
    // warmup total = (volLookback−1) + (refLookback−1) = volLookback+refLookback−2.
    static size_t firstDecidable_(int volLookback, int refLookback) {
        const size_t vw = volLookback > 0 ? static_cast<size_t>(volLookback) - 1 : 0;
        const size_t rw = refLookback > 0 ? static_cast<size_t>(refLookback) - 1 : 0;
        return vw + rw;
    }

    // « YYYY-MM-DD » → jours civils (days_from_civil, Hinnant) ; -1 si invalide.
    static long volDaysFromCivil_(const std::string& date) {
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

    VolRegimeResult compute_(size_t startIdx, size_t endIdx) const {
        VolRegimeResult r;
        r.initialCapital = cfg_.initialCapital;
        r.finalValue     = cfg_.initialCapital;

        const size_t n = closes_.size();
        if (endIdx > n) endIdx = n;
        if (startIdx >= endIdx) return r;
        const size_t M = endIdx - startIdx;

        // Sous-série de clôtures (ré-amorçage par fenêtre).
        std::vector<double> px(closes_.begin() + static_cast<long>(startIdx),
                               closes_.begin() + static_cast<long>(endIdx));

        // Vol réalisée + médiane de référence, calculées SUR LA SOUS-FENÊTRE.
        const std::vector<double> rv  = realizedVol(px, cfg_.volLookback);
        const std::vector<double> med = trailingMedian(rv, cfg_.refLookback);
        const size_t w = firstDecidable_(cfg_.volLookback, cfg_.refLookback);

        const double c = perSideCost_(cfg_);
        double eq       = cfg_.initialCapital;
        bool   held     = false;   // false = CASH, true = LONG
        size_t entryLoc = 0;
        size_t invested = 0;

        r.equityCurve.reserve(M);
        r.equityDates.reserve(M);

        auto closeStint = [&](size_t exitLoc, const char* reason) {
            const double gross = px[entryLoc] > 0.0 ? px[exitLoc] / px[entryLoc] : 1.0;
            const double net   = gross * std::pow(1.0 - c, 2.0);   // 1 côté entrée + 1 côté sortie
            TradeRecord t;
            t.buyDate    = dates_[startIdx + entryLoc];
            t.sellDate   = dates_[startIdx + exitLoc];
            t.buyPrice   = px[entryLoc];
            t.sellPrice  = px[exitLoc];
            t.shares     = 0;
            t.pnlPct     = (net - 1.0) * 100.0;
            t.pnl        = cfg_.initialCapital * (net - 1.0);   // indicatif (plein déploiement)
            t.holdDays   = static_cast<int>(exitLoc - entryLoc);
            t.exitReason = reason;
            t.isWin      = t.pnlPct > 0.0;
            t.deployedFraction = 1.0;   // long/cash = plein investissement quand détenu
            r.trades.push_back(std::move(t));
        };

        for (size_t i = 0; i < M; ++i) {
            // Valorisation au marché AVANT toute décision (marks pré-liquidation).
            r.equityCurve.push_back(eq);
            r.equityDates.push_back(dates_[startIdx + i]);
            if (i + 1 >= M) break;   // dernière barre : aucune séance suivante

            // Régime au close[i] : décidable seulement après le warmup. Calme si la
            // vol réalisée est ≤ thresholdMult × médiane de référence (données ≤ i).
            // Régime inconnu (i < w) → CASH (inconnu ≠ calme, comme le filtre SMA200).
            bool target = false;
            if (i >= w)
                target = rv[i] <= cfg_.thresholdMult * med[i];

            if (target != held) {
                ++r.switchCount;
                const int sides = (held ? 1 : 0) + (target ? 1 : 0);
                eq *= std::pow(1.0 - c, static_cast<double>(sides));
                if (held)  closeStint(i, "regime");   // clôt le stint long sortant
                if (target) entryLoc = i;             // ouvre le nouveau stint long
            }
            held = target;

            if (held) {
                const double ratio = px[i] > 0.0 ? px[i + 1] / px[i] : 1.0;
                eq *= ratio;   // pas de look-ahead : rendement gagné de i à i+1
                ++invested;
            }
        }

        // Liquidation forcée du stint ouvert au dernier close.
        if (held) {
            eq *= (1.0 - c);
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

        // Sharpe / Sortino stratégie : rendements journaliers depuis la fin du warmup
        // (avant, l'équité est plate — tout cash — et n'ajouterait que des zéros).
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
            const long d0 = volDaysFromCivil_(r.equityDates.front());
            const long d1 = volDaysFromCivil_(r.equityDates.back());
            if (d0 >= 0 && d1 > d0) years = static_cast<double>(d1 - d0) / 365.25;
        }
        if (years <= 0.0 && r.equityCurve.size() > 1)
            years = static_cast<double>(r.equityCurve.size()) / 252.0;
        if (years > 0.0 && cfg_.initialCapital > 0.0 && eq > 0.0)
            r.cagrPct = (std::pow(eq / cfg_.initialCapital, 1.0 / years) - 1.0) * 100.0;
        r.calmarRatio = r.maxDrawdownPct > 0.0 ? r.cagrPct / r.maxDrawdownPct
                                               : (r.cagrPct > 0.0 ? 999.0 : 0.0);

        // % du temps réellement long (mesure du cash drag T4).
        if (M > 1)
            r.pctTimeInvested = std::min(100.0,
                100.0 * static_cast<double>(invested) / static_cast<double>(M - 1));

        return r;
    }
};

// ─── VolRegimeWalkForward ─────────────────────────────────────────────────────
// Pave la série mono-actif en fenêtres IS/OOS contiguës (même mécanique que
// RotationWalkForward / PairsWalkForward). Le filtre n'AJUSTE aucun paramètre par
// fenêtre → l'IS est purement informatif ; les 3 pavages (canonique / fin /
// décalé) testent la ROBUSTESSE du verdict OOS sur des sous-périodes différentes.
struct VolWindow {
    size_t isStart = 0, isEnd = 0, oosStart = 0, oosEnd = 0;
    VolRegimeResult is;
    VolRegimeResult oos;
};

class VolRegimeWalkForward {
public:
    VolRegimeWalkForward(VolRegimeConfig cfg, std::string csvPath,
                         size_t isBars, size_t oosBars, size_t step = 0,
                         size_t offset = 0)
        : vbt_(std::move(cfg), std::move(csvPath))
        , isBars_(isBars), oosBars_(oosBars)
        , step_(step == 0 ? oosBars : step), offset_(offset) {}

    std::vector<VolWindow> run() const {
        std::vector<VolWindow> out;
        const size_t n    = vbt_.size();
        const size_t span = isBars_ + oosBars_;
        if (span == 0 || n < span) return out;
        for (size_t s = offset_; s + span <= n; s += step_) {
            VolWindow w;
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
    VolRegimeBacktester vbt_;
    size_t isBars_, oosBars_, step_, offset_;
};

} // namespace trading
