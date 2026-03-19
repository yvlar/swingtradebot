// ============================================================
//  test_watchdog_unit.cpp  —  Tests UNITAIRES
//  Cible : watchdog.h — config, heartbeat, start/stop
// ============================================================
#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include "core/watchdog.h"
#include "core/bot_state.h"

using namespace std::chrono_literals;

// ── Fixture ───────────────────────────────────────────────
class WatchdogUnit : public ::testing::Test {
protected:
    BotState    state_;
    AlertConfig cfg_;

    void SetUp() override {
        cfg_.email_enabled   = false;
        cfg_.sms_enabled     = false;
        cfg_.webhook_enabled = false;
        cfg_.heartbeat_interval_sec = 1;
        cfg_.max_silence_sec        = 2;
    }
};

// ── AlertConfig ───────────────────────────────────────────
TEST_F(WatchdogUnit, DefaultConfigAllDisabled) {
    AlertConfig c;
    EXPECT_FALSE(c.email_enabled);
    EXPECT_FALSE(c.sms_enabled);
    EXPECT_FALSE(c.webhook_enabled);
}

TEST_F(WatchdogUnit, DefaultTimingsCoherent) {
    AlertConfig c;
    EXPECT_GT(c.heartbeat_interval_sec, 0);
    EXPECT_GT(c.max_silence_sec, c.heartbeat_interval_sec);
}

// ── Heartbeat ─────────────────────────────────────────────
TEST_F(WatchdogUnit, HeartbeatNoThrow) {
    Watchdog wd(cfg_, state_);
    for (int i = 0; i < 10; ++i)
        EXPECT_NO_THROW(wd.heartbeat());
}

TEST_F(WatchdogUnit, HeartbeatBeforeStartNoThrow) {
    Watchdog wd(cfg_, state_);
    EXPECT_NO_THROW(wd.heartbeat()); // avant start()
}

// ── Start / Stop ──────────────────────────────────────────
TEST_F(WatchdogUnit, StartStopNoThrow) {
    Watchdog wd(cfg_, state_);
    EXPECT_NO_THROW(wd.start());
    std::this_thread::sleep_for(50ms);
    EXPECT_NO_THROW(wd.stop());
}

TEST_F(WatchdogUnit, StopWithoutStartNoThrow) {
    Watchdog wd(cfg_, state_);
    EXPECT_NO_THROW(wd.stop());
}

TEST_F(WatchdogUnit, DoubleStopNoThrow) {
    Watchdog wd(cfg_, state_);
    wd.start();
    EXPECT_NO_THROW(wd.stop());
    EXPECT_NO_THROW(wd.stop());
}

// ── Callback ──────────────────────────────────────────────
TEST_F(WatchdogUnit, OnAlertCallbackRegistered) {
    Watchdog wd(cfg_, state_);
    bool set = false;
    EXPECT_NO_THROW(wd.on_alert([&](const std::string&){ set = true; }));
}
