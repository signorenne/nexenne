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

#include <nexenne/filter/filter.hpp>

namespace {

namespace flt = nexenne::filter;

// Every guard must satisfy the shared push/value/reset surface. validator
// needs a concrete predicate to name a type, so it is checked inline below.
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
  CHECK(f.last_accepted() == false);
  CHECK(f.value() == doctest::Approx(0.0));

  CHECK(f.push(50.0) == doctest::Approx(50.0));
  CHECK(f.last_accepted() == true);
  CHECK(f.push(200.0) == doctest::Approx(50.0));  // above hi -> rejected, holds
  CHECK(f.push(75.0) == doctest::Approx(75.0));   // back in range -> accepted
  CHECK(f.push(-10.0) == doctest::Approx(75.0));  // below lo -> rejected, holds
  CHECK(f.value() == doctest::Approx(75.0));
}

TEST_CASE("nexenne::filter::range_guard exact boundaries lo and hi are inclusive") {
  auto f{flt::range_guard{10.0, 90.0}};
  CHECK(f.push(10.0) == doctest::Approx(10.0));  // exactly lo -> accepted
  CHECK(f.push(90.0) == doctest::Approx(90.0));  // exactly hi -> accepted
  // Just outside each boundary is rejected and holds the last accepted (90).
  CHECK(f.push(9.0) == doctest::Approx(90.0));
  CHECK(f.push(91.0) == doctest::Approx(90.0));
}

TEST_CASE("nexenne::filter::range_guard clamps an out-of-range first sample to nearest bound") {
  auto above{flt::range_guard{10.0, 90.0}};
  CHECK(above.push(200.0) == doctest::Approx(90.0));  // clamped up to hi
  CHECK(above.last_accepted() == true);

  auto below{flt::range_guard{10.0, 90.0}};
  CHECK(below.push(-5.0) == doctest::Approx(10.0));  // clamped down to lo
  CHECK(below.last_accepted() == true);
  // Once primed by the clamp, a later out-of-range sample is rejected, not clamped.
  CHECK(below.push(1000.0) == doctest::Approx(10.0));
}

TEST_CASE("nexenne::filter::range_guard reset re-arms the first-sample clamp") {
  auto f{flt::range_guard{0.0, 10.0}};
  CHECK(f.push(5.0) == doctest::Approx(5.0));
  f.reset();
  CHECK(f.last_accepted() == false);
  CHECK(f.value() == doctest::Approx(0.0));
  // After reset the first out-of-range sample clamps again rather than rejecting.
  CHECK(f.push(20.0) == doctest::Approx(10.0));
  CHECK(f.last_accepted() == true);
}

TEST_CASE("nexenne::filter::range_guard degenerate range lo == hi admits only that value") {
  auto f{flt::range_guard{5.0, 5.0}};
  CHECK(f.push(5.0) == doctest::Approx(5.0));  // the only in-range value
  CHECK(f.push(4.0) == doctest::Approx(5.0));  // rejected, holds
  CHECK(f.push(6.0) == doctest::Approx(5.0));  // rejected, holds
}

// Note: an inverted range (lo > hi) violates the documented `@pre lo <= hi`
// and feeds std::clamp a precondition violation, so it is intentionally NOT
// tested (it is undefined by contract). The lo == hi degenerate case is valid
// and is covered above.

TEST_CASE("nexenne::filter::range_guard range() swaps bounds without touching held value") {
  auto f{flt::range_guard{0.0, 10.0}};
  CHECK(f.push(8.0) == doctest::Approx(8.0));
  f.range(20.0, 30.0);  // held 8.0 now falls outside the new range
  CHECK(f.lo() == doctest::Approx(20.0));
  CHECK(f.hi() == doctest::Approx(30.0));
  CHECK(f.value() == doctest::Approx(8.0));      // held value unchanged
  CHECK(f.push(15.0) == doctest::Approx(8.0));   // below new lo -> rejected
  CHECK(f.push(25.0) == doctest::Approx(25.0));  // in new range -> accepted
}

TEST_CASE("nexenne::filter::range_guard integer instantiation") {
  auto f{flt::range_guard<int>{-100, 100}};
  CHECK(f.push(0) == 0);
  CHECK(f.push(100) == 100);    // exact hi
  CHECK(f.push(-100) == -100);  // exact lo
  CHECK(f.push(101) == -100);   // out of range -> holds
  CHECK(f.push(-101) == -100);  // out of range -> holds
}

TEST_CASE("nexenne::filter::range_guard NaN never passes and never clamps to a number") {
  auto const nan{std::numeric_limits<double>::quiet_NaN()};
  // First-sample NaN: the in-range comparison is false, the guard primes and
  // clamps. std::clamp(NaN, lo, hi) is NaN (no comparison is true), so value
  // stays NaN.
  auto first{flt::range_guard{0.0, 10.0}};
  CHECK(std::isnan(first.push(nan)));
  CHECK(first.last_accepted() == true);

  // NaN after a good value is rejected and holds the last good value.
  auto later{flt::range_guard{0.0, 10.0}};
  CHECK(later.push(5.0) == doctest::Approx(5.0));
  CHECK(later.push(nan) == doctest::Approx(5.0));
}

TEST_CASE("nexenne::filter::range_guard infinity is out of a finite range") {
  auto const inf{std::numeric_limits<double>::infinity()};
  auto f{flt::range_guard{0.0, 10.0}};
  CHECK(f.push(5.0) == doctest::Approx(5.0));
  CHECK(f.push(inf) == doctest::Approx(5.0));   // +inf > hi -> rejected
  CHECK(f.push(-inf) == doctest::Approx(5.0));  // -inf < lo -> rejected
}

TEST_CASE("nexenne::filter::rate_guard first sample seeds and within-rate passes") {
  auto f{flt::rate_guard{5.0}};
  CHECK(f.value() == doctest::Approx(0.0));
  CHECK(f.push(100.0) == doctest::Approx(100.0));  // first seeds unconditionally
  CHECK(f.push(103.0) == doctest::Approx(103.0));  // |3| <= 5 -> accepted
  CHECK(f.push(200.0) == doctest::Approx(103.0));  // |97| > 5 -> rejected, holds
  CHECK(f.push(105.0) == doctest::Approx(105.0));  // |2| <= 5 from held 103
  CHECK(f.max_delta() == doctest::Approx(5.0));
}

TEST_CASE("nexenne::filter::rate_guard exact-boundary delta is accepted (<=)") {
  auto f{flt::rate_guard{5.0}};
  static_cast<void>(f.push(10.0));
  CHECK(f.push(15.0) == doctest::Approx(15.0));  // delta exactly 5 -> accepted
  CHECK(f.push(10.0) == doctest::Approx(10.0));  // delta exactly 5 downward
  // Just over the boundary is rejected.
  CHECK(f.push(15.0001) == doctest::Approx(10.0));
}

TEST_CASE("nexenne::filter::rate_guard rejection is measured from the held value, not the input") {
  auto f{flt::rate_guard{5.0}};
  static_cast<void>(f.push(0.0));
  CHECK(f.push(100.0) == doctest::Approx(0.0));  // rejected, still 0
  // A later sample is judged against 0 (the held value), not the rejected 100.
  CHECK(f.push(3.0) == doctest::Approx(3.0));
}

TEST_CASE("nexenne::filter::rate_guard negative max_delta clamps to zero and freezes after first") {
  auto f{flt::rate_guard{-1.0}};
  CHECK(f.max_delta() == doctest::Approx(0.0));
  CHECK(f.push(7.0) == doctest::Approx(7.0));  // first still seeds
  CHECK(f.push(7.0) == doctest::Approx(7.0));  // delta 0 <= 0 -> accepted
  CHECK(f.push(8.0) == doctest::Approx(7.0));  // any nonzero change rejected
}

TEST_CASE("nexenne::filter::rate_guard max_delta(d) setter clamps negatives and keeps value") {
  auto f{flt::rate_guard{1.0}};
  static_cast<void>(f.push(50.0));
  f.max_delta(-3.0);
  CHECK(f.max_delta() == doctest::Approx(0.0));
  CHECK(f.value() == doctest::Approx(50.0));  // held value untouched
  f.max_delta(20.0);
  CHECK(f.max_delta() == doctest::Approx(20.0));
  CHECK(f.push(65.0) == doctest::Approx(65.0));  // |15| <= 20 now passes
}

TEST_CASE("nexenne::filter::rate_guard reset() unprimes so next push seeds") {
  auto f{flt::rate_guard{2.0}};
  static_cast<void>(f.push(50.0));
  f.reset();
  CHECK(f.value() == doctest::Approx(0.0));
  CHECK(f.max_delta() == doctest::Approx(2.0));      // rate limit preserved
  CHECK(f.push(1000.0) == doctest::Approx(1000.0));  // first after reset seeds
}

TEST_CASE("nexenne::filter::rate_guard reset(initial) primes to a known value") {
  auto f{flt::rate_guard{5.0}};
  f.reset(20.0);
  CHECK(f.value() == doctest::Approx(20.0));
  CHECK(f.push(100.0) == doctest::Approx(20.0));  // |80| > 5 -> rejected vs 20
  CHECK(f.push(24.0) == doctest::Approx(24.0));   // |4| <= 5 -> accepted
}

TEST_CASE("nexenne::filter::rate_guard float instantiation") {
  auto f{flt::rate_guard<float>{0.5f}};
  CHECK(f.push(1.0f) == doctest::Approx(1.0));
  CHECK(f.push(1.5f) == doctest::Approx(1.5));  // delta 0.5 == limit
  CHECK(f.push(3.0f) == doctest::Approx(1.5));  // too far, holds
}

TEST_CASE("nexenne::filter::rate_guard NaN sample fails the <= test and is rejected") {
  auto const nan{std::numeric_limits<double>::quiet_NaN()};
  auto f{flt::rate_guard{5.0}};
  static_cast<void>(f.push(10.0));
  // std::abs(NaN - 10) is NaN; NaN <= 5 is false, so the sample is rejected.
  CHECK(f.push(nan) == doctest::Approx(10.0));
  CHECK(f.push(12.0) == doctest::Approx(12.0));  // recovers from the held 10
}

TEST_CASE("nexenne::filter::rate_guard NaN as the first sample seeds NaN") {
  auto const nan{std::numeric_limits<double>::quiet_NaN()};
  auto f{flt::rate_guard{5.0}};
  CHECK(std::isnan(f.push(nan)));  // first sample seeds unconditionally
  // Held value is NaN; every subsequent delta is NaN and rejected, so it sticks.
  CHECK(std::isnan(f.push(5.0)));
}

TEST_CASE("nexenne::filter::validator unprimed accepts the first sample unconditionally") {
  // One-arg (unprimed) construction has no deduction guide (T is not deducible
  // from the predicate alone), so the type is named, as in the explicit case.
  auto const pred{[](int const& x) { return x > 0; }};
  auto f{flt::validator<int, decltype(pred)>{pred}};
  // First sample fails the predicate but is still accepted to seed state.
  CHECK(f.push(-7) == -7);
  CHECK(f.value() == -7);
  // Now primed: a failing sample holds, a passing sample updates.
  CHECK(f.push(-1) == -7);
  CHECK(f.push(4) == 4);
}

TEST_CASE("nexenne::filter::validator primed ctor rejects a bad first sample") {
  auto f{flt::validator{[](int const& x) { return x > 0; }, int{99}}};
  CHECK(f.value() == 99);
  CHECK(f.push(-3) == 99);  // fails predicate, primed -> holds initial
  CHECK(f.push(5) == 5);    // passes -> accepted
}

TEST_CASE("nexenne::filter::validator passes valid, holds last-good through a run of invalids") {
  auto f{flt::validator{[](int const& x) { return x > 0; }, int{0}}};
  static_cast<void>(f.push(5));
  CHECK(f.push(-1) == 5);  // invalid -> holds 5
  CHECK(f.push(0) == 5);   // boundary: 0 is not > 0 -> holds 5
  CHECK(f.push(-9) == 5);  // sustained invalid run keeps last good
  CHECK(f.push(7) == 7);   // recovery
  CHECK(f.value() == 7);
}

TEST_CASE("nexenne::filter::validator reset unprimes so next push is accepted unconditionally") {
  auto f{flt::validator{[](int const& x) { return x > 0; }, int{50}}};
  static_cast<void>(f.push(10));
  f.reset();
  CHECK(f.value() == 0);        // value-initialised int
  CHECK(f.push(-100) == -100);  // unprimed again -> accepted unconditionally
  CHECK(f.push(-5) == -100);    // now primed -> failing sample holds
}

TEST_CASE("nexenne::filter::validator parity predicate over a register type") {
  // Accept only even register values (bit 0 clear), seeded with an even value.
  auto f{flt::validator{[](std::uint16_t const& r) { return (r & 1u) == 0u; }, std::uint16_t{0}}};
  CHECK(f.push(std::uint16_t{4}) == std::uint16_t{4});
  CHECK(f.push(std::uint16_t{7}) == std::uint16_t{4});  // odd -> rejected
  CHECK(f.push(std::uint16_t{8}) == std::uint16_t{8});  // even -> accepted
}

TEST_CASE("nexenne::filter::validator floating predicate with finite check") {
  auto const nan{std::numeric_limits<double>::quiet_NaN()};
  auto const inf{std::numeric_limits<double>::infinity()};
  auto f{flt::validator{[](double const& x) { return std::isfinite(x); }, 0.0}};
  CHECK(f.push(1.5) == doctest::Approx(1.5));
  CHECK(f.push(nan) == doctest::Approx(1.5));  // not finite -> holds
  CHECK(f.push(inf) == doctest::Approx(1.5));  // not finite -> holds
  CHECK(f.push(2.5) == doctest::Approx(2.5));  // finite -> accepted
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
  static_cast<void>(f.push(42));
  static_cast<void>(f.push(42));
  CHECK(f.push(99) == 42);  // 2x42 vs 1x99 -> 42
  CHECK(f.filled() == true);
}

TEST_CASE("nexenne::filter::majority fill transient resolves the running mode each push") {
  auto f{flt::majority<int, 3>{}};
  CHECK(f.push(7) == 7);  // n=1: only candidate
  CHECK(f.push(7) == 7);  // n=2: 7 twice
  CHECK(f.push(8) == 7);  // n=3: 7,7,8 -> 7
  CHECK(f.filled() == true);
}

TEST_CASE("nexenne::filter::majority tracks a real shift once the window refills") {
  auto f{flt::majority<int, 3>{}};
  static_cast<void>(f.push(10));
  static_cast<void>(f.push(10));
  static_cast<void>(f.push(10));
  static_cast<void>(f.push(20));  // window 10,10,20 -> 10
  CHECK(f.value() == 10);
  static_cast<void>(f.push(20));  // window 10,20,20 -> 20
  CHECK(f.value() == 20);
  CHECK(f.push(20) == 20);  // window 20,20,20 -> 20
}

TEST_CASE("nexenne::filter::majority all-distinct window breaks ties toward the newest") {
  // N=3 with three different values: each appears once, recency wins.
  auto f{flt::majority<int, 3>{}};
  static_cast<void>(f.push(1));
  static_cast<void>(f.push(2));
  CHECK(f.push(3) == 3);  // 1,2,3 all count 1 -> newest (3)
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
    static_cast<void>(f.push(3));
  }
  CHECK(f.filled() == true);
  CHECK(f.value() == 3);
}

TEST_CASE("nexenne::filter::majority bool default type votes a 3-window") {
  auto f{flt::majority<>{}};  // bool, N=3
  static_cast<void>(f.push(true));
  static_cast<void>(f.push(true));
  CHECK(f.push(false) == true);   // 2 true vs 1 false
  CHECK(f.push(false) == false);  // window true,false,false -> false
}

TEST_CASE("nexenne::filter::majority tie-after-wrap: newest sample wins the even-window tie") {
  // N=2 makes ties unavoidable and forces a ring-buffer wrap. This audits the
  // chrono_index((m_idx + N - n + pos) % N) math after m_idx wraps to 0.
  //
  //   push(5): buf=[5,_], idx=1, n=1 -> mode 5
  //   push(7): buf=[5,7], idx=0, n=2 -> tie {5,7}; chrono newest is buf[1]=7 -> 7
  //   push(9): buf=[9,7], idx=1, n=2 -> wrap occurred; chrono oldest is buf[1]=7,
  //            newest is buf[0]=9; tie -> newest 9 wins
  //   push(3): buf=[9,3], idx=0, n=2 -> chrono oldest buf[0]=9, newest buf[1]=3;
  //            tie -> newest 3 wins
  auto f{flt::majority<int, 2>{}};
  CHECK(f.push(5) == 5);
  CHECK(f.push(7) == 7);  // tie broken toward newest before any wrap
  CHECK(f.push(9) == 9);  // tie broken toward newest AFTER the wrap (the audit)
  CHECK(f.push(3) == 3);  // and again on the next wrap
}

TEST_CASE("nexenne::filter::majority even window keeps a clear majority across a wrap") {
  // N=4: a clear (non-tie) majority must survive the wrap correctly.
  auto f{flt::majority<int, 4>{}};
  static_cast<void>(f.push(1));
  static_cast<void>(f.push(2));
  static_cast<void>(f.push(2));
  CHECK(f.push(2) == 2);  // window 1,2,2,2 -> 2 (filled)
  // Wrap: next push overwrites the oldest slot (the 1).
  CHECK(f.push(2) == 2);         // window 2,2,2,2 -> 2
  static_cast<void>(f.push(5));  // window 2,2,2,5 -> 2
  CHECK(f.value() == 2);
}

TEST_CASE("nexenne::filter::majority reset empties the buffer and clears value") {
  auto f{flt::majority<int, 3>{}};
  static_cast<void>(f.push(1));
  static_cast<void>(f.push(1));
  static_cast<void>(f.push(1));
  CHECK(f.filled() == true);
  f.reset();
  CHECK(f.filled() == false);
  CHECK(f.value() == 0);
  // Voting resumes from an empty window: the next push is the sole candidate.
  CHECK(f.push(8) == 8);
}

TEST_CASE("nexenne::filter::stale_detector flags after exactly N identical samples") {
  auto f{flt::stale_detector<int, 3>{}};
  CHECK(f.is_stale() == false);
  CHECK(f.streak() == 0);

  CHECK(f.push(42) == 42);  // streak 1
  CHECK(f.is_stale() == false);
  CHECK(f.streak() == 1);
  static_cast<void>(f.push(42));  // streak 2
  CHECK(f.is_stale() == false);
  CHECK(f.streak() == 2);
  static_cast<void>(f.push(42));  // streak 3 == N -> stale
  CHECK(f.is_stale() == true);
  CHECK(f.streak() == 3);
}

TEST_CASE("nexenne::filter::stale_detector streak saturates at N and stays stale") {
  auto f{flt::stale_detector<int, 3>{}};
  for (auto i{0}; i < 6; ++i) {
    static_cast<void>(f.push(1));
  }
  CHECK(f.is_stale() == true);
  CHECK(f.streak() == 3);  // capped at N, does not run away
}

TEST_CASE("nexenne::filter::stale_detector a changed value clears staleness and resets streak") {
  auto f{flt::stale_detector<int, 3>{}};
  static_cast<void>(f.push(1));
  static_cast<void>(f.push(1));
  static_cast<void>(f.push(1));
  CHECK(f.is_stale() == true);
  CHECK(f.push(2) == 2);  // fresh value
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
  CHECK(f.push(7) == 7);  // first sample primes streak to 1 == N
  CHECK(f.is_stale() == true);
  CHECK(f.streak() == 1);
  CHECK(f.push(8) == 8);  // a different value -> streak resets to 1, still == N
  CHECK(f.is_stale() == true);
}

TEST_CASE("nexenne::filter::stale_detector reset returns to the unprimed condition") {
  auto f{flt::stale_detector<int, 3>{}};
  static_cast<void>(f.push(4));
  static_cast<void>(f.push(4));
  static_cast<void>(f.push(4));
  CHECK(f.is_stale() == true);
  f.reset();
  CHECK(f.is_stale() == false);
  CHECK(f.streak() == 0);
  CHECK(f.value() == 0);
  // After reset the very next push primes a fresh streak of 1.
  CHECK(f.push(4) == 4);
  CHECK(f.streak() == 1);
  CHECK(f.is_stale() == false);
}

TEST_CASE("nexenne::filter::stale_detector double type with default N flags a frozen reading") {
  auto f{flt::stale_detector<double, 10>{}};
  for (auto i{0}; i < 9; ++i) {
    static_cast<void>(f.push(1.5));
  }
  CHECK(f.is_stale() == false);  // one short of the threshold
  CHECK(f.streak() == 9);
  static_cast<void>(f.push(1.5));
  CHECK(f.is_stale() == true);  // tenth identical sample -> stale
  CHECK(f.value() == doctest::Approx(1.5));
}

TEST_CASE("nexenne::filter::stale_detector alternating values never go stale") {
  auto f{flt::stale_detector<int, 3>{}};
  for (auto i{0}; i < 10; ++i) {
    static_cast<void>(f.push(i % 2));
    CHECK(f.is_stale() == false);
    CHECK(f.streak() == 1);
  }
}

}  // namespace
