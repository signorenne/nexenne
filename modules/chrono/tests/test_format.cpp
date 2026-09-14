/**
 * @file
 * @brief Tests for the nexenne::chrono formatters.
 */

#include <doctest/doctest.h>

#include <chrono>
#include <concepts>
#include <format>
#include <sstream>
#include <string>
#include <string_view>

#include <nexenne/chrono/format.hpp>
#include <nexenne/chrono/manual_clock.hpp>

namespace {

namespace ch = nexenne::chrono;
using namespace std::chrono_literals;

TEST_CASE("nexenne::chrono::format.hpp alone formats every printable chrono type") {
  auto const parts{ch::extract_parts(std::chrono::milliseconds{61'500})};
  CHECK(std::format("{}", parts) == ch::format(std::chrono::milliseconds{61'500}));

  ch::stopwatch<> const sw;
  CHECK(std::format("{}", sw) == ch::format(std::chrono::milliseconds{0}));
  ch::static_stopwatch<2> const ssw;
  CHECK(std::format("{}", ssw) == ch::format(std::chrono::milliseconds{0}));

  ch::countdown<> const cd{5s};
  CHECK(!std::format("{}", cd).empty());
  auto const dl{ch::deadline<>::after(5s)};
  CHECK(!std::format("{:!}", dl).empty());
  ch::interval<> const iv{1s};
  CHECK(!std::format("{}", iv).empty());
}

// Checks that to_string and operator<< give exactly the formatter's text.
template <typename T>
auto three_layers_agree(T const& value) -> bool {
  auto os{std::ostringstream{}};
  os << value;
  auto const formatted{std::format("{}", value)};
  return ch::to_string(value) == formatted && os.str() == formatted;
}

TEST_CASE("nexenne::chrono to_string and operator<< print the formatter's text") {
  using clk = ch::basic_manual_clock<struct format_layers_tag>;
  clk::reset();

  CHECK(three_layers_agree(ch::extract_parts(std::chrono::milliseconds{61'500})));

  auto sw{ch::stopwatch<clk>{}};
  sw.start();
  auto ssw{ch::static_stopwatch<2, clk>{}};
  ssw.start();
  auto cd{ch::countdown<clk>{5s}};
  cd.start();
  auto const dl{ch::deadline<clk>::after(5s)};
  auto const iv{ch::interval<clk>{1s}};
  clk::advance(1'250ms);

  CHECK(three_layers_agree(sw));
  CHECK(three_layers_agree(ssw));
  CHECK(three_layers_agree(cd));
  CHECK(three_layers_agree(dl));
  CHECK(three_layers_agree(iv));
  CHECK(ch::to_string(sw) == ch::format(1'250ms));
}

// Checks that the formatter, to_string and operator<< all print the name.
template <typename E>
auto names_agree(E const value, std::string_view const name) -> bool {
  auto os{std::ostringstream{}};
  os << value;
  return ch::to_string(value) == name && std::format("{}", value) == name && os.str() == name;
}

TEST_CASE("nexenne::chrono state enums print their names through every layer") {
  CHECK(names_agree(ch::stopwatch_state::idle, "idle"));
  CHECK(names_agree(ch::stopwatch_state::running, "running"));
  CHECK(names_agree(ch::stopwatch_state::paused, "paused"));
  CHECK(names_agree(ch::countdown_state::expired, "expired"));
  CHECK(names_agree(ch::alarm_mode::one_shot, "one_shot"));
  CHECK(names_agree(ch::alarm_mode::periodic, "periodic"));
  CHECK(std::format("[{:>9}]", ch::countdown_state::paused) == "[   paused]");

  static_assert(std::same_as<ch::stopwatch<>::state, ch::stopwatch_state>);
  static_assert(std::same_as<ch::static_stopwatch<2>::state, ch::stopwatch_state>);
  static_assert(std::same_as<ch::countdown<>::state, ch::countdown_state>);
  CHECK(ch::to_string(ch::stopwatch<>{}.current_state()) == "idle");
}

}  // namespace
