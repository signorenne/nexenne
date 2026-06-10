/**
 * @file
 * @brief Tests for nexenne::utility::function_ref.
 */

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <span>

#include <nexenne/utility/function_ref.hpp>

namespace {

namespace util = nexenne::utility;

auto count_if(std::span<int const> data, util::function_ref<bool(int)> pred) -> std::size_t {
  std::size_t n{0};
  for (auto const x : data) {
    if (pred(x)) {
      ++n;
    }
  }
  return n;
}

auto triple(int x) -> int {
  return x * 3;
}

TEST_CASE("nexenne::utility::function_ref binds lambdas and function pointers") {
  std::array<int, 5> const data{1, 12, 3, 20, 5};
  CHECK(count_if(data, [](int x) { return x > 10; }) == 2);

  util::function_ref<int(int)> const fr{triple};
  CHECK(fr(4) == 12);
  CHECK(static_cast<bool>(fr));

  util::function_ref<int(int)> const empty;
  CHECK_FALSE(static_cast<bool>(empty));
}

TEST_CASE("nexenne::utility::function_ref refers to mutable captured state") {
  int calls{0};
  auto counter{[&](int) {
    ++calls;
    return true;
  }};
  util::function_ref<bool(int)> const fr{counter};
  fr(0);
  fr(0);
  CHECK(calls == 2);
}

TEST_CASE("nexenne::utility::function_ref binds a const callable and a void signature") {
  auto const adder{[](int x) { return x + 1; }};  // const operator()
  util::function_ref<int(int)> const fr{adder};
  CHECK(fr(1) == 2);

  int sink{5};
  util::function_ref<void(int&)> const inc{[](int& v) { ++v; }};
  inc(sink);
  CHECK(sink == 6);  // by-reference argument forwarded
}

TEST_CASE("nexenne::utility::function_ref is reassignable to a new target") {
  util::function_ref<int(int)> fr{triple};
  CHECK(fr(2) == 6);

  auto const identity{[](int x) { return x; }};
  fr = util::function_ref<int(int)>{identity};
  CHECK(fr(2) == 2);
}

}  // namespace
