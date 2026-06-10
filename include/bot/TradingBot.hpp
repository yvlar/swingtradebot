#pragma once
#include "core/Interfaces.hpp"
#include "strategies/SwingStrategy.hpp"
#include "bot/RiskManager.hpp"
#include "bot/Logger.hpp"
#include <memory>
#include <thread>
#include <atomic>
#include <chrono>

namespace trading {

// ─── BotState — état persistant de la position ────────────────────────────────
struct BotState {
    bool   inPosition = false;
    double buyPrice   = 0.0;
    double peakPrice  = 0.0;
    int    holdDays   = 0;
};

// ─── TradingBot ───────────────────────────────────────────────────────────────
// Orchestrateur principal. Dépend UNIQUEMENT des interfaces (DIP).
// Les implémentations concrètes sont injectées via le constructeur.
class TradingBot {
public:
    // ── Injection de dépendances ──────────────────────────────────────────────
    TradingBot(
        std::shared_ptr<IDataFeed>    dataFeed,
        std::shared_ptr<IBroker>      broker,
        std::shared_ptr<IStrategy>    strategy,
        std::shared_ptr<IRiskManager> riskManager,
        std::shared_ptr<ILogger>      logger
    )
        : dataFeed_   (std::move(dataFeed))
        , broker_     (std::move(broker))
        , strategy_   (std::move(strategy))
        , riskManager_(std::move(riskManager))
        , logger_     (std::move(logger))
    {
        logger_->info("TradingBot initialisé │ Stratégie: " + strategy_->name());
    }

    // ── Cycle unique de trading (testable unitairement) ───────────────────────
    void runOnce() {
        if (!dataFeed_->isMarketOpen()) {
            logger_->info("Marché fermé — en attente");
            return;
        }

        // 1. Récupération des données
        auto bars = dataFeed_->getBars(swingCfg_.symbol, 60);
        if (bars.empty()) {
            logger_->warn("Aucune barre reçue");
            return;
        }

        // 2. Génération du signal
        Signal signal = strategy_->evaluate(bars);
        double price  = bars.back().close;

        logger_->info(
            bars.back().date + " │ $" + std::to_string(static_cast<int>(price)) +
            " │ " + signalStr(signal) + " │ " + signal.reason
        );

        // 3. Gestion de la position ouverte
        auto pos = broker_->getPosition(swingCfg_.symbol);
        if (state_.inPosition && pos.has_value()) {
            state_.holdDays++;
            if (price > state_.peakPrice) state_.peakPrice = price;

            auto exitReason = riskManager_->checkExitConditions(
                price, state_.buyPrice, state_.holdDays, state_.peakPrice,
                swingCfg_.stopLossPct, swingCfg_.takeProfitPct,
                swingCfg_.trailingStopPct, swingCfg_.minHoldDays
            );

            bool shouldSell = exitReason.has_value()
                || (signal.isSell() && state_.holdDays >= swingCfg_.minHoldDays);

            if (shouldSell) {
                std::string reason = exitReason.value_or("signal");
                auto order = broker_->submitSell(swingCfg_.symbol, pos->shares);
                // Seul un fill confirmé clôt la position côté bot.
                // PENDING : la position broker disparaîtra une fois l'ordre exécuté
                // (réconciliation au cycle suivant) ; REJECTED/échec : on conserve
                // l'état et on retentera — sinon la position devient orpheline.
                if (order.has_value() && order->status == OrderStatus::FILLED) {
                    double fillPrice = order->price    > 0 ? order->price    : price;
                    int    fillQty   = order->quantity > 0 ? order->quantity : pos->shares;
                    double pnl = (fillPrice - state_.buyPrice) * fillQty;
                    logger_->info("🔴 VENTE (" + reason + ") │ P&L: $" +
                                  std::to_string(static_cast<int>(pnl)));
                    state_ = BotState{};  // reset
                } else {
                    logger_->error("Ordre de vente non exécuté (" + orderStatusStr(order)
                                   + ") — position conservée, nouvel essai au prochain cycle");
                }
            }
        }

        // 4. Entrée en position
        else if (!state_.inPosition && signal.isBuy()) {
            auto account = broker_->getAccount();

            int shares = riskManager_->positionSize(
                account.cash, price,
                swingCfg_.stopLossPct,
                swingCfg_.riskPerTradePct
            );
            if (shares <= 0) {
                logger_->warn("Cash insuffisant pour ouvrir une position — aucun ordre émis");
                return;
            }
            if (!riskManager_->isTradeAllowed(account, pos, price, shares)) {
                logger_->warn("Trade non autorisé par le risk manager");
                return;
            }

            auto order = broker_->submitBuy(swingCfg_.symbol, shares);
            // Seul un fill confirmé ouvre la position côté bot, au prix RÉEL
            // d'exécution. PENDING : l'état ne bouge pas, la réconciliation avec
            // la position broker fera foi au cycle suivant.
            if (order.has_value() && order->status == OrderStatus::FILLED) {
                double fillPrice = order->price    > 0 ? order->price    : price;
                int    fillQty   = order->quantity > 0 ? order->quantity : shares;
                state_.inPosition = true;
                state_.buyPrice   = fillPrice;
                state_.peakPrice  = fillPrice;
                state_.holdDays   = 0;
                logger_->info("🟢 ACHAT │ " + std::to_string(fillQty) +
                              " parts × $" + std::to_string(static_cast<int>(fillPrice)));
            } else if (order.has_value() && order->status == OrderStatus::PENDING) {
                logger_->warn("Ordre d'achat en attente d'exécution — "
                              "réconciliation au prochain cycle");
            } else {
                logger_->error("Ordre d'achat non exécuté (" + orderStatusStr(order) + ")");
            }
        }
    }

    // ── Boucle principale (paper/live trading) ────────────────────────────────
    void run(int intervalSeconds = 86400) {
        running_ = true;
        logger_->info("Bot démarré │ Vérification toutes les " +
                      std::to_string(intervalSeconds) + "s");

        while (running_) {
            try {
                runOnce();
            } catch (const std::exception& e) {
                logger_->error("Exception: " + std::string(e.what()));
            }
            std::this_thread::sleep_for(std::chrono::seconds(intervalSeconds));
        }
    }

    void stop() {
        running_ = false;
        logger_->info("Bot arrêté.");
    }

    // ── Accesseurs pour les tests ─────────────────────────────────────────────
    const BotState&    state()    const { return state_; }
    const SwingConfig& config()   const { return swingCfg_; }
    void setState(const BotState& s)     { state_ = s; }
    void setConfig(const SwingConfig& c) { swingCfg_ = c; }

private:
    std::shared_ptr<IDataFeed>    dataFeed_;
    std::shared_ptr<IBroker>      broker_;
    std::shared_ptr<IStrategy>    strategy_;
    std::shared_ptr<IRiskManager> riskManager_;
    std::shared_ptr<ILogger>      logger_;

    BotState    state_;
    SwingConfig swingCfg_;
    std::atomic<bool> running_{false};

    static std::string signalStr(const Signal& s) {
        switch (s.type) {
            case SignalType::BUY:  return "🟢 BUY";
            case SignalType::SELL: return "🔴 SELL";
            default:               return "⚪ HOLD";
        }
    }

    static std::string orderStatusStr(const std::optional<Order>& o) {
        if (!o.has_value()) return "échec de soumission";
        switch (o->status) {
            case OrderStatus::FILLED:    return "exécuté";
            case OrderStatus::PENDING:   return "en attente";
            case OrderStatus::CANCELLED: return "annulé";
            case OrderStatus::REJECTED:  return "rejeté";
        }
        return "statut inconnu";
    }
};

} // namespace trading
