cmake_minimum_required(VERSION 3.20)
project(SwingBot VERSION 2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Dépendances vcpkg ─────────────────────────────────────
find_package(Boost         REQUIRED COMPONENTS system)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(CURL          REQUIRED)
find_package(GTest         CONFIG REQUIRED)

# SQLite3 : config vcpkg si disponible, sinon paquet système
# (permet de builder/tester hors du conteneur vcpkg, ex. CI Linux)
find_package(unofficial-sqlite3 CONFIG QUIET)
if(unofficial-sqlite3_FOUND)
    set(SQLITE3_TARGET unofficial::sqlite3::sqlite3)
else()
    find_package(SQLite3 REQUIRED)
    set(SQLITE3_TARGET SQLite::SQLite3)
endif()

# ── Sources bot (headers-only + ws_server.cpp) ────────────
file(GLOB_RECURSE BOT_SOURCES
        "${CMAKE_SOURCE_DIR}/include/core/ws_server.cpp"
)

# ── Includes communs ──────────────────────────────────────
set(COMMON_INCLUDES
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/include/bot
        ${CMAKE_SOURCE_DIR}/include/brokers
        ${CMAKE_SOURCE_DIR}/include/core
        ${CMAKE_SOURCE_DIR}/include/indicators
        ${CMAKE_SOURCE_DIR}/include/strategies
        ${CMAKE_SOURCE_DIR}/include/backtest
        ${CMAKE_SOURCE_DIR}/include/models
        ${Boost_INCLUDE_DIRS}
)

# ── Liens communs ─────────────────────────────────────────
set(COMMON_LIBS
        Boost::system
        nlohmann_json::nlohmann_json
        ${SQLITE3_TARGET}
        CURL::libcurl
        $<$<PLATFORM_ID:Windows>:ws2_32>
        $<$<PLATFORM_ID:Windows>:mswsock>
        $<$<PLATFORM_ID:Linux>:pthread>
)

set(GTEST_LIBS GTest::gtest GTest::gtest_main)

# ════════════════════════════════════════════════════════════
#  EXÉCUTABLE PRINCIPAL
# ════════════════════════════════════════════════════════════
add_executable(swing_bot
        main_ibkr.cpp
        ${BOT_SOURCES}
)
target_include_directories(swing_bot PRIVATE ${COMMON_INCLUDES})
target_link_libraries(swing_bot PRIVATE ${COMMON_LIBS})

# ════════════════════════════════════════════════════════════
#  TESTS UNITAIRES
#  gtest_discover_tests → chaque test = processus séparé = 100% indépendant
# ════════════════════════════════════════════════════════════
enable_testing()

add_executable(unit_tests
        tests/unit/test_bot_state_unit.cpp
        tests/unit/test_db_logger_unit.cpp
        tests/unit/test_watchdog_unit.cpp
        tests/unit/test_ws_server_unit.cpp
        tests/unit/test_swing_strategy_unit.cpp
        tests/unit/test_risk_manager_unit.cpp
        tests/unit/test_trading_bot_unit.cpp
        tests/unit/test_state_store_unit.cpp
        tests/unit/test_ibkr_broker_unit.cpp
        tests/unit/test_curl_global_unit.cpp
        tests/unit/test_http_client_unit.cpp
        tests/unit/test_day_indicators_unit.cpp
        tests/unit/test_day_trade_strategy_unit.cpp
        tests/unit/test_indicators_unit.cpp
        tests/unit/test_paper_broker_unit.cpp
        tests/unit/test_csv_data_feed_unit.cpp
        tests/unit/test_backtester_metrics_unit.cpp
        ${BOT_SOURCES}
)
target_include_directories(unit_tests PRIVATE ${COMMON_INCLUDES})
target_link_libraries(unit_tests PRIVATE ${COMMON_LIBS} ${GTEST_LIBS})

include(GoogleTest)
gtest_discover_tests(unit_tests
        TEST_PREFIX "Unit."
        PROPERTIES TIMEOUT 15
)

# ════════════════════════════════════════════════════════════
#  TESTS D'INTÉGRATION
#  gtest_discover_tests → chaque test = processus séparé = 100% indépendant
#  WsServer : port=0 + actual_port() → port unique garanti par processus
# ════════════════════════════════════════════════════════════
add_executable(integration_tests
        tests/integration/test_bot_state_integration.cpp
        tests/integration/test_db_logger_integration.cpp
        tests/integration/test_watchdog_integration.cpp
        tests/integration/test_ws_server_integration.cpp
        tests/integration/test_swing_strategy_integration.cpp
        tests/integration/test_backtester_integration.cpp
        ${BOT_SOURCES}
)
target_include_directories(integration_tests PRIVATE ${COMMON_INCLUDES})
target_link_libraries(integration_tests PRIVATE ${COMMON_LIBS} ${GTEST_LIBS})
# Le golden backtest (item 17) lit QQQ.csv depuis la racine du dépôt,
# indépendamment du répertoire de travail des tests.
target_compile_definitions(integration_tests PRIVATE
        SWINGBOT_QQQ_CSV="${CMAKE_SOURCE_DIR}/QQQ.csv"
)

gtest_discover_tests(integration_tests
        TEST_PREFIX "Integration."
        PROPERTIES TIMEOUT 30
)