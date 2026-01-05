/**
 * @file
 * @brief Tests for nexenne::utility::defer.
 */

#include <doctest/doctest.h>

#include <concepts>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include <nexenne/utility/defer.hpp>

namespace {

TEST_CASE("nexenne::utility::defer runs the callable once at scope exit") {
  auto runs{0};
  {
    auto const guard{nexenne::utility::defer{[&] { ++runs; }}};
    CHECK(runs == 0);
  }
  CHECK(runs == 1);
}

TEST_CASE("nexenne::utility::defer guards run in reverse (LIFO) order") {
  std::vector<int> order;
  {
    auto const first{nexenne::utility::defer{[&] { order.push_back(1); }}};
    auto const second{nexenne::utility::defer{[&] { order.push_back(2); }}};
  }
  CHECK(order == std::vector{2, 1});
}

TEST_CASE("nexenne::utility::defer runs during stack unwinding") {
  bool ran{false};
  try {
    auto const guard{nexenne::utility::defer{[&] { ran = true; }}};
    throw std::runtime_error{"boom"};
  } catch (...) {  // NOLINT(bugprone-empty-catch)
  }
  CHECK(ran);
}

TEST_CASE("nexenne::utility::defer holds a move-only callable") {
  auto resource{std::make_unique<int>(7)};
  int observed{0};
  {
    auto const guard{nexenne::utility::defer{[&observed, held = std::move(resource)] {
      observed = *held;
    }}};
  }
  CHECK(observed == 7);
}

static_assert(
  !std::movable<nexenne::utility::defer<void (*)()>>,
  "defer is scope-bound: neither copyable nor movable"
);

}  // namespace
