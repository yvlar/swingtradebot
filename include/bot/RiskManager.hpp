#pragma once
#include "core/Interfaces.hpp"
#include <cmath>
#include <algorithm>

namespace trading {

// ─── RiskManager ──────────────────────────────────────────────────────────────
// Implémentation concrète de IRiskManager
// Formule de sizing: (Capital × RiskPct) / (Prix × StopLossPct)
class RiskManager final : public IRiskManager {
public:
    explicit RiskManager(double maxCapitalUsagePct = 0.95)
        : maxCapitalUsagePct_(maxCapitalUsagePct) {}

    // Calcule le nombre d'actions basé sur le risque dollar
    // Exemple: capital=$10,000, risk=2%, prix=$420, stop=5%
    //   → risque dollar = $200
    //   → risque par action = $21
    //   → shares = 200/21 = 9 actions (max perte = $189 si stop déclenché)
    int positionSize(
        double capital,
        double price,
        double stopLossPct,
        double riskPerTradePct
    ) const override {
        if (capital <= 0 || price <= 0 || stopLossPct <= 0 || riskPerTradePct <= 0)
            return 0;

        double dollarRisk   = capital * riskPerTradePct;
        double riskPerShare = price   * stopLossPct;
        int    shares       = static_cast<int>(dollarRisk / riskPerShare);

        // Ne pas dépasser le capital disponible
        double maxShares = (capital * maxCapitalUsagePct_) / price;
        shares = std::min(shares, static_cast<int>(maxShares));

        return std::max(1, shares);
    }

    // Vérifie qu'on n'est pas déjà en position et qu'on a assez de cash
    bool isTradeAllowed(
        const Account&                 account,
        const std::optional<Position>& currentPosition
    ) const override {
        if (currentPosition.has_value()) return false;  // déjà en position
        if (account.cash < 100.0)        return false;  // pas assez de cash
        if (account.status != "ACTIVE")  return false;  // compte inactif
        return true;
    }

    // Vérifie les conditions de sortie dans cet ordre de priorité:
    // 1. Stop-loss strict
    // 2. Take-profit
    // 3. Trailing stop (seulement après minHoldDays)
    std::optional<std::string> checkExitConditions(
        double currentPrice,
        double buyPrice,
        int    holdDays,
        double peakPrice,
        double stopLossPct,
        double takeProfitPct,
        double trailingStopPct,
        int    minHoldDays
    ) const override {
        if (buyPrice <= 0) return std::nullopt;

        double pnlPct = (currentPrice - buyPrice) / buyPrice;

        // 1. Stop-loss
        if (pnlPct <= -stopLossPct)
            return "stop-loss (" + formatPct(pnlPct) + ")";

        // 2. Take-profit
        if (pnlPct >= takeProfitPct)
            return "take-profit (" + formatPct(pnlPct) + ")";

        // 3. Trailing stop (seulement après minHoldDays)
        if (holdDays >= minHoldDays && peakPrice > 0) {
            double drawdown = (currentPrice - peakPrice) / peakPrice;
            if (drawdown <= -trailingStopPct)
                return "trailing-stop (" + formatPct(drawdown) + " depuis le pic)";
        }

        return std::nullopt;
    }

private:
    double maxCapitalUsagePct_;

    static std::string formatPct(double pct) {
        std::string s = std::to_string(std::round(pct * 1000) / 10);
        return s.substr(0, s.find('.') + 2) + "%";
    }
};

} // namespace trading
