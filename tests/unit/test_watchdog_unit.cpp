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

// ── alertNow (A6) — envoi immédiat hors détection de silence ──
TEST_F(WatchdogUnit, AlertNowTriggersCallbackWithoutStart) {
    Watchdog wd(cfg_, state_);
    std::string received;
    wd.on_alert([&](const std::string& m) { received = m; });

    wd.alertNow("KILL-SWITCH : drawdown journalier");

    EXPECT_EQ(received, "KILL-SWITCH : drawdown journalier");
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

// ── Concurrence (Sprint 2, item 6) ────────────────────────

// Le timeout curl des alertes doit exister et être strictement positif :
// sans lui, un envoi SMTP/SMS/webhook bloqué gèlerait le watchdog lui-même.
TEST_F(WatchdogUnit, DefaultAlertTimeoutPositive) {
    AlertConfig c;
    EXPECT_GT(c.alert_timeout_sec, 0);
}

// Stress : heartbeats martelés depuis le thread principal pendant que le
// thread watchdog lit last_heartbeat_ et BotState. Avant le fix (item 6),
// ces accès étaient des data races (UB) — détectables sous TSan.
// Le bot étant vivant en continu, aucune alerte ne doit partir.
TEST_F(WatchdogUnit, ConcurrentHeartbeatsWhileMonitoringNoAlert) {
    std::atomic<int> alerts{0};
    Watchdog wd(cfg_, state_);
    wd.on_alert([&](const std::string&){ alerts++; });
    wd.start();

    auto deadline = std::chrono::steady_clock::now() + 1500ms;
    while (std::chrono::steady_clock::now() < deadline) {
        wd.heartbeat();
        // Écritures concurrentes sur BotState pendant que loop_() peut le lire
        state_.push_log("INFO", "tick");
        std::this_thread::sleep_for(5ms);
    }
    wd.stop();
    EXPECT_EQ(alerts.load(), 0);
}
