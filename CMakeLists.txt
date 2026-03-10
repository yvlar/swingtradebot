cmake_minimum_required(VERSION 3.20)
project(SwingBot VERSION 1.0 LANGUAGES CXX)

# ─── Windows/MinGW fix ────────────────────────────────────────────────────────
if(WIN32 AND MINGW)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")
endif()

# ─── Standard C++20 ───────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ─── Includes ─────────────────────────────────────────────────────────────────
include_directories(${PROJECT_SOURCE_DIR}/include)

# ─── Exécutable principal ─────────────────────────────────────────────────────
add_executable(SwingBot main.cpp)
target_include_directories(SwingBot PRIVATE ${PROJECT_SOURCE_DIR}/include)

# ─── Google Test ──────────────────────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
enable_testing()

# ─── Tests unitaires (si le fichier existe) ───────────────────────────────────
set(UNIT_TEST_FILE ${PROJECT_SOURCE_DIR}/tests/unit/test_unit.cpp)
if(EXISTS ${UNIT_TEST_FILE})
    add_executable(unit_tests ${UNIT_TEST_FILE})
    target_link_libraries(unit_tests GTest::gtest_main)
    target_include_directories(unit_tests PRIVATE ${PROJECT_SOURCE_DIR}/include)
    include(GoogleTest)
    gtest_discover_tests(unit_tests)
    message(STATUS "✅ Tests unitaires activés")
else()
    message(WARNING "⚠️  tests/unit/test_unit.cpp introuvable — crée le fichier")
endif()

# ─── Tests d'intégration (si le fichier existe) ───────────────────────────────
set(INTEG_TEST_FILE ${PROJECT_SOURCE_DIR}/tests/integration/test_integration.cpp)
if(EXISTS ${INTEG_TEST_FILE})
    add_executable(integration_tests ${INTEG_TEST_FILE})
    target_link_libraries(integration_tests GTest::gtest_main)
    target_include_directories(integration_tests PRIVATE ${PROJECT_SOURCE_DIR}/include)
    include(GoogleTest)
    gtest_discover_tests(integration_tests)
    message(STATUS "✅ Tests d'intégration activés")
else()
    message(WARNING "⚠️  tests/integration/test_integration.cpp introuvable — crée le fichier")
endif()

# ─── Résumé ───────────────────────────────────────────────────────────────────
message(STATUS "")
message(STATUS "══════════════════════════════════════")
message(STATUS "  SwingBot — Build Configuration     ")
message(STATUS "══════════════════════════════════════")
message(STATUS "  Compilateur : ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build type  : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Sources dir : ${PROJECT_SOURCE_DIR}")
message(STATUS "══════════════════════════════════════")
message(STATUS "")
