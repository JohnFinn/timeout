cmake_minimum_required(VERSION 3.28)
project(timeout)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)

find_package(GTest)
add_executable(test test.cpp)
target_link_libraries(test GTest::gtest GTest::gmock GTest::gtest_main benchmark::benchmark_main)

find_package(benchmark)
add_executable(bench bench.cpp)
target_link_libraries(bench benchmark::benchmark)
