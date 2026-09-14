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
#include <type_traits>

#include <nexenne/chrono/format.hpp>
#include <nexenne/chrono/manual_clock.hpp>

namespace {

namespace ch = nexenne::chrono;
using namespace std::chrono_literals;
using namespace std::string_literals;

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

/**
 * @brief Whether \c to_string and \c operator<< give exactly the formatter's text.
 *
 * @tparam T Formattable chrono type.
 * @param value Value printed through all three layers.
 *
 * @return \c true when the three renderings match.
 *
 * @pre None.
 * @post None.
 */
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

TEST_CASE("nexenne::chrono stateful engines print a one-line summary through every layer") {
  using clk = ch::basic_manual_clock<struct engine_summary_tag>;
  clk::reset();

  auto a{ch::alarm<clk>{}};
  CHECK(std::format("{}", a) == "alarm(disarmed, mode=one_shot)");
  a.arm_periodic(clk::now(), 2s);
  CHECK(std::format("{}", a) == "alarm(armed, mode=periodic, next_fire_time=02s)");
  CHECK(three_layers_agree(a));

  auto const sink{[](clk::duration const) noexcept {}};
  ch::scope_timer<std::remove_const_t<decltype(sink)>, clk> const timer{sink};
  auto ft{ch::frame_timer<4, clk>{}};
  CHECK(std::format("{}", ft) == "frame_timer(frames=0, fps=0)");
  ft.tick();
  clk::advance(100ms);
  ft.tick();
  clk::advance(100ms);
  ft.tick();
  clk::advance(1'050ms);
  CHECK(std::format("{}", ft) == "frame_timer(frames=3, fps=10)");
  CHECK(three_layers_agree(ft));

  CHECK(std::format("{}", clk{}) == "manual_clock(now=01s:250ms)");
  CHECK(three_layers_agree(clk{}));
  CHECK(std::format("{}", timer) == "scope_timer(elapsed=01s:250ms)");
  CHECK(three_layers_agree(timer));

  CHECK(std::format("{}", ch::hertz<1000>{}) == "hertz(1000)");
  CHECK(three_layers_agree(ch::hertz<1000>{}));

  auto const limiter{ch::rate_limiter<clk>{10.0, 2.5}};
  CHECK(std::format("{}", limiter) == "rate_limiter(capacity=10, refill_rate=2.5)");
  CHECK(three_layers_agree(limiter));
}

TEST_CASE("nexenne::chrono profiler prints its buckets by name") {
  using clk = ch::basic_manual_clock<struct profiler_summary_tag>;
  auto prof{ch::profiler<clk>{}};
  CHECK(std::format("{}", prof) == "profiler{}");
  CHECK(std::format("{}", ch::profiler<clk>::stats{}) == "profiler_stats(count=0)");

  prof.record("decode", 1ms);
  prof.record("decode", 2ms);
  prof.record("parse", 40us);
  auto const decode{
    "profiler_stats(count=2, total=3.00 ms, min=1.00 ms, max=2.00 ms, mean=1.50 ms)"s
  };
  auto const parse{
    "profiler_stats(count=1, total=40.00 us, min=40.00 us, max=40.00 us, mean=40.00 us)"s
  };
  CHECK(std::format("{}", prof["decode"]) == decode);
  CHECK(three_layers_agree(prof["decode"]));
  CHECK(
    std::format("{}", prof)
    == std::format(R"(profiler{{"decode": {}, "parse": {}}})", decode, parse)
  );
  CHECK(three_layers_agree(prof));
}

/**
 * @brief Whether the formatter, \c to_string and \c operator<< all print \p name.
 *
 * @tparam E Chrono enum type.
 * @param value Enumerator printed through all three layers.
 * @param name Expected enumerator name.
 *
 * @return \c true when every layer prints \p name.
 *
 * @pre None.
 * @post None.
 */
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
