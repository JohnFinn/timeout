#include "timeout.hpp"
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <thread>

TEST(Foo, Bar) {
  using namespace std::chrono_literals;
  EXPECT_EQ(timeout(10ms, [](){ return 42; }), 42);
  EXPECT_EQ(timeout(10ms, [](){ std::this_thread::sleep_for(11ms); return 42; }), std::nullopt);
  int a = 10;
  EXPECT_EQ(timeout(10ms, [&](){ return a + a; }), 20);
  EXPECT_EQ(timeout(10ms, [](int a, int b){ return a + b; }, 42, 54), 96);
}
