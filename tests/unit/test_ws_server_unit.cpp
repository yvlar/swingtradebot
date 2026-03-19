// ============================================================
//  test_ws_server_unit.cpp  —  Tests UNITAIRES
//  Cible : ws_server.h — démarrage, arrêt, état initial
// ============================================================
#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include "core/ws_server.h"
#include "core/bot_state.h"

using namespace std::chrono_literals;

// ── Fixture ───────────────────────────────────────────────
class WsServerUnit : public ::testing::Test {
protected:
    BotState  state_;
    WsServer* server_ = nullptr;
    const unsigned short PORT = 0;

    void SetUp() override {
        state_.equity = 10000.0;
        state_.mode   = "paper";
        server_ = new WsServer(PORT, state_);
        server_->start();
        std::this_thread::sleep_for(80ms);
    }
    void TearDown() override {
        if (server_) { server_->stop(); delete server_; server_ = nullptr; }
        std::this_thread::sleep_for(80ms);
    }
};

TEST_F(WsServerUnit, ServerCreatedSuccessfully) {
    EXPECT_NE(server_, nullptr);
}

TEST_F(WsServerUnit, InitialClientCountIsZero) {
    EXPECT_EQ(server_->client_count(), 0u);
}

TEST_F(WsServerUnit, BroadcastWithNoClientsNoThrow) {
    EXPECT_NO_THROW(server_->broadcast("{\"type\":\"state\"}"));
}

TEST_F(WsServerUnit, BroadcastEmptyStringNoThrow) {
    EXPECT_NO_THROW(server_->broadcast(""));
}

TEST_F(WsServerUnit, StopNoThrow) {
    EXPECT_NO_THROW(server_->stop());
    delete server_; server_ = nullptr;
}

TEST_F(WsServerUnit, DoubleStopNoThrow) {
    EXPECT_NO_THROW(server_->stop());
    EXPECT_NO_THROW(server_->stop());
    delete server_; server_ = nullptr;
}

TEST_F(WsServerUnit, BroadcastLargePayloadNoThrow) {
    // Génère un JSON avec 50 signaux
    std::string big = "{\"type\":\"state\",\"signals\":[";
    for (int i = 0; i < 50; ++i) {
        if (i) big += ",";
        big += "{\"symbol\":\"QQQ\",\"score\":3}";
    }
    big += "]}";
    EXPECT_NO_THROW(server_->broadcast(big));
}
