#pragma once
#include "core/Interfaces.hpp"
#include "strategies/SwingStrategy.hpp"
#include "bot/RiskManager.hpp"
#include "bot/Logger.hpp"
#include <memory>
#include <thread>
#include <atomic>
#include <chrono>
#include <algorithm>

namespace trading {

// ─── TradingBot ───────────────────────────────────────────────────────────────
// (BotState vit dans models/Models.hpp, persisté via IStateStore)
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
        std::shared_ptr<ILogger>      logger,
        std::shared_ptr<IStateStore>  stateStore = nullptr   // optionnel : persistance
    )
        : dataFeed_   (std::move(dataFeed))
        , broker_     (std::move(broker))
        , strategy_   (std::move(strategy))
        , riskManager_(std::move(riskManager))
        , logger_     (std::move(logger))
        , stateStore_ (std::move(stateStore))
    {
        logger_->info("TradingBot initialisé │ Stratégie: " + strategy_->name());
    }

    // ── Cycle unique de trading (testable unitairement) ───────────────────────
    void runOnce() {
        // Restauration de l'état persisté au premier cycle (après le setConfig
        // du composition root, pour charger le bon symbole)
        if (!stateLoaded_) {
            stateLoaded_ = true;
            if (stateStore_) {
                if (auto persisted = stateStore_->load(swingCfg_.symbol)) {
                    state_ = *persisted;
                    logger_->info("État restauré │ inPosition="
                                  + std::string(state_.inPosition ? "oui" : "non")
                                  + " │ buyPrice=$"
                                  + std::to_string(static_cast<int>(state_.buyPrice)));
                }
            }
        }

        if (!dataFeed_->isMarketOpen()) {
            logger_->info("Marché fermé — en attente");
            return;
        }

        // 1. Récupération des données
        // Panne du feed ≠ « pas de donnée » (item 10) : sur panne, on saute
        // le cycle sans toucher à l'état
        auto barsRes = dataFeed_->getBars(swingCfg_.symbol, 60);
        if (!barsRes.ok()) {
            logger_->error("Panne du data feed (" + barsRes.error()
                           + ") — cycle ignoré");
            return;
        }
        const auto& bars = barsRes.value();
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
        // Panne broker : la position est INCONNUE — ne SURTOUT pas réconcilier
        // (l'ancien nullopt ambigu réinitialisait l'état sur simple panne
        // réseau — item 10) ni trader à l'aveugle : cycle ignoré, état conservé
        auto posRes = broker_->getPosition(swingCfg_.symbol);
        if (!posRes.ok()) {
            logger_->error("Panne broker sur getPosition (" + posRes.error()
                           + ") — cycle ignoré, état conservé");
            return;
        }
        const auto& pos = posRes.value();

        // Réconciliation : la position broker fait foi (restart, ordre PENDING
        // exécuté entre deux cycles, position fermée à la main…) — uniquement
        // sur une réponse broker CERTAINE
        reconcilePosition_(pos, price, bars.back().date);

        if (state_.inPosition && pos.has_value()) {
            // holdDays = jours de bourse réels : +1 seulement quand la DATE de
            // la dernière barre change (la boucle prod tourne toutes les 60 min)
            bool dirty = false;
            const std::string& barDate = bars.back().date;
            if (barDate != state_.lastBarDate) {
                state_.holdDays++;
                state_.lastBarDate = barDate;
                dirty = true;
            }
            if (price > state_.peakPrice) { state_.peakPrice = price; dirty = true; }
            if (dirty) saveState_();

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
                    saveState_();
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
                state_.inPosition  = true;
                state_.buyPrice    = fillPrice;
                state_.peakPrice   = fillPrice;
                state_.holdDays    = 0;
                state_.lastBarDate = bars.back().date;  // jour d'entrée = jour 0
                saveState_();
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
    std::shared_ptr<IStateStore>  stateStore_;

    BotState    state_;
    SwingConfig swingCfg_;
    std::atomic<bool> running_{false};
    bool stateLoaded_ = false;

    // ── Réconciliation état interne ↔ position broker ─────────────────────────
    // La position broker fait foi : couvre le redémarrage (état perdu ou
    // restauré obsolète), l'ordre PENDING exécuté entre deux cycles, et la
    // position fermée hors du bot (découverte D3).
    void reconcilePosition_(const std::optional<Position>& pos,
                            double price,
                            const std::string& barDate) {
        if (state_.inPosition && !pos.has_value()) {
            logger_->warn("Position absente chez le broker (fermée hors bot ?) — "
                          "réinitialisation de l'état");
            state_ = BotState{};
            saveState_();
        } else if (!state_.inPosition && pos.has_value()) {
            double basis = pos->avgPrice > 0 ? pos->avgPrice : price;
            logger_->warn("Position broker non suivie (redémarrage ?) — adoption : "
                          + std::to_string(pos->shares) + " parts @ $"
                          + std::to_string(static_cast<int>(basis)));
            state_.inPosition  = true;
            state_.buyPrice    = basis;
            state_.peakPrice   = std::max(basis, price);
            state_.holdDays    = 0;   // conservateur : minHoldDays repart de zéro,
                                      // les stops (prioritaires) sont actifs immédiatement
            state_.lastBarDate = barDate;
            saveState_();
        }
    }

    void saveState_() {
        if (stateStore_ && !stateStore_->save(swingCfg_.symbol, state_))
            logger_->error("Échec de la persistance de l'état du bot");
    }

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
