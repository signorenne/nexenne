/**
 * @file
 * @brief Linear smoothing and frequency-shaping filters.
 *
 * Runs a noisy step signal through an exponential moving average, a simple
 * moving average, a first-order low-pass, and a second-order biquad low-pass,
 * then walks a unit impulse through a short FIR to show its kernel. Every
 * filter shares the same push / value / reset surface.
 */

#include <array>
#include <cmath>
#include <print>
#include <span>

#include <nexenne/filter/biquad.hpp>
#include <nexenne/filter/ema.hpp>
#include <nexenne/filter/fir.hpp>
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
  auto ema{flt::ema{0.3}};
  auto sma{flt::sma<double, 4>{}};
  auto lp{flt::lowpass{20.0, 1000.0}};
  auto bq{flt::biquad<double>::make_lowpass(20.0, 1000.0)};

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

  // A 3-tap FIR weighted toward the newest sample: a unit impulse walks the
  // kernel out one tap per sample.
  auto const taps{std::array<double, 3>{0.5, 0.3, 0.2}};
  auto fir{flt::fir<double, 3>{std::span<double const, 3>{taps}}};
  std::print("\nFIR impulse response:");
  for (auto const x : std::array{1.0, 0.0, 0.0, 0.0}) {
    std::print(" {:.2f}", fir.push(x));
  }
  std::println("");
}
