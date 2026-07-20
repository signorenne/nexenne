#pragma once

/**
 * @file
 * @brief The per-line behaviour requested when a line is opened.
 *
 * A \c line_config carries the runtime-only knobs that do not belong on the
 * static \c line_spec.hpp description: the edge-event subscription, a
 * backend-side debounce period, and the initial drive value for an output.
 * The spec says what a line is; the config says how this particular request
 * wants it. Backends receive one config per spec at open time.
 *
 * On Linux the debounce period maps to the kernel's own per-line debouncer,
 * which filters bounces before they ever reach userspace and costs no
 * wakeups; not every driver supports it, in which case the open fails and
 * the userspace \c debounce.hpp path is the fallback that works everywhere.
 */

#include <chrono>

#include <nexenne/gpio/line_types.hpp>

namespace nexenne::gpio {

/**
 * @brief Runtime knobs supplied alongside a \c line_spec when opening a line.
 *
 * Every field has a mutable and a const accessor. The defaults request a
 * polling-only line: no edge subscription, no debounce, initial output low.
 */
class line_config {
public:
  using value_type = bool;

private:
  edge_detection m_edges{edge_detection::none};
  std::chrono::nanoseconds m_debounce_period{0};
  bool m_initial_value{false};

public:
  /**
   * @brief Constructs a polling-only config.
   *
   * @pre None.
   * @post No edges are subscribed, the debounce period is zero, and the
   *       initial output value is \c false.
   */
  constexpr line_config() noexcept = default;

  /**
   * @brief Constructs a config from its three knobs.
   *
   * @param edges Edge events the backend is asked to deliver.
   * @param debounce_period Backend-side debounce period; zero disables it.
   * @param initial_value Initial drive value for an output; ignored on inputs.
   *
   * @pre \p debounce_period is non-negative.
   * @post Every accessor returns the corresponding argument.
   */
  explicit constexpr line_config(
    edge_detection const edges,
    std::chrono::nanoseconds const debounce_period = std::chrono::nanoseconds{0},
    bool const initial_value = false
  ) noexcept
    : m_edges{edges}, m_debounce_period{debounce_period}, m_initial_value{initial_value} {}

  /**
   * @brief The requested edge-event subscription.
   *
   * @return The stored edge-detection mode.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto edges() const noexcept -> edge_detection {
    return m_edges;
  }

  /**
   * @brief Mutable access to the edge-event subscription.
   *
   * @return Reference to the stored edge-detection mode.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto edges() noexcept -> edge_detection& {
    return m_edges;
  }

  /**
   * @brief The requested backend-side debounce period.
   *
   * @return The stored period; zero means no backend debounce.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto debounce_period() const noexcept -> std::chrono::nanoseconds {
    return m_debounce_period;
  }

  /**
   * @brief Mutable access to the backend-side debounce period.
   *
   * @return Reference to the stored period.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto debounce_period() noexcept -> std::chrono::nanoseconds& {
    return m_debounce_period;
  }

  /**
   * @brief The initial drive value applied to an output line at open time.
   *
   * @return The stored initial value; ignored for input lines.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto initial_value() const noexcept -> bool {
    return m_initial_value;
  }

  /**
   * @brief Mutable access to the initial drive value.
   *
   * @return Reference to the stored initial value.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto initial_value() noexcept -> bool& {
    return m_initial_value;
  }

  /**
   * @brief Member-wise equality of two configs.
   *
   * @param lhs Left operand.
   * @param rhs Right operand.
   *
   * @return \c true when every field is equal.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] friend constexpr auto
  operator==(line_config const& lhs, line_config const& rhs) noexcept -> bool = default;
};

}  // namespace nexenne::gpio
