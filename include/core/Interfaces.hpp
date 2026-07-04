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

    // ── Stop résident côté broker (item 19, décision « doubler ») ──────────────
    // En COMPLÉMENT du stop logiciel (checkExitConditions reste primaire) : un
    // ordre stop déposé chez le broker protège la position même si le bot est
    // hors-ligne. Par défaut non supporté (no-op) — seuls les brokers réels
    // (IBKR) le surchargent ; PaperBroker/backtest s'appuient sur le stop
    // logiciel, donc le golden est inchangé.
    virtual std::optional<Order> submitStopLoss(const std::string& symbol,
                                                int qty, double stopPrice) {
        (void)symbol; (void)qty; (void)stopPrice;
        return std::nullopt;
    }
    // Annule le stop résident courant (à la sortie, pour ne pas laisser un
    // ordre orphelin se déclencher après coup). false = rien à annuler / non
    // supporté.
    virtual bool cancelStopLoss(const std::string& symbol) {
        (void)symbol;
        return false;
    }
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

    // Calcule l'indicateur sur des barres OHLCV complètes (item 8.0, D18).
    // Implémentation par défaut RÉTRO-COMPATIBLE : extrait les clôtures et
    // délègue à compute(prices). Les indicateurs qui exploitent réellement
    // high/low/volume (ATR true-range, VWAP pondéré) la surchargent ; EMA/RSI
    // restent inchangés (ils ne dépendent que des clôtures).
    virtual std::vector<T> computeBars(const std::vector<Bar>& bars) const {
        std::vector<double> closes;
        closes.reserve(bars.size());
        for (const auto& b : bars) closes.push_back(b.close);
        return compute(closes);
    }

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

    // Variante « fenêtre de barres » (item 8o.2) : quand volSizingAtrRef > 0,
    // l'implémentation PEUT réduire la taille en régime VOLATIL — multiplier
    // les actions par min(1, volSizingAtrRef / (ATR(14)/prix)) : pleine taille
    // en marché calme, réduite quand la volatilité relative dépasse la
    // référence (hypothèse D44 : couper le risque de queue des achats de creux
    // volatils sans les supprimer). Implémentation par défaut RÉTRO-COMPATIBLE
    // (patron computeBars/8q.1) : ignore les barres et délègue à la surcharge
    // historique — un IRiskManager qui n'en tient pas compte garde son
    // comportement. Fail-open : ATR incalculable → pleine taille.
    virtual int positionSize(
        double capital,
        double price,
        double stopLossPct,
        double riskPerTradePct,
        const std::vector<Bar>& bars,   // fenêtre courante (peut être vide)
        double volSizingAtrRef          // ≤ 0 = désactivé (sizing historique)
    ) const {
        (void)bars; (void)volSizingAtrRef;
        return positionSize(capital, price, stopLossPct, riskPerTradePct);
    }

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

    // Variante « fenêtre de barres » (item 8q.1) : quand trailingAtrMult > 0,
    // l'implémentation PEUT remplacer le trailing % par un trailing en
    // multiples d'ATR(14) calculé sur les barres de la fenêtre courante
    // (vrai true-range — la « bonne » distance de trailing dépend de la
    // volatilité, hypothèse 8b.4). Implémentation par défaut RÉTRO-COMPATIBLE
    // (même patron que IIndicator::computeBars, item 8.0) : ignore les barres
    // et délègue à la surcharge historique — un IRiskManager qui ne connaît
    // pas l'ATR garde exactement son comportement.
    virtual std::optional<std::string> checkExitConditions(
        double currentPrice,
        double buyPrice,
        int    holdDays,
        double peakPrice,
        double stopLossPct,
        double takeProfitPct,
        double trailingStopPct,
        int    minHoldDays,
        const std::vector<Bar>& bars,   // fenêtre courante (peut être vide)
        double trailingAtrMult          // ≤ 0 = désactivé (trailing % historique)
    ) const {
        (void)bars;
        (void)trailingAtrMult;
        return checkExitConditions(currentPrice, buyPrice, holdDays, peakPrice,
                                   stopLossPct, takeProfitPct, trailingStopPct,
                                   minHoldDays);
    }

    // Variante « sortie structurelle » (item 8s.1) : quand exitOnLowestLowN > 0,
    // l'implémentation PEUT sortir sur cassure de STRUCTURE — clôture sous le
    // plus bas des N barres PRÉCÉDENTES (barre courante exclue). Hypothèse
    // 8-quinquies : une cassure de support récent colle mieux aux retournements
    // qu'une distance depuis le pic. Implémentation par défaut RÉTRO-COMPATIBLE
    // (même patron que la variante ATR ci-dessus) : ignore N et délègue — un
    // IRiskManager qui ne connaît pas la structure garde son comportement.
    virtual std::optional<std::string> checkExitConditions(
        double currentPrice,
        double buyPrice,
        int    holdDays,
        double peakPrice,
        double stopLossPct,
        double takeProfitPct,
        double trailingStopPct,
        int    minHoldDays,
        const std::vector<Bar>& bars,   // fenêtre courante (peut être vide)
        double trailingAtrMult,         // ≤ 0 = désactivé (trailing % historique)
        int    exitOnLowestLowN         // ≤ 0 = désactivé (pas de sortie structure)
    ) const {
        (void)exitOnLowestLowN;
        return checkExitConditions(currentPrice, buyPrice, holdDays, peakPrice,
                                   stopLossPct, takeProfitPct, trailingStopPct,
                                   minHoldDays, bars, trailingAtrMult);
    }

    // Kill-switch (item 18) : retourne la raison de coupure si un garde-fou
    // est franchi (→ aucune nouvelle ENTRÉE), nullopt si les entrées sont
    // permises. Ne concerne JAMAIS une position déjà ouverte.
    virtual std::optional<std::string> checkKillSwitch(
        const KillSwitchConfig& cfg,
        double dayStartEquity,
        double currentEquity,
        int    consecutiveLosses,
        int    ordersToday
    ) const = 0;
};

} // namespace trading
