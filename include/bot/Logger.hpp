#pragma once
#include "core/Interfaces.hpp"
#include <iostream>
#include <fstream>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace trading {

// ─── Utilitaire timestamp ─────────────────────────────────────────────────────
inline std::string currentTimestamp() {
    auto now  = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

// ─── ConsoleLogger ────────────────────────────────────────────────────────────
class ConsoleLogger final : public ILogger {
public:
    void info (const std::string& msg) override { log("INFO ", "\033[32m", msg); }
    void warn (const std::string& msg) override { log("WARN ", "\033[33m", msg); }
    void error(const std::string& msg) override { log("ERROR", "\033[31m", msg); }
    void debug(const std::string& msg) override {
        if (verbose_) log("DEBUG", "\033[36m", msg);
    }

    void setVerbose(bool v) { verbose_ = v; }

private:
    bool verbose_ = false;
    std::mutex mutex_;

    void log(const std::string& level, const std::string& color, const std::string& msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::cout << color
                  << currentTimestamp() << " │ " << level << " │ "
                  << msg << "\033[0m\n";
    }
};

// ─── FileLogger ───────────────────────────────────────────────────────────────
class FileLogger final : public ILogger {
public:
    explicit FileLogger(const std::string& path)
        : file_(path, std::ios::app) {}

    void info (const std::string& msg) override { log("INFO ", msg); }
    void warn (const std::string& msg) override { log("WARN ", msg); }
    void error(const std::string& msg) override { log("ERROR", msg); }
    void debug(const std::string& msg) override { log("DEBUG", msg); }

private:
    std::ofstream file_;
    std::mutex    mutex_;

    void log(const std::string& level, const std::string& msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        file_ << currentTimestamp() << " │ " << level << " │ " << msg << "\n";
        file_.flush();
    }
};

// ─── CompositeLogger — délègue à plusieurs loggers ───────────────────────────
class CompositeLogger final : public ILogger {
public:
    void addLogger(std::shared_ptr<ILogger> logger) {
        loggers_.push_back(std::move(logger));
    }

    void info (const std::string& m) override { for (auto& l : loggers_) l->info(m);  }
    void warn (const std::string& m) override { for (auto& l : loggers_) l->warn(m);  }
    void error(const std::string& m) override { for (auto& l : loggers_) l->error(m); }
    void debug(const std::string& m) override { for (auto& l : loggers_) l->debug(m); }

private:
    std::vector<std::shared_ptr<ILogger>> loggers_;
};

// ─── NullLogger — pour les tests ──────────────────────────────────────────────
class NullLogger final : public ILogger {
public:
    void info (const std::string&) override {}
    void warn (const std::string&) override {}
    void error(const std::string&) override {}
    void debug(const std::string&) override {}
};

} // namespace trading
