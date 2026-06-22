#pragma once

/**
 * @file
 * @brief Receiver-side aggregator that auto-disconnects every
 *        tracked subscription on destruction.
 *
 * A \c slot is the thing a class holds to subscribe safely to one
 * or more signals: every subscription it makes is registered with
 * this object, and when the object dies every subscription dies
 * with it. Solves the "subscriber died before signal" UAF that the
 * \c weak_ptr in \c connection cannot: connections protect the
 * signal's lifetime, \c slot protects the receiver's.
 *
 * Vocabulary note: in Qt parlance a "slot" is a single member
 * function. In this library it's the receiver-side handle, the
 * thing you'd reach for to play the role Qt's \c QObject plays
 * implicitly. We chose the shorter name for ergonomics.
 *
 * Embedded-friendly:
 *
 *   - Inline storage of \c Capacity connections (zero heap).
 *   - Fixed upper bound: \c track returns \c false when full so
 *     the caller can react instead of silently allocating.
 *
 * Typical use:
 *
 * \code
 * class widget {
 *     signal::slot<4> m_slot;
 * public:
 *     widget(audio_source& src, ui_bus& ui) noexcept {
 *         src.on_frame.connect<&widget::on_frame>(*this, m_slot);
 *         ui .on_click.connect<&widget::on_click>(*this, m_slot);
 *     }
 *     // ~widget() -> ~m_slot -> both subscriptions disconnect.
 * };
 * \endcode
 */

#include <cstddef>
#include <utility>

#include <nexenne/container/static_vector.hpp>
#include <nexenne/signal/connection.hpp>

namespace nexenne::signal {

/**
 * @brief Receiver-side aggregator of up to \p Capacity tracked
 *        subscriptions.
 *
 * See the file-level documentation for the full rationale. Holds its
 * connections in fixed inline storage (zero heap) and disconnects all
 * of them on destruction.
 *
 * @tparam Capacity  Maximum number of connections tracked inline.
 *                   \c track returns \c false once this many are held.
 */
template <std::size_t Capacity = 8>
class slot {
public:
  using size_type = std::size_t;  ///< Unsigned count type for sizes and capacity.

private:
  nexenne::container::static_vector<scoped_connection, Capacity> m_owned{};

public:
  /**
   * @brief Constructs an empty slot tracking no connections.
   *
   * @pre None.
   * @post \c empty() is \c true.
   */
  constexpr slot() noexcept = default;

  /// @brief Deleted copy constructor; a slot uniquely owns its connections (move-only).
  slot(slot const&) = delete;
  /// @brief Deleted copy assignment; a slot uniquely owns its connections (move-only).
  auto operator=(slot const&) -> slot& = delete;

  /**
   * @brief Move-constructs, transferring tracked connections.
   *
   * @pre None.
   * @post This slot owns the source's connections; the source is left
   *       empty.
   */
  slot(slot&&) noexcept = default;

  /**
   * @brief Move-assigns, transferring tracked connections.
   *
   * @return Reference to \c *this.
   *
   * @pre None.
   * @post This slot owns the source's connections; the source is left
   *       empty.
   */
  auto operator=(slot&&) noexcept -> slot& = default;

  /**
   * @brief Destroys the slot, disconnecting every tracked connection.
   *
   * @pre None.
   * @post All connections this slot tracked are disconnected.
   */
  ~slot() noexcept = default;

  /**
   * @brief Tracks \p c so it auto-disconnects with this slot.
   *
   * @param c  Connection to take ownership of.
   *
   * @return \c true when \p c was tracked, \c false when the slot is
   *         already full. On \c false the connection stays live and
   *         the caller can fall back to manual lifetime management.
   *
   * @pre None.
   * @post On \c true, \c size() has increased by one and \p c will be
   *       disconnected when this slot dies or is cleared. On \c false
   *       the slot is unchanged.
   */
  [[nodiscard]] auto track(connection c) noexcept -> bool {
    // Check for room before wrapping: a failed push_back returns before moving
    // from its argument, so an owning scoped_connection temporary would survive
    // and disconnect \p c on the full path, breaking the "stays live" contract.
    if (m_owned.size() == Capacity) {
      return false;
    }
    static_cast<void>(m_owned.push_back(scoped_connection{std::move(c)}));
    return true;
  }

  /**
   * @brief Disconnects every tracked connection immediately.
   *
   * Use this to release subscriptions before the slot is destroyed.
   * The slot stays usable and can track new connections afterward.
   *
   * @pre None.
   * @post \c empty() is \c true; every previously tracked connection
   *       is disconnected.
   */
  auto clear() noexcept -> void {
    m_owned.clear();
  }

  /**
   * @brief Number of currently tracked connections.
   *
   * @return The count of tracked connections, in \c [0, Capacity].
   *
   * @pre None.
   * @post The slot is unchanged.
   */
  [[nodiscard]] constexpr auto size() const noexcept -> size_type {
    return m_owned.size();
  }

  /**
   * @brief Reports whether the slot tracks no connections.
   *
   * @return \c true iff \c size() is 0.
   *
   * @pre None.
   * @post The slot is unchanged.
   */
  [[nodiscard]] constexpr auto empty() const noexcept -> bool {
    return m_owned.size() == 0;
  }

  /**
   * @brief Reports whether the slot has reached its capacity.
   *
   * @return \c true iff \c size() equals \c Capacity, in which case
   *         the next \c track returns \c false.
   *
   * @pre None.
   * @post The slot is unchanged.
   */
  [[nodiscard]] constexpr auto full() const noexcept -> bool {
    return m_owned.size() == Capacity;
  }

  /**
   * @brief The fixed maximum number of trackable connections.
   *
   * @return The \c Capacity template argument.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] static constexpr auto capacity() noexcept -> size_type {
    return Capacity;
  }
};

}  // namespace nexenne::signal
