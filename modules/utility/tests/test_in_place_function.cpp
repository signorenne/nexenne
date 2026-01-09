/**
 * @file
 * @brief Tests for nexenne::utility::in_place_function.
 */

#include <doctest/doctest.h>

#include <array>
#include <memory>
#include <utility>

#include <nexenne/utility/in_place_function.hpp>

namespace {

namespace util = nexenne::utility;

using callback = util::in_place_function<int(int), 32>;

static_assert(callback::capacity == 32);
static_assert(!std::is_copy_constructible_v<callback>);
static_assert(std::is_move_constructible_v<callback>);

struct big_functor {
  std::array<char, 64> data{};

  auto operator()() const -> void {}
};

// A too-large callable is rejected at compile time; a fitting capacity accepts it.
static_assert(!std::is_constructible_v<util::in_place_function<void(), 8>, big_functor>);
static_assert(std::is_constructible_v<util::in_place_function<void(), 64>, big_functor>);

TEST_CASE("nexenne::utility::in_place_function stores and invokes a capturing lambda") {
  auto cb{callback{[x = 42](int y) { return x + y; }}};
  CHECK(static_cast<bool>(cb));
  CHECK(cb(10) == 52);
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

TEST_CASE("nexenne::utility::in_place_function invokes through a const wrapper") {
  int sum{0};
  auto cb{util::in_place_function<void(int), 32>{[&sum](int y) { sum += y; }}};
  auto const& cref{cb};
  cref(3);
  cref(4);
  CHECK(sum == 7);
}

TEST_CASE("nexenne::utility::in_place_function self-move-assign is a no-op") {
  auto cb{callback{[](int y) { return y + 1; }}};
  auto& alias{cb};
  cb = std::move(alias);  // guarded self-move
  CHECK(static_cast<bool>(cb));
  CHECK(cb(1) == 2);
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

TEST_CASE("nexenne::utility::in_place_function move preserves captured state") {
  auto cb{callback{[x = 100](int y) { return x + y; }}};
  auto const moved{std::move(cb)};
  CHECK(moved(5) == 105);
}

}  // namespace
