/**
 * @file
 * @brief Tests for the nexenne::filter formatters (to_string, operator<<,
 * std::formatter) declared in format.hpp.
 */

#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <format>
#include <sstream>
#include <string>

#include <nexenne/filter/format.hpp>
#include <nexenne/utility/discard.hpp>

namespace {

namespace flt = nexenne::filter;

TEST_CASE("nexenne::filter::to_string renders smoothers with value and knobs") {
  auto ema{flt::ema{0.5}};
  nexenne::utility::discard(ema.push(2.0));
  CHECK(flt::to_string(ema) == "ema(value=2, alpha=0.5)");

  auto sma{flt::sma<double, 4>{}};
  nexenne::utility::discard(sma.push(4.0));
  CHECK(flt::to_string(sma) == "sma(value=4, count=1, window=4)");
}

TEST_CASE("nexenne::filter::to_string renders the guard family with status flags") {
  auto rg{flt::range_guard{0.0, 10.0}};
  nexenne::utility::discard(rg.push(5.0));
  CHECK(flt::to_string(rg) == "range_guard(value=5, lo=0, hi=10, primed=true)");

  auto rate{flt::rate_guard{1.0}};
  nexenne::utility::discard(rate.push(0.0));
  nexenne::utility::discard(rate.push(50.0));  // rejected -> rejected_streak grows
  CHECK(flt::to_string(rate) == "rate_guard(value=0, max_delta=1, rejected_streak=1)");

  auto stale{flt::stale_detector<int, 2>{}};
  nexenne::utility::discard(stale.push(7));
  nexenne::utility::discard(stale.push(7));
  CHECK(flt::to_string(stale) == "stale_detector(value=7, streak=2, stale=true)");
}

TEST_CASE("nexenne::filter operator<< streams the same text as to_string") {
  auto f{flt::slew{2.0}};
  nexenne::utility::discard(f.push(3.0));
  std::ostringstream os;
  os << f;
  CHECK(os.str() == flt::to_string(f));
}

TEST_CASE("nexenne::filter std::format uses the formatter specialization") {
  auto hy{flt::hysteresis{1.0, 2.0}};
  nexenne::utility::discard(hy.push(3.0));
  CHECK(std::format("{}", hy) == flt::to_string(hy));

  auto k{flt::kalman{0.1, 1.0}};
  nexenne::utility::discard(k.push(5.0));
  CHECK(std::format("{}", k) == flt::to_string(k));

  auto v{flt::validator{[](std::uint16_t r) { return (r & 1u) == 0u; }, std::uint16_t{4}}};
  CHECK(std::format("{}", v) == "validator(value=4)");
}

TEST_CASE("nexenne::filter::to_string covers converter and windowed types") {
  auto med{flt::median<double, 3>{}};
  nexenne::utility::discard(med.push(1.0));
  CHECK(flt::to_string(med) == "median(value=1, window=3, filled=false)");

  auto td{flt::timed_debounce{std::chrono::milliseconds{5}}};
  nexenne::utility::discard(td.update(std::chrono::milliseconds{0}, true));
  CHECK(std::format("{}", td) == flt::to_string(td));
}

}  // namespace
