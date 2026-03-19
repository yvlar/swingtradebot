// ============================================================
//  test_bot_state_unit.cpp  —  Tests UNITAIRES uniquement
//  Cible : bot_state.h (structs, JSON, logs)
// ============================================================
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include "core/bot_state.h"

using json = nlohmann::json;

// ════════════════════════════════════════════════════════════
//  SignalData
// ════════════════════════════════════════════════════════════

TEST(SignalDataUnit, DefaultValues) {
    SignalData s;
    EXPECT_EQ(s.score, 0);
    EXPECT_DOUBLE_EQ(s.rsi, 0.0);
    EXPECT_DOUBLE_EQ(s.entry_price, 0.0);
}

TEST(SignalDataUnit, SerializationRoundTrip) {
    SignalData s{"QQQ","LONG",3,28.5,472.30,2.85,466.60,483.70};
    SignalData s2 = json(s).get<SignalData>();
    EXPECT_EQ(s2.symbol,    "QQQ");
    EXPECT_EQ(s2.direction, "LONG");
    EXPECT_EQ(s2.score,     3);
    EXPECT_DOUBLE_EQ(s2.rsi,         28.5);
    EXPECT_DOUBLE_EQ(s2.entry_price, 472.30);
    EXPECT_DOUBLE_EQ(s2.stop_loss,   466.60);
    EXPECT_DOUBLE_EQ(s2.take_profit, 483.70);
}

TEST(SignalDataUnit, AllDirections) {
    for (const auto& dir : {"LONG", "SHORT", "FLAT"}) {
        SignalData s; s.direction = dir;
        EXPECT_EQ(json(s)["direction"], dir);
    }
}

// ════════════════════════════════════════════════════════════
//  PositionData
// ════════════════════════════════════════════════════════════

TEST(PositionDataUnit, SerializationRoundTrip) {
    PositionData p{"QQQ","long",10,472.30,466.60,483.70,475.00,27.0,0.57};
    PositionData p2 = json(p).get<PositionData>();
    EXPECT_EQ(p2.symbol, "QQQ");
    EXPECT_EQ(p2.side,   "long");
    EXPECT_EQ(p2.qty,    10);
    EXPECT_DOUBLE_EQ(p2.pnl,     27.0);
    EXPECT_DOUBLE_EQ(p2.pnl_pct, 0.57);
}

TEST(PositionDataUnit, NegativePnl) {
    PositionData p; p.pnl = -150.0; p.pnl_pct = -1.5;
    PositionData p2 = json(p).get<PositionData>();
    EXPECT_DOUBLE_EQ(p2.pnl,     -150.0);
    EXPECT_DOUBLE_EQ(p2.pnl_pct, -1.5);
}

// ════════════════════════════════════════════════════════════
//  LogEntry
// ════════════════════════════════════════════════════════════

TEST(LogEntryUnit, SerializationRoundTrip) {
    LogEntry l{"INFO", "Signal QQQ LONG score=3", "14:32:01"};
    LogEntry l2 = json(l).get<LogEntry>();
    EXPECT_EQ(l2.level, "INFO");
    EXPECT_EQ(l2.msg,   "Signal QQQ LONG score=3");
    EXPECT_EQ(l2.ts,    "14:32:01");
}

TEST(LogEntryUnit, SpecialCharsInMessage) {
    LogEntry l{"ERROR", "Equity: $10,000.00 — PnL: +$27.50", "09:00:00"};
    EXPECT_EQ(json(l).get<LogEntry>().msg, l.msg);
}

TEST(LogEntryUnit, AllLevels) {
    for (const auto& level : {"INFO","WARN","ERROR","OK"}) {
        LogEntry l{level, "msg", "00:00:00"};
        EXPECT_EQ(json(l)["level"], level);
    }
}

// ════════════════════════════════════════════════════════════
//  BotState — état initial
// ════════════════════════════════════════════════════════════

TEST(BotStateUnit, InitialValues) {
    BotState st;
    EXPECT_DOUBLE_EQ(st.equity,  10000.0);
    EXPECT_DOUBLE_EQ(st.pnl,     0.0);
    EXPECT_DOUBLE_EQ(st.pnl_pct, 0.0);
    EXPECT_EQ(st.cycle,   0);
    EXPECT_FALSE(st.running);
    EXPECT_TRUE(st.dry_run);
    EXPECT_EQ(st.mode,  "paper");
    EXPECT_EQ(st.stage, PipelineStage::IDLE);
}

TEST(BotStateUnit, PushLogAddsEntry) {
    BotState st;
    st.push_log("INFO", "Test message");
    ASSERT_EQ(st.logs.size(), 1u);
    EXPECT_EQ(st.logs[0].level, "INFO");
    EXPECT_EQ(st.logs[0].msg,   "Test message");
}

TEST(BotStateUnit, PushLogTimestampNotEmpty) {
    BotState st;
    st.push_log("OK", "msg");
    EXPECT_FALSE(st.logs[0].ts.empty());
}

TEST(BotStateUnit, LogCircularBufferMax100) {
    BotState st;
    for (int i = 0; i < 110; ++i)
        st.push_log("INFO", "msg " + std::to_string(i));
    EXPECT_EQ(st.logs.size(), 100u);
    EXPECT_EQ(st.logs[0].msg, "msg 10"); // les 10 premiers supprimés
}

TEST(BotStateUnit, SetStageUpdatesField) {
    BotState st;
    st.set_stage(PipelineStage::SIGNAL);
    EXPECT_EQ(st.stage, PipelineStage::SIGNAL);
    st.set_stage(PipelineStage::IDLE);
    EXPECT_EQ(st.stage, PipelineStage::IDLE);
}

// ════════════════════════════════════════════════════════════
//  BotState — sérialisation JSON
// ════════════════════════════════════════════════════════════

TEST(BotStateJsonUnit, ContainsAllTopLevelFields) {
    BotState st;
    json j = json::parse(st.to_json_str());
    EXPECT_TRUE(j.contains("type"));
    EXPECT_TRUE(j.contains("equity"));
    EXPECT_TRUE(j.contains("pnl"));
    EXPECT_TRUE(j.contains("pnl_pct"));
    EXPECT_TRUE(j.contains("cycle"));
    EXPECT_TRUE(j.contains("stage"));
    EXPECT_TRUE(j.contains("running"));
    EXPECT_TRUE(j.contains("dry_run"));
    EXPECT_TRUE(j.contains("mode"));
    EXPECT_TRUE(j.contains("signals"));
    EXPECT_TRUE(j.contains("positions"));
    EXPECT_TRUE(j.contains("logs"));
}

TEST(BotStateJsonUnit, TypeFieldIsState) {
    BotState st;
    json j = json::parse(st.to_json_str());
    EXPECT_EQ(j["type"], "state");
}

TEST(BotStateJsonUnit, EquityReflected) {
    BotState st; st.equity = 12500.0;
    json j = json::parse(st.to_json_str());
    EXPECT_DOUBLE_EQ(j["equity"].get<double>(), 12500.0);
}

TEST(BotStateJsonUnit, EmptyArraysWhenNoData) {
    BotState st;
    json j = json::parse(st.to_json_str());
    EXPECT_TRUE(j["signals"].empty());
    EXPECT_TRUE(j["positions"].empty());
    EXPECT_TRUE(j["logs"].empty());
}

TEST(BotStateJsonUnit, StageSerializedAsInt) {
    BotState st;
    st.set_stage(PipelineStage::RISK);   // RISK = 2
    json j = json::parse(st.to_json_str());
    EXPECT_EQ(j["stage"].get<int>(), 2);
}

TEST(BotStateJsonUnit, LogsLimitedTo30InOutput) {
    BotState st;
    for (int i = 0; i < 50; ++i)
        st.push_log("INFO", "log " + std::to_string(i));
    json j = json::parse(st.to_json_str());
    EXPECT_LE(j["logs"].size(), 30u);
}

TEST(BotStateJsonUnit, MultipleSignalsAndPositions) {
    BotState st;
    st.signals.push_back({"QQQ","LONG", 3,28.5,472.30,2.85,466.60,483.70});
    st.signals.push_back({"SPY","SHORT",2,71.0,520.00,3.10,526.20,507.60});
    st.positions.push_back({"QQQ","long",10,472.30,466.60,483.70,475.00,27.0,0.57});
    json j = json::parse(st.to_json_str());
    EXPECT_EQ(j["signals"].size(),   2u);
    EXPECT_EQ(j["positions"].size(), 1u);
    EXPECT_EQ(j["signals"][0]["symbol"], "QQQ");
    EXPECT_EQ(j["signals"][1]["symbol"], "SPY");
}

TEST(BotStateJsonUnit, OutputIsValidJson) {
    BotState st;
    st.signals.push_back({"QQQ","LONG",3,28.5,472.30,2.85,466.60,483.70});
    EXPECT_NO_THROW(json::parse(st.to_json_str()));
}
