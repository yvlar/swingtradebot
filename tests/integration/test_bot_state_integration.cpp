// ============================================================
//  test_bot_state_integration.cpp  —  Tests INTÉGRATION
//  Cible : bot_state.h — thread-safety multi-thread
// ============================================================
#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include <nlohmann/json.hpp>
#include "core/bot_state.h"

using json = nlohmann::json;

TEST(BotStateIntegration, ConcurrentPushLog) {
    BotState st;
    constexpr int N_THREADS       = 8;
    constexpr int LOGS_PER_THREAD = 20;
    std::vector<std::thread> threads;

    for (int t = 0; t < N_THREADS; ++t) {
        threads.emplace_back([&st, t](){
            for (int i = 0; i < LOGS_PER_THREAD; ++i)
                st.push_log("INFO", "t=" + std::to_string(t)
                            + " i=" + std::to_string(i));
        });
    }
    for (auto& th : threads) th.join();

    // Aucun crash + taille plafonnée à 100
    EXPECT_LE(st.logs.size(), 100u);
}

TEST(BotStateIntegration, ConcurrentSetStage) {
    BotState st;
    std::vector<std::thread> threads;

    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&st, t](){
            for (int i = 0; i < 50; ++i)
                st.set_stage(static_cast<PipelineStage>(i % 4 - 1));
        });
    }
    for (auto& th : threads) th.join();
    SUCCEED(); // aucun crash = mutex fonctionnel
}

TEST(BotStateIntegration, ConcurrentToJsonStr) {
    BotState st;
    st.equity = 10000.0;
    st.signals.push_back({"QQQ","LONG",3,28.5,472.30,2.85,466.60,483.70});

    std::atomic<int> errors{0};
    std::vector<std::thread> threads;

    for (int t = 0; t < 6; ++t) {
        threads.emplace_back([&st, &errors](){
            for (int i = 0; i < 20; ++i) {
                try {
                    auto j = json::parse(st.to_json_str()); // doit être valide
                    (void)j;
                } catch (...) { errors++; }
            }
        });
    }
    for (auto& th : threads) th.join();
    EXPECT_EQ(errors.load(), 0);
}

TEST(BotStateIntegration, ConcurrentReadWriteMixed) {
    // Simule le bot : un thread écrit, un thread broadcast (lit)
    BotState st;
    std::atomic<bool> stop{false};
    std::atomic<int>  parse_errors{0};

    std::thread writer([&](){
        for (int i = 0; i < 100 && !stop; ++i) {
            st.push_log("INFO", "cycle " + std::to_string(i));
            st.set_stage(static_cast<PipelineStage>(i % 4 - 1));
            {
                std::lock_guard<std::mutex> lk(st.mtx);
                st.equity = 10000.0 + i * 10;
                st.cycle  = i;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        stop = true;
    });

    std::thread reader([&](){
        while (!stop) {
            try { auto j = json::parse(st.to_json_str()); (void)j; }
            catch (...) { parse_errors++; }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });

    writer.join();
    reader.join();
    EXPECT_EQ(parse_errors.load(), 0);
}