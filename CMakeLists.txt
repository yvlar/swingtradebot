cmake_minimum_required(VERSION 3.20)
project(SwingBot VERSION 2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Durcissement (Sprint Sécurité-Réel, passe 3) : zéro warning toléré —
# l'argent réel ne se code pas avec des avertissements ignorés.
if(NOT MSVC)
    add_compile_options(-Wall -Wextra -Werror)
endif()

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
# Config de production externalisée (item 9.1) : chemin injecté à la
# compilation — même fichier pour la prod ET le golden d'intégration.
target_compile_definitions(swing_bot PRIVATE
        SWINGBOT_PROD_CONFIG_JSON="${CMAKE_SOURCE_DIR}/config/prod.json"
)

# ════════════════════════════════════════════════════════════
#  HARNAIS DE VALIDATION (Sprint 7) — walk-forward / grille / Monte-Carlo
#  Sortie humaine pour inspecter l'edge ; les chiffres sont verrouillés par
#  les tests d'intégration. Lance : ./build/validate
# ════════════════════════════════════════════════════════════
add_executable(validate
        main_validate.cpp
        ${BOT_SOURCES}
)
target_include_directories(validate PRIVATE ${COMMON_INCLUDES})
target_link_libraries(validate PRIVATE ${COMMON_LIBS})
target_compile_definitions(validate PRIVATE
        SWINGBOT_QQQ_CSV="${CMAKE_SOURCE_DIR}/QQQ.csv"
        SWINGBOT_SPY_CSV="${CMAKE_SOURCE_DIR}/SPY.csv"
        SWINGBOT_IWM_CSV="${CMAKE_SOURCE_DIR}/IWM.csv"
        SWINGBOT_MDY_CSV="${CMAKE_SOURCE_DIR}/MDY.csv"
        # Données longues (item 8d.1) : historique max ~1999+, dot-com/2008 inclus
        SWINGBOT_QQQ_MAX_CSV="${CMAKE_SOURCE_DIR}/QQQ_max.csv"
        SWINGBOT_SPY_MAX_CSV="${CMAKE_SOURCE_DIR}/SPY_max.csv"
        SWINGBOT_IWM_MAX_CSV="${CMAKE_SOURCE_DIR}/IWM_max.csv"
        SWINGBOT_MDY_MAX_CSV="${CMAKE_SOURCE_DIR}/MDY_max.csv"
        SWINGBOT_PROD_CONFIG_JSON="${CMAKE_SOURCE_DIR}/config/prod.json"
)

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
        tests/unit/test_alpaca_broker_unit.cpp
        tests/unit/test_alpaca_data_feed_unit.cpp
        tests/unit/test_ibkr_data_feed_unit.cpp
        tests/unit/test_logger_unit.cpp
        tests/unit/test_market_calendar_unit.cpp
        tests/unit/test_data_quality_unit.cpp
        tests/unit/test_walk_forward_unit.cpp
        tests/unit/test_grid_optimizer_unit.cpp
        tests/unit/test_monte_carlo_unit.cpp
        tests/unit/test_rotation_backtester_unit.cpp
        tests/unit/test_config_loader_unit.cpp
        tests/unit/test_live_gate_unit.cpp
        tests/unit/test_mean_reversion_unit.cpp
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
        tests/integration/test_http_client_integration.cpp
        tests/integration/test_multi_asset_integration.cpp
        tests/integration/test_walk_forward_integration.cpp
        tests/integration/test_grid_optimizer_integration.cpp
        tests/integration/test_monte_carlo_integration.cpp
        tests/integration/test_strategy_v2_integration.cpp
        tests/integration/test_candidate_validation_integration.cpp
        tests/integration/test_trailing_atr_integration.cpp
        tests/integration/test_signal_families_integration.cpp
        tests/integration/test_pullback_volatility_integration.cpp
        tests/integration/test_pullback_confirmation_integration.cpp
        tests/integration/test_rotation_oos_integration.cpp
        ${BOT_SOURCES}
)
target_include_directories(integration_tests PRIVATE ${COMMON_INCLUDES})
target_link_libraries(integration_tests PRIVATE ${COMMON_LIBS} ${GTEST_LIBS})
# Le golden backtest (item 17) lit QQQ.csv depuis la racine du dépôt,
# indépendamment du répertoire de travail des tests.
# Les actifs multi-CSV (item 7.4, D29) : séries total-return réelles
# (Adj Close ≠ Close) pour valider la stratégie hors d'un seul actif.
target_compile_definitions(integration_tests PRIVATE
        SWINGBOT_QQQ_CSV="${CMAKE_SOURCE_DIR}/QQQ.csv"
        SWINGBOT_SPY_CSV="${CMAKE_SOURCE_DIR}/SPY.csv"
        SWINGBOT_IWM_CSV="${CMAKE_SOURCE_DIR}/IWM.csv"
        SWINGBOT_MDY_CSV="${CMAKE_SOURCE_DIR}/MDY.csv"
        # Données longues (item 8d.1) : historique max ~1999+, dot-com/2008 inclus
        SWINGBOT_QQQ_MAX_CSV="${CMAKE_SOURCE_DIR}/QQQ_max.csv"
        SWINGBOT_SPY_MAX_CSV="${CMAKE_SOURCE_DIR}/SPY_max.csv"
        SWINGBOT_IWM_MAX_CSV="${CMAKE_SOURCE_DIR}/IWM_max.csv"
        SWINGBOT_MDY_MAX_CSV="${CMAKE_SOURCE_DIR}/MDY_max.csv"
        SWINGBOT_PROD_CONFIG_JSON="${CMAKE_SOURCE_DIR}/config/prod.json"
)

gtest_discover_tests(integration_tests
        TEST_PREFIX "Integration."
        PROPERTIES TIMEOUT 30
)