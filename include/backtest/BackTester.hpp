#pragma once
#include "brokers/CsvDataFeed.hpp"
#include "brokers/PaperBroker.hpp"
#include "bot/Logger.hpp"
#include "strategies/SwingStrategy.hpp"
#include "bot/RiskManager.hpp"
#include "bot/TradingBot.hpp"
#include <memory>
#include <numeric>
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <sstream>

namespace trading {

// ─── ReplayDataFeed ───────────────────────────────────────────────────────────
// Rejoue un CsvDataFeed barre par barre pour le Backtester : le curseur fixe
// la « dernière barre connue » et getBars sert la fenêtre glissante que verrait
// le bot en production. La fenêtre est bornée par `lookback` (emaSlow + 30,
// l'historique du backtest d'origine) pour que les EMA — seedées par SMA en
// début de fenêtre — restent identiques aux valeurs golden (item 17).
//
// `floorIdx` (Sprint 7, item 7.1) : index minimal servi. Par défaut 0 (aucun
// effet, golden inchangé). En walk-forward, on le fixe au début de la fenêtre
// pour que les indicateurs NE voient JAMAIS de barre antérieure à la fenêtre —
// chaque sous-période IS/OOS ré-amorce ses EMA/RSI (isolation honnête, pas de
// fuite d'historique d'une fenêtre à l'autre).
class ReplayDataFeed final : public IDataFeed {
public:
    ReplayDataFeed(std::shared_ptr<CsvDataFeed> csv, int lookback, size_t floorIdx = 0)
        : csv_(std::move(csv)), lookback_(lookback), floorIdx_(floorIdx) {}

    void setCursor(size_t index) { cursor_ = index; }

    Result<std::vector<Bar>> getBars(const std::string& /*symbol*/, int days) override {
        int window = std::min(days, lookback_);
        // Ne jamais remonter avant floorIdx_ : getBarsUpTo(cursor, L) sert
        // [cursor+1-L, cursor+1) ; borner L à cursor+1-floorIdx_ cale le début
        // de la fenêtre sur floorIdx_. À floorIdx_ = 0 c'est l'ancien calcul.
        const int maxFromFloor = static_cast<int>(cursor_ + 1 - floorIdx_);
        window = std::min(window, maxFromFloor);
        return Result<std::vector<Bar>>::Ok(
            csv_->getBarsUpTo(cursor_, window));
    }

    // En backtest, le marché est toujours « ouvert »
    bool isMarketOpen() override { return true; }

private:
    std::shared_ptr<CsvDataFeed> csv_;
    size_t cursor_   = 0;
    int    lookback_;
    size_t floorIdx_ = 0;
};

// ─── BacktestResult ───────────────────────────────────────────────────────────
struct BacktestResult {
    double initialCapital   = 0.0;
    double finalValue       = 0.0;
    double totalReturnPct   = 0.0;
    double buyHoldReturnPct = 0.0;
    double alpha            = 0.0;
    double maxDrawdownPct   = 0.0;
    double sharpeRatio      = 0.0;
    double volatilityPct    = 0.0;
    // ── Métriques d'objectif (Sprint 6.4, D23) : « faire de l'argent » =
    // alpha net vs Buy & Hold à drawdown maîtrisé, pas « retour positif »
    double cagrPct          = 0.0;   // croissance annualisée composée
    double sortinoRatio     = 0.0;   // Sharpe pénalisant la seule volatilité basse
    double calmarRatio      = 0.0;   // CAGR / max drawdown
    double pctTimeInvested  = 0.0;   // % des barres post-warmup passées en position
    bool   beatsBuyHold     = false; // verdict : bat le Buy & Hold net de coûts ?
    int    totalTrades      = 0;
    int    winningTrades    = 0;
    int    losingTrades     = 0;
    double winRate          = 0.0;
    double avgWinPct        = 0.0;
    double avgLossPct       = 0.0;
    double profitFactor     = 0.0;
    double expectancy       = 0.0;
    double avgHoldDays      = 0.0;
    int    stopLossCount    = 0;
    int    takeProfitCount  = 0;
    int    trailingCount    = 0;
    int    signalCount      = 0;
    std::vector<double>      equityCurve;
    std::vector<std::string> equityDates;
    std::vector<TradeRecord> trades;
};

// ─── Backtester ───────────────────────────────────────────────────────────────
class Backtester {
public:
    // Coûts par défaut (Sprint 6.2, D22) : QQQ est très liquide (spread
    // ≈ 1 cent sur plusieurs centaines de dollars), mais un ordre au marché
    // paie toujours slippage + demi-spread. Défauts volontairement
    // CONSERVATEURS : 2 bps de slippage + 0,5 bp de demi-spread par côté —
    // un backtest doit sous-estimer l'edge, pas le flatter. Mettre 0/0 pour
    // retrouver l'ancien comportement « fill au close ».
    Backtester(
        SwingConfig        config,
        const std::string& csvPath,
        double             initialCapital = 10'000.0,
        double             commissionPct  = 0.001,
        double             slippageBps    = 2.0,
        double             halfSpreadBps  = 0.5
    )
        : config_(std::move(config))
        , csvPath_(csvPath)
        , initialCapital_(initialCapital)
        , commissionPct_(commissionPct)
        , slippageBps_(slippageBps)
        , halfSpreadBps_(halfSpreadBps)
    {}

    // ── Exécution ─────────────────────────────────────────────────────────────
    // Le backtest fait tourner le VRAI code de production (item 11) :
    // TradingBot::runOnce + PaperBroker + RiskManager. Plus aucune logique de
    // sortie/sizing dupliquée ici — le rapport reflète exactement le moteur.
    // run() backteste tout le CSV ; il délègue à runRange (item 7.1) qui sait
    // restreindre l'exécution à une sous-fenêtre [startIdx, endIdx). Avec
    // startIdx = 0 et endIdx = N, runRange reproduit run() au centime (golden).
    BacktestResult run() {
        auto csv = std::make_shared<CsvDataFeed>(csvPath_);
        return runRange(csv, 0, csv->allBars().size());
    }

    // Backteste la sous-fenêtre [startIdx, endIdx) du CSV déjà chargé. Le warmup
    // est LOCAL à la fenêtre (les `warmup` premières barres ne tradent pas), et
    // le feed est plafonné par floorIdx = startIdx → les indicateurs ne voient
    // aucune barre antérieure à la fenêtre (ré-amorçage par fenêtre, item 7.1).
    // Les métriques (B&H, CAGR, drawdown) sont calculées sur la SEULE fenêtre.
    BacktestResult runRange(std::shared_ptr<CsvDataFeed> csv,
                            size_t startIdx, size_t endIdx) {
        const auto& allBars = csv->allBars();
        if (endIdx > allBars.size()) endIdx = allBars.size();
        const int warmup = config_.emaSlow + config_.rsiPeriod + 2;

        auto feed   = std::make_shared<ReplayDataFeed>(csv, config_.emaSlow + 30,
                                                       startIdx);
        auto broker = std::make_shared<PaperBroker>(initialCapital_, commissionPct_,
                                                    slippageBps_, halfSpreadBps_);
        auto logger = std::make_shared<NullLogger>();

        // Une seule instance de stratégie pour tout le backtest (D6 : elle était
        // recréée à chaque barre) — evaluate() est sans état, même résultat
        std::shared_ptr<IStrategy> strategy = SwingStrategy::create(config_);

        TradingBot bot(feed, broker, strategy,
                       std::make_shared<RiskManager>(), logger);
        bot.setConfig(config_);
        // La raison de sortie du bot alimente le TradeRecord du broker simulé
        bot.setExitObserver([&broker](const std::string& reason) {
            broker->setLastExitReason(reason);
        });

        for (size_t i = startIdx; i < endIdx; ++i) {
            const Bar& bar = allBars[i];
            broker->setCurrentPrice(bar.close);
            broker->setCurrentDate(bar.date);

            // Valeur du portefeuille ce jour (avant les trades de la barre)
            broker->equitySnapshot(broker->portfolioValue(), bar.date);

            // Warmup local : on n'ouvre aucune position sur les `warmup`
            // premières barres DE LA FENÊTRE (i < startIdx + warmup)
            if (i < startIdx + static_cast<size_t>(warmup)) continue;

            broker->incrementHoldDays();
            feed->setCursor(i);
            bot.runOnce();
        }

        // Clôture de la position ouverte en fin de backtest
        broker->closeOpenPosition();

        // Métriques calculées sur la sous-série [startIdx, endIdx) : le warmup
        // local devient l'index de warmup relatif au sous-vecteur.
        std::vector<Bar> subBars(allBars.begin() + startIdx,
                                 allBars.begin() + endIdx);
        return computeMetrics(broker->cash(), subBars,
                              broker->equityCurve(), broker->equityDates(),
                              broker->trades(), warmup);
    }

    // ── Rapport console ───────────────────────────────────────────────────────
    void printReport(const BacktestResult& r) const {
        auto lineEq   = []() { std::cout << std::string(58, '=') << "\n"; };
        auto lineDash = []() { std::cout << std::string(58, '-') << "\n"; };

        auto pct = [](double v) {
            std::ostringstream s;
            s << std::fixed << std::setprecision(2) << (v >= 0 ? "+" : "") << v << "%";
            return s.str();
        };
        auto usd = [](double v) {
            std::ostringstream s;
            s << std::fixed << std::setprecision(2) << (v >= 0 ? "+$" : "-$") << std::abs(v);
            return s.str();
        };
        auto row = [](const std::string& label, const std::string& value) {
            std::cout << "  " << std::left << std::setw(36) << label << value << "\n";
        };

        std::cout << "\n";
        lineEq();
        std::cout << "  BACKTEST — " << config_.symbol << "\n";
        std::cout << "  EMA " << config_.emaFast << "/" << config_.emaSlow
                  << "  RSI " << config_.rsiPeriod
                  << "  SL "  << config_.stopLossPct  * 100 << "%"
                  << "  TP "  << config_.takeProfitPct * 100 << "%\n";
        lineEq();

        std::cout << "\n  PERFORMANCE GLOBALE\n";
        lineDash();
        row("Capital initial",    "$" + fmt(r.initialCapital));
        row("Capital final",      "$" + fmt(r.finalValue));
        row("Retour total",       pct(r.totalReturnPct));
        row("Buy & Hold QQQ",     pct(r.buyHoldReturnPct));
        row("Alpha (vs B&H)",     pct(r.alpha));
        row("Max Drawdown",       pct(-r.maxDrawdownPct));
        row("Sharpe Ratio",       fmtD(r.sharpeRatio, 3));
        row("Volatilite (ann.)",  pct(r.volatilityPct));

        // L'objectif est explicite (D23) : battre le Buy & Hold net de coûts,
        // pas seulement finir positif
        std::cout << "\n  OBJECTIF — BAT-ON LE BUY & HOLD ?\n";
        lineDash();
        row("CAGR",               pct(r.cagrPct));
        row("Sortino",            fmtD(r.sortinoRatio, 3));
        row("Calmar",             fmtD(r.calmarRatio, 3));
        row("Temps investi",      pct(r.pctTimeInvested));
        row("Verdict",            r.beatsBuyHold
                                      ? "BAT le Buy & Hold (net de couts)"
                                      : "NE BAT PAS le Buy & Hold");

        std::cout << "\n  STATISTIQUES DES TRADES\n";
        lineDash();
        row("Trades total",       std::to_string(r.totalTrades));
        row("Gagnants",           std::to_string(r.winningTrades) + " (" + pct(r.winRate) + ")");
        row("Perdants",           std::to_string(r.losingTrades));
        row("Gain moyen",         pct(r.avgWinPct));
        row("Perte moyenne",      pct(r.avgLossPct));
        row("Profit Factor",      fmtD(r.profitFactor, 2));
        row("Esperance/trade",    usd(r.expectancy));
        row("Duree moy. trade",   fmtD(r.avgHoldDays, 1) + " jours");

        std::cout << "\n  RAISONS DE SORTIE\n";
        lineDash();
        row("Stop-Loss",          std::to_string(r.stopLossCount));
        row("Take-Profit",        std::to_string(r.takeProfitCount));
        row("Trailing Stop",      std::to_string(r.trailingCount));
        row("Signal (EMA/RSI)",   std::to_string(r.signalCount));

        std::cout << "\n  HISTORIQUE DES TRADES\n";
        lineDash();
        std::cout << "  " << std::left
                  << std::setw(12) << "Achat"
                  << std::setw(12) << "Vente"
                  << std::setw(7)  << "Jours"
                  << std::setw(10) << "P&L %"
                  << std::setw(10) << "P&L $"
                  << "Raison\n";
        lineDash();

        int start = std::max(0, (int)r.trades.size() - 10);
        for (int i = start; i < (int)r.trades.size(); ++i) {
            const auto& t = r.trades[i];
            std::string buyD  = t.buyDate.size()  >= 10 ? t.buyDate.substr(0,10)  : t.buyDate;
            std::string sellD = t.sellDate.size() >= 10 ? t.sellDate.substr(0,10) : t.sellDate;
            std::cout << "  "
                      << std::left << std::setw(12) << buyD
                      << std::setw(12) << sellD
                      << std::setw(7)  << t.holdDays
                      << std::setw(10) << pct(t.pnlPct)
                      << std::setw(10) << usd(t.pnl)
                      << t.exitReason << "\n";
        }
        lineEq();
        std::cout << "\n";
    }

private:
    SwingConfig config_;
    std::string csvPath_;
    double      initialCapital_;
    double      commissionPct_;
    double      slippageBps_;
    double      halfSpreadBps_;

public:
    // Public pour les tests unitaires des métriques : permet de vérifier
    // chaque formule (drawdown, Sharpe, profit factor…) sur des données
    // synthétiques, sans dépendre du backtest golden complet
    BacktestResult computeMetrics(
        double                          finalCash,
        const std::vector<Bar>&         allBars,
        const std::vector<double>&      equityCurve,
        const std::vector<std::string>& equityDates,
        const std::vector<TradeRecord>& trades,
        int                             warmup
    ) const {
        BacktestResult r;
        r.initialCapital = initialCapital_;
        r.finalValue     = finalCash;
        r.totalReturnPct = (finalCash - initialCapital_) / initialCapital_ * 100.0;

        // Buy & Hold depuis la fin du warmup
        if (static_cast<int>(allBars.size()) > warmup)
            r.buyHoldReturnPct = (allBars.back().close - allBars[warmup].close)
                                  / allBars[warmup].close * 100.0;

        r.alpha       = r.totalReturnPct - r.buyHoldReturnPct;
        r.equityCurve = equityCurve;
        r.equityDates = equityDates;
        r.trades      = trades;
        r.totalTrades = static_cast<int>(trades.size());

        // Max Drawdown
        double peak = 0;
        for (double v : equityCurve) {
            peak = std::max(peak, v);
            double dd = peak > 0 ? (peak - v) / peak * 100.0 : 0;
            r.maxDrawdownPct = std::max(r.maxDrawdownPct, dd);
        }

        // Sharpe + Volatilité + Sortino (rendements journaliers annualisés)
        if (equityCurve.size() > warmup + 1) {
            std::vector<double> rets;
            for (size_t i = warmup + 1; i < equityCurve.size(); ++i)
                if (equityCurve[i-1] > 0)
                    rets.push_back((equityCurve[i] - equityCurve[i-1]) / equityCurve[i-1]);

            if (!rets.empty()) {
                double mean = 0;
                for (double x : rets) mean += x;
                mean /= rets.size();

                double var = 0;
                for (double x : rets) var += (x - mean) * (x - mean);
                var /= rets.size();
                double stddev = std::sqrt(var);

                r.volatilityPct = stddev * std::sqrt(252.0) * 100.0;
                r.sharpeRatio   = stddev > 0
                    ? (mean * 252.0) / (stddev * std::sqrt(252.0))
                    : 0.0;

                // Sortino (D23) : seule la volatilité BASSE est un risque.
                // Déviation basse = √(Σ min(0, ret)² / N), cible 0, N = tous
                // les rendements. Aucun rendement négatif → sentinelle 999
                // (convention du profit factor) si la moyenne est positive.
                double downVar = 0;
                for (double x : rets) if (x < 0) downVar += x * x;
                downVar /= rets.size();
                const double downDev = std::sqrt(downVar);
                r.sortinoRatio = downDev > 0
                    ? (mean * 252.0) / (downDev * std::sqrt(252.0))
                    : (mean > 0 ? 999.0 : 0.0);
            }
        }

        // ── CAGR (D23) : durée réelle via les dates d'équité si possible,
        // sinon repli « N barres ≈ N/252 années »
        double years = 0.0;
        if (equityDates.size() >= 2) {
            const long d0 = daysSinceEpoch_(equityDates.front());
            const long d1 = daysSinceEpoch_(equityDates.back());
            if (d0 >= 0 && d1 > d0) years = (d1 - d0) / 365.25;
        }
        if (years <= 0.0 && equityCurve.size() > 1)
            years = static_cast<double>(equityCurve.size()) / 252.0;
        if (years > 0.0 && initialCapital_ > 0 && finalCash > 0)
            r.cagrPct = (std::pow(finalCash / initialCapital_, 1.0 / years) - 1.0)
                        * 100.0;

        // Calmar (D23) : rendement annualisé par unité de drawdown subi
        r.calmarRatio = r.maxDrawdownPct > 0
            ? r.cagrPct / r.maxDrawdownPct
            : (r.cagrPct > 0 ? 999.0 : 0.0);

        // Verdict explicite (D23) : le retour de la stratégie est net des
        // commissions (PaperBroker) — « faire de l'argent » = battre le B&H
        r.beatsBuyHold = r.totalReturnPct > r.buyHoldReturnPct;

        // Stats trades
        double totalWinPnl = 0, totalLossPnl = 0;
        double totalWinPct = 0, totalLossPct = 0;
        int    totalHold   = 0;

        for (const auto& t : trades) {
            if (t.isWin) {
                r.winningTrades++;
                totalWinPnl += t.pnl;
                totalWinPct += t.pnlPct;
            } else {
                r.losingTrades++;
                totalLossPnl += std::abs(t.pnl);
                totalLossPct += t.pnlPct;
            }
            totalHold += t.holdDays;

            if (t.exitReason.find("stop-loss")   != std::string::npos) r.stopLossCount++;
            else if (t.exitReason.find("take-profit") != std::string::npos) r.takeProfitCount++;
            else if (t.exitReason.find("trailing") != std::string::npos) r.trailingCount++;
            else r.signalCount++;
        }

        if (r.totalTrades > 0) {
            r.winRate    = (double)r.winningTrades / r.totalTrades * 100.0;
            r.avgHoldDays = (double)totalHold / r.totalTrades;
            r.expectancy  = (totalWinPnl - totalLossPnl) / r.totalTrades;
        }
        if (r.winningTrades > 0) r.avgWinPct  = totalWinPct  / r.winningTrades;
        if (r.losingTrades  > 0) r.avgLossPct = totalLossPct / r.losingTrades;
        r.profitFactor = totalLossPnl > 0 ? totalWinPnl / totalLossPnl : 999.0;

        // % du temps investi (D23) : jours détenus / barres post-warmup —
        // mesure le cash drag (T4 : hors position l'essentiel du temps)
        const size_t barsPostWarmup =
            equityCurve.size() > static_cast<size_t>(warmup)
                ? equityCurve.size() - static_cast<size_t>(warmup) : 0;
        if (barsPostWarmup > 0)
            r.pctTimeInvested = std::min(100.0,
                100.0 * totalHold / static_cast<double>(barsPostWarmup));

        return r;
    }

private:
    // Convertit « YYYY-MM-DD » en jours depuis l'époque civile (algorithme
    // days_from_civil de Howard Hinnant) ; -1 si le format est invalide
    static long daysSinceEpoch_(const std::string& date) {
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

    static std::string fmt(double v) {
        std::ostringstream s;
        s << std::fixed << std::setprecision(2) << v;
        return s.str();
    }
    static std::string fmtD(double v, int p) {
        std::ostringstream s;
        s << std::fixed << std::setprecision(p) << v;
        return s.str();
    }
};

} // namespace trading