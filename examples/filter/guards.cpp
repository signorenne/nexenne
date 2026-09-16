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
 *
 * The program walks nine steps:
 *
 * 1. range_guard holds the last valid sample when a corrupted transfer returns a
 *    value outside the sensor's physical range.
 * 2. An out-of-range first sample has no prior value to hold, so it is clamped
 *    to the nearest bound; only later out-of-range samples are rejected.
 * 3. rate_guard rejects a jump larger than its limit from the last accepted
 *    value: in range, but changed too fast to be real.
 * 4. The two compose: range first rejects the impossible (999), then rate rejects
 *    the physically too fast (60, a 50-unit jump from 10).
 * 5. validator accepts a sample only when its predicate does, so any domain rule
 *    (a parity bit, a status byte, a plausibility check) plugs in.
 * 6. majority(3) is software triple modular redundancy: a single corrupted read
 *    loses the vote 2 to 1.
 * 7. A wider vote tolerates more corruption: majority(5) outvotes two bad reads.
 * 8. stale_detector is a diagnostic, not a corrective stage: it passes every
 *    sample through and only sets is_stale() after three identical reads.
 * 9. A fresh value clears the stale flag and restarts the streak.
 */

#include <print>

#include <nexenne/filter/majority.hpp>
#include <nexenne/filter/range_guard.hpp>
#include <nexenne/filter/rate_guard.hpp>
#include <nexenne/filter/stale_detector.hpp>
#include <nexenne/filter/validator.hpp>
#include <nexenne/utility/ignore.hpp>

namespace {

namespace flt = nexenne::filter;

}  // namespace

auto main() -> int {
  auto rg{flt::range_guard{0.0, 100.0}};
  auto const g1{rg.push(50.0)};
  auto const g2{rg.push(200.0)};
  auto const g3{rg.push(75.0)};
  std::println(
    "1. range_guard[0,100]: 50 -> {:.0f}, 200 -> {:.0f} (held), 75 -> {:.0f}", g1, g2, g3
  );

  auto rg2{flt::range_guard{0.0, 100.0}};
  std::println("2. range_guard first sample 150 (clamped, not held): {:.0f}", rg2.push(150.0));

  auto rt{flt::rate_guard{5.0}};
  auto const r1{rt.push(100.0)};
  auto const r2{rt.push(200.0)};
  auto const r3{rt.push(103.0)};
  std::println("3. rate_guard(5): 100 -> {:.0f}, 200 -> {:.0f} (held), 103 -> {:.0f}", r1, r2, r3);

  std::println("4. range_guard[0,100] -> rate_guard(5) composed:");
  auto cr{flt::range_guard{0.0, 100.0}};
  auto ct{flt::rate_guard{5.0}};
  for (auto const x : {10.0, 999.0, 60.0, 12.0}) {
    auto const guarded{cr.push(x)};
    auto const rated{ct.push(guarded)};
    std::println("   in {:6.1f} -> range {:5.1f} -> rate {:5.1f}", x, guarded, rated);
  }

  auto vd{flt::validator{[](int const x) { return x > 0; }, int{0}}};
  auto const v1{vd.push(5)};
  auto const v2{vd.push(-1)};
  auto const v3{vd.push(7)};
  std::println("5. validator(x>0): 5 -> {}, -1 -> {} (held), 7 -> {}", v1, v2, v3);

  auto mj{flt::majority<int, 3>{}};
  nexenne::utility::ignore(mj.push(42));
  nexenne::utility::ignore(mj.push(42));
  std::println("6. majority(3) of 42,42,99: {}", mj.push(99));

  auto mj5{flt::majority<int, 5>{}};
  nexenne::utility::ignore(mj5.push(7));
  nexenne::utility::ignore(mj5.push(99));
  nexenne::utility::ignore(mj5.push(7));
  nexenne::utility::ignore(mj5.push(13));
  std::println("7. majority(5) of 7,99,7,13,7: {} (two bad reads outvoted)", mj5.push(7));

  auto st{flt::stale_detector<int, 3>{}};
  nexenne::utility::ignore(st.push(7));
  nexenne::utility::ignore(st.push(7));
  nexenne::utility::ignore(st.push(7));
  std::println(
    "8. stale_detector(3) after three 7s: stale={} streak={}", st.is_stale(), st.streak()
  );

  nexenne::utility::ignore(st.push(8));
  std::println("   after a fresh 8: stale={} streak={}", st.is_stale(), st.streak());
  return 0;
}
