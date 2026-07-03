// ============================================================
//  test_watchdog_unit.cpp  —  Tests UNITAIRES
//  Cible : watchdog.h — config, heartbeat, start/stop
// ============================================================
#include <gtest/gtest.h>
#include <chrono>
#include <map>
#include <string>
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

// ── alertConfigFromEnv (A4) — secrets par environnement ──
namespace {
// Lecteur d'environnement simulé : map figée, injectée dans le loader
EnvReader fakeEnv(std::map<std::string, std::string> vars) {
    static std::map<std::string, std::string> store;
    store = std::move(vars);
    return [](const char* k) -> const char* {
        auto it = store.find(k);
        return it == store.end() ? nullptr : it->second.c_str();
    };
}
} // namespace

TEST_F(WatchdogUnit, EmptyEnvDisablesAllChannels) {
    auto c = alertConfigFromEnv(fakeEnv({}));
    EXPECT_FALSE(c.email_enabled);
    EXPECT_FALSE(c.sms_enabled);
    EXPECT_FALSE(c.webhook_enabled);
    EXPECT_FALSE(anyAlertChannelEnabled(c));
}

TEST_F(WatchdogUnit, CompleteTwilioSetEnablesSmsOnly) {
    auto c = alertConfigFromEnv(fakeEnv({
        {"SWINGBOT_TWILIO_SID",   "SIDTEST"},
        {"SWINGBOT_TWILIO_TOKEN", "tok"},
        {"SWINGBOT_TWILIO_FROM",  "+15550001111"},
        {"SWINGBOT_TWILIO_TO",    "+15550002222"},
    }));
    EXPECT_TRUE(c.sms_enabled);
    EXPECT_EQ(c.twilio_sid, "SIDTEST");
    EXPECT_FALSE(c.email_enabled);
    EXPECT_FALSE(c.webhook_enabled);
    EXPECT_TRUE(anyAlertChannelEnabled(c));
}

// Jeu SMTP incomplet (PASS manquant) : le canal reste DÉSACTIVÉ — un canal
// à moitié configuré enverrait silencieusement dans le vide
TEST_F(WatchdogUnit, IncompleteSmtpSetKeepsEmailDisabled) {
    auto c = alertConfigFromEnv(fakeEnv({
        {"SWINGBOT_SMTP_URL",   "smtps://smtp.example.com:465"},
        {"SWINGBOT_SMTP_USER",  "bot@example.com"},
        {"SWINGBOT_EMAIL_TO",   "ops@example.com"},
        {"SWINGBOT_EMAIL_FROM", "bot@example.com"},
    }));
    EXPECT_FALSE(c.email_enabled);
    EXPECT_FALSE(anyAlertChannelEnabled(c));
}

TEST_F(WatchdogUnit, WebhookUrlAloneEnablesWebhook) {
    auto c = alertConfigFromEnv(fakeEnv({
        {"SWINGBOT_WEBHOOK_URL", "https://discord.example/webhook"},
    }));
    EXPECT_TRUE(c.webhook_enabled);
    EXPECT_TRUE(anyAlertChannelEnabled(c));
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
