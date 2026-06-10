#pragma once
// ============================================================
//  db_log_sink.h  —  Pont trading::ILogger → DbLogger (item 21)
//
//  Unifie les deux systèmes de journalisation : les logs texte du
//  moteur (trading::ILogger) atterrissent dans la table `logs` de
//  SQLite, à côté des trades et de la courbe d'equity. À combiner
//  avec ConsoleLogger via CompositeLogger au composition root.
// ============================================================
#include "core/Interfaces.hpp"
#include "core/db_logger.h"
#include "bot_state.h"          // LogEntry
#include <ctime>
#include <string>

namespace trading {

class DbLogSink final : public ILogger {
public:
    explicit DbLogSink(DbLogger& db) : db_(db) {}

    void info (const std::string& m) override { write_("INFO",  m); }
    void warn (const std::string& m) override { write_("WARN",  m); }
    void error(const std::string& m) override { write_("ERROR", m); }
    void debug(const std::string& m) override { write_("DEBUG", m); }

private:
    DbLogger& db_;

    void write_(const std::string& level, const std::string& msg) {
        db_.log(LogEntry{level, msg, nowTs_()});
    }

    static std::string nowTs_() {
        std::time_t t = std::time(nullptr);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        char buf[20];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
        return std::string(buf);
    }
};

} // namespace trading
