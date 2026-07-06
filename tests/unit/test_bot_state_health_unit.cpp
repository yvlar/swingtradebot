// ============================================================
//  test_bot_state_health_unit.cpp  —  Tests UNITAIRES
//  Cible : BotState — instantané de santé opérationnelle (bot_state.h, item 15.1)
//  Le seam `now_epoch` injecté rend l'âge du dernier cycle sain déterministe
//  (aucune horloge murale). Suite nommée BotStateHealthUnit → couverte par le
//  job TSan ciblé (regex `WsServer|Watchdog|BotState`).
// ============================================================
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <vector>
#include "core/bot_state.h"

using json = nlohmann::json;

// ════════════════════════════════════════════════════════════
//  Défauts : aucun cycle sain encore → tout « malsain », âge -1
// ════════════════════════════════════════════════════════════
TEST(BotStateHealthUnit, DefaultsAreUnhealthy) {
    BotState st;
    const json h = st.health_json(/*now=*/1'000'000);
    EXPECT_FALSE(h["last_cycle_healthy"].get<bool>());
    EXPECT_EQ(h["last_healthy_cycle_epoch"].get<long>(), 0);
    EXPECT_EQ(h["seconds_since_healthy_cycle"].get<long>(), -1);   // jamais sain
    EXPECT_FALSE(h["gateway_authenticated"].get<bool>());
    EXPECT_FALSE(h["kill_switch_tripped"].get<bool>());
    EXPECT_EQ(h["kill_switch_reason"].get<std::string>(), "");
}

// ════════════════════════════════════════════════════════════
//  set_health reflété + âge dérivé du now injecté
// ════════════════════════════════════════════════════════════
TEST(BotStateHealthUnit, ReflectsSetHealthAndDerivesAge) {
    BotState st;
    st.set_health(/*cycle_healthy=*/true, /*healthy_epoch=*/1'000, /*gw_auth=*/true,
                  /*ks_tripped=*/false, /*ks_reason=*/"");
    const json h = st.health_json(/*now=*/1'042);
    EXPECT_TRUE(h["last_cycle_healthy"].get<bool>());
    EXPECT_EQ(h["last_healthy_cycle_epoch"].get<long>(), 1'000);
    EXPECT_EQ(h["seconds_since_healthy_cycle"].get<long>(), 42);
    EXPECT_TRUE(h["gateway_authenticated"].get<bool>());
}

// ════════════════════════════════════════════════════════════
//  Le cas « silencieusement malsain » : le process TOURNE (dernier cycle sain à
//  t=1000) mais aucun cycle sain depuis longtemps → l'âge grandit, même si le
//  HEALTHCHECK TCP verrait un process vivant.
// ════════════════════════════════════════════════════════════
TEST(BotStateHealthUnit, StaleHealthyCycleSurfacesLargeAge) {
    BotState st;
    st.set_health(/*cycle_healthy=*/false, /*healthy_epoch=*/1'000, /*gw_auth=*/true,
                  false, "");
    const json h = st.health_json(/*now=*/6'000);
    EXPECT_FALSE(h["last_cycle_healthy"].get<bool>());        // dernier cycle KO
    EXPECT_EQ(h["seconds_since_healthy_cycle"].get<long>(), 5'000);  // 83 min sans cycle sain
}

// ════════════════════════════════════════════════════════════
//  Le kill-switch armé remonte avec sa raison
// ════════════════════════════════════════════════════════════
TEST(BotStateHealthUnit, KillSwitchReasonSurfaces) {
    BotState st;
    st.set_health(false, 1'000, true, /*ks_tripped=*/true,
                  /*ks_reason=*/"drawdown journalier max atteint");
    const json h = st.health_json(2'000);
    EXPECT_TRUE(h["kill_switch_tripped"].get<bool>());
    EXPECT_NE(h["kill_switch_reason"].get<std::string>().find("drawdown"),
              std::string::npos);
}

// ════════════════════════════════════════════════════════════
//  Gateway dé-authentifié remonte
// ════════════════════════════════════════════════════════════
TEST(BotStateHealthUnit, GatewayDeAuthSurfaces) {
    BotState st;
    st.set_health(/*cycle_healthy=*/true, 1'000, /*gw_auth=*/false, false, "");
    EXPECT_FALSE(st.health_json(1'010)["gateway_authenticated"].get<bool>());
}

// ════════════════════════════════════════════════════════════
//  Dérive d'horloge (now < timestamp) → âge borné à 0, jamais négatif
// ════════════════════════════════════════════════════════════
TEST(BotStateHealthUnit, AgeClampsToZeroOnClockSkew) {
    BotState st;
    st.set_health(true, /*healthy_epoch=*/2'000, true, false, "");
    EXPECT_EQ(st.health_json(/*now=*/1'000)["seconds_since_healthy_cycle"].get<long>(), 0);
}

// ════════════════════════════════════════════════════════════
//  to_json_str embarque l'objet « health » avec toutes ses sous-clés
// ════════════════════════════════════════════════════════════
TEST(BotStateHealthUnit, ToJsonStrContainsHealthObject) {
    BotState st;
    st.set_health(true, 1'000, true, false, "");
    const json j = json::parse(st.to_json_str());
    ASSERT_TRUE(j.contains("health"));
    const json& h = j["health"];
    EXPECT_TRUE(h.contains("last_cycle_healthy"));
    EXPECT_TRUE(h.contains("seconds_since_healthy_cycle"));
    EXPECT_TRUE(h.contains("gateway_authenticated"));
    EXPECT_TRUE(h.contains("kill_switch_tripped"));
    EXPECT_TRUE(h.contains("kill_switch_reason"));
}

// ════════════════════════════════════════════════════════════
//  Thread-safety : écritures set_health concurrentes + lectures health_json /
//  to_json_str, sans data race (le job TSan exerce cette suite).
// ════════════════════════════════════════════════════════════
TEST(BotStateHealthUnit, ConcurrentHealthAccessNoRace) {
    BotState st;
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t)
        threads.emplace_back([&st, t]() {
            for (int i = 0; i < 200; ++i) {
                st.set_health(i % 2 == 0, 1'000 + i, t % 2 == 0, i % 3 == 0, "r");
                (void)st.health_json(2'000 + i);
                (void)st.to_json_str();
            }
        });
    for (auto& th : threads) th.join();
    SUCCEED();
}
