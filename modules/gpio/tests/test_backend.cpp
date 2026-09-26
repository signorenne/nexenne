/**
 * @file
 * @brief Concept tests for the backend ladder and the edge sinks.
 */

#include <doctest/doctest.h>

#include <chrono>
#include <optional>
#include <span>

#include <nexenne/gpio/backend.hpp>
#include <nexenne/gpio/sink.hpp>

namespace {

namespace ng = nexenne::gpio;

struct basic_backend {
  auto open(std::span<ng::line_spec const>, std::span<ng::line_config const>) -> ng::result<void> {
    return {};
  }

  auto close() noexcept -> void {}

  [[nodiscard]] auto is_open() const noexcept -> bool {
    return false;
  }

  [[nodiscard]] auto read(ng::line_offset) const -> ng::result<bool> {
    return false;
  }

  auto write(ng::line_offset, bool) -> ng::result<void> {
    return {};
  }
};

struct bulk_backend : basic_backend {
  auto read_lines(std::span<ng::line_offset const>, std::span<bool>) const -> ng::result<void> {
    return {};
  }

  auto write_lines(std::span<ng::line_offset const>, std::span<bool const>) -> ng::result<void> {
    return {};
  }
};

struct reshaping_backend : basic_backend {
  auto reconfigure(std::span<ng::line_spec const>, std::span<ng::line_config const>)
    -> ng::result<void> {
    return {};
  }
};

struct event_backend : basic_backend {
  using native_handle_type = int;

  auto wait_event(std::chrono::nanoseconds) -> ng::result<std::optional<ng::line_event>> {
    return std::optional<ng::line_event>{};
  }

  [[nodiscard]] auto native_handle() const noexcept -> native_handle_type {
    return -1;
  }
};

struct push_sink {
  auto push(ng::line_event const&) noexcept -> bool {
    return true;
  }
};

struct pop_sink : push_sink {
  auto try_pop() noexcept -> std::optional<ng::line_event> {
    return std::nullopt;
  }
};

struct throwing_close : basic_backend {
  auto close() -> void {}
};

struct throwing_push {
  auto push(ng::line_event const&) -> bool {
    return true;
  }
};

TEST_CASE("gpio_backend: each tier admits exactly the types that model it") {
  static_assert(ng::gpio_backend<basic_backend>);
  static_assert(ng::gpio_backend<bulk_backend>);
  static_assert(ng::gpio_backend<event_backend>);

  static_assert(!ng::bulk_gpio_backend<basic_backend>);
  static_assert(ng::bulk_gpio_backend<bulk_backend>);

  static_assert(!ng::edge_source<basic_backend>);
  static_assert(!ng::edge_source<bulk_backend>);
  static_assert(ng::edge_source<event_backend>);

  static_assert(!ng::reconfigurable_gpio_backend<basic_backend>);
  static_assert(ng::reconfigurable_gpio_backend<reshaping_backend>);

  static_assert(!ng::gpio_backend<throwing_close>);
  static_assert(!ng::gpio_backend<int>);

  CHECK(true);
}

TEST_CASE("edge_sink: push is required noexcept, try_pop marks the draining tier") {
  static_assert(ng::edge_sink<push_sink>);
  static_assert(!ng::draining_edge_sink<push_sink>);

  static_assert(ng::edge_sink<pop_sink>);
  static_assert(ng::draining_edge_sink<pop_sink>);

  static_assert(!ng::edge_sink<throwing_push>);

  CHECK(true);
}

}  // namespace
