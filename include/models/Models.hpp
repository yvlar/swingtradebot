#pragma once
#include <string>
#include <chrono>
#include <optional>

namespace trading {

// ─── Bar (chandelier OHLCV) ───────────────────────────────────────────────────
struct Bar {
    std::string date;
    double open   = 0.0;
    double high   = 0.0;
    double low    = 0.0;
    double close  = 0.0;
    long   volume = 0;

    bool operator==(const Bar& o) const {
        return date == o.date && close == o.close;
    }
};

// ─── Signal de trading ────────────────────────────────────────────────────────
enum class SignalType { BUY, SELL, HOLD };

struct Signal {
    SignalType  type      = SignalType::HOLD;
    std::string symbol;
    double      price     = 0.0;
    std::string reason;
    std::string timestamp;

    bool isBuy()  const { return type == SignalType::BUY;  }
    bool isSell() const { return type == SignalType::SELL; }
    bool isHold() const { return type == SignalType::HOLD; }
};

// ─── Ordre ────────────────────────────────────────────────────────────────────
enum class OrderSide { BUY, SELL };
enum class OrderStatus { PENDING, FILLED, CANCELLED, REJECTED };

struct Order {
    std::string symbol;
    OrderSide   side;
    int         quantity  = 0;
    double      price     = 0.0;
    OrderStatus status    = OrderStatus::PENDING;
    std::string orderId;
    std::string timestamp;
};

// ─── Position ouverte ─────────────────────────────────────────────────────────
struct Position {
    std::string symbol;
    int         shares     = 0;
    double      avgPrice   = 0.0;
    double      marketValue = 0.0;
    double      unrealizedPnl = 0.0;
};

// ─── Compte ───────────────────────────────────────────────────────────────────
struct Account {
    double cash   = 0.0;
    double equity = 0.0;
    std::string status;
};

// ─── KillSwitchConfig — garde-fous de coupure d'urgence (item 18) ─────────────
// Trois seuils qui BLOQUENT toute nouvelle ENTRÉE quand la séance dérape (une
// position déjà ouverte reste gérée par ses stops). Valeurs par défaut :
// profil « modéré » arbitré avec l'utilisateur. Un seuil ≤ 0 désactive le
// garde-fou correspondant.
struct KillSwitchConfig {
    double maxDailyDrawdownPct  = 0.05;  // -5% d'équité sur la journée → stop entrées
    int    maxConsecutiveLosses = 4;     // 4 trades perdants d'affilée → stop entrées
    int    maxOrdersPerDay      = 10;    // 10 ordres soumis dans la journée → stop entrées
};

// ─── RiskConfig — paramètres de risque et d'orchestration du bot ─────────────
// Découplé de la stratégie (item 12) : TradingBot n'a besoin que du symbole et
// des règles de gestion du risque — les paramètres d'indicateurs (EMA, RSI…)
// restent dans la config de la stratégie concernée.
struct RiskConfig {
    std::string symbol     = "QQQ";
    double stopLossPct     = 0.05;  // -5%
    double takeProfitPct   = 0.0;   // ≤ 0 = désactivé (item 8.2 : ne pas plafonner les gagnants)
    double trailingStopPct = 0.03;  // -3% depuis le pic
    // Item 8q.1 : > 0 = trailing en multiples d'ATR(14) (seuil peak − mult×ATR,
    // vrai true-range) — il REMPLACE le trailing % (repli sur le % si l'ATR est
    // incalculable) ; ≤ 0 = désactivé (trailing % historique).
    double trailingAtrMult = 0.0;
    // Item 8s.1 : > 0 = sortie structurelle sous le plus bas des N barres
    // précédentes (barre courante exclue), gatée par minHoldDays ; ≤ 0 =
    // désactivé (comportement historique).
    int    exitOnLowestLowN = 0;
    double riskPerTradePct = 0.02;  // risque 2% du capital par trade
    // Item 8o.2 (D44) : > 0 = référence de volatilité relative (ATR/prix) du
    // sizing modulé — la taille est réduite en régime plus volatil que ce
    // seuil. ≤ 0 = désactivé (sizing par le risque seul).
    double volSizingAtrRef = 0.0;
    int    minHoldDays     = 3;
    // Nombre de barres demandées au data feed à chaque cycle (item 8.1) : doit
    // couvrir la période du filtre de régime (SMA200 → ~230). Défaut 60 = ancien
    // comportement. La conversion SwingConfig→RiskConfig l'aligne sur smaTrendPeriod.
    // (Unification prod/backtest amorcée — le volet complet est D19/Sprint 9.2.)
    int    lookback        = 60;
    KillSwitchConfig killSwitch;    // garde-fous de coupure (item 18)
};

// ─── BotState — état de position du bot ──────────────────────────────────────
// Persisté via IStateStore pour survivre aux redémarrages (sinon une position
// ouverte deviendrait orpheline : plus de stop-loss, plus de suivi).
struct BotState {
    bool   inPosition = false;
    double buyPrice   = 0.0;
    double peakPrice  = 0.0;
    int    holdDays   = 0;          // jours de BOURSE depuis l'entrée (pas cycles)
    std::string lastBarDate;        // date de la dernière barre comptée
    // Champs ajoutés EN FIN de struct : l'ordre ci-dessus est figé par les
    // initialisations agrégat existantes (tests, preload des mocks).
    bool   stopArmed  = false;      // stop résident déposé chez le broker pour CETTE position (B1)
    std::string lastExitDate;       // dernier jour de bourse avec une sortie — cooldown de ré-entrée (M2)
};

} // namespace trading
