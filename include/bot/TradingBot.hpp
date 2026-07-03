#pragma once
#include "core/Interfaces.hpp"
#include <memory>
#include <thread>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <functional>

namespace trading {

// ─── TradeFill — événement de fill confirmé (item 21) ────────────────────────
// Transmis au composition root pour journaliser les trades (DbLogger
// record_trade/close_trade) et alimenter le dashboard (BotState::positions).
// Émis UNIQUEMENT sur un fill confirmé : achat ouvrant, vente clôturant.
struct TradeFill {
    OrderSide   side;             // BUY = ouverture, SELL = clôture
    std::string symbol;
    int         quantity = 0;
    double      price    = 0.0;   // prix de fill réel
    double      pnl      = 0.0;   // P&L $ (significatif à la vente)
    std::string reason;           // raison de sortie (vente) ; "" à l'achat
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
    // Enveloppe : mémorise la santé du cycle (A2, passe 3) — le composition
    // root ne bat le heartbeat du watchdog QUE si le cycle a réellement
    // fonctionné, sinon toute panne resterait invisible (watchdog vert).
    void runOnce() { lastCycleHealthy_ = runCycle_(); }

    // Santé du DERNIER cycle : false = panne (feed, broker, compte inactif).
    // Marché fermé, HOLD, kill-switch, cooldown ou ordre rejeté restent des
    // cycles SAINS : le watchdog surveille le SILENCE, pas les décisions de
    // trading (les rejets/kill-switch ont leurs propres alertes).
    bool lastCycleHealthy() const { return lastCycleHealthy_; }

private:
    // Implémentation du cycle — retourne sa santé (voir lastCycleHealthy)
    bool runCycle_() {
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
            return true;    // attente normale, pas une panne
        }

        // 1. Récupération des données
        // Panne du feed ≠ « pas de donnée » (item 10) : sur panne, on saute
        // le cycle sans toucher à l'état
        auto barsRes = dataFeed_->getBars(riskCfg_.symbol, riskCfg_.lookback);
        if (!barsRes.ok()) {
            logger_->error("Panne du data feed (" + barsRes.error()
                           + ") — cycle ignoré");
            return false;
        }
        const auto& bars = barsRes.value();
        if (bars.empty()) {
            logger_->warn("Aucune barre reçue");
            return false;   // marché ouvert sans donnée = anormal
        }
        // Feed plus court que demandé (B3.2) : le filtre de régime (SMA200)
        // peut être incalculable → aucune entrée ne partira. Signalé chaque
        // cycle, sinon le non-trade est indiscernable d'un marché sans signal.
        if (bars.size() < static_cast<size_t>(riskCfg_.lookback)) {
            logger_->warn("Historique incomplet : " + std::to_string(bars.size())
                          + "/" + std::to_string(riskCfg_.lookback)
                          + " barres reçues — le filtre de régime peut être incalculable");
        }

        // 2. Génération du signal
        Signal signal = strategy_->evaluate(bars);
        double price  = bars.back().close;

        logger_->info(
            bars.back().date + " │ $" + std::to_string(static_cast<int>(price)) +
            " │ " + signalStr(signal) + " │ " + signal.reason
        );

        // Bookkeeping journalier du kill-switch (item 18) : au changement de
        // jour de bourse, on remet à zéro les compteurs journaliers et on fige
        // l'équité de référence de la séance (base du drawdown journalier).
        if (bars.back().date != killSwitchDay_) {
            killSwitchDay_  = bars.back().date;
            ordersToday_    = 0;
            dayStartEquity_ = broker_->getAccount().equity;
            lastHaltNotified_.clear();   // nouvelle séance : ré-alerter si rechute (A6)
        }

        // 3. Gestion de la position ouverte
        // Panne broker : la position est INCONNUE — ne SURTOUT pas réconcilier
        // (l'ancien nullopt ambigu réinitialisait l'état sur simple panne
        // réseau — item 10) ni trader à l'aveugle : cycle ignoré, état conservé
        auto posRes = broker_->getPosition(riskCfg_.symbol);
        if (!posRes.ok()) {
            logger_->error("Panne broker sur getPosition (" + posRes.error()
                           + ") — cycle ignoré, état conservé");
            return false;
        }
        const auto& pos = posRes.value();

        // Réconciliation : la position broker fait foi (restart, ordre PENDING
        // exécuté entre deux cycles, position fermée à la main…) — uniquement
        // sur une réponse broker CERTAINE
        reconcilePosition_(pos, price, bars.back().date);

        if (state_.inPosition && pos.has_value()) {
            // Filet B1 : si le stop résident n'a pas pu être déposé (échec
            // réseau, adoption ancienne), on retente à chaque cycle.
            armResidentStopIfNeeded_(pos->shares, state_.buyPrice);

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

            // Variante « barres » (item 8q.1) : la fenêtre courante permet au
            // risk manager de calculer un trailing ATR quand trailingAtrMult
            // est configuré (mult ≤ 0 → strictement le comportement historique).
            auto exitReason = riskManager_->checkExitConditions(
                price, state_.buyPrice, state_.holdDays, state_.peakPrice,
                riskCfg_.stopLossPct, riskCfg_.takeProfitPct,
                riskCfg_.trailingStopPct, riskCfg_.minHoldDays,
                bars, riskCfg_.trailingAtrMult
            );

            bool shouldSell = exitReason.has_value()
                || (signal.isSell() && state_.holdDays >= riskCfg_.minHoldDays);

            if (shouldSell) {
                std::string reason = exitReason.value_or("signal");
                // Observateur de sortie (backtest) : notifié AVANT la soumission
                // pour que le broker simulé enregistre la raison dans le trade
                if (exitObserver_) exitObserver_(reason);
                auto order = broker_->submitSell(riskCfg_.symbol, pos->shares);
                ordersToday_++;  // tout ordre soumis compte au plafond (item 18)
                // Seul un fill confirmé clôt la position côté bot.
                // PENDING : la position broker disparaîtra une fois l'ordre exécuté
                // (réconciliation au cycle suivant) ; REJECTED/échec : on conserve
                // l'état et on retentera — sinon la position devient orpheline.
                if (order.has_value() && order->status == OrderStatus::FILLED) {
                    double fillPrice = order->price    > 0 ? order->price    : price;
                    int    fillQty   = order->quantity > 0 ? order->quantity : pos->shares;
                    double pnl = (fillPrice - state_.buyPrice) * fillQty;
                    // Série de pertes consécutives pour le kill-switch (item 18) :
                    // un trade perdant l'allonge, un gagnant la remet à zéro
                    if (pnl < 0) consecutiveLosses_++;
                    else         consecutiveLosses_ = 0;
                    logger_->info("🔴 VENTE (" + reason + ") │ P&L: $" +
                                  std::to_string(static_cast<int>(pnl)));
                    if (tradeObserver_)
                        tradeObserver_({OrderSide::SELL, riskCfg_.symbol,
                                        fillQty, fillPrice, pnl, reason});
                    // Annule le stop résident : la sortie logicielle a déjà
                    // vendu — pas de stop orphelin (item 19, réduit D14)
                    broker_->cancelStopLoss(riskCfg_.symbol);
                    // Reset + cooldown (M2) : pas de ré-entrée le même jour
                    // de bourse (cycle 60 min : sortie 10h30 → rachat 11h30
                    // sinon, churn/wash-trades)
                    BotState fresh;
                    fresh.lastExitDate = bars.back().date;
                    state_ = fresh;
                    saveState_();
                } else {
                    logger_->error("Ordre de vente non exécuté (" + orderStatusStr(order)
                                   + ") — position conservée, nouvel essai au prochain cycle");
                }
            }
        }

        // 4. Entrée en position
        else if (!state_.inPosition && signal.isBuy()) {
            // Cooldown de ré-entrée (M2) : une sortie a déjà eu lieu ce jour
            // de bourse — on ne rachète pas avant la barre suivante.
            if (!state_.lastExitDate.empty()
                && state_.lastExitDate == bars.back().date) {
                logger_->info("Ré-entrée bloquée (cooldown) : sortie déjà "
                              "exécutée le " + state_.lastExitDate);
                return true;    // décision de trading, pas une panne
            }

            auto account = broker_->getAccount();

            // Compte non ACTIF (panne, résumé incomplet…) : la cause réelle
            // doit être visible — sans ce garde, on tombait dans « Cash
            // insuffisant » (cash=0), diagnostic trompeur en prod.
            if (account.status != "ACTIVE") {
                logger_->error("Compte broker non ACTIF (status=" + account.status
                               + ") — entrée bloquée");
                return false;   // panne broker : cycle non sain
            }

            // Kill-switch (item 18) : si un garde-fou est franchi, on ne prend
            // AUCUNE nouvelle position. Les positions déjà ouvertes sont gérées
            // plus haut (branche de sortie) et conservent leurs stops.
            if (auto halt = riskManager_->checkKillSwitch(
                    riskCfg_.killSwitch, dayStartEquity_, account.equity,
                    consecutiveLosses_, ordersToday_)) {
                logger_->warn("🛑 Kill-switch (" + *halt + ") — entrée bloquée");
                // Notification opérateur (A6), dédupliquée par raison : la
                // boucle 60 min re-frapperait sinon l'alerte à chaque cycle.
                if (haltObserver_ && *halt != lastHaltNotified_) {
                    haltObserver_(*halt);
                    lastHaltNotified_ = *halt;
                }
                return true;    // garde-fou assumé (alerté ci-dessus), pas une panne
            }

            int shares = riskManager_->positionSize(
                account.cash, price,
                riskCfg_.stopLossPct,
                riskCfg_.riskPerTradePct
            );
            if (shares <= 0) {
                logger_->warn("Cash insuffisant pour ouvrir une position — aucun ordre émis");
                return true;
            }
            if (!riskManager_->isTradeAllowed(account, pos, price, shares)) {
                logger_->warn("Trade non autorisé par le risk manager");
                return true;
            }

            auto order = broker_->submitBuy(riskCfg_.symbol, shares);
            ordersToday_++;  // tout ordre soumis compte au plafond (item 18)
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
                if (tradeObserver_)
                    tradeObserver_({OrderSide::BUY, riskCfg_.symbol,
                                    fillQty, fillPrice, 0.0, ""});
                // Stop résident côté broker en COMPLÉMENT du stop logiciel
                // (item 19, décision « doubler ») : protège la position même
                // si le bot est hors-ligne. No-op sur PaperBroker/backtest.
                armResidentStopIfNeeded_(fillQty, fillPrice);
            } else if (order.has_value() && order->status == OrderStatus::PENDING) {
                logger_->warn("Ordre d'achat en attente d'exécution — "
                              "réconciliation au prochain cycle");
            } else {
                logger_->error("Ordre d'achat non exécuté (" + orderStatusStr(order) + ")");
            }
        }
        return true;    // cycle complet (HOLD, ordres traités — même rejetés)
    }

public:
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

    // Seam pour le backtest : raison de sortie transmise au broker simulé
    // juste avant chaque ordre de vente (TradeRecord du rapport)
    void setExitObserver(std::function<void(const std::string&)> obs) {
        exitObserver_ = std::move(obs);
    }

    // Observateur de cycle de vie des trades (item 21) : notifié sur chaque
    // fill CONFIRMÉ (achat ouvrant, vente clôturant) pour journalisation
    // (DbLogger) et dashboard. Le composition root y branche record_trade/
    // close_trade et BotState::positions.
    void setTradeObserver(std::function<void(const TradeFill&)> obs) {
        tradeObserver_ = std::move(obs);
    }

    // Observateur du kill-switch (A6) : notifié avec la raison quand un
    // garde-fou bloque les entrées — une fois par raison et par séance
    // (dédup). Le composition root y branche l'alerte opérateur.
    void setHaltObserver(std::function<void(const std::string&)> obs) {
        haltObserver_ = std::move(obs);
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
    bool lastCycleHealthy_ = false;   // pessimiste avant le 1er cycle (A2)
    std::function<void(const std::string&)> exitObserver_;
    std::function<void(const TradeFill&)>   tradeObserver_;
    std::function<void(const std::string&)> haltObserver_;
    std::string lastHaltNotified_;   // dédup des notifications kill-switch (A6)

    // ── Compteurs du kill-switch (item 18) ────────────────────────────────────
    // Réinitialisés au changement de jour de bourse (sauf la série de pertes
    // consécutives, qui est un compteur de tendance, pas journalier).
    std::string killSwitchDay_;        // jour de bourse courant
    double      dayStartEquity_ = 0.0; // équité de référence en début de séance
    int         ordersToday_    = 0;   // ordres soumis dans la journée
    int         consecutiveLosses_ = 0;// trades perdants d'affilée

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
            // Le stop résident éventuel ne protège plus rien : l'annuler,
            // sinon un ordre orphelin peut se déclencher après coup (B1).
            // No-op si aucun stop n'est suivi côté broker.
            broker_->cancelStopLoss(riskCfg_.symbol);
            // Sortie exécutée hors bot ce jour → même cooldown de ré-entrée
            // que pour une sortie logicielle (M2)
            BotState fresh;
            fresh.lastExitDate = barDate;
            state_ = fresh;
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
            // B1 : une position adoptée (ordre PENDING exécuté entre deux
            // cycles, restart…) doit être protégée comme une entrée normale —
            // c'était LE trou : la réconciliation n'armait jamais le stop.
            armResidentStopIfNeeded_(pos->shares, basis);
        }
    }

    // ── Stop résident (B1) — armement idempotent ──────────────────────────────
    // Dépose le stop broker si nécessaire. state_.stopArmed (persisté) garantit
    // qu'on ne dépose JAMAIS deux stops pour la même position ; un échec de
    // dépôt laisse stopArmed=false → nouvel essai au cycle suivant (filet).
    void armResidentStopIfNeeded_(int qty, double basisPrice) {
        if (state_.stopArmed || qty <= 0 || basisPrice <= 0) return;
        double stopPrice = basisPrice * (1.0 - riskCfg_.stopLossPct);
        auto stop = broker_->submitStopLoss(riskCfg_.symbol, qty, stopPrice);
        if (stop.has_value()) {
            state_.stopArmed = true;
            saveState_();
            logger_->info("Stop résident armé │ " + std::to_string(qty) +
                          " parts @ $" + std::to_string(static_cast<int>(stopPrice)));
        } else {
            // Broker sans support (défaut no-op Paper/Alpaca) ou échec réseau :
            // en debug pour ne pas polluer chaque cycle des brokers no-op.
            logger_->debug("Stop résident non armé — nouvel essai au prochain cycle");
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
