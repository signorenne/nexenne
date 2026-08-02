#pragma once

/**
 * @file
 * @brief An in-memory GPIO backend for tests and hardware-free development.
 *
 * The mock models a chip the way the concepts see one: a request set of
 * lines, a physical level per line, and a FIFO of pending edge events. It
 * satisfies \c gpio_backend, \c bulk_gpio_backend, and \c edge_source, so
 * code written against the concepts runs unchanged on it, and it adds a
 * test-rig surface the concepts do not know about: \c set_physical drives
 * the level an input will read, \c physical observes what an output was
 * driven to, and \c inject queues an edge event for \c wait_event to
 * deliver.
 *
 * Like every backend it exchanges raw PHYSICAL levels; the handles and the
 * decode step above it apply polarity. Storage is fixed-capacity and
 * allocation-free, so the mock also stands in for a HAL backend when
 * portable code is developed off-target.
 *
 * @thread_safety A mock_chip instance is not safe for concurrent calls.
 */

#include <chrono>
#include <cstddef>
#include <optional>
#include <span>

#include <nexenne/container/static_vector.hpp>
#include <nexenne/gpio/error.hpp>
#include <nexenne/gpio/line_config.hpp>
#include <nexenne/gpio/line_event.hpp>
#include <nexenne/gpio/line_spec.hpp>
#include <nexenne/gpio/line_types.hpp>
#include <nexenne/utility/discard.hpp>

namespace nexenne::gpio {

/**
 * @brief An in-memory backend holding levels and a queue of injected events.
 *
 * @tparam Capacity Maximum number of requested lines and of buffered events.
 */
template <std::size_t Capacity = 64>
class mock_chip {
public:
  using value_type = bool;
  /// Pollable-handle type; the mock has no real handle and returns \c -1.
  using native_handle_type = int;

  /// Maximum number of requested lines and of buffered events.
  static constexpr std::size_t capacity{Capacity};

private:
  container::static_vector<line_spec, Capacity> m_specs{};
  container::static_vector<line_config, Capacity> m_configs{};
  container::static_vector<bool, Capacity> m_levels{};
  container::static_vector<line_event, Capacity> m_events{};
  std::size_t m_next_event{0};
  bool m_open{false};

  // An index loop instead of std::ranges::find_if: static_vector's iterators
  // are not constexpr, and tens of lines per chip keeps a linear scan cheap.
  [[nodiscard]] constexpr auto index_of(line_offset const offset
  ) const noexcept -> std::optional<std::size_t> {
    for (std::size_t i{0}; i < m_specs.size(); ++i) {
      if (m_specs[i].offset() == offset) {
        return i;
      }
    }
    return std::nullopt;
  }

public:
  /**
   * @brief Constructs a closed mock with no request set.
   *
   * @pre None.
   * @post \c is_open() is \c false.
   */
  constexpr mock_chip() noexcept = default;

  /**
   * @brief Opens a request set, adopting the given specs and configs.
   *
   * Output lines start at their config's initial value; input lines start
   * low until \c set_physical drives them.
   *
   * @param specs Specs to request, one per line.
   * @param configs Per-line open-time config, parallel to \p specs.
   *
   * @return Nothing on success; \c gpio_error::invalid_argument when the
   *         spans differ in length, are empty, or exceed the capacity.
   *
   * @pre None.
   * @post On success \c is_open() is \c true and pending events are cleared.
   */
  constexpr auto open(
    std::span<line_spec const> const specs, std::span<line_config const> const configs
  ) -> result<void> {
    if (specs.size() != configs.size() || specs.empty() || specs.size() > Capacity) {
      return std::unexpected{gpio_error::invalid_argument};
    }
    close();
    for (std::size_t i{0}; i < specs.size(); ++i) {
      auto const starts_high{
        specs[i].direction() == line_direction::output && configs[i].initial_value()
      };
      // The capacity check above guarantees these cannot fail.
      utility::discard(
        m_specs.push_back(specs[i]),
        m_configs.push_back(configs[i]),
        m_levels.push_back(starts_high)
      );
    }
    m_open = true;
    return {};
  }

  /**
   * @brief Closes the request set and drops all state and pending events.
   *
   * Safe to call when already closed.
   *
   * @pre None.
   * @post \c is_open() is \c false.
   */
  constexpr auto close() noexcept -> void {
    m_specs.clear();
    m_configs.clear();
    m_levels.clear();
    m_events.clear();
    m_next_event = 0;
    m_open = false;
  }

  /**
   * @brief Whether a request set is currently open.
   *
   * @return \c true between a successful \c open and the next \c close.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto is_open() const noexcept -> bool {
    return m_open;
  }

  /**
   * @brief Reads the physical level of one requested line.
   *
   * @param offset Line offset within the chip.
   *
   * @return The physical level; \c gpio_error::not_open when closed,
   *         \c gpio_error::not_found when \p offset is not in the request set.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto read(line_offset const offset) const -> result<bool> {
    if (!m_open) {
      return std::unexpected{gpio_error::not_open};
    }
    auto const index{index_of(offset)};
    if (!index.has_value()) {
      return std::unexpected{gpio_error::not_found};
    }
    return m_levels[*index];
  }

  /**
   * @brief Drives the physical level of one requested output line.
   *
   * @param offset Line offset within the chip.
   * @param physical Physical level to drive.
   *
   * @return Nothing on success; \c gpio_error::not_open when closed,
   *         \c gpio_error::not_found when \p offset is not in the request
   *         set, \c gpio_error::invalid_argument when the line is an input.
   *
   * @pre None.
   * @post On success \c physical(offset) observes \p physical.
   */
  constexpr auto write(line_offset const offset, bool const physical) -> result<void> {
    if (!m_open) {
      return std::unexpected{gpio_error::not_open};
    }
    auto const index{index_of(offset)};
    if (!index.has_value()) {
      return std::unexpected{gpio_error::not_found};
    }
    if (m_specs[*index].direction() != line_direction::output) {
      return std::unexpected{gpio_error::invalid_argument};
    }
    m_levels[*index] = physical;
    return {};
  }

  /**
   * @brief Changes the configuration of the held lines without reopening.
   *
   * Mirrors the kernel's reconfigure semantics: the tables must address the
   * request's lines element by element and in the opened order. Output
   * lines take their new config's initial value (the kernel applies the
   * output-values attribute on reconfigure too); input lines keep their
   * current level. Pending injected events are preserved.
   *
   * @param specs New specs, parallel to the open request's lines.
   * @param configs New per-line config, parallel to \p specs.
   *
   * @return Nothing on success; \c gpio_error::not_open when closed,
   *         \c gpio_error::invalid_argument when the tables are malformed
   *         or do not match the request's lines.
   *
   * @pre \p specs and \p configs describe the same lines element by element.
   * @post On success the stored specs and configs are replaced; on failure
   *       nothing changed.
   */
  constexpr auto reconfigure(
    std::span<line_spec const> const specs, std::span<line_config const> const configs
  ) -> result<void> {
    if (!m_open) {
      return std::unexpected{gpio_error::not_open};
    }
    if (specs.size() != configs.size() || specs.size() != m_specs.size()) {
      return std::unexpected{gpio_error::invalid_argument};
    }
    for (std::size_t i{0}; i < specs.size(); ++i) {
      if (specs[i].offset() != m_specs[i].offset()) {
        return std::unexpected{gpio_error::invalid_argument};
      }
    }
    for (std::size_t i{0}; i < specs.size(); ++i) {
      m_specs[i] = specs[i];
      m_configs[i] = configs[i];
      if (specs[i].direction() == line_direction::output) {
        m_levels[i] = configs[i].initial_value();
      }
    }
    return {};
  }

  /**
   * @brief Reads several lines in one operation.
   *
   * @param offsets Line offsets to read.
   * @param levels_out Caller buffer filled with physical levels, parallel to
   *                   \p offsets.
   *
   * @return Nothing on success; \c gpio_error::invalid_argument when the
   *         spans differ in length, otherwise the first per-line error.
   *
   * @pre None.
   * @post On success \p levels_out holds the level of each offset.
   */
  constexpr auto read_lines(
    std::span<line_offset const> const offsets, std::span<bool> const levels_out
  ) const -> result<void> {
    if (offsets.size() != levels_out.size()) {
      return std::unexpected{gpio_error::invalid_argument};
    }
    for (std::size_t i{0}; i < offsets.size(); ++i) {
      auto const level{read(offsets[i])};
      if (!level.has_value()) {
        return std::unexpected{level.error()};
      }
      levels_out[i] = *level;
    }
    return {};
  }

  /**
   * @brief Drives several output lines in one operation.
   *
   * @param offsets Line offsets to write.
   * @param levels_in Physical levels to drive, parallel to \p offsets.
   *
   * @return Nothing on success; \c gpio_error::invalid_argument when the
   *         spans differ in length, otherwise the first per-line error.
   *
   * @pre None.
   * @post On success every named line holds its requested level.
   */
  constexpr auto write_lines(
    std::span<line_offset const> const offsets, std::span<bool const> const levels_in
  ) -> result<void> {
    if (offsets.size() != levels_in.size()) {
      return std::unexpected{gpio_error::invalid_argument};
    }
    for (std::size_t i{0}; i < offsets.size(); ++i) {
      auto const written{write(offsets[i], levels_in[i])};
      if (!written.has_value()) {
        return std::unexpected{written.error()};
      }
    }
    return {};
  }

  /**
   * @brief Delivers the next injected event, never blocking.
   *
   * A test double has no kernel to wait on, so the timeout is ignored: the
   * next queued event returns immediately and an empty queue is an immediate
   * clean miss.
   *
   * @param timeout Ignored.
   *
   * @return The next injected event, or \c std::nullopt when none is queued;
   *         \c gpio_error::not_open when closed.
   *
   * @pre None.
   * @post On a value result the event is consumed from the queue.
   */
  auto wait_event([[maybe_unused]] std::chrono::nanoseconds const timeout
  ) -> result<std::optional<line_event>> {
    if (!m_open) {
      return std::unexpected{gpio_error::not_open};
    }
    if (m_next_event >= m_events.size()) {
      m_events.clear();
      m_next_event = 0;
      return std::optional<line_event>{};
    }
    auto const event{m_events[m_next_event]};
    m_next_event += 1;
    return std::optional<line_event>{event};
  }

  /**
   * @brief The pollable handle; the mock has none.
   *
   * @return Always \c -1.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto native_handle() const noexcept -> native_handle_type {
    return -1;
  }

  /**
   * @brief Test rig: drives the physical level an input line will read.
   *
   * @param offset Line offset within the chip.
   * @param physical Physical level the line now shows.
   *
   * @return Nothing on success; \c gpio_error::not_open when closed,
   *         \c gpio_error::not_found when \p offset is not in the request set.
   *
   * @pre None.
   * @post On success \c read(offset) returns \p physical.
   */
  constexpr auto set_physical(line_offset const offset, bool const physical) -> result<void> {
    if (!m_open) {
      return std::unexpected{gpio_error::not_open};
    }
    auto const index{index_of(offset)};
    if (!index.has_value()) {
      return std::unexpected{gpio_error::not_found};
    }
    m_levels[*index] = physical;
    return {};
  }

  /**
   * @brief Test rig: observes the physical level of any requested line.
   *
   * Unlike \c read this also works on outputs, which is how a test asserts
   * what the code under test drove.
   *
   * @param offset Line offset within the chip.
   *
   * @return The level, or \c std::nullopt when closed or \p offset is not in
   *         the request set.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto physical(line_offset const offset
  ) const noexcept -> std::optional<bool> {
    if (!m_open) {
      return std::nullopt;
    }
    auto const index{index_of(offset)};
    if (!index.has_value()) {
      return std::nullopt;
    }
    return m_levels[*index];
  }

  /**
   * @brief Test rig: queues an edge event for \c wait_event to deliver.
   *
   * The event is queued verbatim; the line's stored level is not touched, so
   * a test that wants \c read to agree with the event also calls
   * \c set_physical.
   *
   * @param event Event to queue.
   *
   * @return \c true when queued, \c false when closed or the queue is full.
   *
   * @pre None.
   * @post On \c true the event is delivered after all earlier injected ones.
   */
  constexpr auto inject(line_event const& event) noexcept -> bool {
    if (!m_open || m_events.size() >= Capacity) {
      return false;
    }
    // The size check above guarantees the push cannot fail.
    utility::discard(m_events.push_back(event));
    return true;
  }
};

}  // namespace nexenne::gpio
