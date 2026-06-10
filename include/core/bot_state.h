#pragma once
// ============================================================
//  bot_state.h  v2  —  Structures + sérialisation nlohmann/json
//
//  vcpkg : vcpkg install nlohmann-json
//  CMake : find_package(nlohmann_json CONFIG REQUIRED)
//          target_link_libraries(SwingBot nlohmann_json::nlohmann_json)
// ============================================================
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <mutex>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

// (pas d'alias `json` global : il polluait tous les TU incluant ce header — D9)

// ─── Structures de données ────────────────────────────────

struct SignalData {
    std::string symbol;
    std::string direction;   // "LONG" | "SHORT" | "FLAT"
    int         score   = 0;
    double      rsi     = 0;
    double      entry_price = 0;
    double      atr     = 0;
    double      stop_loss   = 0;
    double      take_profit = 0;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(SignalData,
        symbol, direction, score, rsi, entry_price, atr, stop_loss, take_profit)
};

struct PositionData {
    std::string symbol;
    std::string side;        // "long" | "short"
    int         qty        = 0;
    double      avg_entry  = 0;
    double      stop_loss  = 0;
    double      take_profit= 0;
    double      current_price = 0;
    double      pnl        = 0;
    double      pnl_pct    = 0;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(PositionData,
        symbol, side, qty, avg_entry, stop_loss, take_profit,
        current_price, pnl, pnl_pct)
};

struct LogEntry {
    std::string level;   // "INFO" | "WARN" | "ERROR" | "OK"
    std::string msg;
    std::string ts;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(LogEntry, level, msg, ts)
};

enum class PipelineStage { IDLE=-1, DATA=0, SIGNAL=1, RISK=2, BROKER=3 };

// ─── État global partagé ──────────────────────────────────

struct BotState {
    double        equity    = 10000.0;
    double        pnl       = 0.0;
    double        pnl_pct   = 0.0;
    int           cycle     = 0;
    PipelineStage stage     = PipelineStage::IDLE;
    bool          running   = false;
    bool          dry_run   = true;
    std::string   mode      = "paper";

    std::vector<SignalData>   signals;
    std::vector<PositionData> positions;
    std::vector<LogEntry>     logs;      // circulaire, max 100

    mutable std::mutex mtx;

    // ── Helpers thread-safe ──────────────────────────────

    void push_log(const std::string& level, const std::string& msg) {
        std::lock_guard<std::mutex> lk(mtx);
        auto now = std::chrono::system_clock::now();
        auto tt  = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &tt);
#else
        localtime_r(&tt, &tm);
#endif
        char buf[16];
        std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
        logs.push_back({level, msg, std::string(buf)});
        if (logs.size() > 100) logs.erase(logs.begin());
    }

    void set_stage(PipelineStage s) {
        std::lock_guard<std::mutex> lk(mtx);
        stage = s;
    }

    // ── Sérialisation JSON (thread-safe) ─────────────────
    std::string to_json_str() const {
        std::lock_guard<std::mutex> lk(mtx);

        // Derniers 30 logs seulement vers le WS
        std::vector<LogEntry> recent_logs;
        size_t start = logs.size() > 30 ? logs.size() - 30 : 0;
        for (size_t i = start; i < logs.size(); ++i)
            recent_logs.push_back(logs[i]);

        nlohmann::json j = {
            {"type",      "state"},
            {"equity",    equity},
            {"pnl",       pnl},
            {"pnl_pct",   pnl_pct},
            {"cycle",     cycle},
            {"stage",     static_cast<int>(stage)},
            {"running",   running},
            {"dry_run",   dry_run},
            {"mode",      mode},
            {"signals",   signals},
            {"positions", positions},
            {"logs",      recent_logs},
        };
        return j.dump();
    }
};
