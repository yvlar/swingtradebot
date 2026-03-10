#pragma once
#include "brokers/CsvDataFeed.hpp"
#include "brokers/PaperBroker.hpp"
#include "bot/Logger.hpp"
#include "strategies/SwingStrategy.hpp"
#include "bot/RiskManager.hpp"
#include <numeric>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <sstream>

namespace trading {

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
    Backtester(
        SwingConfig        config,
        const std::string& csvPath,
        double             initialCapital = 10'000.0,
        double             commissionPct  = 0.001
    )
        : config_(std::move(config))
        , csvPath_(csvPath)
        , initialCapital_(initialCapital)
        , commissionPct_(commissionPct)
    {}

    // ── Exécution ─────────────────────────────────────────────────────────────
    BacktestResult run() {
        auto feed = std::make_shared<CsvDataFeed>(csvPath_);
        const auto& allBars = feed->allBars();
        const int warmup = config_.emaSlow + config_.rsiPeriod + 2;

        // État du portefeuille
        double cash       = initialCapital_;
        int    shares     = 0;
        double buyPrice   = 0.0;
        double peakPrice  = 0.0;
        int    holdDays   = 0;
        bool   inPos      = false;
        std::string buyDate;

        std::vector<TradeRecord>  trades;
        std::vector<double>       equityCurve;
        std::vector<std::string>  equityDates;

        RiskManager rm;

        for (size_t i = 0; i < allBars.size(); ++i) {
            const Bar& bar = allBars[i];
            double price   = bar.close;

            // Valeur du portefeuille ce jour
            equityCurve.push_back(cash + shares * price);
            equityDates.push_back(bar.date);

            if (static_cast<int>(i) < warmup) continue;

            // Mise à jour du pic si en position
            if (inPos) {
                if (price > peakPrice) peakPrice = price;
                holdDays++;
            }

            // ── Vérification des sorties ──────────────────────────────────────
            if (inPos && shares > 0) {
                double pnlPct    = (price - buyPrice) / buyPrice;
                double drawdown  = (price - peakPrice) / peakPrice;

                std::string exitReason;
                if (pnlPct <= -config_.stopLossPct)
                    exitReason = "stop-loss";
                else if (pnlPct >= config_.takeProfitPct)
                    exitReason = "take-profit";
                else if (holdDays >= config_.minHoldDays && drawdown <= -config_.trailingStopPct)
                    exitReason = "trailing-stop";

                // Signal de vente (EMA cross ou RSI)
                if (exitReason.empty() && holdDays >= config_.minHoldDays) {
                    auto window = feed->getBarsUpTo(i, config_.emaSlow + 30);
                    auto strat  = SwingStrategy::create(config_);
                    Signal sig  = strat->evaluate(window);
                    if (sig.isSell()) exitReason = "signal";
                }

                if (!exitReason.empty()) {
                    // ── Vente : calcul propre du cash ─────────────────────────
                    double proceeds = shares * price * (1.0 - commissionPct_);
                    cash += proceeds;

                    TradeRecord t;
                    t.buyDate    = buyDate;
                    t.sellDate   = bar.date;
                    t.buyPrice   = buyPrice;
                    t.sellPrice  = price;
                    t.shares     = shares;
                    t.pnl        = (price - buyPrice) * shares - (shares * price * commissionPct_);
                    t.pnlPct     = pnlPct * 100.0;
                    t.holdDays   = holdDays;
                    t.exitReason = exitReason;
                    t.isWin      = t.pnl > 0;
                    trades.push_back(t);

                    shares    = 0;
                    inPos     = false;
                    holdDays  = 0;
                    buyPrice  = 0.0;
                    peakPrice = 0.0;
                }
            }

            // ── Entrée en position ────────────────────────────────────────────
            if (!inPos && cash > price) {
                auto window = feed->getBarsUpTo(i, config_.emaSlow + 30);
                auto strat  = SwingStrategy::create(config_);
                Signal sig  = strat->evaluate(window);

                if (sig.isBuy()) {
                    // Calcul du sizing basé sur le risque
                    double riskDollar  = cash * config_.riskPerTradePct;
                    double riskPerSh   = price * config_.stopLossPct;
                    int    qty         = std::max(1, static_cast<int>(riskDollar / riskPerSh));

                    // Ne pas dépasser 95% du cash disponible
                    double maxQty = static_cast<int>((cash * 0.95) / (price * (1.0 + commissionPct_)));
                    qty = std::min(qty, static_cast<int>(maxQty));

                    if (qty > 0) {
                        double cost = qty * price * (1.0 + commissionPct_);
                        // ── Achat : débit du cash ─────────────────────────────
                        cash     -= cost;
                        shares    = qty;
                        buyPrice  = price;
                        peakPrice = price;
                        holdDays  = 0;
                        inPos     = true;
                        buyDate   = bar.date;
                    }
                }
            }
        }

        // ── Clôture de la position ouverte en fin de backtest ─────────────────
        if (inPos && shares > 0) {
            double price    = allBars.back().close;
            double proceeds = shares * price * (1.0 - commissionPct_);
            cash += proceeds;

            TradeRecord t;
            t.buyDate    = buyDate;
            t.sellDate   = allBars.back().date;
            t.buyPrice   = buyPrice;
            t.sellPrice  = price;
            t.shares     = shares;
            t.pnl        = (price - buyPrice) * shares;
            t.pnlPct     = (price - buyPrice) / buyPrice * 100.0;
            t.holdDays   = holdDays;
            t.exitReason = "fin-backtest";
            t.isWin      = t.pnl > 0;
            trades.push_back(t);
        }

        return computeMetrics(cash, allBars, equityCurve, equityDates, trades, warmup);
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

        // Sharpe + Volatilité (rendements journaliers annualisés)
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
            }
        }

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

        return r;
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