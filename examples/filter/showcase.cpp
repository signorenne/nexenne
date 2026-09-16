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
 * That is what lets step() below take "any filter" generically. Read it top
 * to bottom.
 *
 * How each stage is tuned in main():
 *
 * - The range guard admits 0..100, the sensor's physical output. It is the
 *   cheapest defence and goes first, so a wild value never reaches a smoother's
 *   state.
 * - The median is width 5: odd so the middle is unambiguous, and wide enough
 *   that a lone spike loses 4 to 1. It lags about two samples, rounds true peaks
 *   a little, and does not smooth jitter, hence the linear stage after it.
 * - The Kalman stage is told the sensor is fairly noisy (R = 4) and the truth
 *   drifts slowly (Q = 0.05); its adaptive gain tracks a trend faster than an
 *   EMA of equal smoothness. An EMA (alpha 0.25) and a 30 Hz low-pass run beside
 *   it to show the latency against smoothness trade; the low-pass is the same
 *   math tuned in physical units.
 * - The slew cap is 0.3 units per sample, set tight on purpose: the Kalman
 *   command steps up to about 0.5 per sample on the steep parts, so the slew
 *   column trails it there and catches up on the flats. Size it to the real
 *   actuator.
 *
 * The raw stream is deterministic (two incommensurate sines stand in for
 * jitter, no rng), so the trace is identical on every run. After the table the
 * guard, median, and Kalman stages are reset() and replayed to draw a bar trace,
 * and the mean absolute error of the raw stream and of the final command against
 * the clean signal quantifies the cleanup.
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

/**
 * @brief The clean waveform to recover: a 12 Hz sine biased to 50, swinging over 40..60.
 *
 * @param n Sample index.
 *
 * @return The clean value at sample \p n.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] auto clean_signal(int const n) -> double {
  auto const t{static_cast<double>(n) / sample_rate_hz};
  return 50.0 + 10.0 * std::sin(2.0 * std::numbers::pi * 12.0 * t);
}

/**
 * @brief The raw sensor stream at sample \p n.
 *
 * The clean signal plus a deterministic jitter, a stuck-high spike at sample 12,
 * a stuck-low spike at sample 25, and an impossible read of 900 at sample 31,
 * the value a corrupted transfer would return.
 *
 * @param n Sample index.
 *
 * @return The noisy reading at sample \p n.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] auto raw_sample(int const n) -> double {
  auto x{clean_signal(n)};

  x += 1.5 * std::sin(static_cast<double>(n) * 1.7);
  x += 0.8 * std::cos(static_cast<double>(n) * 0.9);

  if (n == 12) {
    x += 35.0;
  }
  if (n == 25) {
    x -= 30.0;
  }
  if (n == 31) {
    x = 900.0;
  }
  return x;
}

/**
 * @brief Renders \p v in [lo, hi] as one bar position, so a column traces the signal's shape.
 *
 * An out-of-range value clamps to the edge and marks it, so the eye catches it.
 *
 * @param v Value to place.
 * @param lo Value at the left edge.
 * @param hi Value at the right edge.
 *
 * @return A fixed-width line holding one marker.
 *
 * @pre \p lo is less than \p hi.
 * @post None.
 */
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

/**
 * @brief Pushes one sample through any \c filter_like stage.
 *
 * Constrained on the concept, so the same call site drives the median, the
 * smoother, or the slew limiter: the payoff of the shared push / value / reset
 * surface.
 *
 * @tparam F Stage type modelling \c filter_like.
 * @param stage Stage to advance.
 * @param in Input sample.
 *
 * @return The stage output for \p in.
 *
 * @pre None.
 * @post \p stage has consumed \p in.
 */
template <flt::filter_like F>
[[nodiscard]] auto step(F& stage, double const in) -> double {
  return stage.push(in);
}

}  // namespace

auto main() -> int {
  std::println("== nexenne::filter pipeline: a noisy 1 kHz sensor, cleaned in stages ==\n");

  auto guard{flt::range_guard{0.0, 100.0}};
  auto despike{flt::median<double, 5>{}};
  auto kf{flt::kalman{0.05, 4.0}};
  auto ema{flt::ema{0.25}};
  auto lp{flt::lowpass{30.0, sample_rate_hz}};
  auto slew{flt::slew{0.3}};

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

    auto const guarded{step(guard, raw)};
    auto const medianed{step(despike, guarded)};
    auto const kalmaned{step(kf, medianed)};
    auto const emaed{step(ema, medianed)};
    auto const lped{step(lp, medianed)};
    auto const slewed{step(slew, kalmaned)};

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

  guard.reset();
  despike.reset();
  kf.reset();
  for (int n{0}; n < sample_count; ++n) {
    auto const cleaned{step(kf, step(despike, step(guard, raw_sample(n))))};
    std::println("{:3}  |{}|  {:5.1f}", n, bar(cleaned, 35.0, 65.0), cleaned);
  }

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
