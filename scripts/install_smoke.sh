#!/usr/bin/env bash
#
# End-to-end packaging check. Scaffolds a throwaway module, installs the whole
# project to a temp prefix, then builds a tiny consumer that pulls the module
# in two ways: find_package(nexenne-<mod>) and find_package(nexenne COMPONENTS
# <mod>). Exercises the install rules, target exports, and package configs that
# nothing else touches. Cleans up after itself.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

mod="smoke_probe"
prefix="$(mktemp -d)"
build="$(mktemp -d)"
consumer_src="$(mktemp -d)"
consumer_build="$(mktemp -d)"
created_module=0

cleanup() {
    [ "$created_module" = "1" ] && rm -rf "modules/$mod"
    rm -rf "$prefix" "$build" "$consumer_src" "$consumer_build"
}
trap cleanup EXIT

if [ ! -d "modules/$mod" ]; then
    ./template/new-module.sh "$mod" >/dev/null
    created_module=1
fi

echo "== build + install to a temp prefix =="
cmake -S . -B "$build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DNEXENNE_BUILD_TESTS=OFF \
    -DNEXENNE_BUILD_EXAMPLES=OFF \
    -DCMAKE_INSTALL_PREFIX="$prefix" >/dev/null
cmake --build "$build" >/dev/null
cmake --install "$build" >/dev/null

echo "== generate consumer =="
cat > "$consumer_src/CMakeLists.txt" <<EOF
cmake_minimum_required(VERSION 3.23)
project(nexenne_consumer LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(nexenne-${mod} REQUIRED)
find_package(nexenne REQUIRED COMPONENTS ${mod})

add_executable(nexenne_consumer main.cpp)
target_link_libraries(nexenne_consumer PRIVATE nexenne::${mod})
EOF

cat > "$consumer_src/main.cpp" <<EOF
#include <nexenne/${mod}/example.hpp>

auto main() -> int {
    return nexenne::${mod}::identity(40.0) == 40.0 ? 0 : 1;
}
EOF

echo "== build + run consumer against the installed package =="
cmake -S "$consumer_src" -B "$consumer_build" -G Ninja \
    -DCMAKE_PREFIX_PATH="$prefix" >/dev/null
cmake --build "$consumer_build" >/dev/null
"$consumer_build/nexenne_consumer"

echo "install smoke: OK"
