#pragma once
#include "core/Interfaces.hpp"
#include "indicators/DayIndicators.hpp"
#include <cmath>
#include <algorithm>
#include <sstream>

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

        // 0 est une réponse valide : cash insuffisant → on n'achète RIEN
        // (l'ancien max(1, shares) forçait un achat à découvert du budget de risque)
        return std::max(0, shares);
    }

    // Vérifie qu'on n'est pas déjà en position, que le compte est actif
    // et que le coût total de l'ordre tient dans le cash disponible
    bool isTradeAllowed(
        const Account&                 account,
        const std::optional<Position>& currentPosition,
        double                         price,
        int                            qty
    ) const override {
        if (currentPosition.has_value()) return false;  // déjà en position
        if (account.status != "ACTIVE")  return false;  // compte inactif
        if (qty <= 0 || price <= 0)      return false;  // ordre vide ou prix invalide
        if (account.cash < price * qty)  return false;  // coût total > cash
        return true;
    }

    // Surcharge historique (8 arguments) : trailing % pur — délègue à la
    // variante « barres » avec l'ATR désactivé (comportement inchangé).
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
        return checkExitConditions(currentPrice, buyPrice, holdDays, peakPrice,
                                   stopLossPct, takeProfitPct, trailingStopPct,
                                   minHoldDays, /*bars=*/{},
                                   /*trailingAtrMult=*/0.0);
    }

    // Vérifie les conditions de sortie dans cet ordre de priorité:
    // 1. Stop-loss strict
    // 2. Take-profit
    // 3. Trailing stop (seulement après minHoldDays) — deux modes (item 8q.1) :
    //    • trailingAtrMult > 0 : seuil peak − mult × ATR(14) (vrai true-range
    //      sur les barres de la fenêtre courante). Il REMPLACE le trailing %
    //      (décision utilisateur 2026-07-03 : pas de cumul — A/B propre).
    //      Repli DÉFENSIF sur le % si l'ATR est incalculable (fenêtre < 15
    //      barres → série vide) ou nul (barres plates) : une position ouverte
    //      ne reste JAMAIS sans filet.
    //      NB : comme le seed EMA du backtest, la valeur d'ATR dépend du début
    //      de fenêtre (seed SMA de Wilder) — déterministe, mais la fenêtre
    //      prod doit rester ≥ 15 barres (l'unification lookback prod/backtest
    //      reste D19/9.2).
    //    • sinon : trailing % historique (peak × (1 − trailingStopPct)).
    std::optional<std::string> checkExitConditions(
        double currentPrice,
        double buyPrice,
        int    holdDays,
        double peakPrice,
        double stopLossPct,
        double takeProfitPct,
        double trailingStopPct,
        int    minHoldDays,
        const std::vector<Bar>& bars,
        double trailingAtrMult
    ) const override {
        if (buyPrice <= 0) return std::nullopt;

        double pnlPct = (currentPrice - buyPrice) / buyPrice;

        // 1. Stop-loss
        if (pnlPct <= -stopLossPct)
            return "stop-loss (" + formatPct(pnlPct) + ")";

        // 2. Take-profit — takeProfitPct ≤ 0 = désactivé (item 8.2, D26) :
        // une stratégie de tendance gagne sur les queues, on ne plafonne pas
        // les gagnants ; la sortie est pilotée par le trailing/structure.
        // Même convention « seuil ≤ 0 = off » que le kill-switch.
        if (takeProfitPct > 0 && pnlPct >= takeProfitPct)
            return "take-profit (" + formatPct(pnlPct) + ")";

        // 3. Trailing stop (seulement après minHoldDays)
        if (holdDays >= minHoldDays && peakPrice > 0) {
            bool atrActif = false;
            if (trailingAtrMult > 0) {
                const auto serie = ATR(kAtrPeriode).computeBars(bars);
                const double atr = serie.empty() ? 0.0 : serie.back();
                if (atr > 0) {
                    atrActif = true;
                    const double seuil = peakPrice - trailingAtrMult * atr;
                    if (currentPrice <= seuil)
                        return "trailing-stop ATR (clôture " + formatPrix(currentPrice)
                             + " ≤ pic " + formatPrix(peakPrice) + " − "
                             + formatMult(trailingAtrMult) + "×ATR " + formatPrix(atr) + ")";
                }
            }
            if (!atrActif) {
                double drawdown = (currentPrice - peakPrice) / peakPrice;
                if (drawdown <= -trailingStopPct)
                    return "trailing-stop (" + formatPct(drawdown) + " depuis le pic)";
            }
        }

        return std::nullopt;
    }

    // Kill-switch (item 18) : coupe les ENTRÉES quand la séance dérape.
    // Évalué dans cet ordre (du plus structurel au plus conjoncturel) :
    //   1. Pertes consécutives (signal de stratégie en perdition)
    //   2. Plafond d'ordres journalier (emballement / boucle)
    //   3. Drawdown journalier (saignée d'équité sur la séance)
    // Un seuil ≤ 0 désactive le garde-fou correspondant.
    std::optional<std::string> checkKillSwitch(
        const KillSwitchConfig& cfg,
        double dayStartEquity,
        double currentEquity,
        int    consecutiveLosses,
        int    ordersToday
    ) const override {
        if (cfg.maxConsecutiveLosses > 0
            && consecutiveLosses >= cfg.maxConsecutiveLosses)
            return "pertes consécutives (" + std::to_string(consecutiveLosses)
                   + "/" + std::to_string(cfg.maxConsecutiveLosses) + ")";

        if (cfg.maxOrdersPerDay > 0 && ordersToday >= cfg.maxOrdersPerDay)
            return "plafond d'ordres journalier (" + std::to_string(ordersToday)
                   + "/" + std::to_string(cfg.maxOrdersPerDay) + ")";

        if (cfg.maxDailyDrawdownPct > 0 && dayStartEquity > 0) {
            double dd = (dayStartEquity - currentEquity) / dayStartEquity;
            if (dd >= cfg.maxDailyDrawdownPct)
                return "drawdown journalier (" + formatPct(-dd) + ")";
        }

        return std::nullopt;
    }

private:
    // Période d'ATR du trailing adaptatif (item 8q.1) : ATR(14), le standard
    // de Wilder — non configurable tant que l'edge du mécanisme n'est pas
    // démontré (seul le multiplicateur est un axe de recherche, 8q.2).
    static constexpr int kAtrPeriode = 14;

    double maxCapitalUsagePct_;

    static std::string formatPct(double pct) {
        std::string s = std::to_string(std::round(pct * 1000) / 10);
        return s.substr(0, s.find('.') + 2) + "%";
    }

    // Prix/ATR avec 2 décimales (les 6 décimales de std::to_string bruitent
    // les logs de sortie)
    static std::string formatPrix(double v) {
        std::ostringstream oss;
        oss.precision(2);
        oss << std::fixed << v;
        return oss.str();
    }

    // Multiplicateur ATR sans zéros de traîne (3 → « 3 », 2,5 → « 2.5 »)
    static std::string formatMult(double m) {
        std::string s = formatPrix(m);
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
        return s;
    }
};

} // namespace trading
