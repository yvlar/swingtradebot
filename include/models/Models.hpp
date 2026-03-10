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

} // namespace trading
