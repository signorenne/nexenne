#!/usr/bin/env bash
#
# End-to-end packaging check. Installs the whole project to a temp prefix, then
# consumes it the way a downstream user would:
#
#   1. every real module's umbrella header compiles against the prefix alone,
#   2. the generated repo umbrella compiles and links through nexenne::all,
#      calling into every compiled module so a missing archive is a link error,
#   3. a scaffolded throwaway module resolves via find_package(nexenne-<mod>)
#      and find_package(nexenne COMPONENTS <mod>).
#
# Checks 1 and 2 are the ones that matter: the in-tree build compiles against
# the whole include directory, so a header missing from a target's install file
# set is invisible until something consumes the installed prefix. Cleans up
# after itself.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

mod="smoke_probe"
prefix="$(mktemp -d)"
build="$(mktemp -d)"
consumer_src="$(mktemp -d)"
consumer_build="$(mktemp -d)"
aggregate_src="$(mktemp -d)"
aggregate_build="$(mktemp -d)"
version_backup="$(mktemp)"
created_module=0
cp cmake/nexenne_version.cmake "$version_backup"

cleanup() {
    [ "$created_module" = "1" ] && rm -rf "modules/$mod"
    cp "$version_backup" cmake/nexenne_version.cmake
    rm -rf "$prefix" "$build" "$consumer_src" "$consumer_build" \
        "$aggregate_src" "$aggregate_build" "$version_backup"
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

echo "== compile every installed module umbrella =="
umbrella_src="$consumer_src/umbrella.cpp"
missing=()
for dir in modules/*/; do
    name="$(basename "$dir")"
    [ "$name" = "$mod" ] && continue
    header="nexenne/${name}/${name}.hpp"
    if [ ! -f "$prefix/include/$header" ]; then
        echo "  $name: umbrella header not installed ($header)" >&2
        missing+=("$name")
        continue
    fi
    printf '#include <%s>\nauto main() -> int { return 0; }\n' "$header" > "$umbrella_src"
    if diagnostics="$("${CXX:-c++}" -std=c++23 -I"$prefix/include" -fsyntax-only "$umbrella_src" 2>&1)"; then
        echo "  $name: ok"
    else
        echo "  $name: FAILED to compile from the installed prefix" >&2
        printf '%s\n' "$diagnostics" | head -5 | sed 's/^/    /' >&2
        missing+=("$name")
    fi
done

printf '#include <nexenne/nexenne.hpp>\nauto main() -> int { return 0; }\n' > "$umbrella_src"
if "${CXX:-c++}" -std=c++23 -I"$prefix/include" -fsyntax-only "$umbrella_src"; then
    echo "  nexenne (repo umbrella): ok"
else
    echo "  nexenne (repo umbrella): FAILED to compile from the installed prefix" >&2
    missing+=("nexenne")
fi

if [ "${#missing[@]}" -gt 0 ]; then
    echo "install smoke: these umbrellas do not compile from the install prefix:" >&2
    printf '  %s\n' "${missing[@]}" >&2
    exit 1
fi

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

# Separate project on purpose: nexenne::all only exists after a full
# find_package(nexenne), so asking for it in the COMPONENTS consumer above would
# test the aggregate instead of the subset path.
echo "== build + run aggregate consumer (find_package(nexenne), nexenne::all) =="
cat > "$aggregate_src/CMakeLists.txt" <<EOF
cmake_minimum_required(VERSION 3.23)
project(nexenne_aggregate_consumer LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(nexenne REQUIRED)

add_executable(nexenne_aggregate_consumer main.cpp)
target_link_libraries(nexenne_aggregate_consumer PRIVATE nexenne::all)
EOF

# Calls one function from each compiled module on purpose. Including a header
# links nothing, so an empty or missing archive for can, gpio, logging, or
# serialization used to ship green through this script. Each call below forces
# the linker to resolve a symbol out of the matching archive.
cat > "$aggregate_src/main.cpp" <<'EOF'
#include <nexenne/nexenne.hpp>

#include <string_view>
#include <thread>

auto main() -> int {
  auto failures{0};

  // serialization: defined in src/json/parse.cpp.
  if (!nexenne::serialization::json::parse(std::string_view{"{}"}).has_value()) {
    ++failures;
  }

  // can: defined in src/database.cpp.
  if (nexenne::can::database{}.message_count() != 0) {
    ++failures;
  }

  // logging: defined in src/record.cpp.
  if (nexenne::logging::detail::thread_id_to_string(std::this_thread::get_id()).empty()) {
    ++failures;
  }

#ifdef __linux__
  // gpio: defined in src/io/chardev_chip.cpp. Every gpio source is behind the
  // same Linux guard, so off Linux the archive is legitimately empty.
  auto chip{nexenne::gpio::chardev_chip{nexenne::gpio::chip_id{0}}};
  if (chip.is_open()) {
    ++failures;
  }
#endif

  return failures;
}
EOF

cmake -S "$aggregate_src" -B "$aggregate_build" -G Ninja \
    -DCMAKE_PREFIX_PATH="$prefix" >/dev/null
cmake --build "$aggregate_build" >/dev/null
"$aggregate_build/nexenne_aggregate_consumer"

echo "install smoke: OK"
