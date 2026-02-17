/**
 * @file
 * @brief Tests for nexenne::utility::in_place_function.
 */

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>

#include <nexenne/utility/in_place_function.hpp>

namespace {

namespace util = nexenne::utility;

using callback = util::in_place_function<int(int), 32>;

static_assert(callback::capacity == 32);
static_assert(!std::is_copy_constructible_v<callback>);
static_assert(!std::is_copy_assignable_v<callback>);
static_assert(std::is_move_constructible_v<callback>);
static_assert(std::is_move_assignable_v<callback>);
static_assert(std::is_nothrow_move_constructible_v<callback>);
static_assert(std::is_nothrow_move_assignable_v<callback>);
static_assert(std::is_same_v<callback::result_type, int>);

// The default capacity is 64 bytes (documented contract).
static_assert(util::in_place_function<void()>::capacity == 64);

struct big_functor {
  std::array<char, 64> data{};

  auto operator()() const -> void {}
};

// A too-large callable is rejected at compile time; a fitting capacity accepts it.
static_assert(!std::is_constructible_v<util::in_place_function<void(), 8>, big_functor>);
static_assert(std::is_constructible_v<util::in_place_function<void(), 64>, big_functor>);

// A callable that exactly fills the capacity is accepted; one byte larger is not.
struct exact_32 {
  std::array<char, 32> data{};

  auto operator()() const -> void {}
};

struct over_32 {
  std::array<char, 40> data{};

  auto operator()() const -> void {}
};

static_assert(std::is_constructible_v<util::in_place_function<void(), 32>, exact_32>);
static_assert(!std::is_constructible_v<util::in_place_function<void(), 32>, over_32>);

// An over-aligned callable is rejected (alignment must be <= max_align_t).
// NOTE: this is a documented COMPILE error, asserted negatively rather than
// instantiated. A type aligned beyond max_align_t fails the requires clause.
struct alignas(2 * alignof(std::max_align_t)) over_aligned {
  auto operator()() const -> void {}
};

static_assert(!std::is_constructible_v<util::in_place_function<void(), 256>, over_aligned>);

// A non-invocable type is rejected.
static_assert(!std::is_constructible_v<callback, int>);

// COMPILE-ERROR DOCUMENTATION: the following would fail to compile because the
// target exceeds the inline capacity; we do NOT instantiate it, only assert the
// negative trait above (big_functor into capacity 8).
//   util::in_place_function<void(), 8> bad{big_functor{}};  // ill-formed

auto free_doubler(int x) -> int {
  return x * 2;
}

TEST_CASE("nexenne::utility::in_place_function stores and invokes a capturing lambda") {
  auto cb{callback{[x = 42](int y) { return x + y; }}};
  CHECK(static_cast<bool>(cb));
  CHECK(cb(10) == 52);
}

TEST_CASE("nexenne::utility::in_place_function stores a free function pointer") {
  callback cb{&free_doubler};
  CHECK(static_cast<bool>(cb));
  CHECK(cb(21) == 42);

  callback cb2{free_doubler};  // decays to pointer
  CHECK(cb2(5) == 10);
}

TEST_CASE("nexenne::utility::in_place_function stores a non-capturing lambda") {
  callback cb{[](int y) { return y + 1; }};
  CHECK(cb(41) == 42);
}

TEST_CASE("nexenne::utility::in_place_function stores a stateful mutable lambda") {
  callback cb{[n = 0](int y) mutable {
    n += y;
    return n;
  }};
  CHECK(cb(2) == 2);
  CHECK(cb(3) == 5);  // mutable captures persist across calls
  CHECK(cb(10) == 15);
}

TEST_CASE("nexenne::utility::in_place_function stores a callable object") {
  struct multiplier {
    int factor;

    auto operator()(int y) const -> int {
      return y * factor;
    }
  };

  callback cb{multiplier{3}};
  CHECK(cb(4) == 12);
}

TEST_CASE("nexenne::utility::in_place_function is move-only and empties the source") {
  auto a{callback{[](int y) { return y * 2; }}};
  auto b{std::move(a)};
  CHECK(static_cast<bool>(b));
  CHECK_FALSE(static_cast<bool>(a));
  CHECK(b(5) == 10);

  callback c;
  c = std::move(b);
  CHECK(c(5) == 10);
  CHECK_FALSE(static_cast<bool>(b));
}

TEST_CASE("nexenne::utility::in_place_function nullptr, reset, reassign") {
  callback cb{nullptr};
  CHECK_FALSE(static_cast<bool>(cb));
  cb = [](int y) { return y; };
  CHECK(static_cast<bool>(cb));
  cb.reset();
  CHECK_FALSE(static_cast<bool>(cb));
}

TEST_CASE("nexenne::utility::in_place_function reset on an empty instance is a no-op") {
  callback cb;
  cb.reset();  // must not crash or assert
  CHECK_FALSE(static_cast<bool>(cb));
  cb.reset();
  CHECK_FALSE(static_cast<bool>(cb));
}

TEST_CASE("nexenne::utility::in_place_function reassignment destroys the old callable") {
  auto first{std::make_shared<int>(0)};
  callback cb{[first](int y) { return y; }};
  CHECK(first.use_count() == 2);

  auto second{std::make_shared<int>(0)};
  cb = callback{[second](int y) { return y; }};  // move-assign a fresh target
  CHECK(first.use_count() == 1);                 // old callable destroyed exactly once
  CHECK(second.use_count() == 2);                // new callable now held
  CHECK(static_cast<bool>(cb));
}

TEST_CASE("nexenne::utility::in_place_function invokes through a const wrapper") {
  int sum{0};
  auto cb{util::in_place_function<void(int), 32>{[&sum](int y) { sum += y; }}};
  auto const& cref{cb};
  cref(3);
  cref(4);
  CHECK(sum == 7);
}

TEST_CASE("nexenne::utility::in_place_function const wrapper invokes a mutating stored lambda") {
  // operator() is const, but a mutable stored lambda still mutates its captures.
  auto cb{util::in_place_function<int(), 32>{[n = 0]() mutable { return ++n; }}};
  auto const& cref{cb};
  CHECK(cref() == 1);
  CHECK(cref() == 2);
  CHECK(cref() == 3);
}

TEST_CASE("nexenne::utility::in_place_function self-move-assign is a no-op") {
  auto cb{callback{[](int y) { return y + 1; }}};
  auto& alias{cb};
  cb = std::move(alias);  // guarded self-move
  CHECK(static_cast<bool>(cb));
  CHECK(cb(1) == 2);
}

TEST_CASE("nexenne::utility::in_place_function self-move-assign does not destroy state") {
  auto tracker{std::make_shared<int>(0)};
  util::in_place_function<void(), 32> cb{[tracker] {}};
  CHECK(tracker.use_count() == 2);
  auto& alias{cb};
  cb = std::move(alias);
  CHECK(tracker.use_count() == 2);  // self-move preserved the stored callable
  CHECK(static_cast<bool>(cb));
}

TEST_CASE("nexenne::utility::in_place_function destroys the stored callable") {
  auto tracker{std::make_shared<int>(0)};
  {
    util::in_place_function<void(), 32> const held{[tracker] {}};
    CHECK(tracker.use_count() == 2);  // capture copied into storage
  }
  CHECK(tracker.use_count() == 1);  // destructor ran the callable's destructor
}

TEST_CASE("nexenne::utility::in_place_function move-assign destroys the old callable") {
  auto first{std::make_shared<int>(0)};
  auto second{std::make_shared<int>(0)};
  util::in_place_function<void(), 32> a{[first] {}};
  util::in_place_function<void(), 32> b{[second] {}};

  a = std::move(b);
  CHECK(first.use_count() == 1);  // a's previous callable destroyed exactly once
  CHECK(static_cast<bool>(a));
}

TEST_CASE("nexenne::utility::in_place_function move-construct transfers ownership exactly once") {
  auto tracker{std::make_shared<int>(0)};
  util::in_place_function<void(), 32> a{[tracker] {}};
  CHECK(tracker.use_count() == 2);
  util::in_place_function<void(), 32> b{std::move(a)};
  CHECK(tracker.use_count() == 2);  // moved, not copied: still exactly one holder
  CHECK_FALSE(static_cast<bool>(a));
  CHECK(static_cast<bool>(b));
}

TEST_CASE("nexenne::utility::in_place_function move-assign into empty does not leak") {
  auto tracker{std::make_shared<int>(0)};
  util::in_place_function<void(), 32> dst;
  {
    util::in_place_function<void(), 32> src{[tracker] {}};
    CHECK(tracker.use_count() == 2);
    dst = std::move(src);
  }
  CHECK(tracker.use_count() == 2);  // dst holds it; src destroyed empty
  dst.reset();
  CHECK(tracker.use_count() == 1);
}

TEST_CASE("nexenne::utility::in_place_function move preserves captured state") {
  auto cb{callback{[x = 100](int y) { return x + y; }}};
  auto const moved{std::move(cb)};
  CHECK(moved(5) == 105);
}

TEST_CASE("nexenne::utility::in_place_function destruction counter via explicit destructor") {
  // A target whose destructor increments a counter: verify destruction happens
  // exactly when expected (no leak, no double free).
  static int destructions{0};
  destructions = 0;

  struct tracked {
    bool active{true};
    tracked() = default;

    tracked(tracked&& other) noexcept {
      other.active = false;
    }

    tracked(tracked const&) = delete;
    auto operator=(tracked&&) -> tracked& = delete;
    auto operator=(tracked const&) -> tracked& = delete;

    ~tracked() {
      if (active) {
        ++destructions;
      }
    }

    auto operator()() const -> int {
      return 1;
    }
  };

  {
    util::in_place_function<int(), 32> a{tracked{}};
    CHECK(destructions == 0);
    util::in_place_function<int(), 32> b{std::move(a)};
    CHECK(destructions == 0);  // moved-from inner target was deactivated
    CHECK(b() == 1);
  }
  CHECK(destructions == 1);  // destroyed exactly once at end of scope
}

TEST_CASE("nexenne::utility::in_place_function returns by value, reference, and void") {
  // by value
  util::in_place_function<int(int), 32> by_value{[](int x) { return x + 1; }};
  CHECK(by_value(1) == 2);

  // by reference, with mutation propagating through the returned reference
  int store{0};
  util::in_place_function<int&(int), 32> by_ref{[&store](int v) -> int& {
    store = v;
    return store;
  }};
  int& ref{by_ref(7)};
  CHECK(&ref == &store);
  ref = 13;
  CHECK(store == 13);

  // void return with a by-reference out-parameter mutated in place
  util::in_place_function<void(int&), 32> voider{[](int& v) { v *= 2; }};
  int sink{21};
  voider(sink);
  CHECK(sink == 42);
}

TEST_CASE("nexenne::utility::in_place_function forwards multiple args and perfect-forwards") {
  util::in_place_function<int(int, int, int), 32> sum3{[](int a, int b, int c) { return a + b + c; }
  };
  CHECK(sum3(1, 2, 3) == 6);

  struct move_only {
    int v{0};
    move_only() = default;

    explicit move_only(int x) : v{x} {}

    move_only(move_only const&) = delete;
    move_only(move_only&&) = default;
    auto operator=(move_only const&) -> move_only& = delete;
    auto operator=(move_only&&) -> move_only& = default;
  };

  util::in_place_function<int(move_only), 32> taker{[](move_only m) { return m.v; }};
  CHECK(taker(move_only{9}) == 9);
}

TEST_CASE("nexenne::utility::in_place_function reassign from empty back to filled") {
  callback cb{[](int y) { return y; }};
  cb = nullptr;  // nullptr converts to an empty target
  CHECK_FALSE(static_cast<bool>(cb));
  cb = [](int y) { return y * 5; };
  CHECK(static_cast<bool>(cb));
  CHECK(cb(2) == 10);
}

}  // namespace
