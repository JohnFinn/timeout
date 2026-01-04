#include "timeout.hpp"
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <thread>

TEST(Foo, Bar) {
  using namespace std::chrono_literals;
  EXPECT_EQ(timeout(10ms, [](){ return 42; }), 42);
  EXPECT_EQ(timeout(10ms, [](){ std::this_thread::sleep_for(11ms); return 42; }), std::nullopt);
}
