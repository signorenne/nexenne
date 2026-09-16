/**
 * @file
 * @brief Linear smoothing and frequency-shaping filters.
 *
 * Runs a noisy step signal through an exponential moving average, a simple
 * moving average, a first-order low-pass, and a second-order biquad low-pass,
 * then explores the parameter knobs (alpha, window, cutoff, Q, order), the
 * complementary high-pass, and the standard probes: a unit impulse to read a
 * filter's kernel and a step to read its rise and overshoot. Every filter shares
 * the same push / value / reset surface.
 *
 * The program walks eight steps:
 *
 * 1. The four workhorse smoothers on one noisy step. Every linear filter smears
 *    the spike at n == 8 (rejecting an outlier is the median's job, in
 *    robust.cpp); the EMA and the low-pass are the same single-pole math, while
 *    the SMA weighs its window equally and lags by a fixed N/2 samples.
 * 2. The EMA alpha knob: each filter starts primed at zero, so the printed value
 *    after eight unit samples is pure rise; a smaller alpha is still climbing.
 * 3. The SMA window knob: a wider window rejects more noise but ramps over more
 *    samples.
 * 4. The biquad Q knob: 0.7071 is the flat Butterworth corner, a higher Q peaks
 *    near the cutoff and overshoots a step before settling.
 * 5. Filter order: each Butterworth section adds two poles, so the order-6
 *    cascade attenuates a 200 Hz tone harder than the order-2 one; the residual
 *    amplitude is measured after the transient has died.
 * 6. The high-pass, the low-pass's complement, strips a DC offset and keeps the
 *    fast wiggle.
 * 7. The impulse response: an FIR's response to a unit impulse is its
 *    coefficients, walked out one per sample.
 * 8. reset() returns a primed EMA to its initial condition, so the next push
 *    reseeds with no memory of the old value.
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <print>
#include <span>

#include <nexenne/filter/biquad.hpp>
#include <nexenne/filter/butterworth.hpp>
#include <nexenne/filter/ema.hpp>
#include <nexenne/filter/fir.hpp>
#include <nexenne/filter/highpass.hpp>
#include <nexenne/filter/lowpass.hpp>
#include <nexenne/filter/sma.hpp>
#include <nexenne/utility/ignore.hpp>

namespace {

namespace flt = nexenne::filter;

/**
 * @brief A 0 to 1 step at sample 4, with a single +0.5 spike at sample 8.
 *
 * @param n Sample index.
 *
 * @return The test signal at sample \p n.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] auto noisy_step(int const n) -> double {
  auto x{n >= 4 ? 1.0 : 0.0};
  if (n == 8) {
    x += 0.5;
  }
  return x;
}

}  // namespace

auto main() -> int {
  auto ema{flt::ema{0.3}};
  auto sma{flt::sma<double, 4>{}};
  auto lp{flt::lowpass{20.0, 1000.0}};
  auto bq{flt::biquad<double>::make_lowpass(20.0, 1000.0)};

  std::println("1. Four smoothers on a noisy step (spike at n=8):");
  std::println("  n   raw    ema    sma     lp   biquad");
  for (auto n{0}; n < 16; ++n) {
    auto const x{noisy_step(n)};
    std::println(
      "{:3}  {:.2f}  {:.3f}  {:.3f}  {:.3f}  {:.3f}",
      n,
      x,
      ema.push(x),
      sma.push(x),
      lp.push(x),
      bq.push(x)
    );
  }

  std::println("\n2. EMA alpha vs. step response (output after 8 samples of 1.0):");
  for (auto const a : {0.1, 0.3, 0.6}) {
    auto e{flt::ema{a}};
    auto y{0.0};
    e.reset(0.0);
    for (auto k{0}; k < 8; ++k) {
      y = e.push(1.0);
    }
    std::println("  alpha {:.1f}: y = {:.3f}", a, y);
  }

  std::println("\n3. SMA window vs. step rise:");
  auto sma2{flt::sma<double, 2>{}};
  auto sma8{flt::sma<double, 8>{}};
  std::print("   N=2 :");
  for (auto n{0}; n < 8; ++n) {
    std::print(" {:.2f}", sma2.push(n >= 2 ? 1.0 : 0.0));
  }
  std::println("");
  std::print("   N=8 :");
  for (auto n{0}; n < 8; ++n) {
    std::print(" {:.2f}", sma8.push(n >= 2 ? 1.0 : 0.0));
  }
  std::println("");

  std::println("\n4. Biquad low-pass Q vs. step overshoot (peak output):");
  for (auto const q : {0.7071, 2.0, 6.0}) {
    auto f{flt::biquad<double>::make_lowpass(60.0, 1000.0, q)};
    auto peak{0.0};
    for (auto n{0}; n < 60; ++n) {
      auto const y{f.push(n >= 2 ? 1.0 : 0.0)};
      peak = std::max(peak, y);
    }
    std::println("  Q {:.4f}: peak = {:.3f}", q, peak);
  }

  std::println("\n5. Butterworth order vs. stop-band rejection (200 Hz tone, 60 Hz cutoff):");
  auto bw1{flt::butterworth<double, 1>{}};
  auto bw3{flt::butterworth<double, 3>{}};
  bw1.design_low_pass(60.0, 1000.0);
  bw3.design_low_pass(60.0, 1000.0);
  auto amp1{0.0};
  auto amp3{0.0};
  for (auto n{0}; n < 400; ++n) {
    auto const tone{std::sin(2.0 * std::numbers::pi * 200.0 * n / 1000.0)};
    auto const y1{bw1.push(tone)};
    auto const y3{bw3.push(tone)};
    if (n >= 200) {
      amp1 = std::max(amp1, std::abs(y1));
      amp3 = std::max(amp3, std::abs(y3));
    }
  }
  std::println("  order 2: residual amplitude = {:.4f}", amp1);
  std::println("  order 6: residual amplitude = {:.4f}", amp3);

  std::println("\n6. High-pass strips a DC offset (input = 5.0 + small wiggle):");
  auto hp{flt::highpass{20.0, 1000.0}};
  std::print("   out:");
  for (auto n{0}; n < 8; ++n) {
    auto const in{5.0 + 0.2 * std::sin(static_cast<double>(n))};
    std::print(" {:+.3f}", hp.push(in));
  }
  std::println("  (offset removed, only the wiggle survives)");

  std::println("\n7. FIR impulse response = its coefficients:");
  auto const taps{std::array<double, 3>{0.5, 0.3, 0.2}};
  auto fir{flt::fir<double, 3>{std::span<double const, 3>{taps}}};
  std::print("   {{0.5, 0.3, 0.2}} ->");
  for (auto const x : std::array{1.0, 0.0, 0.0, 0.0}) {
    std::print(" {:.2f}", fir.push(x));
  }
  std::println("");

  std::println("\n8. reset() clears filter memory:");
  auto e{flt::ema{0.2}};
  nexenne::utility::ignore(e.push(100.0));
  nexenne::utility::ignore(e.push(100.0));
  std::println("   primed high, value() = {:.1f}", e.value());
  e.reset();
  std::println("   after reset, push(3.0) = {:.1f} (reseeds, no lag)", e.push(3.0));
  return 0;
}
