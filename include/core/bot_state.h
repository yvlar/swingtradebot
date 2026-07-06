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

    // ── Santé opérationnelle (observabilité, item 15.1) ──
    // Surface explicite de liveness pour distinguer un « process gelé » (déjà
    // couvert par le HEALTHCHECK TCP du Dockerfile) d'un « process qui TOURNE mais
    // silencieusement malsain » : cycles qui échouent en série, Gateway
    // dé-authentifié, kill-switch armé. Ces faits sont calculés à chaque cycle par
    // le composition root (main_ibkr.cpp) mais n'étaient jusqu'ici que loggés.
    bool          last_cycle_healthy       = false;
    long          last_healthy_cycle_epoch = 0;   // 0 = aucun cycle sain encore
    bool          gateway_authenticated    = false;
    bool          kill_switch_tripped      = false;
    std::string   kill_switch_reason       = "";

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

    // ── Santé opérationnelle (item 15.1) ─────────────────

    // Met à jour l'instantané de santé en un seul verrou (appelé une fois par
    // cycle par le composition root avec les faits déjà calculés de la boucle).
    void set_health(bool cycle_healthy, long healthy_epoch, bool gateway_auth,
                    bool ks_tripped, const std::string& ks_reason) {
        std::lock_guard<std::mutex> lk(mtx);
        last_cycle_healthy       = cycle_healthy;
        last_healthy_cycle_epoch = healthy_epoch;
        gateway_authenticated    = gateway_auth;
        kill_switch_tripped      = ks_tripped;
        kill_switch_reason       = ks_reason;
    }

    // Objet JSON de santé. `now_epoch` (secondes depuis l'époque) est INJECTÉ pour
    // que l'âge du dernier cycle sain soit testable sans horloge murale (patron du
    // seam `now_()` d'IBKRDataFeed / `EnvReader` du watchdog). Thread-safe.
    nlohmann::json health_json(long now_epoch) const {
        std::lock_guard<std::mutex> lk(mtx);
        return health_obj_locked_(now_epoch);
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
            {"health",    health_obj_locked_(now_epoch_())},
        };
        return j.dump();
    }

private:
    // Suppose le lock DÉJÀ pris (appelé par health_json et to_json_str). Dérive
    // `seconds_since_healthy_cycle` : -1 si aucun cycle sain, sinon max(0, now−ts)
    // (borné à 0 en cas de dérive d'horloge, now < ts).
    nlohmann::json health_obj_locked_(long now_epoch) const {
        long age = -1;
        if (last_healthy_cycle_epoch > 0)
            age = now_epoch > last_healthy_cycle_epoch
                ? now_epoch - last_healthy_cycle_epoch : 0;
        return nlohmann::json{
            {"last_cycle_healthy",          last_cycle_healthy},
            {"last_healthy_cycle_epoch",    last_healthy_cycle_epoch},
            {"seconds_since_healthy_cycle", age},
            {"gateway_authenticated",       gateway_authenticated},
            {"kill_switch_tripped",         kill_switch_tripped},
            {"kill_switch_reason",          kill_switch_reason},
        };
    }

    static long now_epoch_() {
        return static_cast<long>(std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    }
};
