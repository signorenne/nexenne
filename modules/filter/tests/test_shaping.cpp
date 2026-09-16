/**
 * @file
 * @brief Deep tests for the control-shaper filters of nexenne::filter:
 * slew, debounce, timed_debounce, hysteresis, glitch.
 */

#include <doctest/doctest.h>

#include <chrono>
#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <utility>

#include <nexenne/filter/filter.hpp>
#include <nexenne/utility/ignore.hpp>

namespace {

namespace flt = nexenne::filter;

using ns = std::chrono::nanoseconds;
using namespace std::chrono_literals;

static_assert(flt::filter_like<flt::slew<double>>);
static_assert(flt::filter_like<flt::slew<float>>);

TEST_CASE("nexenne::filter::slew first push primes directly to the target") {
  auto f{flt::slew{5.0}};
  CHECK(f.push(100.0) == doctest::Approx(100.0));
  CHECK(f.value() == doctest::Approx(100.0));
}

TEST_CASE("nexenne::filter::slew limits the per-sample step toward a higher target") {
  auto f{flt::slew{5.0}};
  nexenne::utility::ignore(f.push(0.0));
  CHECK(f.push(100.0) == doctest::Approx(5.0));
  CHECK(f.push(100.0) == doctest::Approx(10.0));
  CHECK(f.push(100.0) == doctest::Approx(15.0));
  CHECK(f.value() == doctest::Approx(15.0));
}

TEST_CASE("nexenne::filter::slew is symmetric for falling targets") {
  auto f{flt::slew{5.0}};
  nexenne::utility::ignore(f.push(0.0));
  CHECK(f.push(-100.0) == doctest::Approx(-5.0));
  CHECK(f.push(-100.0) == doctest::Approx(-10.0));
  CHECK(f.push(-100.0) == doctest::Approx(-15.0));
}

TEST_CASE("nexenne::filter::slew a big jump arrives in the exact number of steps") {
  auto f{flt::slew{5.0}};
  nexenne::utility::ignore(f.push(0.0));
  CHECK(f.push(23.0) == doctest::Approx(5.0));
  CHECK(f.push(23.0) == doctest::Approx(10.0));
  CHECK(f.push(23.0) == doctest::Approx(15.0));
  CHECK(f.push(23.0) == doctest::Approx(20.0));
  CHECK(f.push(23.0) == doctest::Approx(23.0));
  CHECK(f.push(23.0) == doctest::Approx(23.0));
}

TEST_CASE("nexenne::filter::slew small changes pass straight through") {
  auto f{flt::slew{10.0}};
  nexenne::utility::ignore(f.push(0.0));
  CHECK(f.push(5.0) == doctest::Approx(5.0));
  CHECK(f.push(2.0) == doctest::Approx(2.0));
  CHECK(f.push(-7.0) == doctest::Approx(-7.0));
}

TEST_CASE("nexenne::filter::slew an exactly-at-rate change is admitted whole") {
  auto f{flt::slew{5.0}};
  nexenne::utility::ignore(f.push(0.0));
  CHECK(f.push(5.0) == doctest::Approx(5.0));
  CHECK(f.push(0.0) == doctest::Approx(0.0));
}

TEST_CASE("nexenne::filter::slew a negative rate is clamped to zero and freezes output") {
  auto f{flt::slew{-5.0}};
  CHECK(f.max_rate() == doctest::Approx(0.0));
  nexenne::utility::ignore(f.push(10.0));
  CHECK(f.value() == doctest::Approx(10.0));
  CHECK(f.push(100.0) == doctest::Approx(10.0));
  CHECK(f.push(-100.0) == doctest::Approx(10.0));
}

TEST_CASE("nexenne::filter::slew rate zero freezes after priming") {
  auto f{flt::slew{0.0}};
  nexenne::utility::ignore(f.push(7.0));
  CHECK(f.push(999.0) == doctest::Approx(7.0));
  CHECK(f.push(-999.0) == doctest::Approx(7.0));
}

TEST_CASE("nexenne::filter::slew max_rate is read and written through its accessor") {
  auto f{flt::slew{2.0}};
  CHECK(f.max_rate() == doctest::Approx(2.0));
  f.max_rate() = 8.0;
  CHECK(f.max_rate() == doctest::Approx(8.0));
}

TEST_CASE("nexenne::filter::slew changing the rate mid-run takes effect next push") {
  auto f{flt::slew{1.0}};
  nexenne::utility::ignore(f.push(0.0));
  CHECK(f.push(100.0) == doctest::Approx(1.0));
  f.max_rate() = 50.0;
  CHECK(f.push(100.0) == doctest::Approx(51.0));
}

TEST_CASE("nexenne::filter::slew reset returns to the unprimed zero state") {
  auto f{flt::slew{5.0}};
  nexenne::utility::ignore(f.push(50.0));
  CHECK(f.value() == doctest::Approx(50.0));
  f.reset();
  CHECK(f.value() == doctest::Approx(0.0));
  CHECK(f.push(80.0) == doctest::Approx(80.0));
}

TEST_CASE("nexenne::filter::slew reset to a primed initial rate-limits the next push") {
  auto f{flt::slew{5.0}};
  f.reset(40.0);
  CHECK(f.value() == doctest::Approx(40.0));
  CHECK(f.push(100.0) == doctest::Approx(45.0));
}

TEST_CASE("nexenne::filter::slew float instantiation behaves identically") {
  auto f{flt::slew<float>{5.0F}};
  nexenne::utility::ignore(f.push(0.0F));
  CHECK(f.push(100.0F) == doctest::Approx(5.0));
  CHECK(f.push(100.0F) == doctest::Approx(10.0));
}

TEST_CASE("nexenne::filter::debounce first push of an unprimed filter is accepted") {
  auto f{flt::debounce<bool, 3>{}};
  CHECK(f.push(true) == true);
  CHECK(f.value() == true);
}

TEST_CASE("nexenne::filter::debounce accepts a change only after exactly N stable samples") {
  auto f{flt::debounce<bool, 3>{}};
  nexenne::utility::ignore(f.push(false));
  CHECK(f.push(true) == false);
  CHECK(f.push(true) == false);
  CHECK(f.push(true) == true);
  CHECK(f.value() == true);
}

TEST_CASE("nexenne::filter::debounce a glitch shorter than N is rejected") {
  auto f{flt::debounce<bool, 3>{}};
  nexenne::utility::ignore(f.push(false));
  nexenne::utility::ignore(f.push(false));
  nexenne::utility::ignore(f.push(false));
  CHECK(f.push(true) == false);
  CHECK(f.push(false) == false);
}

TEST_CASE("nexenne::filter::debounce a bouncing input never reaches the threshold") {
  auto f{flt::debounce<bool, 3>{false}};
  for (auto i{0}; i < 10; ++i) {
    CHECK(f.push(true) == false);
    CHECK(f.push(false) == false);
  }
}

TEST_CASE("nexenne::filter::debounce an interrupted streak restarts the count") {
  auto f{flt::debounce<bool, 3>{false}};
  CHECK(f.push(true) == false);
  CHECK(f.push(true) == false);
  CHECK(f.push(false) == false);
  CHECK(f.push(true) == false);
  CHECK(f.push(true) == false);
  CHECK(f.push(true) == true);
}

TEST_CASE("nexenne::filter::debounce Threshold==1 accepts a change immediately (off-by-one)") {
  auto f{flt::debounce<int, 1>{0}};
  CHECK(f.push(1) == 1);
  CHECK(f.push(2) == 2);
  CHECK(f.push(2) == 2);

  auto g{flt::debounce<int, 1>{}};
  nexenne::utility::ignore(g.push(0));
  CHECK(g.push(1) == 1);
  CHECK(g.push(0) == 0);
}

TEST_CASE("nexenne::filter::debounce default Threshold is 3") {
  auto f{flt::debounce<bool>{}};
  CHECK(flt::debounce<bool>::threshold == std::size_t{3});
  nexenne::utility::ignore(f.push(false));
  CHECK(f.push(true) == false);
  CHECK(f.push(true) == false);
  CHECK(f.push(true) == true);
}

TEST_CASE("nexenne::filter::debounce numeric samples debounce like booleans") {
  auto f{flt::debounce<int, 2>{10}};
  CHECK(f.push(20) == 10);
  CHECK(f.push(20) == 20);
  CHECK(f.push(30) == 20);
  CHECK(f.push(30) == 30);
}

TEST_CASE("nexenne::filter::debounce a stable run holds the value without re-promoting") {
  auto f{flt::debounce<bool, 3>{true}};
  for (auto i{0}; i < 20; ++i) {
    CHECK(f.push(true) == true);
  }
}

TEST_CASE("nexenne::filter::debounce reset returns to the unprimed condition") {
  auto f{flt::debounce<bool, 3>{true}};
  f.reset();
  CHECK(f.value() == false);
  CHECK(f.push(true) == true);
}

TEST_CASE("nexenne::filter::debounce reset to a value is already fully confirmed") {
  auto f{flt::debounce<int, 3>{}};
  f.reset(42);
  CHECK(f.value() == 42);
  CHECK(f.push(7) == 42);
  CHECK(f.push(7) == 42);
  CHECK(f.push(7) == 7);
}

TEST_CASE("nexenne::filter::timed_debounce first sample is accepted immediately") {
  auto db{flt::timed_debounce<ns>{20ms}};
  auto const r{db.update(ns{0}, true)};
  REQUIRE(r.has_value());
  CHECK(*r == true);
  CHECK(db.stable_value() == true);
  CHECK(db.has_stable());
}

TEST_CASE("nexenne::filter::timed_debounce a same-as-stable sample yields nothing") {
  auto db{flt::timed_debounce<ns>{20ms}};
  nexenne::utility::ignore(db.update(ns{0}, true));
  auto const r{db.update(ns{500'000}, true)};
  CHECK_FALSE(r.has_value());
  CHECK(db.stable_value() == true);
}

TEST_CASE("nexenne::filter::timed_debounce a candidate that does not hold is rejected") {
  auto db{flt::timed_debounce<ns>{20ms}};
  nexenne::utility::ignore(db.update(ns{0}, false));
  CHECK_FALSE(db.update(ns{1'000'000}, true).has_value());
  CHECK_FALSE(db.update(ns{2'000'000}, false).has_value());
  CHECK(db.stable_value() == false);
  CHECK_FALSE(db.update(ns{3'000'000}, true).has_value());
  CHECK(db.stable_value() == false);
}

TEST_CASE("nexenne::filter::timed_debounce a candidate held for the period is promoted") {
  auto db{flt::timed_debounce<ns>{20ms}};
  nexenne::utility::ignore(db.update(ns{0}, false));
  CHECK_FALSE(db.update(ns{1'000'000}, true).has_value());
  auto const r{db.update(ns{1'000'000 + 20'000'000}, true)};
  REQUIRE(r.has_value());
  CHECK(*r == true);
  CHECK(db.stable_value() == true);
}

TEST_CASE("nexenne::filter::timed_debounce promotion needs elapsed >= period, not just >") {
  auto db{flt::timed_debounce<ns>{20ms}};
  nexenne::utility::ignore(db.update(ns{0}, false));
  CHECK_FALSE(db.update(ns{1'000'000}, true).has_value());
  CHECK_FALSE(db.update(ns{1'000'000 + 20'000'000 - 1}, true).has_value());
  CHECK(db.stable_value() == false);
  auto const r{db.update(ns{1'000'000 + 20'000'000}, true)};
  REQUIRE(r.has_value());
  CHECK(*r == true);
}

TEST_CASE("nexenne::filter::timed_debounce a candidate that changes restarts the timer") {
  auto db{flt::timed_debounce<ns>{20ms}};
  nexenne::utility::ignore(db.update(ns{0}, false));
  CHECK_FALSE(db.update(ns{0}, true).has_value());
  auto const r{db.update(ns{20'000'000}, true)};
  REQUIRE(r.has_value());
  CHECK(*r == true);
}

TEST_CASE("nexenne::filter::timed_debounce zero period collapses to pass-through") {
  auto db{flt::timed_debounce<ns>{0ms}};
  nexenne::utility::ignore(db.update(ns{0}, false));
  auto const r{db.update(ns{1}, true)};
  REQUIRE(r.has_value());
  CHECK(*r == true);
  CHECK(db.stable_value() == true);
  auto const r2{db.update(ns{2}, false)};
  REQUIRE(r2.has_value());
  CHECK(*r2 == false);
}

TEST_CASE("nexenne::filter::timed_debounce a negative period is clamped to zero") {
  auto db{flt::timed_debounce<ns>{ns{-5}}};
  CHECK(db.period() == ns{0});
  db.period() = 20ms;
  CHECK(db.period() == ns{20'000'000});
}

TEST_CASE("nexenne::filter::timed_debounce default construction has zero period and no stable") {
  auto db{flt::timed_debounce<ns>{}};
  CHECK(db.period() == ns{0});
  CHECK_FALSE(db.has_stable());
  CHECK(db.stable_value() == false);
}

TEST_CASE("nexenne::filter::timed_debounce reset clears the stable state") {
  auto db{flt::timed_debounce<ns>{20ms}};
  nexenne::utility::ignore(db.update(ns{0}, true));
  CHECK(db.has_stable());
  db.reset();
  CHECK_FALSE(db.has_stable());
  CHECK(db.stable_value() == false);
  auto const r{db.update(ns{100}, true)};
  REQUIRE(r.has_value());
  CHECK(*r == true);
}

TEST_CASE(
  "nexenne::filter::timed_debounce a candidate cancelled mid-flight then re-held promotes"
) {
  auto db{flt::timed_debounce<ns>{10ms}};
  nexenne::utility::ignore(db.update(ns{0}, false));
  CHECK_FALSE(db.update(ns{1'000'000}, true).has_value());
  CHECK_FALSE(db.update(ns{2'000'000}, false).has_value());
  CHECK_FALSE(db.update(ns{3'000'000}, true).has_value());
  CHECK_FALSE(db.update(ns{3'000'000 + 10'000'000 - 1}, true).has_value());
  auto const r{db.update(ns{3'000'000 + 10'000'000}, true)};
  REQUIRE(r.has_value());
  CHECK(*r == true);
}

TEST_CASE("nexenne::filter::hysteresis starts low and exposes its thresholds") {
  auto f{flt::hysteresis{20.0, 25.0}};
  CHECK(f.value() == false);
  CHECK(f.low_threshold() == doctest::Approx(20.0));
  CHECK(f.high_threshold() == doctest::Approx(25.0));
}

TEST_CASE("nexenne::filter::hysteresis flips only across the correct thresholds") {
  auto f{flt::hysteresis{20.0, 25.0}};
  CHECK(f.push(10.0) == false);
  CHECK(f.push(22.0) == false);
  CHECK(f.push(24.9) == false);
  CHECK(f.push(26.0) == true);
  CHECK(f.push(22.0) == true);
  CHECK(f.push(20.1) == true);
  CHECK(f.push(19.0) == false);
}

TEST_CASE("nexenne::filter::hysteresis the dead band holds the previous state both ways") {
  auto f{flt::hysteresis{20.0, 25.0}};
  nexenne::utility::ignore(f.push(30.0));
  CHECK(f.value() == true);
  CHECK(f.push(24.0) == true);
  CHECK(f.push(21.0) == true);
  nexenne::utility::ignore(f.push(10.0));
  CHECK(f.value() == false);
  CHECK(f.push(21.0) == false);
  CHECK(f.push(24.0) == false);
}

TEST_CASE("nexenne::filter::hysteresis the high threshold is inclusive") {
  auto f{flt::hysteresis{20.0, 25.0}};
  CHECK(f.push(25.0) == true);
}

TEST_CASE("nexenne::filter::hysteresis the low threshold is inclusive") {
  auto f{flt::hysteresis{20.0, 25.0}};
  nexenne::utility::ignore(f.push(30.0));
  CHECK(f.push(20.0) == false);
}

TEST_CASE("nexenne::filter::hysteresis equal thresholds act as a plain comparator") {
  auto f{flt::hysteresis{5.0, 5.0}};
  CHECK(f.push(5.0) == true);
  CHECK(f.push(4.9) == false);
  CHECK(f.push(5.0) == true);
  CHECK(f.push(5.1) == true);
}

TEST_CASE("nexenne::filter::hysteresis reset clears to false") {
  auto f{flt::hysteresis{20.0, 25.0}};
  nexenne::utility::ignore(f.push(30.0));
  CHECK(f.value() == true);
  f.reset();
  CHECK(f.value() == false);
}

TEST_CASE("nexenne::filter::hysteresis reset to a known state") {
  auto f{flt::hysteresis{20.0, 25.0}};
  f.reset(true);
  CHECK(f.value() == true);
  CHECK(f.push(22.0) == true);
  f.reset(false);
  CHECK(f.value() == false);
  CHECK(f.push(22.0) == false);
}

TEST_CASE("nexenne::filter::hysteresis thresholds setter replaces both bounds") {
  auto f{flt::hysteresis{20.0, 25.0}};
  nexenne::utility::ignore(f.push(30.0));
  f.thresholds(0.0, 100.0);
  CHECK(f.low_threshold() == doctest::Approx(0.0));
  CHECK(f.high_threshold() == doctest::Approx(100.0));
  CHECK(f.value() == true);
  CHECK(f.push(50.0) == true);
  CHECK(f.push(-1.0) == false);
}

TEST_CASE("nexenne::filter::hysteresis works on integer signals") {
  auto f{flt::hysteresis<int>{2, 8}};
  CHECK(f.push(0) == false);
  CHECK(f.push(5) == false);
  CHECK(f.push(8) == true);
  CHECK(f.push(5) == true);
  CHECK(f.push(2) == false);
}

TEST_CASE("nexenne::filter::hysteresis alternating across the band does not chatter mid-band") {
  auto f{flt::hysteresis{20.0, 25.0}};
  for (auto i{0}; i < 8; ++i) {
    CHECK(f.push(22.5) == false);
  }
}

TEST_CASE("nexenne::filter::glitch first push seeds the stable value") {
  auto f{flt::glitch<bool, 3>{}};
  CHECK(f.push(true) == true);
  CHECK(f.value() == true);
  CHECK_FALSE(f.pending());
}

TEST_CASE("nexenne::filter::glitch suppresses a pulse narrower than N") {
  auto f{flt::glitch<bool, 3>{false}};
  CHECK(f.push(true) == false);
  CHECK(f.pending());
  CHECK(f.push(true) == false);
  CHECK(f.push(false) == false);
  CHECK_FALSE(f.pending());
}

TEST_CASE("nexenne::filter::glitch accepts a pulse of width exactly N") {
  auto f{flt::glitch<bool, 3>{false}};
  CHECK(f.push(true) == false);
  CHECK(f.push(true) == false);
  CHECK(f.push(true) == true);
  CHECK(f.value() == true);
  CHECK_FALSE(f.pending());
}

TEST_CASE("nexenne::filter::glitch a width-(N-1) pulse just misses acceptance") {
  auto f{flt::glitch<bool, 4>{false}};
  CHECK(f.push(true) == false);
  CHECK(f.push(true) == false);
  CHECK(f.push(true) == false);
  CHECK(f.push(false) == false);
  CHECK_FALSE(f.pending());
}

TEST_CASE("nexenne::filter::glitch N==1 promotes a single sample (off-by-one)") {
  auto f{flt::glitch<int, 1>{0}};
  CHECK(f.push(1) == 1);
  CHECK(f.push(0) == 0);
  CHECK(f.push(5) == 5);
}

TEST_CASE("nexenne::filter::glitch back-to-back glitches are all suppressed") {
  auto f{flt::glitch<bool, 3>{false}};
  for (auto i{0}; i < 6; ++i) {
    CHECK(f.push(true) == false);
    CHECK(f.push(false) == false);
  }
  CHECK(f.value() == false);
}

TEST_CASE("nexenne::filter::glitch a fresh candidate restarts the hold counter") {
  auto f{flt::glitch<int, 3>{0}};
  CHECK(f.push(1) == 0);
  CHECK(f.push(1) == 0);
  CHECK(f.push(2) == 0);
  CHECK(f.push(2) == 0);
  CHECK(f.push(2) == 2);
}

TEST_CASE("nexenne::filter::glitch a candidate equal to stable cancels immediately") {
  auto f{flt::glitch<int, 3>{7}};
  CHECK(f.push(9) == 7);
  CHECK(f.pending());
  CHECK(f.push(7) == 7);
  CHECK_FALSE(f.pending());
  CHECK(f.push(9) == 7);
  CHECK(f.pending());
}

TEST_CASE("nexenne::filter::glitch default N is 3") {
  CHECK(flt::glitch<bool>::hold_count == std::size_t{3});
  auto f{flt::glitch<bool>{false}};
  CHECK(f.push(true) == false);
  CHECK(f.push(true) == false);
  CHECK(f.push(true) == true);
}

TEST_CASE("nexenne::filter::glitch pending reflects the in-flight transition") {
  auto f{flt::glitch<bool, 3>{false}};
  CHECK_FALSE(f.pending());
  nexenne::utility::ignore(f.push(true));
  CHECK(f.pending());
  nexenne::utility::ignore(f.push(false));
  CHECK_FALSE(f.pending());
}

TEST_CASE("nexenne::filter::glitch unprimed reset re-seeds on next push") {
  auto f{flt::glitch<int, 3>{5}};
  nexenne::utility::ignore(f.push(9));
  f.reset();
  CHECK(f.value() == 0);
  CHECK_FALSE(f.pending());
  CHECK(f.push(123) == 123);
}

TEST_CASE("nexenne::filter::glitch reset to a value clears any pending candidate") {
  auto f{flt::glitch<int, 3>{0}};
  nexenne::utility::ignore(f.push(1));
  CHECK(f.pending());
  f.reset(42);
  CHECK(f.value() == 42);
  CHECK_FALSE(f.pending());
  CHECK(f.push(7) == 42);
  CHECK(f.push(7) == 42);
  CHECK(f.push(7) == 7);
}

TEST_CASE("nexenne::filter::glitch a sustained run after acceptance keeps the value") {
  auto f{flt::glitch<bool, 2>{false}};
  CHECK(f.push(true) == false);
  CHECK(f.push(true) == true);
  for (auto i{0}; i < 10; ++i) {
    CHECK(f.push(true) == true);
    CHECK_FALSE(f.pending());
  }
}

static_assert(!noexcept(std::declval<flt::debounce<std::string, 3>&>().push(std::string{})));
static_assert(!noexcept(std::declval<flt::glitch<std::string, 3>&>().push(std::string{})));
static_assert(noexcept(std::declval<flt::debounce<bool, 3>&>().push(true)));

TEST_CASE("nexenne::filter::slew rejects non-finite targets and a NaN rate") {
  auto const inf{std::numeric_limits<double>::infinity()};
  auto const nan{std::numeric_limits<double>::quiet_NaN()};
  auto s{flt::slew{1.0}};
  CHECK(s.push(inf) == doctest::Approx(0.0));
  CHECK(s.push(0.0) == doctest::Approx(0.0));
  CHECK(s.push(10.0) == doctest::Approx(1.0));

  auto no_rate{flt::slew{nan}};
  CHECK(no_rate.max_rate() == doctest::Approx(0.0));
  nexenne::utility::ignore(no_rate.push(0.0));
  CHECK(no_rate.push(1000.0) == doctest::Approx(0.0));
}

}  // namespace
