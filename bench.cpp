#include "timeout.hpp"
#include <benchmark/benchmark.h>

int sum(int a, int b) { return a + b; }

static void BM_baseline(benchmark::State &state) {
  for (auto _ : state) {
    auto v = sum(42, 197);
    benchmark::DoNotOptimize(v);
  }
}

static void BM_timeout(benchmark::State &state) {
  using namespace std::chrono_literals;
  for (auto _ : state) {
    auto v = timeout(1s, []() { return sum(42, 197); }).value();
    benchmark::DoNotOptimize(v);
  }
}

BENCHMARK(BM_baseline);
BENCHMARK(BM_timeout);

BENCHMARK_MAIN();
