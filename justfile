install:
    conan install . --build=missing -of ./build
    cmake --preset conan-release

build: install
    cmake --build ./build

test: build
    ./build/test --gtest_color=yes

bench: build
    ./build/bench --benchmark_color=yes
