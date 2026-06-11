#pragma once
#include "core/Interfaces.hpp"
#include <memory>
#include <thread>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <functional>

namespace trading {

// ─── TradeEvent — événement de trade émis par le moteur (item 21) ────────────
// Permet au composition root de persister les trades (DbLogger) et d'alimenter
// le dashboard SANS coupler TradingBot à ces dépendances. Émis UNIQUEMENT sur
// un fill confirmé (entrée ou sortie), au prix et quantité réels d'exécution.
struct TradeEvent {
    enum class Kind { ENTRY, EXIT };
    Kind        kind;
    std::string symbol;
    int         qty    = 0;
    double      price  = 0.0;    // prix de fill
    double      pnl    = 0.0;    // renseigné pour EXIT (net du buyPrice)
    std::string reason;          // "signal" / "stop-loss (...)" / "take-profit (...)"…
};

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
                if (auto persisted = stateStore_->load(riskCfg_.symbol)) {
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
        auto barsRes = dataFeed_->getBars(riskCfg_.symbol, 60);
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

        // Compteurs journaliers du kill-switch (item 18) : remis à zéro au
        // changement de date de barre ; l'equity de début de journée sert de
        // référence au drawdown. L'account est récupéré une fois par cycle et
        // réutilisé par la branche d'entrée.
        Account account = broker_->getAccount();
        if (bars.back().date != riskDay_) {
            riskDay_        = bars.back().date;
            dayStartEquity_ = account.equity;
            ordersToday_    = 0;
        }

        // 3. Gestion de la position ouverte
        // Panne broker : la position est INCONNUE — ne SURTOUT pas réconcilier
        // (l'ancien nullopt ambigu réinitialisait l'état sur simple panne
        // réseau — item 10) ni trader à l'aveugle : cycle ignoré, état conservé
        auto posRes = broker_->getPosition(riskCfg_.symbol);
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
                riskCfg_.stopLossPct, riskCfg_.takeProfitPct,
                riskCfg_.trailingStopPct, riskCfg_.minHoldDays
            );

            bool shouldSell = exitReason.has_value()
                || (signal.isSell() && state_.holdDays >= riskCfg_.minHoldDays);

            if (shouldSell) {
                std::string reason = exitReason.value_or("signal");
                // Observateur de sortie (backtest) : notifié AVANT la soumission
                // pour que le broker simulé enregistre la raison dans le trade
                if (exitObserver_) exitObserver_(reason);
                auto order = broker_->submitSell(riskCfg_.symbol, pos->shares);
                ordersToday_++;
                // Seul un fill confirmé clôt la position côté bot.
                // PENDING : la position broker disparaîtra une fois l'ordre exécuté
                // (réconciliation au cycle suivant) ; REJECTED/échec : on conserve
                // l'état et on retentera — sinon la position devient orpheline.
                if (order.has_value() && order->status == OrderStatus::FILLED) {
                    double fillPrice = order->price    > 0 ? order->price    : price;
                    int    fillQty   = order->quantity > 0 ? order->quantity : pos->shares;
                    double pnl = (fillPrice - state_.buyPrice) * fillQty;
                    // Série de pertes pour le kill-switch (item 18) : un trade
                    // perdant incrémente, un gagnant remet le compteur à zéro.
                    if (pnl < 0) consecutiveLosses_++;
                    else         consecutiveLosses_ = 0;
                    logger_->info("🔴 VENTE (" + reason + ") │ P&L: $" +
                                  std::to_string(static_cast<int>(pnl)));
                    if (tradeObserver_)
                        tradeObserver_({TradeEvent::Kind::EXIT, riskCfg_.symbol,
                                        fillQty, fillPrice, pnl, reason});
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
            // Coupe-circuit (item 18) : un plafond de risque franchi bloque
            // toute NOUVELLE entrée pour la journée (les positions ouvertes,
            // gérées plus haut, gardent leurs stops).
            if (auto halt = riskManager_->checkKillSwitch(
                    account.equity, dayStartEquity_, consecutiveLosses_,
                    ordersToday_, riskCfg_.killSwitch)) {
                logger_->warn("🛑 Entrée bloquée — " + *halt);
                return;
            }

            // Item 9.0b : sizing à fraction fixe (exposition cible) si configuré
            // — le sizing de la V2 (suivi de tendance) ; sinon risk-based (défaut).
            int shares = riskCfg_.targetExposurePct > 0.0
                ? riskManager_->positionSizeFixedExposure(
                      account.cash, price, riskCfg_.targetExposurePct)
                : riskManager_->positionSize(
                      account.cash, price,
                      riskCfg_.stopLossPct,
                      riskCfg_.riskPerTradePct);
            if (shares <= 0) {
                logger_->warn("Cash insuffisant pour ouvrir une position — aucun ordre émis");
                return;
            }
            if (!riskManager_->isTradeAllowed(account, pos, price, shares)) {
                logger_->warn("Trade non autorisé par le risk manager");
                return;
            }

            auto order = broker_->submitBuy(riskCfg_.symbol, shares);
            ordersToday_++;
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
                if (tradeObserver_)
                    tradeObserver_({TradeEvent::Kind::ENTRY, riskCfg_.symbol,
                                    fillQty, fillPrice, 0.0, "signal"});
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
    const BotState&   state()  const { return state_; }
    const RiskConfig& config() const { return riskCfg_; }
    void setState(const BotState& s)    { state_ = s; }
    void setConfig(const RiskConfig& c) { riskCfg_ = c; }

    // Compteurs du kill-switch (item 18) — exposés pour les tests et le dashboard
    int    ordersToday()       const { return ordersToday_; }
    int    consecutiveLosses() const { return consecutiveLosses_; }

    // Seam pour le backtest : raison de sortie transmise au broker simulé
    // juste avant chaque ordre de vente (TradeRecord du rapport)
    void setExitObserver(std::function<void(const std::string&)> obs) {
        exitObserver_ = std::move(obs);
    }

    // Observateur de trades (item 21) : notifié sur chaque fill confirmé
    // (entrée/sortie). Le composition root y branche DbLogger + dashboard.
    void setTradeObserver(std::function<void(const TradeEvent&)> obs) {
        tradeObserver_ = std::move(obs);
    }

private:
    std::shared_ptr<IDataFeed>    dataFeed_;
    std::shared_ptr<IBroker>      broker_;
    std::shared_ptr<IStrategy>    strategy_;
    std::shared_ptr<IRiskManager> riskManager_;
    std::shared_ptr<ILogger>      logger_;
    std::shared_ptr<IStateStore>  stateStore_;

    BotState    state_;
    RiskConfig  riskCfg_;
    std::atomic<bool> running_{false};
    bool stateLoaded_ = false;
    std::function<void(const std::string&)> exitObserver_;
    std::function<void(const TradeEvent&)>  tradeObserver_;

    // État du kill-switch (item 18). En mémoire : remis à zéro au redémarrage —
    // acceptable pour une boucle de 60 min (les stops, eux, sont persistés). La
    // persistance de ces compteurs est notée comme découverte pour un sprint futur.
    std::string riskDay_;               // date de barre de référence du jour courant
    double      dayStartEquity_   = 0.0;
    int         ordersToday_      = 0;
    int         consecutiveLosses_ = 0;

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
        if (stateStore_ && !stateStore_->save(riskCfg_.symbol, state_))
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
