/**
 * @file
 * @brief Tests for the nexenne::gpio formatters.
 */

#include <doctest/doctest.h>

#include <array>
#include <chrono>
#include <format>
#include <sstream>

#include <nexenne/gpio/format.hpp>

namespace {

namespace ng = nexenne::gpio;
using namespace std::chrono_literals;

TEST_CASE("format: enums print their enumerator names") {
  CHECK(std::format("{}", ng::gpio_error::busy) == "busy");
  CHECK(std::format("{}", ng::line_direction::output) == "output");
  CHECK(std::format("{}", ng::line_polarity::active_low) == "active_low");
  CHECK(std::format("{}", ng::line_bias::pull_up) == "pull_up");
  CHECK(std::format("{}", ng::line_drive::open_drain) == "open_drain");
  CHECK(std::format("{}", ng::edge_kind::rising) == "rising");
  CHECK(std::format("{}", ng::edge_detection::both) == "both");

  std::ostringstream os;
  os << ng::gpio_error::timeout << ' ' << ng::edge_kind::falling;
  CHECK(os.str() == "timeout falling");
}

TEST_CASE("format: a spec renders every field") {
  auto const spec{ng::line_spec::input(
    "button", ng::chip_id{0}, ng::line_offset{17}, ng::line_polarity::active_low,
    ng::line_bias::pull_up
  )};
  CHECK(
    std::format("{}", spec)
    == "line_spec(button, chip 0, line 17, input, active_low, pull_up, push_pull)"
  );
  CHECK(ng::to_string(ng::line_spec{}).starts_with("line_spec(unnamed"));
}

TEST_CASE("format: a config renders its four knobs") {
  ng::line_config const config{ng::edge_detection::both, 5ms, true, ng::line_clock::realtime};
  CHECK(
    std::format("{}", config)
    == "line_config(edges=both, debounce=5000000ns, initial=high, clock=realtime)"
  );
  CHECK(std::format("{}", ng::line_clock::monotonic) == "monotonic");
  CHECK(std::format("{}", ng::line_clock::hte) == "hte");
}

TEST_CASE("format: events and observations render identity and state") {
  ng::line_event event{};
  event.chip = ng::chip_id{0};
  event.offset = ng::line_offset{17};
  event.sequence = ng::event_sequence{7};
  event.timestamp = ng::event_time{1200ns};
  event.physical = true;
  event.edge = ng::edge_kind::rising;
  CHECK(
    std::format("{}", event)
    == "line_event(chip 0, line 17, rising, physical=high, seq=7, t=1200ns)"
  );

  auto const spec{ng::line_spec::input("button", ng::chip_id{0}, ng::line_offset{17})};
  ng::line_value const value{
    spec, true, ng::edge_kind::rising, ng::event_sequence{7}, ng::event_time{1200ns}
  };
  CHECK(
    std::format("{}", value)
    == "line_value(button, chip 0, line 17, logical=true, rising, seq=7, t=1200ns)"
  );
}

TEST_CASE("format: discovery records render their identity") {
  std::array<char, 32> name{'g', 'p', 'i', 'o', 'c', 'h', 'i', 'p', '0', '\0'};
  ng::chip_info const chip{name, {}, 32};
  CHECK(std::format("{}", chip) == "chip_info(gpiochip0, unlabeled, 32 lines)");

  std::array<char, 32> line_name{'L', 'E', 'D', '\0'};
  std::array<char, 32> consumer{'l', 'e', 'd', 's', '\0'};
  ng::line_info const line{
    line_name,
    consumer,
    ng::line_offset{4},
    ng::line_direction::output,
    ng::line_bias::as_is,
    ng::line_drive::push_pull,
    ng::edge_detection::none,
    true,
    false,
  };
  CHECK(
    std::format("{}", line)
    == "line_info(line 4, LED, used by leds, output, as_is, push_pull, edges=none)"
  );

  ng::line_info const unclaimed{};
  CHECK(ng::to_string(unclaimed).find("unused") != std::string::npos);
}

TEST_CASE("format: a width spec applies to the whole rendered string") {
  CHECK(std::format("{:>10}", ng::gpio_error::busy) == "      busy");
}

}  // namespace
