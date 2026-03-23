/**
 * @file
 * @brief Linear smoothing and frequency-shaping filters.
 *
 * Runs a noisy step signal through an exponential moving average, a simple
 * moving average, a first-order low-pass, and a second-order biquad low-pass,
 * then explores the parameter knobs (alpha, window, cutoff, Q, order), the
 * complementary high-pass, and the standard probes - a unit impulse to read a
 * filter's kernel and a step to read its rise and overshoot. Every filter shares
 * the same push / value / reset surface.
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

namespace {

namespace flt = nexenne::filter;

// A 0..1 step at sample 4, with a single +0.5 spike at sample 8.
[[nodiscard]] auto noisy_step(int const n) -> double {
  auto x{n >= 4 ? 1.0 : 0.0};
  if (n == 8) {
    x += 0.5;
  }
  return x;
}

}  // namespace

auto main() -> int {
  // 1. The four workhorse smoothers side by side on the same noisy step. Watch
  // the spike at n == 8: every linear filter here smears it (none can reject an
  // outlier - that is the median filter's job, in robust.cpp). The EMA and the
  // low-pass are the same single-pole math; the SMA gives every sample in its
  // window equal weight and so has a sharper, fixed N/2-sample delay.
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

  // 2. The alpha knob on the EMA. Smaller alpha forgets old data more slowly, so
  // it smooths harder but lags further behind a step. We feed each a clean unit
  // step and print where the output has reached after eight samples: the heavier
  // smoother is still climbing.
  std::println("\n2. EMA alpha vs. step response (output after 8 samples of 1.0):");
  for (auto const a : {0.1, 0.3, 0.6}) {
    auto e{flt::ema{a}};
    auto y{0.0};
    e.reset(0.0);  // start primed at zero so we measure pure rise, not the seed
    for (auto k{0}; k < 8; ++k) {
      y = e.push(1.0);
    }
    std::println("  alpha {:.1f}: y = {:.3f}", a, y);
  }

  // 3. The window knob on the SMA. A wider window averages more samples, so it
  // rejects more noise but delays the output by N/2 samples. Each filter sees the
  // same step at sample 2; we print the per-sample rise so the longer ramp of the
  // wider window is visible.
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

  // 4. The biquad Q knob. A biquad's resonance is set by Q: 0.7071 is the flat
  // Butterworth corner, while a higher Q peaks near the cutoff and overshoots a
  // step. We probe each with a step and report the peak the output reaches; the
  // high-Q filter rings past 1.0 before settling.
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

  // 5. Filter order via a Butterworth cascade. Each biquad section adds two poles
  // and a steeper rolloff; a sustained 200 Hz tone (above a 60 Hz cutoff) is
  // attenuated harder by the higher-order cascade. We report the output amplitude
  // of the tone once the transient has died.
  std::println("\n5. Butterworth order vs. stop-band rejection (200 Hz tone, 60 Hz cutoff):");
  auto bw1{flt::butterworth<double, 1>{}};  // order 2
  auto bw3{flt::butterworth<double, 3>{}};  // order 6
  bw1.design_low_pass(60.0, 1000.0);
  bw3.design_low_pass(60.0, 1000.0);
  auto amp1{0.0};
  auto amp3{0.0};
  for (auto n{0}; n < 400; ++n) {
    auto const tone{std::sin(2.0 * std::numbers::pi * 200.0 * n / 1000.0)};
    auto const y1{bw1.push(tone)};
    auto const y3{bw3.push(tone)};
    if (n >= 200) {  // measure after the transient settles
      amp1 = std::max(amp1, std::abs(y1));
      amp3 = std::max(amp3, std::abs(y3));
    }
  }
  std::println("  order 2: residual amplitude = {:.4f}", amp1);
  std::println("  order 6: residual amplitude = {:.4f}", amp3);

  // 6. The high-pass is the low-pass's complement: it removes slow drift and DC
  // offset and keeps the fast changes. We add a constant 5.0 offset to a small
  // wiggle; the high-pass output settles around zero, having stripped the bias.
  std::println("\n6. High-pass strips a DC offset (input = 5.0 + small wiggle):");
  auto hp{flt::highpass{20.0, 1000.0}};
  std::print("   out:");
  for (auto n{0}; n < 8; ++n) {
    auto const in{5.0 + 0.2 * std::sin(static_cast<double>(n))};
    std::print(" {:+.3f}", hp.push(in));
  }
  std::println("  (offset removed, only the wiggle survives)");

  // 7. The impulse response: feed 1 then zeros and the output traces the filter's
  // kernel. For a 3-tap FIR weighted toward the newest sample, a unit impulse
  // walks the taps out one per sample - an FIR's impulse response is literally
  // its coefficients. (An IIR would ring on forever; an FIR is always finite.)
  std::println("\n7. FIR impulse response = its coefficients:");
  auto const taps{std::array<double, 3>{0.5, 0.3, 0.2}};
  auto fir{flt::fir<double, 3>{std::span<double const, 3>{taps}}};
  std::print("   {{0.5, 0.3, 0.2}} ->");
  for (auto const x : std::array{1.0, 0.0, 0.0, 0.0}) {
    std::print(" {:.2f}", fir.push(x));
  }
  std::println("");

  // 8. reset() returns a stateful filter to its initial condition. We prime an
  // EMA high, reset it, and confirm the next push reseeds directly with no
  // lingering memory of the old value.
  std::println("\n8. reset() clears filter memory:");
  auto e{flt::ema{0.2}};
  static_cast<void>(e.push(100.0));
  static_cast<void>(e.push(100.0));
  std::println("   primed high, value() = {:.1f}", e.value());
  e.reset();
  std::println("   after reset, push(3.0) = {:.1f} (reseeds, no lag)", e.push(3.0));
  return 0;
}
