#include "timeout.hpp"
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <thread>

TEST(Timeout, Smoke) {
  using namespace std::chrono_literals;
  EXPECT_EQ(timeout(10ms, []() { return 42; }), 42);
  EXPECT_EQ(timeout(10ms,
                    []() {
                      std::this_thread::sleep_for(11ms);
                      return 42;
                    }),
            std::nullopt);
  int a = 10;
  EXPECT_EQ(timeout(10ms, [&]() { return a + a; }), 20);
  EXPECT_EQ(timeout(10ms, [](int a, int b) { return a + b; }, 42, 54), 96);
}

TEST(Timeout, Threads) {
  auto f = [](int a, int b) { return a + b; };
  using namespace std::chrono_literals;
  std::jthread t([&]() {
    for (int i = 0; i < 100; ++i) {
      EXPECT_EQ(timeout(10ms, f, 1, 2), 3);
    }
  });
  std::jthread t2([&]() {
    for (int i = 0; i < 100; ++i) {
      EXPECT_EQ(timeout(10ms, f, 2, 2), 4);
    }
  });
}
