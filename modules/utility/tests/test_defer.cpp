/**
 * @file
 * @brief Tests for nexenne::utility::defer.
 */

#include <doctest/doctest.h>

#include <concepts>
#include <memory>
#include <stdexcept>
#include <type_traits>
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

TEST_CASE("nexenne::utility::defer runs exactly once, never more") {
  auto runs{0};
  for (auto i{0}; i < 5; ++i) {
    auto const guard{nexenne::utility::defer{[&] { ++runs; }}};
    static_cast<void>(guard);
  }
  CHECK(runs == 5);  // exactly one run per scope entry, no extras
}

TEST_CASE("nexenne::utility::defer runs on early return out of a scope") {
  auto runs{0};
  auto const fn{[&] {
    auto const guard{nexenne::utility::defer{[&] { ++runs; }}};
    if (runs == 0) {
      return;  // early return still triggers cleanup
    }
    ++runs;  // unreachable
  }};
  fn();
  CHECK(runs == 1);
}

TEST_CASE("nexenne::utility::defer guards run in reverse (LIFO) order") {
  std::vector<int> order;
  {
    auto const first{nexenne::utility::defer{[&] { order.push_back(1); }}};
    auto const second{nexenne::utility::defer{[&] { order.push_back(2); }}};
    auto const third{nexenne::utility::defer{[&] { order.push_back(3); }}};
  }
  CHECK(order == std::vector{3, 2, 1});
}

TEST_CASE("nexenne::utility::defer guards in nested scopes unwind innermost first") {
  std::vector<int> order;
  {
    auto const outer{nexenne::utility::defer{[&] { order.push_back(1); }}};
    {
      auto const inner{nexenne::utility::defer{[&] { order.push_back(2); }}};
    }
    order.push_back(3);
  }
  CHECK(order == std::vector{2, 3, 1});
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

TEST_CASE("nexenne::utility::defer unwinding still runs guards LIFO") {
  std::vector<int> order;
  try {
    auto const first{nexenne::utility::defer{[&] { order.push_back(1); }}};
    auto const second{nexenne::utility::defer{[&] { order.push_back(2); }}};
    throw std::runtime_error{"boom"};
  } catch (...) {  // NOLINT(bugprone-empty-catch)
  }
  CHECK(order == std::vector{2, 1});
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

TEST_CASE("nexenne::utility::defer captures by value snapshots state at construction") {
  auto value{1};
  int observed{0};
  {
    auto const guard{nexenne::utility::defer{[&observed, value] { observed = value; }}};
    value = 99;  // mutating the original does not change the captured copy
  }
  CHECK(observed == 1);
}

TEST_CASE("nexenne::utility::defer works with a function pointer") {
  static int counter{0};
  counter = 0;
  {
    auto const guard{nexenne::utility::defer{+[] { ++counter; }}};
    CHECK(counter == 0);
  }
  CHECK(counter == 1);
}

TEST_CASE("nexenne::utility::defer mutable lambda mutates its own captured state") {
  int observed{0};
  {
    auto guard{nexenne::utility::defer{[&observed, n = 0]() mutable {
      ++n;
      observed = n;
    }}};
  }
  CHECK(observed == 1);
}

// A defer over a function pointer is neither copyable nor movable: it is bound
// to its scope.
static_assert(
  !std::movable<nexenne::utility::defer<void (*)()>>,
  "defer is scope-bound: neither copyable nor movable"
);
static_assert(!std::copyable<nexenne::utility::defer<void (*)()>>, "defer is non-copyable");
static_assert(
  !std::is_copy_constructible_v<nexenne::utility::defer<void (*)()>>,
  "defer has a deleted copy constructor"
);
static_assert(
  !std::is_move_constructible_v<nexenne::utility::defer<void (*)()>>,
  "defer has an implicitly deleted move constructor"
);

// The CTAD guide deduces Fn from the constructor argument.
static_assert(
  std::is_same_v<
    decltype(nexenne::utility::defer{std::declval<void (*)()>()}),
    nexenne::utility::defer<void (*)()>>,
  "defer CTAD deduces Fn from its argument"
);

// function_type is the stored callable type.
static_assert(
  std::is_same_v<nexenne::utility::defer<void (*)()>::function_type, void (*)()>,
  "defer exposes its Fn as function_type"
);

// The constructor is explicit: no implicit conversion from a callable.
static_assert(
  !std::is_convertible_v<void (*)(), nexenne::utility::defer<void (*)()>>,
  "defer has an explicit constructor"
);

}  // namespace
