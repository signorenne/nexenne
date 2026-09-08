/**
 * @file
 * @brief Tests for nexenne::utility::in_place_function.
 */

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <memory>
#include <stdexcept>
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

static_assert(util::in_place_function<void()>::capacity == 64);

struct big_functor {
  std::array<char, 64> data{};

  auto operator()() const -> void {}
};

static_assert(!std::is_constructible_v<util::in_place_function<void(), 8>, big_functor>);
static_assert(std::is_constructible_v<util::in_place_function<void(), 64>, big_functor>);

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

struct alignas(2 * alignof(std::max_align_t)) over_aligned {
  auto operator()() const -> void {}
};

static_assert(!std::is_constructible_v<util::in_place_function<void(), 256>, over_aligned>);

static_assert(!std::is_constructible_v<callback, int>);

struct returns_pointer {
  auto operator()(int) const -> char const* {
    return "x";
  }
};

static_assert(!std::is_constructible_v<callback, returns_pointer>);

struct throwing_move {
  throwing_move() = default;

  throwing_move(throwing_move&&) noexcept(false) {}

  throwing_move(throwing_move const&) = default;
  auto operator=(throwing_move&&) -> throwing_move& = delete;
  auto operator=(throwing_move const&) -> throwing_move& = delete;
  ~throwing_move() = default;

  auto operator()(int) const -> int {
    return 0;
  }
};

static_assert(!std::is_constructible_v<callback, throwing_move>);

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

  callback cb2{free_doubler};
  CHECK(cb2(5) == 10);
}

struct gauge {
  int level{};

  [[nodiscard]] auto read() const -> int {
    return level;
  }
};

TEST_CASE("nexenne::utility::in_place_function from a null pointer or empty wrapper is empty") {
  using doubler_ptr = int (*)(int);
  doubler_ptr const null_fn{nullptr};
  CHECK_FALSE(static_cast<bool>(callback{null_fn}));

  using read_ptr = int (gauge::*)() const;
  read_ptr const null_member{nullptr};
  CHECK_FALSE(static_cast<bool>(util::in_place_function<int(gauge const&)>{null_member}));

  CHECK_FALSE(static_cast<bool>(util::in_place_function<int(int), 64>{callback{}}));

  auto const reader{util::in_place_function<int(gauge const&)>{&gauge::read}};
  REQUIRE(static_cast<bool>(reader));
  CHECK(reader(gauge{7}) == 7);
  auto const nested{util::in_place_function<int(int), 64>{callback{&free_doubler}}};
  REQUIRE(static_cast<bool>(nested));
  CHECK(nested(4) == 8);
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
  CHECK(cb(3) == 5);
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
  cb.reset();
  CHECK_FALSE(static_cast<bool>(cb));
  cb.reset();
  CHECK_FALSE(static_cast<bool>(cb));
}

TEST_CASE("nexenne::utility::in_place_function reassignment destroys the old callable") {
  auto first{std::make_shared<int>(0)};
  callback cb{[first](int y) { return y; }};
  CHECK(first.use_count() == 2);

  auto second{std::make_shared<int>(0)};
  cb = callback{[second](int y) { return y; }};
  CHECK(first.use_count() == 1);
  CHECK(second.use_count() == 2);
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
  auto cb{util::in_place_function<int(), 32>{[n = 0]() mutable { return ++n; }}};
  auto const& cref{cb};
  CHECK(cref() == 1);
  CHECK(cref() == 2);
  CHECK(cref() == 3);
}

TEST_CASE("nexenne::utility::in_place_function self-move-assign is a no-op") {
  auto cb{callback{[](int y) { return y + 1; }}};
  auto& alias{cb};
  cb = std::move(alias);
  CHECK(static_cast<bool>(cb));
  CHECK(cb(1) == 2);
}

TEST_CASE("nexenne::utility::in_place_function self-move-assign does not destroy state") {
  auto tracker{std::make_shared<int>(0)};
  util::in_place_function<void(), 32> cb{[tracker] {}};
  CHECK(tracker.use_count() == 2);
  auto& alias{cb};
  cb = std::move(alias);
  CHECK(tracker.use_count() == 2);
  CHECK(static_cast<bool>(cb));
}

TEST_CASE("nexenne::utility::in_place_function destroys the stored callable") {
  auto tracker{std::make_shared<int>(0)};
  {
    util::in_place_function<void(), 32> const held{[tracker] {}};
    CHECK(tracker.use_count() == 2);
  }
  CHECK(tracker.use_count() == 1);
}

TEST_CASE("nexenne::utility::in_place_function move-assign destroys the old callable") {
  auto first{std::make_shared<int>(0)};
  auto second{std::make_shared<int>(0)};
  util::in_place_function<void(), 32> a{[first] {}};
  util::in_place_function<void(), 32> b{[second] {}};

  a = std::move(b);
  CHECK(first.use_count() == 1);
  CHECK(static_cast<bool>(a));
}

TEST_CASE("nexenne::utility::in_place_function move-construct transfers ownership exactly once") {
  auto tracker{std::make_shared<int>(0)};
  util::in_place_function<void(), 32> a{[tracker] {}};
  CHECK(tracker.use_count() == 2);
  util::in_place_function<void(), 32> b{std::move(a)};
  CHECK(tracker.use_count() == 2);
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
  CHECK(tracker.use_count() == 2);
  dst.reset();
  CHECK(tracker.use_count() == 1);
}

TEST_CASE("nexenne::utility::in_place_function move preserves captured state") {
  auto cb{callback{[x = 100](int y) { return x + y; }}};
  auto const moved{std::move(cb)};
  CHECK(moved(5) == 105);
}

TEST_CASE("nexenne::utility::in_place_function destruction counter via explicit destructor") {
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
    CHECK(destructions == 0);
    CHECK(b() == 1);
  }
  CHECK(destructions == 1);
}

TEST_CASE("nexenne::utility::in_place_function returns by value, reference, and void") {
  util::in_place_function<int(int), 32> by_value{[](int x) { return x + 1; }};
  CHECK(by_value(1) == 2);

  int store{0};
  util::in_place_function<int&(int), 32> by_ref{[&store](int v) -> int& {
    store = v;
    return store;
  }};
  int& ref{by_ref(7)};
  CHECK(&ref == &store);
  ref = 13;
  CHECK(store == 13);

  util::in_place_function<void(int&), 32> voider{[](int& v) { v *= 2; }};
  int sink{21};
  voider(sink);
  CHECK(sink == 42);
}

TEST_CASE("nexenne::utility::in_place_function forwards multiple args and perfect-forwards") {
  util::in_place_function<int(int, int, int), 32> sum3{[](int a, int b, int c) {
    return a + b + c;
  }};
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
  cb = nullptr;
  CHECK_FALSE(static_cast<bool>(cb));
  cb = [](int y) { return y * 5; };
  CHECK(static_cast<bool>(cb));
  CHECK(cb(2) == 10);
}

TEST_CASE("nexenne::utility::in_place_function void signature discards the callable's return") {
  int calls{0};
  util::in_place_function<void(int), 32> cb{[&calls](int x) {
    ++calls;
    return x;
  }};
  cb(1);
  cb(2);
  CHECK(calls == 2);
}

TEST_CASE("nexenne::utility::in_place_function propagates exceptions from the callable") {
  util::in_place_function<int(int), 32> cb{[](int x) -> int {
    if (x < 0) {
      throw std::runtime_error{"negative"};
    }
    return x;
  }};
  CHECK(cb(3) == 3);
  CHECK_THROWS_AS(cb(-1), std::runtime_error);
  CHECK(static_cast<bool>(cb));
  CHECK(cb(4) == 4);
}

TEST_CASE("nexenne::utility::in_place_function const-defined wrapper runs a mutable lambda") {
  auto const cb{util::in_place_function<int(), 32>{[n = 0]() mutable { return ++n; }}};
  CHECK(cb() == 1);
  CHECK(cb() == 2);
  CHECK(cb() == 3);
}

}  // namespace
