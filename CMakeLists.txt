cmake_minimum_required(VERSION 3.16)
project(MiniMarket VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler warning flags
if(MSVC)
    add_compile_options(/W4 /EHsc)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static -static-libgcc -static-libstdc++")
endif()

# Core Library
add_library(minimarket_core STATIC
    src/OrderBook.cpp
    src/Portfolio.cpp
    src/Strategy.cpp
    src/Simulator.cpp
)

target_include_directories(minimarket_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# CLI Application
add_executable(minimarket_cli
    src/main.cpp
)

target_link_libraries(minimarket_cli PRIVATE
    minimarket_core
)

# GoogleTest setup via FetchContent
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(minimarket_tests
    tests/test_order_book.cpp
    tests/test_matching.cpp
    tests/test_portfolio.cpp
    tests/test_strategy.cpp
)

target_link_libraries(minimarket_tests PRIVATE
    minimarket_core
    gtest_main
)

include(GoogleTest)
gtest_discover_tests(minimarket_tests)
