/**
 * @file
 * @brief Deep tests for the control-shaper filters of nexenne::filter:
 * slew, debounce, timed_debounce, hysteresis, glitch.
 */

#include <doctest/doctest.h>

#include <chrono>
#include <cstddef>
#include <optional>

#include <nexenne/filter/filter.hpp>
#include <nexenne/utility/discard.hpp>

namespace {

namespace flt = nexenne::filter;

using ns = std::chrono::nanoseconds;
using namespace std::chrono_literals;

static_assert(flt::filter_like<flt::slew<double>>);
static_assert(flt::filter_like<flt::slew<float>>);

TEST_CASE("nexenne::filter::slew first push primes directly to the target") {
  auto f{flt::slew{5.0}};
  CHECK(f.push(100.0) == doctest::Approx(100.0));  // first sample jumps
  CHECK(f.value() == doctest::Approx(100.0));
}

TEST_CASE("nexenne::filter::slew limits the per-sample step toward a higher target") {
  auto f{flt::slew{5.0}};
  nexenne::utility::discard(f.push(0.0));         // prime at 0
  CHECK(f.push(100.0) == doctest::Approx(5.0));   // +5
  CHECK(f.push(100.0) == doctest::Approx(10.0));  // +5
  CHECK(f.push(100.0) == doctest::Approx(15.0));  // +5
  CHECK(f.value() == doctest::Approx(15.0));
}

TEST_CASE("nexenne::filter::slew is symmetric for falling targets") {
  auto f{flt::slew{5.0}};
  nexenne::utility::discard(f.push(0.0));          // prime at 0
  CHECK(f.push(-100.0) == doctest::Approx(-5.0));  // -5
  CHECK(f.push(-100.0) == doctest::Approx(-10.0));
  CHECK(f.push(-100.0) == doctest::Approx(-15.0));
}

TEST_CASE("nexenne::filter::slew a big jump arrives in the exact number of steps") {
  // Prime at 0, target 23 with rate 5: steps 5,10,15,20,then clamp to 23.
  auto f{flt::slew{5.0}};
  nexenne::utility::discard(f.push(0.0));
  CHECK(f.push(23.0) == doctest::Approx(5.0));
  CHECK(f.push(23.0) == doctest::Approx(10.0));
  CHECK(f.push(23.0) == doctest::Approx(15.0));
  CHECK(f.push(23.0) == doctest::Approx(20.0));
  CHECK(f.push(23.0) == doctest::Approx(23.0));  // remaining 3 < rate -> arrive
  CHECK(f.push(23.0) == doctest::Approx(23.0));  // stays put once arrived
}

TEST_CASE("nexenne::filter::slew small changes pass straight through") {
  auto f{flt::slew{10.0}};
  nexenne::utility::discard(f.push(0.0));
  CHECK(f.push(5.0) == doctest::Approx(5.0));    // |5| <= 10
  CHECK(f.push(2.0) == doctest::Approx(2.0));    // |2-5| <= 10
  CHECK(f.push(-7.0) == doctest::Approx(-7.0));  // |-7-2| <= 10
}

TEST_CASE("nexenne::filter::slew an exactly-at-rate change is admitted whole") {
  auto f{flt::slew{5.0}};
  nexenne::utility::discard(f.push(0.0));
  CHECK(f.push(5.0) == doctest::Approx(5.0));  // delta exactly == rate
  CHECK(f.push(0.0) == doctest::Approx(0.0));  // delta exactly == rate down
}

TEST_CASE("nexenne::filter::slew a negative rate is clamped to zero and freezes output") {
  auto f{flt::slew{-5.0}};
  CHECK(f.max_rate() == doctest::Approx(0.0));
  nexenne::utility::discard(f.push(10.0));  // first sample still primes
  CHECK(f.value() == doctest::Approx(10.0));
  CHECK(f.push(100.0) == doctest::Approx(10.0));  // rate 0 -> frozen
  CHECK(f.push(-100.0) == doctest::Approx(10.0));
}

TEST_CASE("nexenne::filter::slew rate zero freezes after priming") {
  auto f{flt::slew{0.0}};
  nexenne::utility::discard(f.push(7.0));
  CHECK(f.push(999.0) == doctest::Approx(7.0));
  CHECK(f.push(-999.0) == doctest::Approx(7.0));
}

TEST_CASE("nexenne::filter::slew max_rate setter clamps negatives to zero") {
  auto f{flt::slew{2.0}};
  CHECK(f.max_rate() == doctest::Approx(2.0));
  f.max_rate(8.0);
  CHECK(f.max_rate() == doctest::Approx(8.0));
  f.max_rate(-3.0);
  CHECK(f.max_rate() == doctest::Approx(0.0));
}

TEST_CASE("nexenne::filter::slew changing the rate mid-run takes effect next push") {
  auto f{flt::slew{1.0}};
  nexenne::utility::discard(f.push(0.0));
  CHECK(f.push(100.0) == doctest::Approx(1.0));
  f.max_rate(50.0);
  CHECK(f.push(100.0) == doctest::Approx(51.0));  // 1 + 50
}

TEST_CASE("nexenne::filter::slew reset returns to the unprimed zero state") {
  auto f{flt::slew{5.0}};
  nexenne::utility::discard(f.push(50.0));
  CHECK(f.value() == doctest::Approx(50.0));
  f.reset();
  CHECK(f.value() == doctest::Approx(0.0));      // unprimed -> value 0
  CHECK(f.push(80.0) == doctest::Approx(80.0));  // next push jumps directly
}

TEST_CASE("nexenne::filter::slew reset to a primed initial rate-limits the next push") {
  auto f{flt::slew{5.0}};
  f.reset(40.0);
  CHECK(f.value() == doctest::Approx(40.0));
  CHECK(f.push(100.0) == doctest::Approx(45.0));  // primed -> limited by rate
}

TEST_CASE("nexenne::filter::slew float instantiation behaves identically") {
  auto f{flt::slew<float>{5.0F}};
  nexenne::utility::discard(f.push(0.0F));
  CHECK(f.push(100.0F) == doctest::Approx(5.0));
  CHECK(f.push(100.0F) == doctest::Approx(10.0));
}

TEST_CASE("nexenne::filter::debounce first push of an unprimed filter is accepted") {
  auto f{flt::debounce<bool, 3>{}};
  CHECK(f.push(true) == true);  // first sample seeds immediately
  CHECK(f.value() == true);
}

TEST_CASE("nexenne::filter::debounce accepts a change only after exactly N stable samples") {
  auto f{flt::debounce<bool, 3>{}};
  nexenne::utility::discard(f.push(false));  // primes stable=false
  CHECK(f.push(true) == false);              // streak 1, one short
  CHECK(f.push(true) == false);              // streak 2, one short
  CHECK(f.push(true) == true);               // streak 3 -> promoted
  CHECK(f.value() == true);
}

TEST_CASE("nexenne::filter::debounce a glitch shorter than N is rejected") {
  auto f{flt::debounce<bool, 3>{}};
  nexenne::utility::discard(f.push(false));
  nexenne::utility::discard(f.push(false));
  nexenne::utility::discard(f.push(false));
  CHECK(f.push(true) == false);   // 1-sample glitch
  CHECK(f.push(false) == false);  // back to stable, never promoted
}

TEST_CASE("nexenne::filter::debounce a bouncing input never reaches the threshold") {
  auto f{flt::debounce<bool, 3>{false}};  // primed stable=false
  // Alternating true/false: candidate restarts each time, streak never hits 3.
  for (auto i{0}; i < 10; ++i) {
    CHECK(f.push(true) == false);
    CHECK(f.push(false) == false);
  }
}

TEST_CASE("nexenne::filter::debounce an interrupted streak restarts the count") {
  auto f{flt::debounce<bool, 3>{false}};
  CHECK(f.push(true) == false);   // streak 1
  CHECK(f.push(true) == false);   // streak 2
  CHECK(f.push(false) == false);  // interruption: candidate=false, streak 1
  CHECK(f.push(true) == false);   // streak restarts: 1
  CHECK(f.push(true) == false);   // 2
  CHECK(f.push(true) == true);    // 3 -> promoted
}

TEST_CASE("nexenne::filter::debounce Threshold==1 accepts a change immediately (off-by-one)") {
  // KNOWN-CORRECT audited behavior: a fresh candidate has a streak of 1, which
  // already meets Threshold==1, so the change promotes on the very first sample.
  auto f{flt::debounce<int, 1>{0}};
  CHECK(f.push(1) == 1);
  CHECK(f.push(2) == 2);
  CHECK(f.push(2) == 2);

  auto g{flt::debounce<int, 1>{}};
  nexenne::utility::discard(g.push(0));  // prime
  CHECK(g.push(1) == 1);
  CHECK(g.push(0) == 0);
}

TEST_CASE("nexenne::filter::debounce default Threshold is 3") {
  auto f{flt::debounce<bool>{}};
  CHECK(flt::debounce<bool>::threshold == std::size_t{3});
  nexenne::utility::discard(f.push(false));
  CHECK(f.push(true) == false);
  CHECK(f.push(true) == false);
  CHECK(f.push(true) == true);
}

TEST_CASE("nexenne::filter::debounce numeric samples debounce like booleans") {
  auto f{flt::debounce<int, 2>{10}};
  CHECK(f.push(20) == 10);  // streak 1
  CHECK(f.push(20) == 20);  // streak 2 -> promoted
  CHECK(f.push(30) == 20);  // new candidate streak 1
  CHECK(f.push(30) == 30);  // streak 2 -> promoted
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
  CHECK(f.value() == false);    // value-initialised bool
  CHECK(f.push(true) == true);  // next push accepted immediately
}

TEST_CASE("nexenne::filter::debounce reset to a value is already fully confirmed") {
  auto f{flt::debounce<int, 3>{}};
  f.reset(42);
  CHECK(f.value() == 42);
  // streak is pre-loaded to Threshold; this is still a change-detection state,
  // and a new value still needs its own Threshold consecutive samples.
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
  nexenne::utility::discard(db.update(ns{0}, true));
  auto const r{db.update(ns{500'000}, true)};
  CHECK_FALSE(r.has_value());
  CHECK(db.stable_value() == true);
}

TEST_CASE("nexenne::filter::timed_debounce a candidate that does not hold is rejected") {
  auto db{flt::timed_debounce<ns>{20ms}};
  nexenne::utility::discard(db.update(ns{0}, false));        // stable=false
  CHECK_FALSE(db.update(ns{1'000'000}, true).has_value());   // candidate starts
  CHECK_FALSE(db.update(ns{2'000'000}, false).has_value());  // back to stable, cancelled
  CHECK(db.stable_value() == false);
  // A subsequent quick blip must not be promoted on the stale timestamp either.
  CHECK_FALSE(db.update(ns{3'000'000}, true).has_value());
  CHECK(db.stable_value() == false);
}

TEST_CASE("nexenne::filter::timed_debounce a candidate held for the period is promoted") {
  auto db{flt::timed_debounce<ns>{20ms}};
  nexenne::utility::discard(db.update(ns{0}, false));
  CHECK_FALSE(db.update(ns{1'000'000}, true).has_value());  // candidate at t=1ms
  auto const r{db.update(ns{1'000'000 + 20'000'000}, true)};
  REQUIRE(r.has_value());
  CHECK(*r == true);
  CHECK(db.stable_value() == true);
}

TEST_CASE("nexenne::filter::timed_debounce promotion needs elapsed >= period, not just >") {
  auto db{flt::timed_debounce<ns>{20ms}};
  nexenne::utility::discard(db.update(ns{0}, false));
  CHECK_FALSE(db.update(ns{1'000'000}, true).has_value());  // candidate since 1ms
  // Exactly one ns short of the period: still bouncing.
  CHECK_FALSE(db.update(ns{1'000'000 + 20'000'000 - 1}, true).has_value());
  CHECK(db.stable_value() == false);
  // Exactly at the period boundary: promote.
  auto const r{db.update(ns{1'000'000 + 20'000'000}, true)};
  REQUIRE(r.has_value());
  CHECK(*r == true);
}

TEST_CASE("nexenne::filter::timed_debounce a candidate that changes restarts the timer") {
  // The header restarts candidate_since whenever raw != current candidate.
  auto db{flt::timed_debounce<ns>{20ms}};
  nexenne::utility::discard(db.update(ns{0}, false));  // stable=false
  CHECK_FALSE(db.update(ns{0}, true).has_value());     // candidate=true since 0
  // A new differing candidate cannot occur for a bool line (only false==stable
  // cancels), so re-feed true after a long gap and confirm it now promotes.
  auto const r{db.update(ns{20'000'000}, true)};
  REQUIRE(r.has_value());
  CHECK(*r == true);
}

TEST_CASE("nexenne::filter::timed_debounce zero period collapses to pass-through") {
  auto db{flt::timed_debounce<ns>{0ms}};
  nexenne::utility::discard(db.update(ns{0}, false));
  auto const r{db.update(ns{1}, true)};
  REQUIRE(r.has_value());
  CHECK(*r == true);
  CHECK(db.stable_value() == true);
  // Each genuine change passes immediately.
  auto const r2{db.update(ns{2}, false)};
  REQUIRE(r2.has_value());
  CHECK(*r2 == false);
}

TEST_CASE("nexenne::filter::timed_debounce a negative period is clamped to zero") {
  auto db{flt::timed_debounce<ns>{ns{-5}}};
  CHECK(db.period() == ns{0});
  db.period(ns{-100});
  CHECK(db.period() == ns{0});
  db.period(20ms);
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
  nexenne::utility::discard(db.update(ns{0}, true));
  CHECK(db.has_stable());
  db.reset();
  CHECK_FALSE(db.has_stable());
  CHECK(db.stable_value() == false);
  // The next update is accepted immediately as the new stable value.
  auto const r{db.update(ns{100}, true)};
  REQUIRE(r.has_value());
  CHECK(*r == true);
}

TEST_CASE("nexenne::filter::timed_debounce a candidate cancelled mid-flight then re-held promotes"
) {
  auto db{flt::timed_debounce<ns>{10ms}};
  nexenne::utility::discard(db.update(ns{0}, false));
  CHECK_FALSE(db.update(ns{1'000'000}, true).has_value());   // candidate at 1ms
  CHECK_FALSE(db.update(ns{2'000'000}, false).has_value());  // cancelled (==stable)
  CHECK_FALSE(db.update(ns{3'000'000}, true).has_value());   // fresh candidate at 3ms
  // Held just shy of period from the *fresh* start: nothing.
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
  CHECK(f.push(10.0) == false);  // below low
  CHECK(f.push(22.0) == false);  // dead band, holds low
  CHECK(f.push(24.9) == false);  // still below high, holds low
  CHECK(f.push(26.0) == true);   // above high -> flips high
  CHECK(f.push(22.0) == true);   // dead band, holds high
  CHECK(f.push(20.1) == true);   // still above low, holds high
  CHECK(f.push(19.0) == false);  // below low -> flips low
}

TEST_CASE("nexenne::filter::hysteresis the dead band holds the previous state both ways") {
  auto f{flt::hysteresis{20.0, 25.0}};
  // Drive high, then sweep down through the band without crossing low.
  nexenne::utility::discard(f.push(30.0));
  CHECK(f.value() == true);
  CHECK(f.push(24.0) == true);
  CHECK(f.push(21.0) == true);
  // Now drive low, then sweep up through the band without crossing high.
  nexenne::utility::discard(f.push(10.0));
  CHECK(f.value() == false);
  CHECK(f.push(21.0) == false);
  CHECK(f.push(24.0) == false);
}

TEST_CASE("nexenne::filter::hysteresis the high threshold is inclusive") {
  auto f{flt::hysteresis{20.0, 25.0}};
  CHECK(f.push(25.0) == true);  // sample >= high -> true
}

TEST_CASE("nexenne::filter::hysteresis the low threshold is inclusive") {
  auto f{flt::hysteresis{20.0, 25.0}};
  nexenne::utility::discard(f.push(30.0));  // go high first
  CHECK(f.push(20.0) == false);             // sample <= low -> false
}

TEST_CASE("nexenne::filter::hysteresis equal thresholds act as a plain comparator") {
  auto f{flt::hysteresis{5.0, 5.0}};  // dead band vanishes
  CHECK(f.push(5.0) == true);         // >= high
  CHECK(f.push(4.9) == false);        // <= low
  CHECK(f.push(5.0) == true);
  CHECK(f.push(5.1) == true);
}

TEST_CASE("nexenne::filter::hysteresis reset clears to false") {
  auto f{flt::hysteresis{20.0, 25.0}};
  nexenne::utility::discard(f.push(30.0));
  CHECK(f.value() == true);
  f.reset();
  CHECK(f.value() == false);
}

TEST_CASE("nexenne::filter::hysteresis reset to a known state") {
  auto f{flt::hysteresis{20.0, 25.0}};
  f.reset(true);
  CHECK(f.value() == true);
  CHECK(f.push(22.0) == true);  // dead band holds the restored state
  f.reset(false);
  CHECK(f.value() == false);
  CHECK(f.push(22.0) == false);
}

TEST_CASE("nexenne::filter::hysteresis thresholds setter replaces both bounds") {
  auto f{flt::hysteresis{20.0, 25.0}};
  nexenne::utility::discard(f.push(30.0));  // latch true
  f.thresholds(0.0, 100.0);
  CHECK(f.low_threshold() == doctest::Approx(0.0));
  CHECK(f.high_threshold() == doctest::Approx(100.0));
  CHECK(f.value() == true);      // latched state unchanged by retuning
  CHECK(f.push(50.0) == true);   // new dead band holds true
  CHECK(f.push(-1.0) == false);  // below new low
}

TEST_CASE("nexenne::filter::hysteresis works on integer signals") {
  auto f{flt::hysteresis<int>{2, 8}};
  CHECK(f.push(0) == false);
  CHECK(f.push(5) == false);  // dead band
  CHECK(f.push(8) == true);   // inclusive high
  CHECK(f.push(5) == true);   // dead band
  CHECK(f.push(2) == false);  // inclusive low
}

TEST_CASE("nexenne::filter::hysteresis alternating across the band does not chatter mid-band") {
  auto f{flt::hysteresis{20.0, 25.0}};
  // Hover at the band centre repeatedly: output never changes from its start.
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
  CHECK(f.push(true) == false);  // hold 1
  CHECK(f.pending());
  CHECK(f.push(true) == false);   // hold 2
  CHECK(f.push(false) == false);  // returned to stable -> pulse suppressed
  CHECK_FALSE(f.pending());
}

TEST_CASE("nexenne::filter::glitch accepts a pulse of width exactly N") {
  auto f{flt::glitch<bool, 3>{false}};
  CHECK(f.push(true) == false);  // hold 1
  CHECK(f.push(true) == false);  // hold 2
  CHECK(f.push(true) == true);   // hold 3 -> accepted
  CHECK(f.value() == true);
  CHECK_FALSE(f.pending());  // hold reset after promotion
}

TEST_CASE("nexenne::filter::glitch a width-(N-1) pulse just misses acceptance") {
  auto f{flt::glitch<bool, 4>{false}};
  CHECK(f.push(true) == false);   // hold 1
  CHECK(f.push(true) == false);   // hold 2
  CHECK(f.push(true) == false);   // hold 3 (one short of 4)
  CHECK(f.push(false) == false);  // dropped just before acceptance
  CHECK_FALSE(f.pending());
}

TEST_CASE("nexenne::filter::glitch N==1 promotes a single sample (off-by-one)") {
  // KNOWN-CORRECT audited behavior: a fresh candidate already holds for one
  // sample, so with N==1 a single differing sample is accepted at once.
  auto f{flt::glitch<int, 1>{0}};
  CHECK(f.push(1) == 1);
  CHECK(f.push(0) == 0);
  CHECK(f.push(5) == 5);  // each one-sample pulse passes
}

TEST_CASE("nexenne::filter::glitch back-to-back glitches are all suppressed") {
  auto f{flt::glitch<bool, 3>{false}};
  for (auto i{0}; i < 6; ++i) {
    CHECK(f.push(true) == false);   // single-sample spike
    CHECK(f.push(false) == false);  // returns to stable each time
  }
  CHECK(f.value() == false);
}

TEST_CASE("nexenne::filter::glitch a fresh candidate restarts the hold counter") {
  // A pulse to one value, then a different value before acceptance, restarts
  // the count rather than accumulating.
  auto f{flt::glitch<int, 3>{0}};
  CHECK(f.push(1) == 0);  // pending=1, hold 1
  CHECK(f.push(1) == 0);  // hold 2
  CHECK(f.push(2) == 0);  // new candidate -> pending=2, hold restarts to 1
  CHECK(f.push(2) == 0);  // hold 2
  CHECK(f.push(2) == 2);  // hold 3 -> accepted
}

TEST_CASE("nexenne::filter::glitch a candidate equal to stable cancels immediately") {
  auto f{flt::glitch<int, 3>{7}};
  CHECK(f.push(9) == 7);  // candidate 9, hold 1
  CHECK(f.pending());
  CHECK(f.push(7) == 7);  // back to stable -> cancelled
  CHECK_FALSE(f.pending());
  CHECK(f.push(9) == 7);  // fresh start, hold 1 again
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
  nexenne::utility::discard(f.push(true));
  CHECK(f.pending());
  nexenne::utility::discard(f.push(false));
  CHECK_FALSE(f.pending());  // cancelled
}

TEST_CASE("nexenne::filter::glitch unprimed reset re-seeds on next push") {
  auto f{flt::glitch<int, 3>{5}};
  nexenne::utility::discard(f.push(9));
  f.reset();
  CHECK(f.value() == 0);  // value-initialised int
  CHECK_FALSE(f.pending());
  CHECK(f.push(123) == 123);  // next push seeds the stable value
}

TEST_CASE("nexenne::filter::glitch reset to a value clears any pending candidate") {
  auto f{flt::glitch<int, 3>{0}};
  nexenne::utility::discard(f.push(1));  // start a candidate
  CHECK(f.pending());
  f.reset(42);
  CHECK(f.value() == 42);
  CHECK_FALSE(f.pending());
  CHECK(f.push(7) == 42);  // a new value still needs N samples
  CHECK(f.push(7) == 42);
  CHECK(f.push(7) == 7);
}

TEST_CASE("nexenne::filter::glitch a sustained run after acceptance keeps the value") {
  auto f{flt::glitch<bool, 2>{false}};
  CHECK(f.push(true) == false);  // hold 1
  CHECK(f.push(true) == true);   // hold 2 -> accepted
  for (auto i{0}; i < 10; ++i) {
    CHECK(f.push(true) == true);  // stays accepted, no pending
    CHECK_FALSE(f.pending());
  }
}

}  // namespace
