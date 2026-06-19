/**
 * @file
 * @brief Tests for nexenne::utility::scope_guard.
 */

#include <doctest/doctest.h>

#include <concepts>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>
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

TEST_CASE("nexenne::utility::scope_guard is active immediately after construction") {
  auto const guard{nexenne::utility::scope_guard{[] {}}};
  CHECK(guard.is_active());
}

TEST_CASE("nexenne::utility::scope_guard active guard runs exactly once") {
  auto runs{0};
  {
    auto const guard{nexenne::utility::scope_guard{[&] { ++runs; }}};
    static_cast<void>(guard);
  }
  CHECK(runs == 1);  // not zero, not two
}

TEST_CASE("nexenne::utility::scope_guard dismiss is idempotent") {
  auto runs{0};
  {
    auto guard{nexenne::utility::scope_guard{[&] { ++runs; }}};
    guard.dismiss();
    guard.dismiss();  // double dismiss is safe
    CHECK_FALSE(guard.is_active());
  }
  CHECK(runs == 0);
}

TEST_CASE("nexenne::utility::scope_guard engage is idempotent") {
  auto runs{0};
  {
    auto guard{nexenne::utility::scope_guard{[&] { ++runs; }}};
    guard.engage();  // already active
    guard.engage();
    CHECK(guard.is_active());
  }
  CHECK(runs == 1);
}

TEST_CASE("nexenne::utility::scope_guard engage after a fired-state toggle runs once") {
  auto runs{0};
  {
    auto guard{nexenne::utility::scope_guard{[&] { ++runs; }}};
    for (auto i{0}; i < 4; ++i) {
      guard.dismiss();
      guard.engage();
    }
  }
  CHECK(runs == 1);
}

TEST_CASE("nexenne::utility::scope_guard runs on early return out of a scope") {
  auto runs{0};
  auto const fn{[&] {
    auto const guard{nexenne::utility::scope_guard{[&] { ++runs; }}};
    return;  // early return triggers cleanup
  }};
  fn();
  CHECK(runs == 1);
}

TEST_CASE("nexenne::utility::scope_guard runs during stack unwinding when active") {
  bool ran{false};
  try {
    auto const guard{nexenne::utility::scope_guard{[&] { ran = true; }}};
    throw std::runtime_error{"boom"};
  } catch (...) {  // NOLINT(bugprone-empty-catch)
  }
  CHECK(ran);  // active guard fires during unwinding
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

TEST_CASE("nexenne::utility::scope_guard guards run LIFO with selective dismissal") {
  std::vector<int> order;
  {
    auto first{nexenne::utility::scope_guard{[&] { order.push_back(1); }}};
    auto second{nexenne::utility::scope_guard{[&] { order.push_back(2); }}};
    auto third{nexenne::utility::scope_guard{[&] { order.push_back(3); }}};
    second.dismiss();  // only the middle one is cancelled
  }
  CHECK(order == std::vector{3, 1});
}

TEST_CASE("nexenne::utility::scope_guard commit-or-rollback idiom") {
  // Classic use: arm a rollback, dismiss it only after the work succeeds.
  std::vector<int> log;
  auto const commit{[&](bool succeed) {
    auto rollback{nexenne::utility::scope_guard{[&] { log.push_back(-1); }}};
    if (!succeed) {
      return;  // rollback fires
    }
    log.push_back(1);
    rollback.dismiss();  // success: no rollback
  }};

  commit(true);
  CHECK(log == std::vector{1});

  commit(false);
  CHECK(log == std::vector{1, -1});
}

TEST_CASE("nexenne::utility::scope_guard holds a move-only callable") {
  auto resource{std::make_unique<int>(11)};
  int observed{0};
  {
    auto const guard{nexenne::utility::scope_guard{[&observed, held = std::move(resource)] {
      observed = *held;
    }}};
  }
  CHECK(observed == 11);
}

TEST_CASE("nexenne::utility::scope_guard works with a function pointer") {
  static int counter{0};
  counter = 0;
  {
    auto guard{nexenne::utility::scope_guard{+[] { ++counter; }}};
    guard.dismiss();
  }
  CHECK(counter == 0);
  {
    auto const guard{nexenne::utility::scope_guard{+[] { ++counter; }}};
    static_cast<void>(guard);
  }
  CHECK(counter == 1);
}

// A scope_guard over a function pointer is neither copyable nor movable.
static_assert(
  !std::movable<nexenne::utility::scope_guard<void (*)()>>,
  "scope_guard is scope-bound: neither copyable nor movable"
);
static_assert(
  !std::copyable<nexenne::utility::scope_guard<void (*)()>>, "scope_guard is non-copyable"
);
static_assert(
  !std::is_move_constructible_v<nexenne::utility::scope_guard<void (*)()>>,
  "scope_guard has an implicitly deleted move constructor"
);

// CTAD deduces Fn; function_type exposes it.
static_assert(
  std::is_same_v<
    decltype(nexenne::utility::scope_guard{std::declval<void (*)()>()}),
    nexenne::utility::scope_guard<void (*)()>>,
  "scope_guard CTAD deduces Fn from its argument"
);
static_assert(
  std::is_same_v<nexenne::utility::scope_guard<void (*)()>::function_type, void (*)()>,
  "scope_guard exposes its Fn as function_type"
);

// The constructor is explicit.
static_assert(
  !std::is_convertible_v<void (*)(), nexenne::utility::scope_guard<void (*)()>>,
  "scope_guard has an explicit constructor"
);

}  // namespace
