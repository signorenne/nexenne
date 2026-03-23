/**
 * @file
 * @brief Validation and guard filters.
 *
 * Each defends the downstream logic from a different kind of bad sample: out of
 * range, an impossible jump, a domain-rule violation, a single corrupted read,
 * and a sensor that has stopped updating. Good samples pass through untouched.
 * The tour also shows the guards composed (a range guard feeding a rate guard),
 * their first-sample edge cases, and reset() behaviour.
 *
 * Each stateful push is bound to a named value before printing: argument
 * evaluation order is unspecified in C++, so feeding one filter several times
 * inside a single call would print the results in an undefined order.
 */

#include <print>

#include <nexenne/filter/majority.hpp>
#include <nexenne/filter/range_guard.hpp>
#include <nexenne/filter/rate_guard.hpp>
#include <nexenne/filter/stale_detector.hpp>
#include <nexenne/filter/validator.hpp>

namespace {

namespace flt = nexenne::filter;

}  // namespace

auto main() -> int {
  // 1. range_guard holds the last valid sample when one falls outside 0..100.
  // This catches the most common bus failure: a corrupted transfer returns a
  // value outside the sensor's physical range, so we forward the last good read
  // instead of garbage.
  auto rg{flt::range_guard{0.0, 100.0}};
  auto const g1{rg.push(50.0)};   // in range
  auto const g2{rg.push(200.0)};  // out of range, holds 50
  auto const g3{rg.push(75.0)};   // in range
  std::println(
    "1. range_guard[0,100]: 50 -> {:.0f}, 200 -> {:.0f} (held), 75 -> {:.0f}", g1, g2, g3
  );

  // 2. Edge case: an out-of-range FIRST sample has no prior value to hold, so it
  // is clamped to the nearest bound to put the guard in a defined state. Only
  // later out-of-range samples are rejected.
  auto rg2{flt::range_guard{0.0, 100.0}};
  std::println("2. range_guard first sample 150 (clamped, not held): {:.0f}", rg2.push(150.0));

  // 3. rate_guard rejects a jump larger than 5 from the last accepted value. It
  // is complementary to range_guard: range catches values outside the absolute
  // limits, rate catches values that are in range but changed too fast to be
  // real (a temperature jumping 25 -> 250 in one period).
  auto rt{flt::rate_guard{5.0}};
  auto const r1{rt.push(100.0)};  // first sample seeds
  auto const r2{rt.push(200.0)};  // jump too large, holds 100
  auto const r3{rt.push(103.0)};  // within 5 of 100, accepted
  std::println("3. rate_guard(5): 100 -> {:.0f}, 200 -> {:.0f} (held), 103 -> {:.0f}", r1, r2, r3);

  // 4. The two guards compose: range first to reject the impossible, then rate to
  // reject the physically-too-fast. A 999 is killed by the range stage; a 60
  // (in range but a 50-unit jump from 10) is killed by the rate stage.
  std::println("4. range_guard[0,100] -> rate_guard(5) composed:");
  auto cr{flt::range_guard{0.0, 100.0}};
  auto ct{flt::rate_guard{5.0}};
  for (auto const x : {10.0, 999.0, 60.0, 12.0}) {
    auto const guarded{cr.push(x)};
    auto const rated{ct.push(guarded)};
    std::println("   in {:6.1f} -> range {:5.1f} -> rate {:5.1f}", x, guarded, rated);
  }

  // 5. validator passes a sample only if the predicate accepts it. This is the
  // most flexible rejection filter: any domain rule expressible as bool(T) plugs
  // in - a parity bit, a status byte, a plausibility check.
  auto vd{flt::validator{[](int const x) { return x > 0; }, int{0}}};
  auto const v1{vd.push(5)};   // accepted
  auto const v2{vd.push(-1)};  // rejected, holds 5
  auto const v3{vd.push(7)};   // accepted
  std::println("5. validator(x>0): 5 -> {}, -1 -> {} (held), 7 -> {}", v1, v2, v3);

  // 6. majority vote over 3 corrects a single corrupted read - the software form
  // of triple modular redundancy. Read a register three times and the odd
  // corrupted read loses 2 to 1.
  auto mj{flt::majority<int, 3>{}};
  static_cast<void>(mj.push(42));
  static_cast<void>(mj.push(42));
  std::println("6. majority(3) of 42,42,99: {}", mj.push(99));

  // 7. A wider vote tolerates more corruption: majority(5) survives up to two bad
  // reads out of five. We feed three good 7s and two bad reads.
  auto mj5{flt::majority<int, 5>{}};
  static_cast<void>(mj5.push(7));
  static_cast<void>(mj5.push(99));
  static_cast<void>(mj5.push(7));
  static_cast<void>(mj5.push(13));
  std::println("7. majority(5) of 7,99,7,13,7: {} (two bad reads outvoted)", mj5.push(7));

  // 8. stale_detector flags a sensor that has not changed for 3 reads. It is a
  // diagnostic, not a corrective filter: it passes every sample through and only
  // sets is_stale(), so you chain it ahead of a corrective stage and decide what
  // to do (ignore, alarm, power-cycle).
  auto st{flt::stale_detector<int, 3>{}};
  static_cast<void>(st.push(7));
  static_cast<void>(st.push(7));
  static_cast<void>(st.push(7));
  std::println(
    "8. stale_detector(3) after three 7s: stale={} streak={}", st.is_stale(), st.streak()
  );

  // 9. A fresh value clears the stale flag and restarts the streak: the sensor
  // is alive again, so the watchdog stands down.
  static_cast<void>(st.push(8));
  std::println("   after a fresh 8: stale={} streak={}", st.is_stale(), st.streak());
  return 0;
}
