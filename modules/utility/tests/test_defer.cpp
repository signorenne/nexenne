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
#include <nexenne/utility/ignore.hpp>

namespace {

struct throwing_move_cleanup {
  int* runs{nullptr};
  bool throw_on_move{false};

  throwing_move_cleanup(int& counter, bool const arm) : runs{&counter}, throw_on_move{arm} {}

  throwing_move_cleanup(throwing_move_cleanup&& other)
      : runs{other.runs}, throw_on_move{other.throw_on_move} {
    if (throw_on_move) {
      throw std::runtime_error{"move failed"};
    }
  }

  throwing_move_cleanup(throwing_move_cleanup const&) = delete;
  auto operator=(throwing_move_cleanup const&) -> throwing_move_cleanup& = delete;
  auto operator=(throwing_move_cleanup&&) -> throwing_move_cleanup& = delete;
  ~throwing_move_cleanup() = default;

  auto operator()() const -> void {
    ++*runs;
  }
};

struct flaky_cleanup {
  std::vector<int>* log{nullptr};
  int tag{0};
  bool copy_throws{false};

  flaky_cleanup(std::vector<int>& sink, int const id, bool const throws) noexcept
      : log{&sink}, tag{id}, copy_throws{throws} {}

  flaky_cleanup(flaky_cleanup const& other)
      : log{other.log}, tag{other.tag}, copy_throws{other.copy_throws} {
    if (copy_throws) {
      throw std::runtime_error{"copy failed"};
    }
  }

  // NOLINTBEGIN(performance-noexcept-move-constructor,bugprone-exception-escape)
  flaky_cleanup(flaky_cleanup&& other) noexcept(false)
      : log{other.log}, tag{other.tag}, copy_throws{other.copy_throws} {
    other.tag = -1;
    throw std::runtime_error{"move failed"};
  }

  // NOLINTEND(performance-noexcept-move-constructor,bugprone-exception-escape)

  auto operator=(flaky_cleanup const&) -> flaky_cleanup& = delete;
  auto operator=(flaky_cleanup&&) -> flaky_cleanup& = delete;
  ~flaky_cleanup() = default;

  auto operator()() const -> void {
    log->push_back(tag);
  }
};

TEST_CASE("nexenne::utility::defer copies a moved-in callable whose move can throw") {
  std::vector<int> log;
  flaky_cleanup source{log, 7, false};
  {
    auto const guard{nexenne::utility::defer{std::move(source)}};
    CHECK(log.empty());
  }
  CHECK(log == std::vector{7});
}

TEST_CASE("nexenne::utility::defer runs the caller's intact callable when copying it throws") {
  std::vector<int> log;
  flaky_cleanup const source{log, 8, true};
  CHECK_THROWS_AS(nexenne::utility::ignore(nexenne::utility::defer{source}), std::runtime_error);
  CHECK(log == std::vector{8});
}

TEST_CASE("nexenne::utility::defer copies an lvalue callable instead of moving it") {
  std::vector<int> log;
  flaky_cleanup const source{log, 9, false};
  {
    auto const guard{nexenne::utility::defer{source}};
    CHECK(log.empty());
  }
  CHECK(log == std::vector{9});
}

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
    [[maybe_unused]] auto const guard{nexenne::utility::defer{[&] { ++runs; }}};
  }
  CHECK(runs == 5);
}

TEST_CASE("nexenne::utility::defer runs on early return out of a scope") {
  auto runs{0};
  auto const fn{[&] {
    auto const guard{nexenne::utility::defer{[&] { ++runs; }}};
    if (runs == 0) {
      return;
    }
    ++runs;
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
    value = 99;
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

TEST_CASE("nexenne::utility::defer propagates a throwing cleanup on a normal scope exit") {
  auto const leave_scope{[] {
    [[maybe_unused]] auto const guard{nexenne::utility::defer{[] {
      throw std::runtime_error{"cleanup failed"};
    }}};
  }};
  CHECK_THROWS_AS(leave_scope(), std::runtime_error);
}

TEST_CASE("nexenne::utility::defer invokes the cleanup when its move into the guard throws") {
  int runs{0};
  CHECK_THROWS_AS(
    nexenne::utility::ignore(nexenne::utility::defer{throwing_move_cleanup{runs, true}}),
    std::runtime_error
  );
  CHECK(runs == 1);
}

static_assert(
  !std::is_nothrow_destructible_v<nexenne::utility::defer<void (*)()>>,
  "a potentially-throwing cleanup gives a potentially-throwing destructor"
);
static_assert(
  std::is_nothrow_destructible_v<nexenne::utility::defer<void (*)() noexcept>>,
  "a noexcept cleanup gives a noexcept destructor"
);

// is_nothrow_constructible folds in the destructor, hence the noexcept callable.
static_assert(
  std::
    is_nothrow_constructible_v<nexenne::utility::defer<void (*)() noexcept>, void (*)() noexcept>,
  "a nothrow-movable, noexcept callable gives a noexcept construct-and-destroy"
);
static_assert(
  !std::is_nothrow_constructible_v<
    nexenne::utility::defer<throwing_move_cleanup>,
    throwing_move_cleanup>,
  "a throwing-move callable gives a potentially-throwing constructor"
);

static_assert(
  [] {
    struct rvalue_only {
      auto operator()() && -> void {}
    };
    return !std::invocable<rvalue_only&>;
  }(),
  "defer rejects a callable invocable only as an rvalue"
);

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

static_assert(
  std::is_same_v<
    decltype(nexenne::utility::defer{std::declval<void (*)()>()}),
    nexenne::utility::defer<void (*)()>>,
  "defer CTAD deduces Fn from its argument"
);

static_assert(
  std::is_same_v<nexenne::utility::defer<void (*)()>::function_type, void (*)()>,
  "defer exposes its Fn as function_type"
);

static_assert(
  !std::is_convertible_v<void (*)(), nexenne::utility::defer<void (*)()>>,
  "defer has an explicit constructor"
);

}  // namespace
