/**
 * @file
 * @brief Tests for nexenne::utility::scope_guard.
 */

#include <doctest/doctest.h>

#include <concepts>
#include <stdexcept>
#include <vector>

#include <nexenne/utility/scope_guard.hpp>

namespace {

TEST_CASE("nexenne::utility::scope_guard runs cleanup unless dismissed") {
  auto runs{0};

  SUBCASE("active guard runs at scope exit") {
    {
      auto const guard{nexenne::utility::scope_guard{[&] { ++runs; }}};
      CHECK(guard.is_active());
    }
    CHECK(runs == 1);
  }

  SUBCASE("dismissed guard does not run") {
    {
      auto guard{nexenne::utility::scope_guard{[&] { ++runs; }}};
      guard.dismiss();
      CHECK_FALSE(guard.is_active());
    }
    CHECK(runs == 0);
  }

  SUBCASE("re-engaged guard runs again") {
    {
      auto guard{nexenne::utility::scope_guard{[&] { ++runs; }}};
      guard.dismiss();
      guard.engage();
    }
    CHECK(runs == 1);
  }

  SUBCASE("toggling that ends dismissed does not run") {
    {
      auto guard{nexenne::utility::scope_guard{[&] { ++runs; }}};
      guard.dismiss();
      guard.engage();
      guard.dismiss();
    }
    CHECK(runs == 0);
  }
}

TEST_CASE("nexenne::utility::scope_guard honours dismissal during unwinding and runs LIFO") {
  bool ran{false};
  try {
    auto guard{nexenne::utility::scope_guard{[&] { ran = true; }}};
    guard.dismiss();
    throw std::runtime_error{"boom"};
  } catch (...) {  // NOLINT(bugprone-empty-catch)
  }
  CHECK_FALSE(ran);  // dismissed: does not run even while unwinding

  std::vector<int> order;
  {
    auto const first{nexenne::utility::scope_guard{[&] { order.push_back(1); }}};
    auto const second{nexenne::utility::scope_guard{[&] { order.push_back(2); }}};
  }
  CHECK(order == std::vector{2, 1});
}

static_assert(
  !std::movable<nexenne::utility::scope_guard<void (*)()>>,
  "scope_guard is scope-bound: neither copyable nor movable"
);

}  // namespace
