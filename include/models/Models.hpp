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

// ─── KillSwitchConfig — coupe-circuit de risque (item 18) ────────────────────
// Plafonds qui, une fois franchis, BLOQUENT toute nouvelle entrée pour la
// journée. Les positions déjà ouvertes restent protégées par leurs stops —
// le kill-switch ne ferme rien, il empêche d'aggraver l'exposition.
struct KillSwitchConfig {
    bool   enabled              = true;
    double maxDailyDrawdownPct  = 0.08;  // -8% d'equity sur la journée → stop entrées
    int    maxConsecutiveLosses = 6;     // 6 trades perdants d'affilée → stop entrées
    int    maxOrdersPerDay      = 20;    // garde-fou anti-emballement (boucle folle)
};

// ─── RiskConfig — paramètres de risque et d'orchestration du bot ─────────────
// Découplé de la stratégie (item 12) : TradingBot n'a besoin que du symbole et
// des règles de gestion du risque — les paramètres d'indicateurs (EMA, RSI…)
// restent dans la config de la stratégie concernée.
struct RiskConfig {
    std::string symbol     = "QQQ";
    double stopLossPct     = 0.05;  // -5%
    double takeProfitPct   = 0.10;  // +10%
    double trailingStopPct = 0.03;  // -3% depuis le pic
    double riskPerTradePct = 0.02;  // risque 2% du capital par trade
    int    minHoldDays     = 3;
    KillSwitchConfig killSwitch{};  // coupe-circuit de risque (item 18)
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
};

} // namespace trading
