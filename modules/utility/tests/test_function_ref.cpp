/**
 * @file
 * @brief Tests for nexenne::utility::function_ref.
 */

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <type_traits>
#include <utility>

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

auto add(int a, int b) -> int {
  return a + b;
}

auto returns_void(int& sink) -> void {
  sink += 1;
}

// A type whose copies/moves are counted, so we can assert function_ref never
// duplicates its target.
struct copy_counter {
  static inline int copies{0};
  static inline int moves{0};

  copy_counter() = default;

  copy_counter(copy_counter const&) {
    ++copies;
  }

  copy_counter(copy_counter&&) noexcept {
    ++moves;
  }

  auto operator=(copy_counter const&) -> copy_counter& {
    ++copies;
    return *this;
  }

  auto operator=(copy_counter&&) noexcept -> copy_counter& {
    ++moves;
    return *this;
  }

  ~copy_counter() = default;

  auto operator()() const -> int {
    return 7;
  }
};

struct stateful_functor {
  int total{0};

  auto operator()(int x) -> int {
    total += x;
    return total;
  }
};

// Traits: the converting constructor must not hijack copy/move construction of
// function_ref itself.
static_assert(std::is_nothrow_default_constructible_v<util::function_ref<int(int)>>);
static_assert(std::is_trivially_copyable_v<util::function_ref<int(int)>>);
static_assert(std::is_copy_constructible_v<util::function_ref<int(int)>>);
static_assert(std::is_copy_assignable_v<util::function_ref<int(int)>>);

// A non-invocable type is rejected by the converting constructor.
static_assert(!std::is_constructible_v<util::function_ref<int(int)>, std::string>);
// An incompatible signature (wrong return convertibility) is rejected.
static_assert(!std::is_constructible_v<util::function_ref<int*(int)>, decltype(&triple)>);

// signature_type is exposed.
static_assert(std::is_same_v<util::function_ref<int(int)>::signature_type, int(int)>);

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

TEST_CASE("nexenne::utility::function_ref binds a free function pointer (dedicated ctor)") {
  // Binds via the function_ptr overload, not via F&&.
  util::function_ref<int(int)> const fr{&triple};
  CHECK(fr(3) == 9);

  // Implicit decay of a function name to a pointer through the same overload.
  util::function_ref<int(int)> const fr2{triple};
  CHECK(fr2(3) == 9);

  // Reassigning to a different free function pointer.
  util::function_ref<int(int, int)> add_ref{&add};
  CHECK(add_ref(2, 3) == 5);
}

TEST_CASE("nexenne::utility::function_ref void return with side effect through a free fn") {
  int sink{0};
  util::function_ref<void(int&)> const fr{returns_void};
  fr(sink);
  fr(sink);
  CHECK(sink == 2);
}

TEST_CASE("nexenne::utility::function_ref binds a stateful functor by reference") {
  stateful_functor acc{};
  util::function_ref<int(int)> const fr{acc};
  CHECK(fr(3) == 3);
  CHECK(fr(4) == 7);
  CHECK(acc.total == 7);  // mutation visible on the live object, not a copy
}

TEST_CASE("nexenne::utility::function_ref does NOT copy or move its target") {
  copy_counter::copies = 0;
  copy_counter::moves = 0;
  copy_counter target{};
  util::function_ref<int()> const fr{target};
  CHECK(fr() == 7);
  CHECK(fr() == 7);
  CHECK(copy_counter::copies == 0);
  CHECK(copy_counter::moves == 0);
  // The view must operate on the very same object we passed.
  auto probe{[&] { return fr() == target(); }};
  CHECK(probe());
}

TEST_CASE("nexenne::utility::function_ref refers to the live object after mutation") {
  stateful_functor acc{};
  util::function_ref<int(int)> const fr{acc};
  acc.total = 100;  // mutate the target after the view was created
  CHECK(fr(1) == 101);
}

TEST_CASE("nexenne::utility::function_ref copies are independent views over the same target") {
  int calls{0};
  auto counter{[&](int x) {
    ++calls;
    return x;
  }};
  util::function_ref<int(int)> const fr{counter};
  util::function_ref<int(int)> const copy{fr};  // copy of the view, not the target
  CHECK(static_cast<bool>(copy));
  CHECK(fr(1) == 1);
  CHECK(copy(2) == 2);
  CHECK(calls == 2);  // both views forward to the same captured lambda
}

TEST_CASE("nexenne::utility::function_ref forwards multiple args and returns by value") {
  util::function_ref<int(int, int)> const fr{[](int a, int b) { return a * b; }};
  CHECK(fr(6, 7) == 42);
}

TEST_CASE("nexenne::utility::function_ref returns by reference and propagates mutation") {
  int store{0};
  // The callable must outlive the non-owning function_ref, so it is a named
  // local rather than a temporary (a temporary capturing lambda would dangle).
  auto setter{[&store](int v) -> int& {
    store = v;
    return store;
  }};
  util::function_ref<int&(int)> const fr{setter};
  int& ref{fr(11)};
  CHECK(&ref == &store);
  ref = 99;
  CHECK(store == 99);
}

TEST_CASE("nexenne::utility::function_ref perfect-forwards a move-only argument") {
  struct move_only {
    int v{0};
    move_only() = default;

    explicit move_only(int x) : v{x} {}

    move_only(move_only const&) = delete;
    move_only(move_only&&) = default;
    auto operator=(move_only const&) -> move_only& = delete;
    auto operator=(move_only&&) -> move_only& = default;
  };

  util::function_ref<int(move_only)> const fr{[](move_only m) { return m.v; }};
  CHECK(fr(move_only{5}) == 5);
}

TEST_CASE("nexenne::utility::function_ref empty-state via operator bool both ways") {
  util::function_ref<int(int)> fr;
  CHECK_FALSE(static_cast<bool>(fr));
  fr = util::function_ref<int(int)>{triple};
  CHECK(static_cast<bool>(fr));
  CHECK(fr(1) == 3);
}

TEST_CASE("nexenne::utility::function_ref operator bool is usable in constant expressions") {
  constexpr util::function_ref<int(int)> empty;
  static_assert(!static_cast<bool>(empty));
  CHECK_FALSE(static_cast<bool>(empty));
}

TEST_CASE("nexenne::utility::function_ref binds a non-capturing lambda by value passing") {
  // Non-capturing lambda lives at the call site; bound as an rvalue F&&.
  util::function_ref<int(int)> const fr{[](int x) { return x - 1; }};
  CHECK(fr(10) == 9);
}

TEST_CASE("nexenne::utility::function_ref binds a mutable lambda and mutates it") {
  auto mut{[n = 0](int x) mutable {
    n += x;
    return n;
  }};
  util::function_ref<int(int)> const fr{mut};
  CHECK(fr(2) == 2);
  CHECK(fr(3) == 5);  // mutable state of the referenced lambda persists
}

}  // namespace
