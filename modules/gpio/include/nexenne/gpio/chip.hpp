#pragma once

/**
 * @file
 * @brief A name-addressed handle over one backend and its request set.
 *
 * A \c chip binds a backend to the table of specs it was opened with, so
 * application code can address lines by their logical name ("button",
 * "led") instead of a raw offset, and can mint \c line.hpp handles. The
 * chip stores a non-owning view over the caller's spec array; it never
 * copies or allocates, which is what lets the same type run over a
 * constexpr spec table on Linux and a static one on a bare-metal target.
 *
 * Name lookup is a linear walk of the spec table. Tens of lines per chip is
 * the realistic case, and at that size a walk beats any map.
 *
 * The backend and the spec storage must both outlive the chip; the chip is
 * a view over state the caller owns.
 */

#include <optional>
#include <span>
#include <string_view>

#include <nexenne/gpio/backend.hpp>
#include <nexenne/gpio/error.hpp>
#include <nexenne/gpio/line.hpp>
#include <nexenne/gpio/line_config.hpp>
#include <nexenne/gpio/line_spec.hpp>
#include <nexenne/gpio/line_types.hpp>

namespace nexenne::gpio {

/**
 * @brief Name-addressed open, read, and write over one backend.
 *
 * @tparam Backend Backend type satisfying \c gpio_backend.
 */
template <gpio_backend Backend>
class chip {
public:
  using value_type = bool;
  /// The backend type this chip was instantiated with.
  using backend_type = Backend;

private:
  backend_type* m_backend{nullptr};
  std::span<line_spec const> m_specs{};

  [[nodiscard]] constexpr auto find(std::string_view const name) const noexcept
    -> line_spec const* {
    for (auto const& spec : m_specs) {
      if (spec.name() == name) {
        return &spec;
      }
    }
    return nullptr;
  }

public:
  /**
   * @brief Constructs an unbound chip.
   *
   * Every operation on an unbound chip returns \c gpio_error::not_open.
   *
   * @pre None.
   * @post \c backend() is \c nullptr and \c specs() is empty.
   */
  constexpr chip() noexcept = default;

  /**
   * @brief Binds a chip to \p backend without opening anything.
   *
   * @param backend Backend to address; no ownership is taken.
   *
   * @pre \p backend outlives this chip and every copy of it.
   * @post \c backend() points at \p backend and \c specs() is empty.
   */
  explicit constexpr chip(backend_type& backend) noexcept : m_backend{&backend} {}

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
   * @brief The spec table of the current request set.
   *
   * @return The view stored by the last successful \c open; empty before
   *         any open and after \c close.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto specs() const noexcept -> std::span<line_spec const> {
    return m_specs;
  }

  /**
   * @brief Whether the chip is bound and its backend reports itself open.
   *
   * @return \c true when a backend is bound and open.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto is_open() const noexcept -> bool {
    return m_backend != nullptr && m_backend->is_open();
  }

  /**
   * @brief Opens the backend with parallel spec and config tables.
   *
   * Validates the request once for every backend: the tables must be the
   * same length and non-empty. On success the chip keeps a view over
   * \p specs for name lookup; the caller's array must stay alive and
   * unchanged until \c close.
   *
   * @param specs Specs to request, one per line; caller-owned storage.
   * @param configs Per-line open-time config, parallel to \p specs.
   *
   * @return Nothing on success; \c gpio_error::not_open when unbound,
   *         \c gpio_error::invalid_argument when the tables differ in length
   *         or are empty, otherwise the backend's open error.
   *
   * @pre \p specs and \p configs describe the same lines element by element,
   *      and the storage behind \p specs outlives this chip's request set.
   * @post On success \c specs() views \p specs; on failure the chip keeps
   *       its previous request set.
   */
  auto open(std::span<line_spec const> const specs, std::span<line_config const> const configs)
    -> result<void> {
    if (m_backend == nullptr) {
      return std::unexpected{gpio_error::not_open};
    }
    if (specs.size() != configs.size() || specs.empty()) {
      return std::unexpected{gpio_error::invalid_argument};
    }
    auto const opened{m_backend->open(specs, configs)};
    if (!opened.has_value()) {
      return std::unexpected{opened.error()};
    }
    m_specs = specs;
    return {};
  }

  /**
   * @brief Changes the request's configuration without reopening it.
   *
   * Forwards to the backend's \c reconfigure and, on success, repoints the
   * name-lookup view at \p specs. Available only when the backend models
   * \c reconfigurable_gpio_backend; the request is never released, so
   * exclusivity is kept and outputs never glitch.
   *
   * @param specs New specs addressing the request's lines element by
   *              element, in the opened order; caller-owned storage.
   * @param configs New per-line config, parallel to \p specs.
   *
   * @return Nothing on success; \c gpio_error::not_open when unbound,
   *         \c gpio_error::invalid_argument when the tables differ in
   *         length or are empty, otherwise the backend's error.
   *
   * @pre \p specs and \p configs describe the same lines element by element,
   *      and the storage behind \p specs outlives this chip's request set.
   * @post On success \c specs() views \p specs; on failure the previous
   *       view and configuration are untouched.
   */
  auto
  reconfigure(std::span<line_spec const> const specs, std::span<line_config const> const configs)
    -> result<void>
    requires reconfigurable_gpio_backend<backend_type>
  {
    if (m_backend == nullptr) {
      return std::unexpected{gpio_error::not_open};
    }
    if (specs.size() != configs.size() || specs.empty()) {
      return std::unexpected{gpio_error::invalid_argument};
    }
    auto const changed{m_backend->reconfigure(specs, configs)};
    if (!changed.has_value()) {
      return std::unexpected{changed.error()};
    }
    m_specs = specs;
    return {};
  }

  /**
   * @brief Closes the backend and drops the spec view.
   *
   * Safe to call when already closed or unbound.
   *
   * @pre None.
   * @post \c specs() is empty and the backend, if bound, is closed.
   */
  auto close() noexcept -> void {
    if (m_backend != nullptr) {
      m_backend->close();
    }
    m_specs = {};
  }

  /**
   * @brief Looks up a spec by logical name.
   *
   * @param name Name to search for.
   *
   * @return A copy of the matching spec, or \c std::nullopt when no spec in
   *         the request set has that name.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto spec(std::string_view const name) const noexcept
    -> std::optional<line_spec> {
    auto const* found{find(name)};
    if (found == nullptr) {
      return std::nullopt;
    }
    return *found;
  }

  /**
   * @brief Mints a \c line handle for a named line.
   *
   * @param name Name of the line to bind the handle to.
   *
   * @return A handle bound to this chip's backend and the named spec, or
   *         \c std::nullopt when the chip is unbound or the name is not in
   *         the request set.
   *
   * @pre The bound backend, if any, outlives the returned handle.
   * @post None.
   */
  [[nodiscard]] constexpr auto line_for(std::string_view const name) const noexcept
    -> std::optional<line<backend_type>> {
    if (m_backend == nullptr) {
      return std::nullopt;
    }
    auto const* found{find(name)};
    if (found == nullptr) {
      return std::nullopt;
    }
    return gpio::line<backend_type>{*m_backend, *found};
  }

  /**
   * @brief Reads a named line's logical level.
   *
   * Resolves the name, reads the physical level, and applies the spec's
   * polarity.
   *
   * @param name Name of the line to read.
   *
   * @return The logical level; \c gpio_error::not_open when unbound,
   *         \c gpio_error::not_found when the name is unknown, otherwise
   *         the backend's read error.
   *
   * @pre The bound backend, if any, is open.
   * @post No state is changed.
   */
  [[nodiscard]] auto read(std::string_view const name) const -> result<bool> {
    if (m_backend == nullptr) {
      return std::unexpected{gpio_error::not_open};
    }
    auto const* found{find(name)};
    if (found == nullptr) {
      return std::unexpected{gpio_error::not_found};
    }
    auto const physical{m_backend->read(found->offset())};
    if (!physical.has_value()) {
      return std::unexpected{physical.error()};
    }
    return found->to_logical(*physical);
  }

  /**
   * @brief Drives a named output line to a logical level.
   *
   * Resolves the name, converts the level to physical per the spec's
   * polarity, and forwards it to the backend.
   *
   * @param name Name of the line to write.
   * @param logical Logical level to drive.
   *
   * @return Nothing on success; \c gpio_error::not_open when unbound,
   *         \c gpio_error::not_found when the name is unknown,
   *         \c gpio_error::invalid_argument when the spec is not an output,
   *         otherwise the backend's write error.
   *
   * @pre The bound backend, if any, is open.
   * @post On success the named line holds the physical level matching
   *       \p logical.
   */
  auto write(std::string_view const name, bool const logical) -> result<void> {
    if (m_backend == nullptr) {
      return std::unexpected{gpio_error::not_open};
    }
    auto const* found{find(name)};
    if (found == nullptr) {
      return std::unexpected{gpio_error::not_found};
    }
    if (found->direction() != line_direction::output) {
      return std::unexpected{gpio_error::invalid_argument};
    }
    return m_backend->write(found->offset(), found->to_physical(logical));
  }
};

}  // namespace nexenne::gpio
