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
// ════════════════════════════════════════════════════════════
//  Envoi d'alertes réel — webhook local, échec SMTP non bloquant
// ════════════════════════════════════════════════════════════
#include "../support/MiniHttpServer.hpp"

// Le webhook Discord/Slack est réellement POSTé (curl) sur un serveur local
TEST_F(WatchdogIntegration, WebhookDeliveredToLocalServer) {
    MiniHttpServer server(204, "");
    cfg_.webhook_enabled = true;
    cfg_.webhook_url     = server.url("/webhook");
    cfg_.alert_timeout_sec = 3;

    Watchdog wd(cfg_, state_);
    wd.start();                       // jamais de heartbeat → alerte

    ASSERT_TRUE(server.waitForRequests(1, 10'000));
    wd.stop();

    const std::string raw = server.requests()[0];
    EXPECT_NE(raw.find("POST /webhook"),                  std::string::npos);
    EXPECT_NE(raw.find("Content-Type: application/json"), std::string::npos);
    // Payload compatible Discord ("content") ET Slack ("text")
    EXPECT_NE(raw.find("\"content\""),                    std::string::npos);
    EXPECT_NE(raw.find("\"text\""),                       std::string::npos);
    EXPECT_NE(raw.find("SWING BOT ALERTE"),               std::string::npos);
}

// Le SMS Twilio est réellement POSTé (curl, form-urlencoded + Basic auth)
// sur un serveur local — E2E du 2e canal d'alerte. NB : MiniHttpServer ne
// parle pas SMTP, l'email reste couvert par son chemin d'échec ci-dessous.
TEST_F(WatchdogIntegration, SmsDeliveredToLocalServer) {
    MiniHttpServer server(201, "{}");            // Twilio répond 201 Created
    cfg_.sms_enabled     = true;
    cfg_.twilio_sid      = "SIDTEST";
    cfg_.twilio_token    = "tok";
    cfg_.twilio_from     = "+15550001111";
    cfg_.twilio_to       = "+15550002222";
    cfg_.twilio_base_url = server.url("");       // substitue api.twilio.com
    cfg_.alert_timeout_sec = 3;

    Watchdog wd(cfg_, state_);
    wd.start();                       // jamais de heartbeat → alerte

    ASSERT_TRUE(server.waitForRequests(1, 10'000));
    wd.stop();

    const std::string raw = server.requests()[0];
    EXPECT_NE(raw.find("POST /2010-04-01/Accounts/SIDTEST/Messages.json"),
              std::string::npos);
    EXPECT_NE(raw.find("Content-Type: application/x-www-form-urlencoded"),
              std::string::npos);
    // Les « + » des numéros sont urlencodés (%2B) dans le corps
    EXPECT_NE(raw.find("To=%2B15550002222"),   std::string::npos);
    EXPECT_NE(raw.find("From=%2B15550001111"), std::string::npos);
    EXPECT_NE(raw.find("Body="),               std::string::npos);
    // CURLOPT_USERNAME/PASSWORD → Basic préemptif sur http
    EXPECT_NE(raw.find("Authorization: Basic"), std::string::npos);
}

// ════════════════════════════════════════════════════════════
//  E2E email SMTP réel via mock STARTTLS (item 17.1)
// ════════════════════════════════════════════════════════════
#include "../support/MiniSmtpServer.hpp"

namespace {
// Config email complète pointée sur le mock — factorisée pour les 3 tests
void configureEmailOnMock(AlertConfig& cfg, const MiniSmtpServer& smtp) {
    cfg.email_enabled = true;
    cfg.smtp_url      = smtp.url();
    cfg.smtp_user     = "bot";
    cfg.smtp_pass     = "mdp";
    cfg.email_to      = "ops@example.com";
    cfg.email_from    = "bot@example.com";
    cfg.smtp_ca_path  = smtp.caPath();   // seam : racine de confiance du test
    cfg.alert_timeout_sec = 10;
}
}  // namespace

// L'email est réellement LIVRÉ : STARTTLS négocié, AUTH, message capturé —
// le succès SMTP (jusqu'ici testé en échec seulement) est enfin E2E.
TEST_F(WatchdogIntegration, EmailDeliveredViaStartTls) {
    MiniSmtpServer smtp;
    configureEmailOnMock(cfg_, smtp);

    Watchdog wd(cfg_, state_);
    wd.alertNow("🚨 SWING BOT ALERTE — test E2E STARTTLS");

    ASSERT_TRUE(smtp.waitForMessage(10'000));
    EXPECT_TRUE(smtp.tlsEstablished());
    EXPECT_NE(smtp.mailFrom().find("bot@example.com"), std::string::npos);
    ASSERT_FALSE(smtp.rcptTo().empty());
    EXPECT_NE(smtp.rcptTo()[0].find("ops@example.com"), std::string::npos);

    const std::string msg = smtp.message();
    EXPECT_NE(msg.find("To: ops@example.com"),    std::string::npos);
    EXPECT_NE(msg.find("From: bot@example.com"),  std::string::npos);
    EXPECT_NE(msg.find("Subject: [SwingBot]"),    std::string::npos);
    EXPECT_NE(msg.find("SWING BOT ALERTE"),       std::string::npos);
    EXPECT_FALSE(smtp.authLine().empty());        // credentials bien envoyés
}

// Verrou : AUCUN credential ni commande sensible avant le chiffrement —
// AUTH et MAIL FROM ne circulent qu'APRÈS la poignée de main TLS.
TEST_F(WatchdogIntegration, EmailAuthCredentialsSentAfterTlsOnly) {
    MiniSmtpServer smtp;
    configureEmailOnMock(cfg_, smtp);

    Watchdog wd(cfg_, state_);
    wd.alertNow("test confidentialité pré-TLS");

    ASSERT_TRUE(smtp.waitForMessage(10'000));
    EXPECT_TRUE(smtp.authInTls());
    for (const auto& l : smtp.plaintextLines()) {
        EXPECT_EQ(l.find("AUTH"),      std::string::npos) << "en clair : " << l;
        EXPECT_EQ(l.find("MAIL FROM"), std::string::npos) << "en clair : " << l;
    }
}

// Verrou anti-régression du défaut CURLUSESSL_ALL : si le serveur n'annonce
// PAS STARTTLS, rien ne part (aucun repli en clair) et le watchdog survit.
TEST_F(WatchdogIntegration, EmailRefusedWhenServerLacksStartTls) {
    MiniSmtpServer smtp(/*advertiseStartTls=*/false);
    configureEmailOnMock(cfg_, smtp);
    cfg_.alert_timeout_sec = 3;

    std::atomic<int> alerts{0};
    Watchdog wd(cfg_, state_);
    wd.on_alert([&](const std::string&){ alerts++; });
    wd.alertNow("test refus sans STARTTLS");

    EXPECT_EQ(alerts.load(), 1);              // callback appelé, pas de gel
    EXPECT_TRUE(smtp.message().empty());      // rien n'a été livré
    EXPECT_FALSE(smtp.tlsEstablished());      // et rien en TLS non plus
}

// SMTP injoignable (port fermé) : l'envoi échoue proprement,
// le watchdog survit et le callback d'alerte est quand même appelé
TEST_F(WatchdogIntegration, EmailFailureDoesNotBlockWatchdog) {
    cfg_.email_enabled     = true;
    cfg_.smtp_url          = "smtp://127.0.0.1:1";   // rien n'écoute ici
    cfg_.alert_timeout_sec = 2;

    std::atomic<int> alerts{0};
    Watchdog wd(cfg_, state_);
    wd.on_alert([&](const std::string&){ alerts++; });
    wd.start();

    for (int i = 0; i < 100 && alerts.load() == 0; ++i)
        std::this_thread::sleep_for(100ms);
    wd.stop();

    EXPECT_GE(alerts.load(), 1);     // l'échec d'envoi n'a pas gelé la boucle
}
