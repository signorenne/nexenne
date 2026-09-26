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
    "button",
    ng::chip_id{0},
    ng::line_offset{17},
    ng::line_polarity::active_low,
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

TEST_CASE("format: a timestamp names its clock unless it is monotonic") {
  CHECK(std::format("{}", ng::event_time{1200ns}) == "1200ns");
  CHECK(std::format("{}", ng::event_time{1200ns, ng::line_clock::realtime}) == "1200ns realtime");
  CHECK(ng::to_string(ng::event_time{5ns, ng::line_clock::hte}) == "5ns hte");
  std::ostringstream os{};
  os << ng::event_time{7ns};
  CHECK(os.str() == "7ns");
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

TEST_CASE("format: a drain_report prints its counters through every layer") {
  ng::drain_report const report{.delivered = 12, .rejected = 3};
  auto os{std::ostringstream{}};
  os << report;
  CHECK(ng::to_string(report) == "drain_report(delivered=12, rejected=3)");
  CHECK(std::format("{}", report) == ng::to_string(report));
  CHECK(os.str() == ng::to_string(report));
}

TEST_CASE("format: a sequence_tracker prints its last sequence and drop count") {
  ng::sequence_tracker tracker{};
  CHECK(tracker.feed(ng::event_sequence{3}) == 0);
  CHECK(tracker.feed(ng::event_sequence{6}) == 2);
  CHECK(std::format("{}", tracker) == "sequence_tracker(last=6, dropped=2)");
  CHECK(ng::to_string(ng::sequence_tracker{}) == "sequence_tracker(last=0, dropped=0)");
  std::ostringstream os{};
  os << tracker;
  CHECK(os.str() == ng::to_string(tracker));
}

TEST_CASE("format: an event_debounce prints its period, settled level, and deadline") {
  CHECK(
    std::format("{}", ng::event_debounce{})
    == "event_debounce(period=0ns, stable=none, deadline=none)"
  );

  ng::event_debounce debounce{5ms};
  ng::line_event event{};
  event.timestamp = ng::event_time{1000ns};
  CHECK(debounce.feed(event).has_value());
  event.physical = true;
  event.timestamp = ng::event_time{2000ns};
  CHECK_FALSE(debounce.feed(event).has_value());
  CHECK(
    std::format("{}", debounce)
    == "event_debounce(period=5000000ns, stable=low, deadline=5002000ns)"
  );
  std::ostringstream os{};
  os << debounce;
  CHECK(os.str() == ng::to_string(debounce));
}

TEST_CASE("format: the sinks print their observable state") {
  ng::callback_sink const direct{[](ng::line_event const&) noexcept { return true; }};
  CHECK(std::format("{}", direct) == "callback_sink()");
  std::ostringstream direct_os{};
  direct_os << direct;
  CHECK(direct_os.str() == ng::to_string(direct));

  ng::queue_sink<2> queued{};
  CHECK(std::format("{}", queued) == "queue_sink(capacity=1, empty=true, dropped=0)");
  CHECK(queued.push(ng::line_event{}));
  CHECK_FALSE(queued.push(ng::line_event{}));
  CHECK(std::format("{}", queued) == "queue_sink(capacity=1, empty=false, dropped=1)");
  std::ostringstream queued_os{};
  queued_os << queued;
  CHECK(queued_os.str() == ng::to_string(queued));
}

TEST_CASE("format: the handles print their binding and request set") {
  using mock = ng::mock_chip<4>;
  std::array const specs{
    ng::line_spec::input(
      "button",
      ng::chip_id{0},
      ng::line_offset{17},
      ng::line_polarity::active_low,
      ng::line_bias::pull_up
    ),
    ng::line_spec::output("led", ng::chip_id{0}, ng::line_offset{4}),
  };
  std::array const configs{ng::line_config{}, ng::line_config{}};

  CHECK(
    std::format("{}", ng::line<mock>{})
    == "line(unbound, line_spec(unnamed, chip 0, line 0, input, active_high, as_is, push_pull))"
  );
  CHECK(std::format("{}", ng::chip<mock>{}) == "chip(unbound, lines={})");

  mock backend{};
  ng::chip<mock> chip{backend};
  CHECK(std::format("{}", chip) == "chip(closed, lines={})");
  REQUIRE(chip.open(specs, configs).has_value());
  CHECK(std::format("{}", chip) == "chip(open, lines={button: 17, led: 4})");

  auto const button{chip.line_for("button")};
  REQUIRE(button.has_value());
  CHECK(
    std::format("{}", *button)
    == "line(bound, line_spec(button, chip 0, line 17, input, active_low, pull_up, push_pull))"
  );

  std::ostringstream os{};
  os << chip << ' ' << *button;
  CHECK(os.str() == ng::to_string(chip) + ' ' + ng::to_string(*button));
}

TEST_CASE("format: the backends print their identity and open state") {
  ng::mock_chip<4> mock{};
  CHECK(std::format("{}", mock) == "mock_chip(closed, 0 lines)");
  std::array const specs{
    ng::line_spec::input("in", ng::chip_id{0}, ng::line_offset{1}),
    ng::line_spec::output("out", ng::chip_id{0}, ng::line_offset{2}),
  };
  std::array const configs{ng::line_config{}, ng::line_config{}};
  REQUIRE(mock.open(specs, configs).has_value());
  CHECK(std::format("{}", mock) == "mock_chip(open, 2 lines)");
  std::ostringstream mock_os{};
  mock_os << mock;
  CHECK(mock_os.str() == ng::to_string(mock));

  ng::chardev_chip const chardev{};
  CHECK(
    std::format("{}", chardev) == "chardev_chip(chip 0, consumer=nexenne-gpio, closed, 0 lines)"
  );
  CHECK(
    ng::to_string(ng::chardev_chip{ng::chip_id{3}, ""})
    == "chardev_chip(chip 3, consumer=unlabeled, closed, 0 lines)"
  );
  std::ostringstream chardev_os{};
  chardev_os << chardev;
  CHECK(chardev_os.str() == ng::to_string(chardev));

  ng::chardev_watcher const watcher{ng::chip_id{2}};
  CHECK(std::format("{}", watcher) == "chardev_watcher(chip 2, closed)");
  std::ostringstream watcher_os{};
  watcher_os << watcher;
  CHECK(watcher_os.str() == ng::to_string(watcher));
}

TEST_CASE("format: a width spec applies to the whole rendered string") {
  CHECK(std::format("{:>10}", ng::gpio_error::busy) == "      busy");
}

}  // namespace
