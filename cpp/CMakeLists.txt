cmake_minimum_required(VERSION 3.20)
project(PayoffEngine VERSION 1.0.0 LANGUAGES CXX)

# ============================================================================
# C++20 Standard
# ============================================================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile_commands.json for IDE integration
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ============================================================================
# Build Types
# ============================================================================
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# ============================================================================
# Compiler Warnings (relaxed for development)
# ============================================================================
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -Wall
        -Wextra
        # -Werror  # Disabled during development
        -Wno-unused-parameter
        -Wno-sign-conversion
        -Wno-conversion
    )
elseif(MSVC)
    add_compile_options(/W3 /permissive-)
endif()

# ============================================================================
# Platform-specific settings
# ============================================================================
if(WIN32)
    add_definitions(-D_WIN32_WINNT=0x0601)
    # Core Windows networking and crypto libs
    link_libraries(ws2_32 winhttp bcrypt)
endif()

# ============================================================================
# vcpkg Integration (if available)
# ============================================================================
if(DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "Vcpkg toolchain file")
endif()

# ============================================================================
# Dependencies (find via vcpkg or system)
# ============================================================================
find_package(nlohmann_json CONFIG QUIET)
find_package(spdlog CONFIG QUIET)
find_package(GTest CONFIG QUIET)

# ============================================================================
# Include Directories
# ============================================================================
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# ============================================================================
# Core Library
# ============================================================================
add_library(payoff_core STATIC
    # Core models and data contracts
    src/core/models.cpp
    src/core/instrument_manager.cpp
    src/core/datasource.cpp
    src/core/market_clock.cpp
    src/core/http_client.cpp
    src/core/clickhouse_http.cpp
    src/core/replay_engine.cpp
    
    # Data sources
    src/datasource/mock_source.cpp
    src/datasource/clickhouse_source.cpp
    
    # Kite API client
    src/kite/kite_client.cpp
    src/kite/kite_websocket.cpp
    
    # Streaming pipeline
    src/streaming/assembler.cpp
    src/streaming/dedup.cpp
    src/streaming/gap_detector.cpp
    src/streaming/pipeline.cpp
    
    # Features engine
    src/features/engine.cpp
    
    # Execution hints
    src/execution/hint_engine.cpp
    
    # Payoff engine
    src/payoff/pricing.cpp
    src/payoff/calculator.cpp
    src/payoff/risk.cpp
    src/payoff/greeks.cpp
    src/payoff/sensitivity.cpp
    
    # Shared market data cache
    src/cache/market_cache.cpp
    
    # Resampling
    src/resampling/bar_builder.cpp
)

target_include_directories(payoff_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(nlohmann_json_FOUND)
    target_link_libraries(payoff_core PUBLIC nlohmann_json::nlohmann_json)
endif()

if(spdlog_FOUND)
    target_link_libraries(payoff_core PUBLIC spdlog::spdlog)
endif()

# ============================================================================
# API Server Library
# ============================================================================
add_library(payoff_api STATIC
    src/api/rest_server.cpp
    src/api/websocket_server.cpp
    src/api/routes.cpp
    src/api/dto.cpp
    src/api/screener_routes.cpp
    src/api/screener_websocket.cpp
    
    # Screener service
    src/screener/screener_service.cpp
)

target_link_libraries(payoff_api PUBLIC payoff_core)

# ============================================================================
# Main Executable
# ============================================================================
add_executable(payoff_engine src/main.cpp)
target_link_libraries(payoff_engine PRIVATE payoff_core payoff_api)

# ============================================================================
# Integration Test (standalone - no GTest required)
# ============================================================================
add_executable(test_integration tests/test_integration.cpp)
target_link_libraries(test_integration PRIVATE payoff_core payoff_api)

# ============================================================================
# Tests (GTest-based)
# ============================================================================
enable_testing()

if(GTest_FOUND)
    add_executable(test_models tests/test_models.cpp)
    target_link_libraries(test_models PRIVATE payoff_core GTest::gtest GTest::gtest_main)
    add_test(NAME TestModels COMMAND test_models)

    add_executable(test_features tests/test_features.cpp)
    target_link_libraries(test_features PRIVATE payoff_core GTest::gtest GTest::gtest_main)
    add_test(NAME TestFeatures COMMAND test_features)

    add_executable(test_payoff tests/test_payoff.cpp)
    target_link_libraries(test_payoff PRIVATE payoff_core GTest::gtest GTest::gtest_main)
    add_test(NAME TestPayoff COMMAND test_payoff)

    add_executable(test_streaming tests/test_streaming.cpp)
    target_link_libraries(test_streaming PRIVATE payoff_core GTest::gtest GTest::gtest_main)
    add_test(NAME TestStreaming COMMAND test_streaming)

    add_executable(test_parity tests/test_parity.cpp)
    target_link_libraries(test_parity PRIVATE payoff_core GTest::gtest GTest::gtest_main)
    add_test(NAME TestParity COMMAND test_parity)
endif()

# Add integration test to CTest
add_test(NAME TestIntegration COMMAND test_integration)

# ============================================================================
# Installation
# ============================================================================
install(TARGETS payoff_engine payoff_core payoff_api test_integration
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(DIRECTORY include/ DESTINATION include)

# ============================================================================
# Summary
# ============================================================================
message(STATUS "")
message(STATUS "=== Payoff Engine Configuration ===")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Platform: ${CMAKE_SYSTEM_NAME}")
message(STATUS "nlohmann_json: ${nlohmann_json_FOUND}")
message(STATUS "spdlog: ${spdlog_FOUND}")
message(STATUS "GTest: ${GTest_FOUND}")
message(STATUS "===================================")
message(STATUS "")
