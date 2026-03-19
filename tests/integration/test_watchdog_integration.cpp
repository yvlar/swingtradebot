// ============================================================
//  test_watchdog_integration.cpp  —  Tests INTÉGRATION
// ============================================================
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <thread>
#include "core/watchdog.h"
#include "core/bot_state.h"

using namespace std::chrono_literals;

class WatchdogIntegration : public ::testing::Test {
protected:
    BotState    state_;
    AlertConfig cfg_;

    void SetUp() override {
        cfg_.email_enabled          = false;
        cfg_.sms_enabled            = false;
        cfg_.webhook_enabled        = false;
        cfg_.heartbeat_interval_sec = 1;
        cfg_.max_silence_sec        = 2;
    }
};

TEST_F(WatchdogIntegration, AlertFiredOnTimeout) {
    std::atomic<int> alert_count{0};
    Watchdog wd(cfg_, state_);
    wd.on_alert([&](const std::string&){ alert_count++; });
    wd.start();
    std::this_thread::sleep_for(4s);   // marge suffisante en Docker
    wd.stop();
    EXPECT_GE(alert_count.load(), 1);
}

TEST_F(WatchdogIntegration, NoAlertWhenHeartbeating) {
    std::atomic<int> alert_count{0};
    Watchdog wd(cfg_, state_);
    wd.on_alert([&](const std::string&){ alert_count++; });
    wd.start();
    for (int i = 0; i < 8; ++i) {
        wd.heartbeat();
        std::this_thread::sleep_for(400ms);
    }
    wd.stop();
    EXPECT_EQ(alert_count.load(), 0);
}

TEST_F(WatchdogIntegration, AlertMessageContainsBotInfo) {
    std::string captured;
    state_.equity = 12500.0;
    state_.cycle  = 7;
    state_.mode   = "paper";
    Watchdog wd(cfg_, state_);
    wd.on_alert([&](const std::string& msg){ captured = msg; });
    wd.start();
    std::this_thread::sleep_for(4s);
    wd.stop();
    EXPECT_NE(captured.find("12500"), std::string::npos);
    EXPECT_NE(captured.find("paper"), std::string::npos);
}

TEST_F(WatchdogIntegration, AlertLoggedInBotState) {
    Watchdog wd(cfg_, state_);
    wd.start();
    std::this_thread::sleep_for(4s);
    wd.stop();
    bool found = false;
    for (const auto& log : state_.logs)
        if (log.level == "ERROR" && log.msg.find("WATCHDOG") != std::string::npos)
            { found = true; break; }
    EXPECT_TRUE(found);
}

TEST_F(WatchdogIntegration, RecoveryAfterSilence) {
    std::atomic<int> alert_count{0};
    Watchdog wd(cfg_, state_);
    wd.on_alert([&](const std::string&){ alert_count++; });
    wd.start();

    // 1. Laisse timeout
    std::this_thread::sleep_for(4s);
    EXPECT_GE(alert_count.load(), 1);
    int before = alert_count.load();

    // 2. Heartbeats réguliers — fenêtre large pour Docker
    for (int i = 0; i < 8; ++i) {
        wd.heartbeat();
        std::this_thread::sleep_for(300ms);
    }
    int after = alert_count.load();
    wd.stop();

    EXPECT_EQ(before, after); // pas de nouvelles alertes
}

TEST_F(WatchdogIntegration, ConcurrentHeartbeatsNoRace) {
    Watchdog wd(cfg_, state_);
    wd.start();
    std::vector<std::thread> threads;
    for (int t = 0; t < 5; ++t)
        threads.emplace_back([&wd](){
            for (int i = 0; i < 10; ++i) {
                wd.heartbeat();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
    for (auto& th : threads) th.join();
    wd.stop();
    SUCCEED();
}

TEST_F(WatchdogIntegration, AlertCallbackNotCalledConcurrently) {
    std::atomic<int> concurrent{0};
    std::atomic<int> max_concurrent{0};
    Watchdog wd(cfg_, state_);
    wd.on_alert([&](const std::string&){
        int c = ++concurrent;
        int expected = max_concurrent.load();
        while (c > expected && !max_concurrent.compare_exchange_weak(expected, c)) {}
        std::this_thread::sleep_for(50ms);
        --concurrent;
    });
    wd.start();
    std::this_thread::sleep_for(5s);
    wd.stop();
    EXPECT_EQ(max_concurrent.load(), 1);
}