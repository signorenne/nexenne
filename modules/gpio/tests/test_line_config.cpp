/**
 * @file
 * @brief Tests for the per-line open-time configuration.
 */

#include <doctest/doctest.h>

#include <chrono>
#include <type_traits>

#include <nexenne/gpio/line_config.hpp>

namespace {

namespace ng = nexenne::gpio;
using namespace std::chrono_literals;

TEST_CASE("line_config: default requests a polling-only line") {
  ng::line_config const config{};

  CHECK(config.edges() == ng::edge_detection::none);
  CHECK(config.debounce_period() == 0ns);
  CHECK(config.initial_value() == false);
  CHECK(config.clock() == ng::line_clock::monotonic);

  static_assert(std::is_trivially_copyable_v<ng::line_config>);
  static_assert(std::is_same_v<ng::line_config::value_type, bool>);
}

TEST_CASE("line_config: the constructor sets all four knobs") {
  ng::line_config const config{ng::edge_detection::both, 5ms, true, ng::line_clock::realtime};

  CHECK(config.edges() == ng::edge_detection::both);
  CHECK(config.debounce_period() == 5ms);
  CHECK(config.initial_value() == true);
  CHECK(config.clock() == ng::line_clock::realtime);

  ng::line_config changed{};
  changed.clock() = ng::line_clock::realtime;
  CHECK(changed.clock() == ng::line_clock::realtime);
  changed.clock() = ng::line_clock::hte;
  CHECK(changed.clock() == ng::line_clock::hte);
}

TEST_CASE("line_config: mutable accessors rewrite one knob at a time") {
  ng::line_config config{};
  config.edges() = ng::edge_detection::falling;
  config.debounce_period() = 20ms;
  config.initial_value() = true;

  CHECK(config.edges() == ng::edge_detection::falling);
  CHECK(config.debounce_period() == 20ms);
  CHECK(config.initial_value() == true);

  CHECK(config != ng::line_config{});
}

}  // namespace
