/**
 * @file
 * @brief Validation and guard filters.
 *
 * Each defends the downstream logic from a different kind of bad sample: out of
 * range, an impossible jump, a domain-rule violation, a single corrupted read,
 * and a sensor that has stopped updating. Good samples pass through untouched.
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
  // range_guard holds the last valid sample when one falls outside 0..100.
  auto rg{flt::range_guard{0.0, 100.0}};
  auto const g1{rg.push(50.0)};   // in range
  auto const g2{rg.push(200.0)};  // out of range, holds 50
  auto const g3{rg.push(75.0)};   // in range
  std::println("range_guard[0,100]: 50 -> {:.0f}, 200 -> {:.0f} (held), 75 -> {:.0f}", g1, g2, g3);

  // rate_guard rejects a jump larger than 5 from the last accepted value.
  auto rt{flt::rate_guard{5.0}};
  auto const r1{rt.push(100.0)};  // first sample seeds
  auto const r2{rt.push(200.0)};  // jump too large, holds 100
  auto const r3{rt.push(103.0)};  // within 5 of 100, accepted
  std::println("rate_guard(5): 100 -> {:.0f}, 200 -> {:.0f} (held), 103 -> {:.0f}", r1, r2, r3);

  // validator passes a sample only if the predicate accepts it.
  auto vd{flt::validator{[](int const x) { return x > 0; }, int{0}}};
  auto const v1{vd.push(5)};   // accepted
  auto const v2{vd.push(-1)};  // rejected, holds 5
  auto const v3{vd.push(7)};   // accepted
  std::println("validator(x>0): 5 -> {}, -1 -> {} (held), 7 -> {}", v1, v2, v3);

  // majority vote over 3 corrects a single corrupted read.
  auto mj{flt::majority<int, 3>{}};
  static_cast<void>(mj.push(42));
  static_cast<void>(mj.push(42));
  std::println("majority(3) of 42,42,99: {}", mj.push(99));

  // stale_detector flags a sensor that has not changed for 3 reads.
  auto st{flt::stale_detector<int, 3>{}};
  static_cast<void>(st.push(7));
  static_cast<void>(st.push(7));
  static_cast<void>(st.push(7));
  std::println("stale_detector(3) after three 7s: stale={} streak={}", st.is_stale(), st.streak());
}
