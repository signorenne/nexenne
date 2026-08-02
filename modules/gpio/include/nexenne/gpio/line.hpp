#pragma once

/**
 * @file
 * @brief A value-typed handle to one line within an open backend.
 *
 * A \c line bundles a non-owning pointer to a backend with the
 * \c line_spec.hpp that addresses one line, and applies the spec's polarity
 * on every read and write, so application code stays in the logical domain:
 * a pressed button is \c true no matter how it is wired. The handle is
 * trivially copyable and compiles down to direct backend calls with no
 * virtual dispatch.
 *
 * The backend must outlive the handle and every copy of it; the handles are
 * views over a backend the caller owns, exactly like a \c std::string_view
 * over a string.
 */

#include <nexenne/gpio/backend.hpp>
#include <nexenne/gpio/error.hpp>
#include <nexenne/gpio/line_spec.hpp>
#include <nexenne/gpio/line_types.hpp>

namespace nexenne::gpio {

/**
 * @brief A handle to one line: logical-domain read and write over a backend.
 *
 * @tparam Backend Backend type satisfying \c gpio_backend.
 */
template <gpio_backend Backend>
class line {
public:
  using value_type = bool;
  /// The backend type this handle was instantiated with.
  using backend_type = Backend;

private:
  backend_type* m_backend{nullptr};
  line_spec m_spec{};

public:
  /**
   * @brief Constructs an unbound handle.
   *
   * Every operation on an unbound handle returns \c gpio_error::not_open.
   *
   * @pre None.
   * @post \c valid() is \c false.
   */
  constexpr line() noexcept = default;

  /**
   * @brief Binds a handle to \p backend for the line described by \p spec.
   *
   * Stores the address of \p backend; no ownership is taken.
   *
   * @param backend Backend that owns the line's request set.
   * @param spec Spec addressing the line; copied into the handle.
   *
   * @pre \p backend outlives this handle and every copy of it.
   * @post \c valid() is \c true and \c spec() equals \p spec.
   */
  constexpr line(backend_type& backend, line_spec const& spec) noexcept
      : m_backend{&backend}, m_spec{spec} {}

  /**
   * @brief The spec used to address the bound line.
   *
   * @return Reference to the stored spec.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto spec() const noexcept -> line_spec const& {
    return m_spec;
  }

  /**
   * @brief The bound backend.
   *
   * @return The stored backend pointer, or \c nullptr when unbound.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto backend() const noexcept -> backend_type* {
    return m_backend;
  }

  /**
   * @brief Whether the handle is bound to a backend.
   *
   * @return \c true when a backend is bound.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto valid() const noexcept -> bool {
    return m_backend != nullptr;
  }

  /**
   * @brief Reads the current logical level of the line.
   *
   * Forwards to the backend's physical read and applies the spec's polarity.
   *
   * @return The logical level; \c gpio_error::not_open when unbound,
   *         otherwise the backend's read error.
   *
   * @pre The bound backend, if any, is open.
   * @post No state is changed.
   */
  [[nodiscard]] auto read() const -> result<bool> {
    if (m_backend == nullptr) {
      return std::unexpected{gpio_error::not_open};
    }
    auto const physical{m_backend->read(m_spec.offset())};
    if (!physical.has_value()) {
      return std::unexpected{physical.error()};
    }
    return m_spec.to_logical(*physical);
  }

  /**
   * @brief Drives the line to a logical level.
   *
   * Converts the level to physical per the spec's polarity and forwards it
   * to the backend.
   *
   * @param logical Logical level to drive.
   *
   * @return Nothing on success; \c gpio_error::not_open when unbound,
   *         \c gpio_error::invalid_argument when the spec is not an output,
   *         otherwise the backend's write error.
   *
   * @pre The bound backend, if any, is open.
   * @post On success the line holds the physical level matching \p logical.
   */
  auto write(bool const logical) -> result<void> {
    if (m_backend == nullptr) {
      return std::unexpected{gpio_error::not_open};
    }
    if (m_spec.direction() != line_direction::output) {
      return std::unexpected{gpio_error::invalid_argument};
    }
    return m_backend->write(m_spec.offset(), m_spec.to_physical(logical));
  }

  /**
   * @brief Drives the line to logical high; shorthand for \c write(true).
   *
   * @return Nothing on success; otherwise the error from \c write.
   *
   * @pre The bound backend, if any, is open.
   * @post On success the line is logically high.
   */
  auto set() -> result<void> {
    return write(true);
  }

  /**
   * @brief Drives the line to logical low; shorthand for \c write(false).
   *
   * @return Nothing on success; otherwise the error from \c write.
   *
   * @pre The bound backend, if any, is open.
   * @post On success the line is logically low.
   */
  auto clear() -> result<void> {
    return write(false);
  }

  /**
   * @brief Reads the line and writes back the inverted logical level.
   *
   * The read and the write are two backend calls and are not atomic with
   * respect to other writers of the same line.
   *
   * @return Nothing on success; the read error when the read fails,
   *         otherwise the error from \c write.
   *
   * @pre The bound backend, if any, is open.
   * @post On success the line holds the complement of the level the read
   *       observed.
   */
  auto toggle() -> result<void> {
    auto const current{read()};
    if (!current.has_value()) {
      return std::unexpected{current.error()};
    }
    return write(!*current);
  }
};

}  // namespace nexenne::gpio
