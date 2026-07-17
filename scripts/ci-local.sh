#!/usr/bin/env bash
#
# Reproduce the .github/workflows/ci.yml matrix locally, so a green run here
# means a green run in CI. Each check is gated on its tool being present and
# reports SKIP when it is missing, so a partial toolchain still runs what it can.
#
# Usage:
#   scripts/ci-local.sh [check ...]
# where each check is one of: format gcc clang asan tidy (default: all of them).
#
# Tool names match CI (gcc-15, clang-20, clang-*-20) and are overridable, each
# falling back to the unversioned name:
#   GXX=g++-15 CLANGXX=clang++-20 CLANG_TIDY=clang-tidy-20 \
#   CLANG_FORMAT=clang-format-20 scripts/ci-local.sh
set -uo pipefail

cd "$(dirname "$0")/.."

# Resolve a tool from a preference list; empty output means none is available.
pick() {
  local candidate
  for candidate in "$@"; do
    if command -v "${candidate}" >/dev/null 2>&1; then
      printf '%s\n' "${candidate}"
      return 0
    fi
  done
  return 1
}

GXX=$(pick "${GXX:-g++-15}" g++ c++) || GXX=""
GCC=$(pick "${GCC:-gcc-15}" gcc cc) || GCC=""
CLANGXX=$(pick "${CLANGXX:-clang++-20}" clang++) || CLANGXX=""
CLANG=$(pick "${CLANG:-clang-20}" clang) || CLANG=""
CLANG_TIDY=$(pick "${CLANG_TIDY:-clang-tidy-20}" clang-tidy) || CLANG_TIDY=""
CLANG_FORMAT=$(pick "${CLANG_FORMAT:-clang-format-20}" clang-format) || CLANG_FORMAT=""

RESULTS=()
record() { RESULTS+=("$1 $2"); }        # status, name
say() { printf '\n\033[1m== %s ==\033[0m\n' "$1"; }

# Wipe the ci preset's build dir so a compiler switch takes effect (CMake ignores
# a changed CC/CXX on an already-configured cache).
reset_ci_dir() { rm -rf build/ci; }

check_format() {
  say "clang-format"
  if [ -z "${CLANG_FORMAT}" ]; then record SKIP format; return; fi
  local files
  files=$(git ls-files '*.hpp' '*.cpp')
  if [ -z "${files}" ]; then record PASS format; return; fi
  # shellcheck disable=SC2086
  if "${CLANG_FORMAT}" --dry-run --Werror ${files}; then record PASS format; else record FAIL format; fi
}

# $1 human name, $2 C compiler, $3 C++ compiler
build_and_test_ci() {
  local name=$1 cc=$2 cxx=$3
  say "build + doctest (${name})"
  if [ -z "${cxx}" ]; then record SKIP "build-${name}"; return; fi
  reset_ci_dir
  if CC="${cc}" CXX="${cxx}" cmake --preset ci \
      && cmake --build --preset ci \
      && cmake --build --preset ci --target nexenne_tests; then
    record PASS "build-${name}"
  else
    record FAIL "build-${name}"
  fi
}

check_asan() {
  say "doctest under asan + ubsan (${GXX:-none})"
  if [ -z "${GXX}" ]; then record SKIP asan; return; fi
  if CC="${GCC}" CXX="${GXX}" cmake --preset asan \
      && cmake --build --preset asan \
      && cmake --build --preset asan --target nexenne_tests; then
    record PASS asan
  else
    record FAIL asan
  fi
}

check_tidy() {
  say "clang-tidy (tests)"
  if [ -z "${CLANG_TIDY}" ] || [ -z "${CLANGXX}" ]; then record SKIP tidy; return; fi
  # A compile database against the same libstdc++ clang-tidy will read.
  if ! CC="${CLANG}" CXX="${CLANGXX}" cmake --preset dev; then record FAIL tidy; return; fi
  local tests
  tests=$(git ls-files 'modules/*/tests/*.cpp')
  if [ -z "${tests}" ]; then record PASS tidy; return; fi
  # shellcheck disable=SC2086
  if printf '%s\n' ${tests} | xargs -r -P"$(nproc)" -I{} \
      "${CLANG_TIDY}" -p build/dev --quiet --config-file="${PWD}/.clang-tidy-tests" {}; then
    record PASS tidy
  else
    record FAIL tidy
  fi
}

checks=("$@")
[ ${#checks[@]} -eq 0 ] && checks=(format gcc clang asan tidy)
for c in "${checks[@]}"; do
  case "${c}" in
    format) check_format ;;
    gcc)    build_and_test_ci gcc "${GCC}" "${GXX}" ;;
    clang)  build_and_test_ci clang "${CLANG}" "${CLANGXX}" ;;
    asan)   check_asan ;;
    tidy)   check_tidy ;;
    *) printf 'unknown check: %s\n' "${c}" >&2; exit 2 ;;
  esac
done

say "summary"
status=0
for r in "${RESULTS[@]}"; do
  printf '  %s\n' "${r}"
  case "${r}" in FAIL*) status=1 ;; esac
done
exit "${status}"
