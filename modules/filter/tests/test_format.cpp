/**
 * @file
 * @brief Tests for the nexenne::filter formatters (to_string, operator<<,
 * std::formatter) declared in format.hpp.
 */

#include <doctest/doctest.h>

#include <array>
#include <chrono>
#include <concepts>
#include <cstdint>
#include <format>
#include <span>
#include <sstream>
#include <string>

#include <nexenne/filter/format.hpp>
#include <nexenne/utility/ignore.hpp>

namespace {

namespace flt = nexenne::filter;

TEST_CASE("nexenne::filter::to_string renders smoothers with value and knobs") {
  auto ema{flt::ema{0.5}};
  nexenne::utility::ignore(ema.push(2.0));
  CHECK(flt::to_string(ema) == "ema(value=2, alpha=0.5)");

  auto sma{flt::sma<double, 4>{}};
  nexenne::utility::ignore(sma.push(4.0));
  CHECK(flt::to_string(sma) == "sma(value=4, count=1, window=4)");
}

TEST_CASE("nexenne::filter::to_string renders the guard family with status flags") {
  auto rg{flt::range_guard{0.0, 10.0}};
  nexenne::utility::ignore(rg.push(5.0));
  CHECK(flt::to_string(rg) == "range_guard(value=5, lo=0, hi=10, primed=true)");

  auto rate{flt::rate_guard{1.0}};
  nexenne::utility::ignore(rate.push(0.0));
  nexenne::utility::ignore(rate.push(50.0));  // rejected -> rejected_streak grows
  CHECK(flt::to_string(rate) == "rate_guard(value=0, max_delta=1, rejected_streak=1)");

  auto stale{flt::stale_detector<int, 2>{}};
  nexenne::utility::ignore(stale.push(7));
  nexenne::utility::ignore(stale.push(7));
  CHECK(flt::to_string(stale) == "stale_detector(value=7, streak=2, stale=true)");
}

TEST_CASE("nexenne::filter operator<< streams the same text as to_string") {
  auto f{flt::slew{2.0}};
  nexenne::utility::ignore(f.push(3.0));
  std::ostringstream os;
  os << f;
  CHECK(os.str() == flt::to_string(f));
}

TEST_CASE("nexenne::filter std::format uses the formatter specialization") {
  auto hy{flt::hysteresis{1.0, 2.0}};
  nexenne::utility::ignore(hy.push(3.0));
  CHECK(std::format("{}", hy) == flt::to_string(hy));

  auto k{flt::kalman{0.1, 1.0}};
  nexenne::utility::ignore(k.push(5.0));
  CHECK(std::format("{}", k) == flt::to_string(k));

  auto v{flt::validator{[](std::uint16_t r) { return (r & 1u) == 0u; }, std::uint16_t{4}}};
  CHECK(std::format("{}", v) == "validator(value=4)");
}

TEST_CASE("nexenne::filter::to_string covers converter and windowed types") {
  auto med{flt::median<double, 3>{}};
  nexenne::utility::ignore(med.push(1.0));
  CHECK(flt::to_string(med) == "median(value=1, window=3, filled=false)");

  auto td{flt::timed_debounce{std::chrono::milliseconds{5}}};
  nexenne::utility::ignore(td.update(std::chrono::milliseconds{0}, true));
  CHECK(std::format("{}", td) == flt::to_string(td));
}

// Checks that the formatter and operator<< give exactly to_string's text.
template <typename T>
auto three_layers_agree(T const& value) -> bool {
  auto os{std::ostringstream{}};
  os << value;
  auto const text{flt::to_string(value)};
  return std::format("{}", value) == text && os.str() == text;
}

TEST_CASE("nexenne::filter every formatted type prints the same text through every layer") {
  CHECK(three_layers_agree(flt::ema{0.5}));
  CHECK(three_layers_agree(flt::sma<double, 4>{}));
  CHECK(three_layers_agree(flt::lowpass{10.0, 1000.0}));
  CHECK(three_layers_agree(flt::highpass{10.0, 1000.0}));
  CHECK(three_layers_agree(flt::biquad<double>::make_lowpass(50.0, 1000.0)));
  CHECK(three_layers_agree(flt::butterworth<double, 2>{}));
  auto const taps{std::array{0.25, 0.5, 0.25}};
  CHECK(three_layers_agree(flt::fir<double, 3>{std::span<double const, 3>{taps}}));
  CHECK(three_layers_agree(flt::median<double, 3>{}));
  CHECK(three_layers_agree(flt::kalman{0.1, 1.0}));
  CHECK(three_layers_agree(flt::complementary{0.98}));
  CHECK(three_layers_agree(flt::lms<double, 4>{}));
  CHECK(three_layers_agree(flt::slew{2.0}));
  CHECK(three_layers_agree(flt::debounce<bool, 3>{}));
  CHECK(three_layers_agree(flt::timed_debounce{std::chrono::milliseconds{5}}));
  CHECK(three_layers_agree(flt::hysteresis{1.0, 2.0}));
  CHECK(three_layers_agree(flt::glitch<bool, 3>{}));
  CHECK(three_layers_agree(flt::range_guard{0.0, 10.0}));
  CHECK(three_layers_agree(flt::rate_guard{1.0}));
  CHECK(three_layers_agree(flt::validator{[](int r) { return r > 0; }, 1}));
  CHECK(three_layers_agree(flt::majority<int, 3>{}));
  CHECK(three_layers_agree(flt::stale_detector<int, 2>{}));

  auto const coefs{flt::biquad<double>::coefficients{.b0 = 0.5, .a1 = -0.25}};
  CHECK(three_layers_agree(coefs));
  CHECK(flt::to_string(coefs) == "biquad_coefficients(b0=0.5, b1=0, b2=0, a1=-0.25, a2=0)");
  static_assert(std::same_as<flt::biquad<float>::coefficients, flt::biquad_coefficients<float>>);
}

}  // namespace
