/**
 * @file
 * @brief A guided tour of nexenne::filter through one realistic task: cleaning
 *        up a noisy sensor stream with a staged conditioning pipeline.
 *
 * Pretend we are reading a slow physical quantity once per millisecond - say a
 * load cell, a thermocouple, or an ultrasonic rangefinder. The raw stream is the
 * sum of three problems, and each filter in the module solves exactly one:
 *
 *   1. Out-of-range garbage  -> range_guard rejects the impossible reads a
 *                               corrupted bus transfer produces (0x0000/0xFFFF).
 *   2. Impulsive spikes       -> a median window deletes lone outliers outright
 *                               instead of smearing them across the output.
 *   3. Gaussian jitter        -> a Kalman / EMA / low-pass smoother trades a
 *                               little lag for a lot less wobble.
 *   4. Actuator-side rate     -> a slew limiter ramps the cleaned setpoint so a
 *                               downstream motor never sees a step.
 *
 * The order matters: reject before you smooth (a smoother would average a spike
 * into the signal), and smooth before you slew (the slew stage shapes the final
 * command, not the noise). We build the stages, run a 1000 Hz signal through
 * them, and print an ASCII trace so the cleanup is visible column by column.
 *
 * Every filter shares the same surface: push(sample) -> output, value(), reset().
 * That is what lets process() below take "any filter" generically. Read it top
 * to bottom.
 */

#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <print>
#include <string>

#include <nexenne/filter/concepts.hpp>
#include <nexenne/filter/ema.hpp>
#include <nexenne/filter/kalman.hpp>
#include <nexenne/filter/lowpass.hpp>
#include <nexenne/filter/median.hpp>
#include <nexenne/filter/range_guard.hpp>
#include <nexenne/filter/slew.hpp>

namespace {

namespace flt = nexenne::filter;

constexpr int sample_count{40};
constexpr double sample_rate_hz{1000.0};

// The clean waveform we are trying to recover: a slow 12 Hz sine biased to 50,
// so it swings roughly over 40..60. Everything else added on top is the enemy.
[[nodiscard]] auto clean_signal(int const n) -> double {
  auto const t{static_cast<double>(n) / sample_rate_hz};
  return 50.0 + 10.0 * std::sin(2.0 * std::numbers::pi * 12.0 * t);
}

// The raw sensor stream: the clean signal plus deterministic Gaussian-ish
// jitter, two impulsive spikes (a stuck-high and a stuck-low bus read), and one
// out-of-range garbage value a corrupted transfer would return. Deterministic
// so the printed trace is identical on every run.
[[nodiscard]] auto raw_sample(int const n) -> double {
  auto x{clean_signal(n)};

  // Small zero-mean wobble built from two incommensurate sines: looks like
  // noise to the filters but needs no rng, so the demo stays reproducible.
  x += 1.5 * std::sin(static_cast<double>(n) * 1.7);
  x += 0.8 * std::cos(static_cast<double>(n) * 0.9);

  // Two lone spikes a median rejects but a mean would smear.
  if (n == 12) {
    x += 35.0;  // a stuck-high transient
  }
  if (n == 25) {
    x -= 30.0;  // a stuck-low transient
  }
  // One physically impossible read: the load cell cannot report 900.
  if (n == 31) {
    x = 900.0;
  }
  return x;
}

// Renders a value in [lo, hi] as a single bar position, so a column of these
// traces the signal's shape down the page. Out-of-range values clamp to the edge
// and are marked so the eye catches them.
[[nodiscard]] auto bar(double const v, double const lo, double const hi) -> std::string {
  constexpr int width{32};
  auto const span{hi - lo};
  auto const frac{(v - lo) / span};
  auto col{static_cast<int>(frac * (width - 1) + 0.5)};
  auto flag{' '};
  if (col < 0) {
    col = 0;
    flag = '<';
  } else if (col >= width) {
    col = width - 1;
    flag = '>';
  }
  auto line{std::string(static_cast<std::size_t>(width), ' ')};
  line[static_cast<std::size_t>(col)] = '*';
  line.front() = (flag == '<') ? '<' : line.front();
  line.back() = (flag == '>') ? '>' : line.back();
  return line;
}

// Pushes one sample through any filter_like stage. Templated on the concept so
// the same call site drives the median, the smoother, or the slew limiter; this
// is the payoff of the shared push/value/reset surface.
template <flt::filter_like F>
[[nodiscard]] auto step(F& stage, double const in) -> double {
  return stage.push(in);
}

}  // namespace

auto main() -> int {
  std::println("== nexenne::filter pipeline: a noisy 1 kHz sensor, cleaned in stages ==\n");

  // Stage 1: the range guard. The sensor's physical output can only fall in
  // 0..100; anything outside is a corrupted read, so we hold the last good value
  // instead of forwarding garbage. This is the cheapest, bluntest defence and it
  // belongs first: it stops a wild value from ever reaching the smoother's state.
  auto guard{flt::range_guard{0.0, 100.0}};

  // Stage 2: a width-5 median. Nonlinear, so it deletes an isolated spike rather
  // than averaging it in. Cost: it lags by about two samples and rounds off true
  // peaks a little. We size the window odd so the middle element is unambiguous,
  // and just large enough to outvote a single bad sample (a lone spike loses 4
  // to 1). It does not smooth Gaussian jitter, which is why a linear stage
  // follows.
  auto despike{flt::median<double, 5>{}};

  // Stage 3a: a Kalman smoother. We tell it the sensor is fairly noisy (R = 4)
  // and the true value drifts only slowly (Q = 0.05). Because the gain adapts,
  // it tracks a genuine trend faster than an EMA of the same steady-state
  // smoothness - the right default when you can characterise the noise.
  auto kf{flt::kalman{0.05, 4.0}};

  // Stage 3b/3c: two cheaper smoothers, shown side by side so the latency vs.
  // smoothness trade-off is visible. The EMA (alpha 0.25) is one multiply-add
  // and forgets old data geometrically; the first-order low-pass is the same
  // math but parameterised by a 30 Hz cutoff at the 1 kHz rate, so you tune it
  // in physical units instead of a raw coefficient.
  auto ema{flt::ema{0.25}};
  auto lp{flt::lowpass{30.0, sample_rate_hz}};

  // Stage 4: the slew limiter. It shapes the final command, not the noise: even
  // the smoothed signal would jolt a motor on its steepest stretches, so we cap
  // the change at 0.3 units per sample. The Kalman command it is fed still steps
  // up to ~0.5/sample on the wave's steep parts, so the limiter engages there -
  // watch the slew column trail the kalman column, then catch up on the flats. It
  // adds a little ramp lag in exchange for a bounded actuator rate (the cap is
  // set tight here to make that visible; size it to your real actuator). This is
  // the trade you want on the output side, never on the measurement.
  auto slew{flt::slew{0.3}};

  // The trace header. Each row is one sample; the bar column traces the Kalman
  // output so the smoothing is visible as a clean curve next to the jagged raw.
  std::println(
    "{:>3}  {:>7}  {:>6}  {:>6}  {:>6}  {:>6}  {:>6}  {:>6}",
    "n",
    "raw",
    "guard",
    "med",
    "kalman",
    "ema",
    "lp",
    "slew"
  );
  std::println("{:->62}", "");

  double sum_abs_err_raw{0.0};
  double sum_abs_err_out{0.0};

  for (int n{0}; n < sample_count; ++n) {
    auto const raw{raw_sample(n)};

    // The pipeline, left to right. Each stage consumes the previous output, so
    // the spike is gone before the smoother sees it and the smooth value is what
    // the slew limiter ramps toward.
    auto const guarded{step(guard, raw)};
    auto const medianed{step(despike, guarded)};
    auto const kalmaned{step(kf, medianed)};
    auto const emaed{step(ema, medianed)};
    auto const lped{step(lp, medianed)};
    auto const slewed{step(slew, kalmaned)};

    // Accumulate how far the raw stream and the final command sit from truth, to
    // quantify the cleanup at the end.
    auto const truth{clean_signal(n)};
    sum_abs_err_raw += std::abs(raw - truth);
    sum_abs_err_out += std::abs(slewed - truth);

    std::println(
      "{:3}  {:7.1f}  {:6.1f}  {:6.1f}  {:6.2f}  {:6.2f}  {:6.2f}  {:6.2f}",
      n,
      raw,
      guarded,
      medianed,
      kalmaned,
      emaed,
      lped,
      slewed
    );
  }

  std::println("\n== Kalman output traced against the 40..60 band ==");
  std::println("   (every row is one sample; the spike rows never bend the curve)\n");

  // Re-run only the guard + median + Kalman path to draw the bar trace. Filters
  // are cheap and stateful, so we reset() them to replay from a clean slate
  // rather than allocating new ones.
  guard.reset();
  despike.reset();
  kf.reset();
  for (int n{0}; n < sample_count; ++n) {
    auto const cleaned{step(kf, step(despike, step(guard, raw_sample(n))))};
    std::println("{:3}  |{}|  {:5.1f}", n, bar(cleaned, 35.0, 65.0), cleaned);
  }

  // The headline number: mean absolute error before and after. The pipeline
  // should shrink it by a wide margin, dominated by killing the spikes and the
  // out-of-range read that the raw stream still carries.
  auto const mae_raw{sum_abs_err_raw / sample_count};
  auto const mae_out{sum_abs_err_out / sample_count};
  std::println("\n== Result ==");
  std::println("  mean abs error, raw stream : {:.2f}", mae_raw);
  std::println("  mean abs error, pipeline   : {:.2f}", mae_out);
  std::println("  improvement                : {:.1f}x", mae_raw / mae_out);

  std::println("\nThat is the module in one signal chain: a guard rejects the");
  std::println("impossible, a median deletes the impulsive, a Kalman tames the");
  std::println("jitter, and a slew limiter hands a smooth command to the actuator -");
  std::println("each stage a single push() call behind the same filter_like surface.");
  return 0;
}
