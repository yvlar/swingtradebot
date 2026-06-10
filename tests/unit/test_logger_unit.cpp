// ============================================================
//  test_logger_unit.cpp  —  Tests UNITAIRES
//  Cible : Logger.hpp — ConsoleLogger, FileLogger,
//          CompositeLogger, NullLogger, currentTimestamp
// ============================================================
#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include "bot/Logger.hpp"

using namespace trading;
namespace fs = std::filesystem;

namespace {

// Logger enregistreur pour vérifier la délégation du CompositeLogger
class RecordingLogger final : public ILogger {
public:
    std::vector<std::string> entries;
    void info (const std::string& m) override { entries.push_back("INFO:"  + m); }
    void warn (const std::string& m) override { entries.push_back("WARN:"  + m); }
    void error(const std::string& m) override { entries.push_back("ERROR:" + m); }
    void debug(const std::string& m) override { entries.push_back("DEBUG:" + m); }
};

} // namespace

// ════════════════════════════════════════════════════════════
//  currentTimestamp
// ════════════════════════════════════════════════════════════

TEST(LoggerUnit, TimestampHasIsoLikeFormat) {
    // "YYYY-MM-DD HH:MM:SS"
    std::regex fmt(R"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})");
    EXPECT_TRUE(std::regex_match(currentTimestamp(), fmt));
}

// ════════════════════════════════════════════════════════════
//  ConsoleLogger — niveaux et flag verbose
// ════════════════════════════════════════════════════════════

TEST(LoggerUnit, ConsoleLoggerWritesLevelsToStdout) {
    ConsoleLogger log;

    testing::internal::CaptureStdout();
    log.info ("message info");
    log.warn ("message warn");
    log.error("message error");
    std::string out = testing::internal::GetCapturedStdout();

    EXPECT_NE(out.find("INFO"),          std::string::npos);
    EXPECT_NE(out.find("message info"),  std::string::npos);
    EXPECT_NE(out.find("WARN"),          std::string::npos);
    EXPECT_NE(out.find("message warn"),  std::string::npos);
    EXPECT_NE(out.find("ERROR"),         std::string::npos);
    EXPECT_NE(out.find("message error"), std::string::npos);
}

TEST(LoggerUnit, ConsoleLoggerDebugSilentUnlessVerbose) {
    ConsoleLogger log;

    testing::internal::CaptureStdout();
    log.debug("invisible");
    std::string silencieux = testing::internal::GetCapturedStdout();
    EXPECT_TRUE(silencieux.empty());

    log.setVerbose(true);
    testing::internal::CaptureStdout();
    log.debug("visible");
    std::string verbeux = testing::internal::GetCapturedStdout();
    EXPECT_NE(verbeux.find("DEBUG"),   std::string::npos);
    EXPECT_NE(verbeux.find("visible"), std::string::npos);
}

// ════════════════════════════════════════════════════════════
//  FileLogger — écriture sur disque
// ════════════════════════════════════════════════════════════

class FileLoggerUnit : public ::testing::Test {
protected:
    std::string path_;

    void SetUp() override {
        path_ = "unit_log_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) + ".log";
    }
    void TearDown() override {
        if (fs::exists(path_)) fs::remove(path_);
    }

    std::string fileContent() const {
        std::ifstream f(path_);
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }
};

TEST_F(FileLoggerUnit, AllLevelsWrittenToFile) {
    FileLogger log(path_);
    log.info ("ligne info");
    log.warn ("ligne warn");
    log.error("ligne error");
    log.debug("ligne debug");

    std::string content = fileContent();
    EXPECT_NE(content.find("INFO"),        std::string::npos);
    EXPECT_NE(content.find("ligne info"),  std::string::npos);
    EXPECT_NE(content.find("WARN"),        std::string::npos);
    EXPECT_NE(content.find("ERROR"),       std::string::npos);
    EXPECT_NE(content.find("DEBUG"),       std::string::npos);
    EXPECT_NE(content.find("ligne debug"), std::string::npos);
}

// Mode append : une nouvelle instance ne doit pas écraser le fichier
TEST_F(FileLoggerUnit, ReopeningAppendsInsteadOfTruncating) {
    { FileLogger log(path_); log.info("premiere session"); }
    { FileLogger log(path_); log.info("seconde session");  }

    std::string content = fileContent();
    EXPECT_NE(content.find("premiere session"), std::string::npos);
    EXPECT_NE(content.find("seconde session"),  std::string::npos);
}

// ════════════════════════════════════════════════════════════
//  CompositeLogger — délégation en éventail
// ════════════════════════════════════════════════════════════

TEST(LoggerUnit, CompositeForwardsToAllLoggers) {
    auto a = std::make_shared<RecordingLogger>();
    auto b = std::make_shared<RecordingLogger>();

    CompositeLogger comp;
    comp.addLogger(a);
    comp.addLogger(b);

    comp.info ("i");
    comp.warn ("w");
    comp.error("e");
    comp.debug("d");

    const std::vector<std::string> attendu =
        {"INFO:i", "WARN:w", "ERROR:e", "DEBUG:d"};
    EXPECT_EQ(a->entries, attendu);
    EXPECT_EQ(b->entries, attendu);
}

TEST(LoggerUnit, EmptyCompositeIsNoop) {
    CompositeLogger comp;
    EXPECT_NO_THROW(comp.info("personne n'ecoute"));
}

// ════════════════════════════════════════════════════════════
//  NullLogger — silencieux par contrat
// ════════════════════════════════════════════════════════════

TEST(LoggerUnit, NullLoggerWritesNothing) {
    NullLogger log;

    testing::internal::CaptureStdout();
    log.info ("a");
    log.warn ("b");
    log.error("c");
    log.debug("d");
    EXPECT_TRUE(testing::internal::GetCapturedStdout().empty());
}
