#pragma once
#include "models/Models.hpp"
#include "models/Result.hpp"
#include <vector>
#include <optional>
#include <functional>
#include <string>

namespace trading {

// ─── ILogger ──────────────────────────────────────────────────────────────────
// DIP: le bot dépend de cette abstraction, pas d'une implémentation concrète
class ILogger {
public:
    virtual ~ILogger() = default;
    virtual void info (const std::string& msg) = 0;
    virtual void warn (const std::string& msg) = 0;
    virtual void error(const std::string& msg) = 0;
    virtual void debug(const std::string& msg) = 0;
};

// ─── IDataFeed ────────────────────────────────────────────────────────────────
// Source de données marché (Alpaca, Yahoo, mock pour tests)
// Canal d'erreur (item 10) : Err = panne réseau/parsing — distinct d'une
// réponse vide légitime (Ok avec vecteur vide / nullopt)
class IDataFeed {
public:
    virtual ~IDataFeed() = default;

    // Récupère les barres journalières pour un symbole
    // Ok(vide) = pas de donnée disponible ; Err = panne
    virtual Result<std::vector<Bar>> getBars(
        const std::string& symbol,
        int                days
    ) = 0;

    // Vérifie si le marché est ouvert
    virtual bool isMarketOpen() = 0;
};

// ─── IBroker ──────────────────────────────────────────────────────────────────
// Exécution des ordres (Alpaca paper, Alpaca live, mock pour tests)
class IBroker {
public:
    virtual ~IBroker() = default;

    virtual std::optional<Order>    submitBuy (const std::string& symbol, int qty) = 0;
    virtual std::optional<Order>    submitSell(const std::string& symbol, int qty) = 0;

    // Ok(nullopt) = le broker confirme qu'AUCUNE position n'existe ;
    // Err = panne : on ne sait pas — ne JAMAIS réconcilier sur ce cas (item 10)
    virtual Result<std::optional<Position>> getPosition(const std::string& symbol) = 0;

    virtual Account                 getAccount() = 0;
};

// ─── IIndicator ───────────────────────────────────────────────────────────────
// Interface générique pour tous les indicateurs techniques
// Template T = type de la valeur retournée (double, bool, etc.)
template<typename T>
class IIndicator {
public:
    virtual ~IIndicator() = default;

    // Calcule l'indicateur sur une série de prix
    virtual std::vector<T> compute(const std::vector<double>& prices) const = 0;

    // Nom de l'indicateur (pour le logging)
    virtual std::string name() const = 0;
};

// ─── IStrategy ────────────────────────────────────────────────────────────────
// Logique de génération de signaux — indépendante du broker et du data feed
class IStrategy {
public:
    virtual ~IStrategy() = default;

    // Génère un signal à partir des barres historiques
    virtual Signal evaluate(const std::vector<Bar>& bars) const = 0;

    // Nom de la stratégie
    virtual std::string name() const = 0;
};

// ─── IStateStore ──────────────────────────────────────────────────────────────
// Persistance de l'état de position du bot — survit aux redémarrages.
// Sans elle, une position ouverte devient orpheline après un restart
// (plus de stop-loss, plus de suivi).
class IStateStore {
public:
    virtual ~IStateStore() = default;

    // Retourne l'état persisté pour un symbole, ou nullopt si aucun
    virtual std::optional<BotState> load(const std::string& symbol) = 0;

    // Persiste l'état ; false en cas d'échec (le bot loggue mais continue)
    virtual bool save(const std::string& symbol, const BotState& state) = 0;
};

// ─── IRiskManager ─────────────────────────────────────────────────────────────
// Calcul du sizing et validation des ordres
class IRiskManager {
public:
    virtual ~IRiskManager() = default;

    // Calcule le nombre d'actions à acheter
    virtual int positionSize(
        double capital,
        double price,
        double stopLossPct,
        double riskPerTradePct
    ) const = 0;

    // Vérifie si le trade respecte les règles de risque
    // (dont coût total = price × qty ≤ cash disponible)
    virtual bool isTradeAllowed(
        const Account&                 account,
        const std::optional<Position>& currentPosition,
        double                         price,
        int                            qty
    ) const = 0;

    // Vérifie si le stop-loss ou take-profit est atteint
    virtual std::optional<std::string> checkExitConditions(
        double currentPrice,
        double buyPrice,
        int    holdDays,
        double peakPrice,
        double stopLossPct,
        double takeProfitPct,
        double trailingStopPct,
        int    minHoldDays
    ) const = 0;

    // Coupe-circuit de risque (item 18) : retourne la raison de coupure si un
    // plafond est franchi (drawdown journalier, pertes consécutives, ordres/jour),
    // sinon nullopt. Pur (sans état) : TradingBot fournit les compteurs courants.
    // Ne gate QUE les nouvelles entrées — les positions ouvertes gardent leurs stops.
    virtual std::optional<std::string> checkKillSwitch(
        double                  currentEquity,
        double                  dayStartEquity,
        int                     consecutiveLosses,
        int                     ordersToday,
        const KillSwitchConfig& cfg
    ) const = 0;
};

} // namespace trading
