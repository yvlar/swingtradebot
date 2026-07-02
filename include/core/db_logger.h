#pragma once
// ============================================================
//  db_logger.h  —  Persistance SQLite (logs + trades + equity)
//
//  Sprint 2, item 9 (+ D2) : chaque sqlite3_prepare_v2 et
//  sqlite3_step est vérifié — un échec est consigné sur stderr
//  et la méthode retourne false (écritures) ou "[]" (lectures),
//  sans jamais toucher un statement invalide.
// ============================================================
#include <sqlite3.h>
#include <string>
#include <stdexcept>
#include <mutex>
#include <iostream>
#include "bot_state.h"

class DbLogger {
public:
    explicit DbLogger(const std::string& path = "swingbot.db") {
        // Un constructeur qui lève n'a PAS de destructeur appelé : le handle
        // sqlite (alloué même quand open échoue, doc SQLite) doit être fermé
        // ici, sinon fuite — trouvée par LeakSanitizer (passe 3, B2).
        if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
            std::string msg = sqlite3_errmsg(db_);
            sqlite3_close(db_);
            db_ = nullptr;
            throw std::runtime_error("SQLite open: " + msg);
        }
        try {
            exec_("PRAGMA journal_mode=WAL;");
            create_tables_();
        } catch (...) {
            sqlite3_close(db_);
            db_ = nullptr;
            throw;
        }
    }

    ~DbLogger() { if (db_) sqlite3_close(db_); }

    bool log(const LogEntry& e) {
        std::lock_guard<std::mutex> lk(mtx_);
        const char* sql = "INSERT INTO logs(ts, level, msg) VALUES(?,?,?);";
        sqlite3_stmt* stmt = prepare_(sql);
        if (!stmt) return false;
        sqlite3_bind_text(stmt, 1, e.ts.c_str(),    -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, e.level.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, e.msg.c_str(),   -1, SQLITE_STATIC);
        return step_done_(stmt);
    }

    bool record_trade(const std::string& symbol,
                      const std::string& side,
                      int qty,
                      double entry,
                      double sl,
                      double tp,
                      const std::string& status = "open") {
        std::lock_guard<std::mutex> lk(mtx_);
        const char* sql =
            "INSERT INTO trades(symbol,side,qty,entry,sl,tp,status,opened_at)"
            " VALUES(?,?,?,?,?,?,?,datetime('now'));";
        sqlite3_stmt* stmt = prepare_(sql);
        if (!stmt) return false;
        sqlite3_bind_text  (stmt, 1, symbol.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text  (stmt, 2, side.c_str(),   -1, SQLITE_STATIC);
        sqlite3_bind_int   (stmt, 3, qty);
        sqlite3_bind_double(stmt, 4, entry);
        sqlite3_bind_double(stmt, 5, sl);
        sqlite3_bind_double(stmt, 6, tp);
        sqlite3_bind_text  (stmt, 7, status.c_str(), -1, SQLITE_STATIC);
        return step_done_(stmt);
    }

    // FIX : utilise un sous-select au lieu de ORDER BY dans UPDATE
    // (SQLite sans SQLITE_ENABLE_UPDATE_DELETE_LIMIT ne supporte pas
    //  ORDER BY / LIMIT dans les requêtes UPDATE)
    bool close_trade(const std::string& symbol,
                     double exit_price,
                     double pnl) {
        std::lock_guard<std::mutex> lk(mtx_);
        const char* sql =
            "UPDATE trades SET status='closed', exit_price=?, pnl=?,"
            " closed_at=datetime('now')"
            " WHERE rowid = ("
            "   SELECT rowid FROM trades"
            "   WHERE symbol=? AND status='open'"
            "   ORDER BY rowid DESC LIMIT 1"
            ");";
        sqlite3_stmt* stmt = prepare_(sql);
        if (!stmt) return false;
        sqlite3_bind_double(stmt, 1, exit_price);
        sqlite3_bind_double(stmt, 2, pnl);
        sqlite3_bind_text  (stmt, 3, symbol.c_str(), -1, SQLITE_STATIC);
        return step_done_(stmt);
    }

    bool snapshot_equity(double equity, double pnl, int cycle) {
        std::lock_guard<std::mutex> lk(mtx_);
        const char* sql =
            "INSERT INTO equity_curve(ts, equity, pnl, cycle)"
            " VALUES(datetime('now'),?,?,?);";
        sqlite3_stmt* stmt = prepare_(sql);
        if (!stmt) return false;
        sqlite3_bind_double(stmt, 1, equity);
        sqlite3_bind_double(stmt, 2, pnl);
        sqlite3_bind_int   (stmt, 3, cycle);
        return step_done_(stmt);
    }

    bool record_signal(const SignalData& s, int cycle) {
        std::lock_guard<std::mutex> lk(mtx_);
        const char* sql =
            "INSERT INTO signals(ts, symbol, direction, score, rsi,"
            " entry, atr, sl, tp, cycle)"
            " VALUES(datetime('now'),?,?,?,?,?,?,?,?,?);";
        sqlite3_stmt* stmt = prepare_(sql);
        if (!stmt) return false;
        sqlite3_bind_text  (stmt, 1, s.symbol.c_str(),    -1, SQLITE_STATIC);
        sqlite3_bind_text  (stmt, 2, s.direction.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int   (stmt, 3, s.score);
        sqlite3_bind_double(stmt, 4, s.rsi);
        sqlite3_bind_double(stmt, 5, s.entry_price);
        sqlite3_bind_double(stmt, 6, s.atr);
        sqlite3_bind_double(stmt, 7, s.stop_loss);
        sqlite3_bind_double(stmt, 8, s.take_profit);
        sqlite3_bind_int   (stmt, 9, cycle);
        return step_done_(stmt);
    }

    std::string equity_history_json(int limit = 100) {
        std::lock_guard<std::mutex> lk(mtx_);
        const char* sql =
            "SELECT ts, equity, pnl, cycle FROM equity_curve"
            " ORDER BY rowid DESC LIMIT ?;";
        sqlite3_stmt* stmt = prepare_(sql);
        if (!stmt) return "[]";
        sqlite3_bind_int(stmt, 1, limit);
        nlohmann::json arr = nlohmann::json::array();
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            arr.push_back({
                {"ts",     (const char*)sqlite3_column_text(stmt, 0)},
                {"equity", sqlite3_column_double(stmt, 1)},
                {"pnl",    sqlite3_column_double(stmt, 2)},
                {"cycle",  sqlite3_column_int   (stmt, 3)},
            });
        }
        sqlite3_finalize(stmt);
        std::reverse(arr.begin(), arr.end());
        return arr.dump();
    }

    std::string trades_json(int limit = 50) {
        std::lock_guard<std::mutex> lk(mtx_);
        const char* sql =
            "SELECT symbol,side,qty,entry,exit_price,sl,tp,pnl,status,"
            " opened_at,closed_at FROM trades"
            " ORDER BY rowid DESC LIMIT ?;";
        sqlite3_stmt* stmt = prepare_(sql);
        if (!stmt) return "[]";
        sqlite3_bind_int(stmt, 1, limit);
        nlohmann::json arr = nlohmann::json::array();
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            auto col_str = [&](int i) -> std::string {
                auto p = sqlite3_column_text(stmt, i);
                return p ? (const char*)p : "";
            };
            arr.push_back({
                {"symbol",    col_str(0)},
                {"side",      col_str(1)},
                {"qty",       sqlite3_column_int(stmt, 2)},
                {"entry",     sqlite3_column_double(stmt, 3)},
                {"exit",      sqlite3_column_double(stmt, 4)},
                {"sl",        sqlite3_column_double(stmt, 5)},
                {"tp",        sqlite3_column_double(stmt, 6)},
                {"pnl",       sqlite3_column_double(stmt, 7)},
                {"status",    col_str(8)},
                {"opened_at", col_str(9)},
                {"closed_at", col_str(10)},
            });
        }
        sqlite3_finalize(stmt);
        return arr.dump();
    }

private:
    sqlite3*   db_ = nullptr;
    std::mutex mtx_;

    // Prépare une requête ; nullptr si échec (consigné, jamais d'UB — D2)
    sqlite3_stmt* prepare_(const char* sql) {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            std::cerr << "[DbLogger] prepare: " << sqlite3_errmsg(db_) << "\n";
            return nullptr;
        }
        return stmt;
    }

    // Exécute et finalise un statement d'écriture ; false si échec
    bool step_done_(sqlite3_stmt* stmt) {
        bool ok = sqlite3_step(stmt) == SQLITE_DONE;
        if (!ok)
            std::cerr << "[DbLogger] step: " << sqlite3_errmsg(db_) << "\n";
        sqlite3_finalize(stmt);
        return ok;
    }

    void exec_(const std::string& sql) {
        char* err = nullptr;
        if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
            std::string msg = err ? err : "unknown";
            sqlite3_free(err);
            throw std::runtime_error("SQLite exec: " + msg);
        }
    }

    void create_tables_() {
        exec_(R"(
            CREATE TABLE IF NOT EXISTS logs (
                id    INTEGER PRIMARY KEY AUTOINCREMENT,
                ts    TEXT NOT NULL,
                level TEXT NOT NULL,
                msg   TEXT NOT NULL
            );
            CREATE TABLE IF NOT EXISTS trades (
                id         INTEGER PRIMARY KEY AUTOINCREMENT,
                symbol     TEXT NOT NULL,
                side       TEXT NOT NULL,
                qty        INTEGER NOT NULL,
                entry      REAL NOT NULL,
                exit_price REAL,
                sl         REAL,
                tp         REAL,
                pnl        REAL,
                status     TEXT DEFAULT 'open',
                opened_at  TEXT,
                closed_at  TEXT
            );
            CREATE TABLE IF NOT EXISTS equity_curve (
                id     INTEGER PRIMARY KEY AUTOINCREMENT,
                ts     TEXT NOT NULL,
                equity REAL NOT NULL,
                pnl    REAL NOT NULL,
                cycle  INTEGER NOT NULL
            );
            CREATE TABLE IF NOT EXISTS signals (
                id        INTEGER PRIMARY KEY AUTOINCREMENT,
                ts        TEXT NOT NULL,
                symbol    TEXT NOT NULL,
                direction TEXT NOT NULL,
                score     INTEGER,
                rsi       REAL,
                entry     REAL,
                atr       REAL,
                sl        REAL,
                tp        REAL,
                cycle     INTEGER
            );
        )");
    }
};