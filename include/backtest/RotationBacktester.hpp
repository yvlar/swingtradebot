#pragma once
#include "brokers/CsvDataFeed.hpp"
#include "brokers/PaperBroker.hpp"      // TradeRecord
#include "indicators/Indicators.hpp"    // SMA
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <limits>

namespace trading {

// ─── RotationBacktester ───────────────────────────────────────────────────────
// Rotation multi-actifs / détention par régime (Sprint 8-nonies). Attaque la
// cause racine T4 du déficit d'alpha : la chaîne mono-actif est en CASH
// l'essentiel du temps et paie le temps hors marché. Ici, à CHAQUE barre, on
// classe les N actifs (QQQ/SPY/IWM/MDY) par FORCE DE RÉGIME — distance relative
// au SMA de fond, (close − SMA)/SMA — et on détient le plus fort s'il est
// haussier (close > son propre SMA), sinon CASH. On ne bascule que lorsque
// l'actif de tête change (règle « hold until rank-1 changes » — décision
// utilisateur : réévaluation quotidienne, pas de rebalancement calendaire).
//
// C'est un moteur SÉPARÉ de la chaîne SwingStrategy : il ne pilote ni TradingBot
// ni RiskManager. Il réutilise CsvDataFeed (chargement + total-return),
// l'indicateur SMA, TradeRecord (pour le Monte-Carlo size-aware, D45) et
// MonteCarlo. Additif : aucun golden existant n'est touché.
//
// Conventions reprises du Backtester :
//  - anti look-ahead (B2) : décision au close[i], rendement gagné de i à i+1 ;
//  - ré-amorçage PAR fenêtre (floorIdx, item 7.1) : le SMA est recalculé sur les
//    seules closes de la sous-fenêtre → aucune fuite d'historique entre fenêtres ;
//  - coûts de transaction (D22) : commission + slippage + demi-spread, par côté ;
//  - liquidation forcée en fin de fenêtre (comme closeOpenPosition()).

// Axe commun : intersection des dates ISO des N actifs. Les dates ISO
// « YYYY-MM-DD » se comparent/trient lexicographiquement = chronologiquement, et
// CsvDataFeed trie déjà chaque série par date ascendante. close[actif][i] est la
// clôture total-return de l'actif à la i-ème date COMMUNE.
struct AlignedAxis {
    std::vector<std::string>         dates;   // dates communes, ascendantes
    std::vector<std::vector<double>> close;   // close[actif][i]
    size_t size()   const { return dates.size(); }
    size_t assets() const { return close.size(); }
};

// Discipline & coûts (défauts miroir du Backtester/PaperBroker, D22).
struct RotationConfig {
    int    smaPeriod      = 200;      // filtre de régime PAR actif (item 8.1)
    double initialCapital = 10'000.0;
    double commissionPct  = 0.001;    // 0,1 % par côté
    double slippageBps    = 2.0;      // 2 bps par côté
    double halfSpreadBps  = 0.5;      // 0,5 bp par côté
};

struct RotationResult {
    double initialCapital  = 0.0;
    double finalValue      = 0.0;
    double totalReturnPct  = 0.0;
    double bestBuyHoldPct  = 0.0;   // meilleur B&H mono-actif sur la fenêtre (B&L)
    double basketReturnPct = 0.0;   // panier équipondéré B&H (statique)
    double alphaVsBest     = 0.0;   // totalReturn − bestBuyHold
    double alphaVsBasket   = 0.0;   // totalReturn − basket
    double maxDrawdownPct  = 0.0;
    double cagrPct         = 0.0;
    double calmarRatio     = 0.0;
    double sharpeRatio     = 0.0;
    double sortinoRatio    = 0.0;
    double pctTimeInvested = 0.0;   // % de barres réellement détenu (hors cash)
    int    switchCount     = 0;
    std::vector<double>      equityCurve;   // une valeur par barre commune
    std::vector<std::string> equityDates;   // PARALLÈLE à equityCurve
    std::vector<TradeRecord> trades;        // un stint détenu = un trade (cash exclu)
};

// ── Fonctions PURES (testables sans I/O, item 8n.1) ──────────────────────────

// Aligne N séries de barres (déjà triées ascendantes) sur leurs dates COMMUNES.
// Résultat : dates ascendantes, close[a] toutes de la même longueur. Les dates
// absentes d'un actif (jours fériés différents, cotation plus tardive, trous)
// sont écartées de façon cohérente pour tous les actifs.
inline AlignedAxis alignOnCommonDates(const std::vector<std::vector<Bar>>& assets) {
    AlignedAxis ax;
    const size_t n = assets.size();
    if (n == 0 || assets[0].empty()) { ax.close.resize(n); return ax; }
    ax.close.resize(n);

    // date → index pour les actifs 1..N-1
    std::vector<std::unordered_map<std::string, size_t>> pos(n);
    for (size_t a = 1; a < n; ++a) {
        pos[a].reserve(assets[a].size() * 2);
        for (size_t j = 0; j < assets[a].size(); ++j)
            pos[a].emplace(assets[a][j].date, j);
    }

    // Piloté par l'actif 0 (ascendant) : on garde les dates présentes PARTOUT
    for (const Bar& b : assets[0]) {
        bool commune = true;
        for (size_t a = 1; a < n && commune; ++a)
            if (pos[a].find(b.date) == pos[a].end()) commune = false;
        if (!commune) continue;

        ax.dates.push_back(b.date);
        ax.close[0].push_back(b.close);
        for (size_t a = 1; a < n; ++a)
            ax.close[a].push_back(assets[a][pos[a].at(b.date)].close);
    }
    return ax;
}

// Renvoie l'indice de l'actif le plus FORT et HAUSSIER, sinon -1 (CASH).
// Force de régime = distance relative au SMA : (close − sma)/sma. Éligible si
// close > sma ET sma > 0 (le SMA vaut 0 pendant le warmup / série trop courte :
// régime INCONNU ≠ baissier, comme SwingStrategy.hpp:185-192 → non éligible, PAS
// « haussier »). Départage déterministe : indice le plus bas (scan avec >
// strict), pour une reproductibilité au bit près.
inline int rankStrongest(const std::vector<double>& closes,
                         const std::vector<double>& smas) {
    int    best      = -1;
    double bestForce = 0.0;
    for (size_t a = 0; a < closes.size(); ++a) {
        const double sma = a < smas.size() ? smas[a] : 0.0;
        if (sma <= 0.0 || closes[a] <= sma) continue;   // régime non haussier / inconnu
        const double force = (closes[a] - sma) / sma;
        if (best < 0 || force > bestForce) { best = static_cast<int>(a); bestForce = force; }
    }
    return best;
}

class RotationBacktester {
public:
    RotationBacktester(RotationConfig cfg, std::vector<std::string> csvPaths)
        : cfg_(std::move(cfg)), csvPaths_(std::move(csvPaths)) {
        std::vector<std::vector<Bar>> series;
        series.reserve(csvPaths_.size());
        for (const auto& p : csvPaths_)
            series.push_back(CsvDataFeed(p).allBars());
        axis_ = alignOnCommonDates(series);
    }

    // Seam de test : construit un moteur sur un axe déjà aligné (données
    // synthétiques en mémoire, sans CSV). Utile aussi si l'axe est déjà connu.
    static RotationBacktester fromAxis(RotationConfig cfg, AlignedAxis axis) {
        RotationBacktester rb(std::move(cfg), std::vector<std::string>{});
        rb.axis_ = std::move(axis);
        return rb;
    }

    RotationResult run() { return runRange(0, axis_.size()); }

    // Rotation sur la sous-fenêtre [startIdx, endIdx) de l'axe commun. Le SMA est
    // RE-CALCULÉ sur les seules closes de la sous-fenêtre (analogue floorIdx).
    RotationResult runRange(size_t startIdx, size_t endIdx) const {
        return compute_(startIdx, endIdx);
    }

    const AlignedAxis& axis() const { return axis_; }

private:
    RotationConfig           cfg_;
    std::vector<std::string> csvPaths_;
    AlignedAxis              axis_;

    // Coût par côté (fraction) : commission + (slippage + demi-spread) en bps.
    static double perSideCost_(const RotationConfig& c) {
        return c.commissionPct + (c.slippageBps + c.halfSpreadBps) / 10'000.0;
    }

    // « YYYY-MM-DD » → jours depuis l'époque civile (days_from_civil, Hinnant) ;
    // -1 si le format est invalide. Dupliqué ici (helper pur, déjà dupliqué dans
    // main_validate.cpp et les tests) plutôt que d'exposer un interne du
    // Backtester (daysSinceEpoch_ est privé — additif oblige).
    static long rotDaysFromCivil_(const std::string& date) {
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

    RotationResult compute_(size_t startIdx, size_t endIdx) const {
        RotationResult r;
        r.initialCapital = cfg_.initialCapital;
        r.finalValue     = cfg_.initialCapital;
        r.totalReturnPct = 0.0;

        const size_t n = axis_.size();
        if (endIdx > n) endIdx = n;
        if (startIdx >= endIdx) return r;
        const size_t A = axis_.assets();
        const size_t M = endIdx - startIdx;

        // Sous-séries de clôtures + SMA ré-amorcé par fenêtre
        std::vector<std::vector<double>> sub(A), sma(A);
        for (size_t a = 0; a < A; ++a) {
            sub[a].assign(axis_.close[a].begin() + static_cast<long>(startIdx),
                          axis_.close[a].begin() + static_cast<long>(endIdx));
            sma[a] = SMA(cfg_.smaPeriod).compute(sub[a]);   // {} si M < smaPeriod
        }

        const double c = perSideCost_(cfg_);
        double eq       = cfg_.initialCapital;
        int    held     = -1;    // -1 = CASH
        size_t entryLoc = 0;
        size_t invested = 0;

        r.equityCurve.reserve(M);
        r.equityDates.reserve(M);

        auto closeStint = [&](int asset, size_t exitLoc, const char* reason) {
            const double gross = sub[asset][entryLoc] > 0.0
                ? sub[asset][exitLoc] / sub[asset][entryLoc] : 1.0;
            const double net = gross * std::pow(1.0 - c, 2.0);   // 1 côté entrée + 1 côté sortie
            TradeRecord t;
            t.buyDate    = axis_.dates[startIdx + entryLoc];
            t.sellDate   = axis_.dates[startIdx + exitLoc];
            t.buyPrice   = sub[asset][entryLoc];
            t.sellPrice  = sub[asset][exitLoc];
            t.shares     = 0;
            t.pnlPct     = (net - 1.0) * 100.0;
            t.pnl        = cfg_.initialCapital * (net - 1.0);   // indicatif (pleine allocation)
            t.holdDays   = static_cast<int>(exitLoc - entryLoc);
            t.exitReason = reason;
            t.isWin      = t.pnlPct > 0.0;
            t.deployedFraction = 1.0;   // rotation = plein investissement quand détenu
            r.trades.push_back(std::move(t));
        };

        for (size_t i = 0; i < M; ++i) {
            // Valorisation au marché AVANT toute décision (comme equitySnapshot)
            r.equityCurve.push_back(eq);
            r.equityDates.push_back(axis_.dates[startIdx + i]);
            if (i + 1 >= M) break;   // dernière barre : aucune séance suivante

            // Décision au close[i] : classe et choisit la cible
            std::vector<double> closesAtI(A), smasAtI(A);
            for (size_t a = 0; a < A; ++a) {
                closesAtI[a] = sub[a][i];
                smasAtI[a]   = sma[a].empty() ? 0.0 : sma[a][i];
            }
            const int target = rankStrongest(closesAtI, smasAtI);

            if (target != held) {
                ++r.switchCount;
                const int sides = (held != -1 ? 1 : 0) + (target != -1 ? 1 : 0);
                eq *= std::pow(1.0 - c, static_cast<double>(sides));
                if (held != -1) closeStint(held, i, "rotation");   // clôt le stint sortant
                if (target != -1) entryLoc = i;                    // ouvre le nouveau stint
            }
            held = target;

            if (held != -1) {
                const double ratio = sub[held][i] > 0.0 ? sub[held][i + 1] / sub[held][i] : 1.0;
                eq *= ratio;         // pas de look-ahead : rendement gagné de i à i+1
                ++invested;
            }
        }

        // Liquidation forcée du stint ouvert au dernier close (comme closeOpenPosition)
        if (held != -1) {
            eq *= (1.0 - c);                       // côté sortie
            closeStint(held, M - 1, "fin");
        }

        r.finalValue     = eq;
        r.totalReturnPct = (eq - cfg_.initialCapital) / cfg_.initialCapital * 100.0;

        // Références B&H sur la fenêtre : depuis la 1re barre décidable (fin du
        // warmup SMA). Meilleur mono-actif (B&L) et panier équipondéré statique.
        const size_t w = static_cast<size_t>(cfg_.smaPeriod) > 0
            ? static_cast<size_t>(cfg_.smaPeriod) - 1 : 0;
        if (M > w + 1) {
            double best = -std::numeric_limits<double>::infinity();
            double sumRatio = 0.0;
            for (size_t a = 0; a < A; ++a) {
                const double ratio = sub[a][w] > 0.0 ? sub[a][M - 1] / sub[a][w] : 1.0;
                best = std::max(best, (ratio - 1.0) * 100.0);
                sumRatio += ratio;
            }
            r.bestBuyHoldPct  = best;
            r.basketReturnPct = (sumRatio / static_cast<double>(A) - 1.0) * 100.0;
        }
        r.alphaVsBest   = r.totalReturnPct - r.bestBuyHoldPct;
        r.alphaVsBasket = r.totalReturnPct - r.basketReturnPct;

        // Max drawdown sur la courbe d'équité (marks pré-liquidation, comme le Backtester)
        double peak = 0.0;
        for (double v : r.equityCurve) {
            peak = std::max(peak, v);
            const double dd = peak > 0.0 ? (peak - v) / peak * 100.0 : 0.0;
            r.maxDrawdownPct = std::max(r.maxDrawdownPct, dd);
        }

        // Sharpe / Sortino : rendements journaliers à partir de la fin du warmup
        // (avant, l'équité est plate — tout cash — et ne ferait qu'ajouter des zéros)
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

        // CAGR (durée réelle via les dates d'équité), Calmar
        double years = 0.0;
        if (r.equityDates.size() >= 2) {
            const long d0 = rotDaysFromCivil_(r.equityDates.front());
            const long d1 = rotDaysFromCivil_(r.equityDates.back());
            if (d0 >= 0 && d1 > d0) years = static_cast<double>(d1 - d0) / 365.25;
        }
        if (years <= 0.0 && r.equityCurve.size() > 1)
            years = static_cast<double>(r.equityCurve.size()) / 252.0;
        if (years > 0.0 && cfg_.initialCapital > 0.0 && eq > 0.0)
            r.cagrPct = (std::pow(eq / cfg_.initialCapital, 1.0 / years) - 1.0) * 100.0;
        r.calmarRatio = r.maxDrawdownPct > 0.0 ? r.cagrPct / r.maxDrawdownPct
                                               : (r.cagrPct > 0.0 ? 999.0 : 0.0);

        // % du temps investi (mesure du cash drag T4 que la rotation vise à réduire)
        if (M > 1)
            r.pctTimeInvested = std::min(100.0,
                100.0 * static_cast<double>(invested) / static_cast<double>(M - 1));

        return r;
    }
};

// ─── RotationWalkForward ──────────────────────────────────────────────────────
// Pave l'axe commun en fenêtres IS/OOS contiguës (même mécanique que
// WalkForward). La rotation n'AJUSTE aucun paramètre → l'IS est purement
// informatif ; les 3 pavages (fin/canonique/décalé) testent la ROBUSTESSE du
// verdict OOS sur des sous-périodes différentes (dot-com/2008 inclus).
struct RotWindow {
    size_t isStart = 0, isEnd = 0, oosStart = 0, oosEnd = 0;
    RotationResult is;
    RotationResult oos;
};

class RotationWalkForward {
public:
    RotationWalkForward(RotationConfig cfg, std::vector<std::string> csvPaths,
                        size_t isBars, size_t oosBars, size_t step = 0,
                        size_t offset = 0)
        : rbt_(std::move(cfg), std::move(csvPaths))
        , isBars_(isBars), oosBars_(oosBars)
        , step_(step == 0 ? oosBars : step), offset_(offset) {}

    std::vector<RotWindow> run() const {
        std::vector<RotWindow> out;
        const size_t n    = rbt_.axis().size();
        const size_t span = isBars_ + oosBars_;
        if (span == 0 || n < span) return out;
        for (size_t s = offset_; s + span <= n; s += step_) {
            RotWindow w;
            w.isStart  = s;           w.isEnd  = s + isBars_;
            w.oosStart = w.isEnd;     w.oosEnd = w.isEnd + oosBars_;
            w.is  = rbt_.runRange(w.isStart,  w.isEnd);
            w.oos = rbt_.runRange(w.oosStart, w.oosEnd);
            out.push_back(std::move(w));
        }
        return out;
    }

    const AlignedAxis& axis() const { return rbt_.axis(); }

private:
    RotationBacktester rbt_;
    size_t isBars_, oosBars_, step_, offset_;
};

} // namespace trading
