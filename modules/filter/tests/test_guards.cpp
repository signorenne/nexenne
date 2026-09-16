/**
 * @file
 * @brief Deep, exhaustive tests for the nexenne::filter validation-guard
 * filters: range_guard, rate_guard, validator, majority, stale_detector.
 */

#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

#include <nexenne/filter/filter.hpp>
#include <nexenne/utility/ignore.hpp>

namespace {

namespace flt = nexenne::filter;

static_assert(flt::filter_like<flt::range_guard<double>>);
static_assert(flt::filter_like<flt::range_guard<int>>);
static_assert(flt::filter_like<flt::rate_guard<double>>);
static_assert(flt::filter_like<flt::rate_guard<float>>);
static_assert(flt::filter_like<flt::majority<int, 3>>);
static_assert(flt::filter_like<flt::majority<bool, 5>>);
static_assert(flt::filter_like<flt::stale_detector<int, 3>>);
static_assert(flt::filter_like<flt::stale_detector<double, 10>>);

TEST_CASE("nexenne::filter::range_guard accepts in-range and holds last valid") {
  auto f{flt::range_guard{0.0, 100.0}};
  CHECK(f.primed() == false);
  CHECK(f.value() == doctest::Approx(0.0));

  CHECK(f.push(50.0) == doctest::Approx(50.0));
  CHECK(f.primed() == true);
  CHECK(f.push(200.0) == doctest::Approx(50.0));
  CHECK(f.push(75.0) == doctest::Approx(75.0));
  CHECK(f.push(-10.0) == doctest::Approx(75.0));
  CHECK(f.value() == doctest::Approx(75.0));
}

TEST_CASE("nexenne::filter::range_guard exact boundaries lo and hi are inclusive") {
  auto f{flt::range_guard{10.0, 90.0}};
  CHECK(f.push(10.0) == doctest::Approx(10.0));
  CHECK(f.push(90.0) == doctest::Approx(90.0));
  CHECK(f.push(9.0) == doctest::Approx(90.0));
  CHECK(f.push(91.0) == doctest::Approx(90.0));
}

TEST_CASE("nexenne::filter::range_guard clamps an out-of-range first sample to nearest bound") {
  auto above{flt::range_guard{10.0, 90.0}};
  CHECK(above.push(200.0) == doctest::Approx(90.0));
  CHECK(above.primed() == true);

  auto below{flt::range_guard{10.0, 90.0}};
  CHECK(below.push(-5.0) == doctest::Approx(10.0));
  CHECK(below.primed() == true);
  CHECK(below.push(1000.0) == doctest::Approx(10.0));
}

TEST_CASE("nexenne::filter::range_guard reset re-arms the first-sample clamp") {
  auto f{flt::range_guard{0.0, 10.0}};
  CHECK(f.push(5.0) == doctest::Approx(5.0));
  f.reset();
  CHECK(f.primed() == false);
  CHECK(f.value() == doctest::Approx(0.0));
  CHECK(f.push(20.0) == doctest::Approx(10.0));
  CHECK(f.primed() == true);
}

TEST_CASE("nexenne::filter::range_guard degenerate range lo == hi admits only that value") {
  auto f{flt::range_guard{5.0, 5.0}};
  CHECK(f.push(5.0) == doctest::Approx(5.0));
  CHECK(f.push(4.0) == doctest::Approx(5.0));
  CHECK(f.push(6.0) == doctest::Approx(5.0));
}

TEST_CASE("nexenne::filter::range_guard range() swaps bounds without touching held value") {
  auto f{flt::range_guard{0.0, 10.0}};
  CHECK(f.push(8.0) == doctest::Approx(8.0));
  f.range(20.0, 30.0);
  CHECK(f.lo() == doctest::Approx(20.0));
  CHECK(f.hi() == doctest::Approx(30.0));
  CHECK(f.value() == doctest::Approx(8.0));
  CHECK(f.push(15.0) == doctest::Approx(8.0));
  CHECK(f.push(25.0) == doctest::Approx(25.0));
}

TEST_CASE("nexenne::filter::range_guard integer instantiation") {
  auto f{flt::range_guard<int>{-100, 100}};
  CHECK(f.push(0) == 0);
  CHECK(f.push(100) == 100);
  CHECK(f.push(-100) == -100);
  CHECK(f.push(101) == -100);
  CHECK(f.push(-101) == -100);
}

TEST_CASE("nexenne::filter::range_guard NaN never passes and never clamps to a number") {
  auto const nan{std::numeric_limits<double>::quiet_NaN()};
  auto first{flt::range_guard{0.0, 10.0}};
  CHECK(first.push(nan) == doctest::Approx(0.0));
  CHECK(first.primed() == false);

  auto later{flt::range_guard{0.0, 10.0}};
  CHECK(later.push(5.0) == doctest::Approx(5.0));
  CHECK(later.push(nan) == doctest::Approx(5.0));
}

TEST_CASE("nexenne::filter::range_guard infinity is out of a finite range") {
  auto const inf{std::numeric_limits<double>::infinity()};
  auto f{flt::range_guard{0.0, 10.0}};
  CHECK(f.push(5.0) == doctest::Approx(5.0));
  CHECK(f.push(inf) == doctest::Approx(5.0));
  CHECK(f.push(-inf) == doctest::Approx(5.0));
}

TEST_CASE("nexenne::filter::rate_guard first sample seeds and within-rate passes") {
  auto f{flt::rate_guard{5.0}};
  CHECK(f.value() == doctest::Approx(0.0));
  CHECK(f.push(100.0) == doctest::Approx(100.0));
  CHECK(f.push(103.0) == doctest::Approx(103.0));
  CHECK(f.push(200.0) == doctest::Approx(103.0));
  CHECK(f.push(105.0) == doctest::Approx(105.0));
  CHECK(f.max_delta() == doctest::Approx(5.0));
}

TEST_CASE("nexenne::filter::rate_guard exact-boundary delta is accepted (<=)") {
  auto f{flt::rate_guard{5.0}};
  nexenne::utility::ignore(f.push(10.0));
  CHECK(f.push(15.0) == doctest::Approx(15.0));
  CHECK(f.push(10.0) == doctest::Approx(10.0));
  CHECK(f.push(15.0001) == doctest::Approx(10.0));
}

TEST_CASE("nexenne::filter::rate_guard rejection is measured from the held value, not the input") {
  auto f{flt::rate_guard{5.0}};
  nexenne::utility::ignore(f.push(0.0));
  CHECK(f.push(100.0) == doctest::Approx(0.0));
  CHECK(f.push(3.0) == doctest::Approx(3.0));
}

TEST_CASE("nexenne::filter::rate_guard negative max_delta clamps to zero and freezes after first") {
  auto f{flt::rate_guard{-1.0}};
  CHECK(f.max_delta() == doctest::Approx(0.0));
  CHECK(f.push(7.0) == doctest::Approx(7.0));
  CHECK(f.push(7.0) == doctest::Approx(7.0));
  CHECK(f.push(8.0) == doctest::Approx(7.0));
}

TEST_CASE("nexenne::filter::rate_guard max_delta changes through its accessor and keeps value") {
  auto f{flt::rate_guard{1.0}};
  nexenne::utility::ignore(f.push(50.0));
  f.max_delta() = 20.0;
  CHECK(f.max_delta() == doctest::Approx(20.0));
  CHECK(f.push(65.0) == doctest::Approx(65.0));
}

TEST_CASE("nexenne::filter::rate_guard reset() unprimes so next push seeds") {
  auto f{flt::rate_guard{2.0}};
  nexenne::utility::ignore(f.push(50.0));
  f.reset();
  CHECK(f.value() == doctest::Approx(0.0));
  CHECK(f.max_delta() == doctest::Approx(2.0));
  CHECK(f.push(1000.0) == doctest::Approx(1000.0));
}

TEST_CASE("nexenne::filter::rate_guard reset(initial) primes to a known value") {
  auto f{flt::rate_guard{5.0}};
  f.reset(20.0);
  CHECK(f.value() == doctest::Approx(20.0));
  CHECK(f.push(100.0) == doctest::Approx(20.0));
  CHECK(f.push(24.0) == doctest::Approx(24.0));
}

TEST_CASE("nexenne::filter::rate_guard float instantiation") {
  auto f{flt::rate_guard<float>{0.5f}};
  CHECK(f.push(1.0f) == doctest::Approx(1.0));
  CHECK(f.push(1.5f) == doctest::Approx(1.5));
  CHECK(f.push(3.0f) == doctest::Approx(1.5));
}

TEST_CASE("nexenne::filter::rate_guard NaN sample fails the <= test and is rejected") {
  auto const nan{std::numeric_limits<double>::quiet_NaN()};
  auto f{flt::rate_guard{5.0}};
  nexenne::utility::ignore(f.push(10.0));
  CHECK(f.push(nan) == doctest::Approx(10.0));
  CHECK(f.push(12.0) == doctest::Approx(12.0));
}

TEST_CASE("nexenne::filter::rate_guard NaN as the first sample does not seed") {
  auto const nan{std::numeric_limits<double>::quiet_NaN()};
  auto f{flt::rate_guard{5.0}};
  CHECK(f.push(nan) == doctest::Approx(0.0));
  CHECK_FALSE(f.accepted());
  CHECK(f.push(5.0) == doctest::Approx(5.0));
}

TEST_CASE("nexenne::filter::validator unprimed accepts the first sample unconditionally") {
  auto const pred{[](int const& x) { return x > 0; }};
  auto f{flt::validator<int, decltype(pred)>{pred}};
  CHECK(f.push(-7) == -7);
  CHECK(f.value() == -7);
  CHECK(f.push(-1) == -7);
  CHECK(f.push(4) == 4);
}

TEST_CASE("nexenne::filter::validator primed ctor rejects a bad first sample") {
  auto f{flt::validator{[](int const& x) { return x > 0; }, int{99}}};
  CHECK(f.value() == 99);
  CHECK(f.push(-3) == 99);
  CHECK(f.push(5) == 5);
}

TEST_CASE("nexenne::filter::validator passes valid, holds last-good through a run of invalids") {
  auto f{flt::validator{[](int const& x) { return x > 0; }, int{0}}};
  nexenne::utility::ignore(f.push(5));
  CHECK(f.push(-1) == 5);
  CHECK(f.push(0) == 5);
  CHECK(f.push(-9) == 5);
  CHECK(f.push(7) == 7);
  CHECK(f.value() == 7);
}

TEST_CASE("nexenne::filter::validator reset unprimes so next push is accepted unconditionally") {
  auto f{flt::validator{[](int const& x) { return x > 0; }, int{50}}};
  nexenne::utility::ignore(f.push(10));
  f.reset();
  CHECK(f.value() == 0);
  CHECK(f.push(-100) == -100);
  CHECK(f.push(-5) == -100);
}

TEST_CASE("nexenne::filter::validator parity predicate over a register type") {
  auto f{flt::validator{[](std::uint16_t const& r) { return (r & 1u) == 0u; }, std::uint16_t{0}}};
  CHECK(f.push(std::uint16_t{4}) == std::uint16_t{4});
  CHECK(f.push(std::uint16_t{7}) == std::uint16_t{4});
  CHECK(f.push(std::uint16_t{8}) == std::uint16_t{8});
}

TEST_CASE("nexenne::filter::validator floating predicate with finite check") {
  auto const nan{std::numeric_limits<double>::quiet_NaN()};
  auto const inf{std::numeric_limits<double>::infinity()};
  auto f{flt::validator{[](double const& x) { return std::isfinite(x); }, 0.0}};
  CHECK(f.push(1.5) == doctest::Approx(1.5));
  CHECK(f.push(nan) == doctest::Approx(1.5));
  CHECK(f.push(inf) == doctest::Approx(1.5));
  CHECK(f.push(2.5) == doctest::Approx(2.5));
}

TEST_CASE("nexenne::filter::validator satisfies filter_like") {
  auto const pred{[](double const& x) { return x > 0.0; }};
  using validator_t = flt::validator<double, decltype(pred)>;
  static_assert(flt::filter_like<validator_t>);
  CHECK(true);
}

TEST_CASE("nexenne::filter::majority corrects a single corrupted read out of three") {
  auto f{flt::majority<int, 3>{}};
  CHECK(f.filled() == false);
  CHECK(f.value() == 0);
  nexenne::utility::ignore(f.push(42));
  nexenne::utility::ignore(f.push(42));
  CHECK(f.push(99) == 42);
  CHECK(f.filled() == true);
}

TEST_CASE("nexenne::filter::majority fill transient resolves the running mode each push") {
  auto f{flt::majority<int, 3>{}};
  CHECK(f.push(7) == 7);
  CHECK(f.push(7) == 7);
  CHECK(f.push(8) == 7);
  CHECK(f.filled() == true);
}

TEST_CASE("nexenne::filter::majority tracks a real shift once the window refills") {
  auto f{flt::majority<int, 3>{}};
  nexenne::utility::ignore(f.push(10));
  nexenne::utility::ignore(f.push(10));
  nexenne::utility::ignore(f.push(10));
  nexenne::utility::ignore(f.push(20));
  CHECK(f.value() == 10);
  nexenne::utility::ignore(f.push(20));
  CHECK(f.value() == 20);
  CHECK(f.push(20) == 20);
}

TEST_CASE("nexenne::filter::majority all-distinct window breaks ties toward the newest") {
  auto f{flt::majority<int, 3>{}};
  nexenne::utility::ignore(f.push(1));
  nexenne::utility::ignore(f.push(2));
  CHECK(f.push(3) == 3);
}

TEST_CASE("nexenne::filter::majority window size 1 always returns the latest sample") {
  auto f{flt::majority<int, 1>{}};
  CHECK(f.push(5) == 5);
  CHECK(f.filled() == true);
  CHECK(f.push(9) == 9);
  CHECK(f.push(9) == 9);
  CHECK(f.value() == 9);
}

TEST_CASE("nexenne::filter::majority all-equal window returns that value") {
  auto f{flt::majority<int, 5>{}};
  for (auto i{0}; i < 5; ++i) {
    nexenne::utility::ignore(f.push(3));
  }
  CHECK(f.filled() == true);
  CHECK(f.value() == 3);
}

TEST_CASE("nexenne::filter::majority bool default type votes a 3-window") {
  auto f{flt::majority<>{}};
  nexenne::utility::ignore(f.push(true));
  nexenne::utility::ignore(f.push(true));
  CHECK(f.push(false) == true);
  CHECK(f.push(false) == false);
}

TEST_CASE("nexenne::filter::majority tie-after-wrap: newest sample wins the even-window tie") {
  auto f{flt::majority<int, 2>{}};
  CHECK(f.push(5) == 5);
  CHECK(f.push(7) == 7);
  CHECK(f.push(9) == 9);
  CHECK(f.push(3) == 3);
}

TEST_CASE("nexenne::filter::majority even window keeps a clear majority across a wrap") {
  auto f{flt::majority<int, 4>{}};
  nexenne::utility::ignore(f.push(1));
  nexenne::utility::ignore(f.push(2));
  nexenne::utility::ignore(f.push(2));
  CHECK(f.push(2) == 2);
  CHECK(f.push(2) == 2);
  nexenne::utility::ignore(f.push(5));
  CHECK(f.value() == 2);
}

TEST_CASE("nexenne::filter::majority reset empties the buffer and clears value") {
  auto f{flt::majority<int, 3>{}};
  nexenne::utility::ignore(f.push(1));
  nexenne::utility::ignore(f.push(1));
  nexenne::utility::ignore(f.push(1));
  CHECK(f.filled() == true);
  f.reset();
  CHECK(f.filled() == false);
  CHECK(f.value() == 0);
  CHECK(f.push(8) == 8);
}

TEST_CASE("nexenne::filter::stale_detector flags after exactly N identical samples") {
  auto f{flt::stale_detector<int, 3>{}};
  CHECK(f.is_stale() == false);
  CHECK(f.streak() == 0);

  CHECK(f.push(42) == 42);
  CHECK(f.is_stale() == false);
  CHECK(f.streak() == 1);
  nexenne::utility::ignore(f.push(42));
  CHECK(f.is_stale() == false);
  CHECK(f.streak() == 2);
  nexenne::utility::ignore(f.push(42));
  CHECK(f.is_stale() == true);
  CHECK(f.streak() == 3);
}

TEST_CASE("nexenne::filter::stale_detector streak saturates at N and stays stale") {
  auto f{flt::stale_detector<int, 3>{}};
  for (auto i{0}; i < 6; ++i) {
    nexenne::utility::ignore(f.push(1));
  }
  CHECK(f.is_stale() == true);
  CHECK(f.streak() == 3);
}

TEST_CASE("nexenne::filter::stale_detector a changed value clears staleness and resets streak") {
  auto f{flt::stale_detector<int, 3>{}};
  nexenne::utility::ignore(f.push(1));
  nexenne::utility::ignore(f.push(1));
  nexenne::utility::ignore(f.push(1));
  CHECK(f.is_stale() == true);
  CHECK(f.push(2) == 2);
  CHECK(f.is_stale() == false);
  CHECK(f.streak() == 1);
}

TEST_CASE("nexenne::filter::stale_detector passes every value through unchanged") {
  auto f{flt::stale_detector<int, 5>{}};
  CHECK(f.push(10) == 10);
  CHECK(f.value() == 10);
  CHECK(f.push(20) == 20);
  CHECK(f.value() == 20);
  CHECK(f.push(30) == 30);
  CHECK(f.is_stale() == false);
}

TEST_CASE("nexenne::filter::stale_detector N == 1 is stale immediately on the first sample") {
  auto f{flt::stale_detector<int, 1>{}};
  CHECK(f.push(7) == 7);
  CHECK(f.is_stale() == true);
  CHECK(f.streak() == 1);
  CHECK(f.push(8) == 8);
  CHECK(f.is_stale() == true);
}

TEST_CASE("nexenne::filter::stale_detector reset returns to the unprimed condition") {
  auto f{flt::stale_detector<int, 3>{}};
  nexenne::utility::ignore(f.push(4));
  nexenne::utility::ignore(f.push(4));
  nexenne::utility::ignore(f.push(4));
  CHECK(f.is_stale() == true);
  f.reset();
  CHECK(f.is_stale() == false);
  CHECK(f.streak() == 0);
  CHECK(f.value() == 0);
  CHECK(f.push(4) == 4);
  CHECK(f.streak() == 1);
  CHECK(f.is_stale() == false);
}

TEST_CASE("nexenne::filter::stale_detector double type with default N flags a frozen reading") {
  auto f{flt::stale_detector<double, 10>{}};
  for (auto i{0}; i < 9; ++i) {
    nexenne::utility::ignore(f.push(1.5));
  }
  CHECK(f.is_stale() == false);
  CHECK(f.streak() == 9);
  nexenne::utility::ignore(f.push(1.5));
  CHECK(f.is_stale() == true);
  CHECK(f.value() == doctest::Approx(1.5));
}

TEST_CASE("nexenne::filter::stale_detector alternating values never go stale") {
  auto f{flt::stale_detector<int, 3>{}};
  for (auto i{0}; i < 10; ++i) {
    nexenne::utility::ignore(f.push(i % 2));
    CHECK(f.is_stale() == false);
    CHECK(f.streak() == 1);
  }
}

TEST_CASE("nexenne::filter::range_guard accepted() reflects the last push, primed() latches") {
  auto f{flt::range_guard{0.0, 10.0}};
  CHECK(f.primed() == false);
  CHECK(f.accepted() == false);

  nexenne::utility::ignore(f.push(5.0));
  CHECK(f.primed() == true);
  CHECK(f.accepted() == true);

  nexenne::utility::ignore(f.push(50.0));
  CHECK(f.primed() == true);
  CHECK(f.accepted() == false);

  nexenne::utility::ignore(f.push(6.0));
  CHECK(f.accepted() == true);
}

TEST_CASE("nexenne::filter::rate_guard escape hatch recovers from a genuine step") {
  auto locked{flt::rate_guard{1.0}};
  nexenne::utility::ignore(locked.push(0.0));
  for (auto i{0}; i < 100; ++i) {
    nexenne::utility::ignore(locked.push(50.0));
  }
  CHECK(locked.value() == doctest::Approx(0.0));
  CHECK(locked.accepted() == false);
  CHECK(locked.rejected_streak() == 100);

  auto escaping{flt::rate_guard{1.0, 3}};
  nexenne::utility::ignore(escaping.push(0.0));
  CHECK(escaping.push(50.0) == doctest::Approx(0.0));
  CHECK(escaping.push(50.0) == doctest::Approx(0.0));
  CHECK(escaping.push(50.0) == doctest::Approx(0.0));
  CHECK(escaping.push(50.0) == doctest::Approx(50.0));
  CHECK(escaping.accepted() == true);
  CHECK(escaping.rejected_streak() == 0);
  CHECK(escaping.push(51.0) == doctest::Approx(51.0));
}

TEST_CASE("nexenne::filter::stale_detector flags a source frozen at NaN") {
  auto const nan{std::numeric_limits<double>::quiet_NaN()};
  auto f{flt::stale_detector<double, 3>{}};
  for (auto i{0}; i < 10; ++i) {
    nexenne::utility::ignore(f.push(nan));
  }
  CHECK(f.is_stale() == true);
  CHECK(f.streak() == 3);
}

TEST_CASE("nexenne::filter::validator push is conditionally noexcept") {
  auto const nothrow_pred{[](int const& x) noexcept { return x > 0; }};
  auto const throwing_pred{[](int const& x) { return x > 0; }};
  auto nothrow_val{flt::validator<int, decltype(nothrow_pred)>{nothrow_pred}};
  auto throwing_val{flt::validator<int, decltype(throwing_pred)>{throwing_pred}};
  static_assert(noexcept(nothrow_val.push(1)));
  static_assert(!noexcept(throwing_val.push(1)));
  CHECK(nothrow_val.push(1) == 1);
}

TEST_CASE("nexenne::filter::rate_guard escape_after 1 still rejects the first spike") {
  auto guard{flt::rate_guard{1.0, 1}};
  nexenne::utility::ignore(guard.push(0.0));
  CHECK(guard.push(50.0) == doctest::Approx(0.0));
  CHECK_FALSE(guard.accepted());
  CHECK(guard.push(50.0) == doctest::Approx(50.0));
  CHECK(guard.accepted());
}

TEST_CASE("nexenne::filter guards never prime on a NaN sample") {
  auto const nan{std::numeric_limits<double>::quiet_NaN()};
  auto range{flt::range_guard{0.0, 10.0}};
  nexenne::utility::ignore(range.push(nan));
  CHECK_FALSE(range.accepted());
  CHECK_FALSE(range.primed());
  CHECK(range.push(20.0) == doctest::Approx(10.0));

  auto rate{flt::rate_guard{1.0}};
  nexenne::utility::ignore(rate.push(nan));
  CHECK_FALSE(rate.accepted());
  CHECK(rate.push(5.0) == doctest::Approx(5.0));
  CHECK(rate.push(nan) == doctest::Approx(5.0));
  CHECK(rate.rejected_streak() == 0);
  CHECK(rate.push(5.5) == doctest::Approx(5.5));
}

static_assert(!noexcept(std::declval<flt::range_guard<std::string>&>().push(std::string{})));
static_assert(!noexcept(std::declval<flt::majority<std::string, 3>&>().push(std::string{})));
static_assert(!noexcept(std::declval<flt::stale_detector<std::string, 3>&>().push(std::string{})));
static_assert(noexcept(std::declval<flt::range_guard<double>&>().push(1.0)));

}  // namespace
