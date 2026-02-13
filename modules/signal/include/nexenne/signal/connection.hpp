#pragma once

/**
 * @file
 * @brief Connection handles for managing connected slots.
 *
 * \c connection is a value-type handle returned by
 * \c signal::connect(). It can be copied freely, lives independently
 * of the signal, and disconnects the corresponding slot when its
 * \c disconnect() method is called.
 *
 * \c scoped_connection is an RAII wrapper that owns a connection and
 * disconnects it on destruction (the \c std::unique_ptr of slots).
 *
 * Both types are type-erased: they refer to the signal through a
 * \c std::weak_ptr to the signal's shared core, so they remain safe
 * to use even after the signal is destroyed (in which case
 * \c valid() reports false and \c disconnect() no-ops).
 */

#include <cstdint>
#include <memory>
#include <utility>

namespace nexenne::signal {

namespace detail {

using slot_id_type = std::uint64_t;

/**
 * @brief Type-erased disconnect dispatch.
 *
 * Stored in the connection alongside a type-erased weak reference to
 * the signal's core. The connection calls \c disconnect_fn(core, id) to
 * remove the slot, no virtual dispatch, just a function pointer.
 */
using disconnect_fn_type = auto (*)(void*, slot_id_type) noexcept -> bool;

}  // namespace detail

/**
 * @brief Value-type handle to a connected slot.
 *
 * Default-constructed handles are invalid.
 *
 * Copies share the same logical connection: any copy can call
 * \c disconnect() (the first wins; subsequent calls report \c false).
 */
class connection {
private:
  std::weak_ptr<void> m_core{};
  detail::disconnect_fn_type m_disconnect_fn{nullptr};
  detail::slot_id_type m_slot_id{0};

public:
  /**
   * @brief Constructs an invalid connection.
   *
   * @pre None.
   * @post \c valid() is \c false and \c disconnect() is a no-op
   *       returning \c false.
   */
  constexpr connection() noexcept = default;

  /**
   * @brief Constructs a handle to a specific slot on a signal's core.
   *
   * Called by \c signal when minting a connection; user code rarely
   * constructs one directly.
   *
   * @param core Weak reference to the signal's shared core.
   * @param fn Type-erased disconnect dispatch function.
   * @param slot_id Identifier of the slot within the signal.
   *
   * @pre None.
   * @post \c slot_id() equals \p slot_id; \c valid() reflects whether
   *       \p core is still alive.
   */
  connection(
    std::weak_ptr<void> core,
    detail::disconnect_fn_type const fn,
    detail::slot_id_type const slot_id
  ) noexcept
      : m_core{std::move(core)}, m_disconnect_fn{fn}, m_slot_id{slot_id} {}

  /**
   * @brief Disconnects the slot.
   *
   * Locks the weak reference to the signal's core; if the signal is
   * gone or the slot was already removed, this is a no-op.
   *
   * @return \c true on first successful disconnect, \c false when the
   *         signal is gone or the slot was already disconnected.
   *
   * @pre None.
   * @post The referenced slot will not fire again. A second call (on
   *       this or a copy) returns \c false.
   */
  auto disconnect() noexcept -> bool {
    auto const pin{m_core.lock()};
    if (!pin || m_disconnect_fn == nullptr) {
      return false;
    }
    return m_disconnect_fn(pin.get(), m_slot_id);
  }

  /**
   * @brief Reports whether the owning signal is still alive.
   *
   * @return \c true iff the signal's core has not been destroyed.
   *         Does not check whether the slot itself is still connected.
   *
   * @pre None.
   * @post The connection is unchanged.
   */
  [[nodiscard]] auto valid() const noexcept -> bool {
    return !m_core.expired();
  }

  /**
   * @brief Slot identifier within the owning signal.
   *
   * @return The slot id assigned at connect time. Useful for
   *         debugging; not stable across different signals.
   *
   * @pre None.
   * @post The connection is unchanged.
   */
  [[nodiscard]] constexpr auto slot_id() const noexcept -> detail::slot_id_type {
    return m_slot_id;
  }

  /**
   * @brief Equality: same slot on the same signal (alive or destroyed).
   *
   * Uses \c owner_before on the weak_ptrs so the check works even when
   * both signals have been destroyed (their cores' control blocks
   * still compare distinctly).
   *
   * @param a Left operand.
   * @param b Right operand.
   *
   * @return \c true iff both connections refer to the same slot id on
   *         the same signal core.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] friend auto operator==(connection const& a, connection const& b) noexcept -> bool {
    if (a.m_slot_id != b.m_slot_id) {
      return false;
    }
    return !a.m_core.owner_before(b.m_core) && !b.m_core.owner_before(a.m_core);
  }
};

/**
 * @brief RAII wrapper that disconnects its connection on destruction.
 *
 * Move-only (matches \c std::unique_ptr semantics).
 */
class scoped_connection {
private:
  connection m_conn{};

public:
  /**
   * @brief Constructs an empty scope owning no connection.
   *
   * @pre None.
   * @post \c valid() is \c false; destruction disconnects nothing.
   */
  constexpr scoped_connection() noexcept = default;

  /**
   * @brief Takes ownership of \p c, disconnecting it at scope end.
   *
   * @param c Connection to own. Implicitly convertible for ergonomic
   *          assignment from a \c connect result.
   *
   * @pre None.
   * @post This object now owns \p c; \p c is disconnected when this
   *       object is destroyed or reassigned.
   */
  scoped_connection(connection c) noexcept : m_conn{std::move(c)} {}

  /// @brief Deleted copy constructor; ownership is unique (move-only).
  scoped_connection(scoped_connection const&) = delete;
  /// @brief Deleted copy assignment; ownership is unique (move-only).
  auto operator=(scoped_connection const&) -> scoped_connection& = delete;

  /**
   * @brief Move-constructs, transferring ownership from \p other.
   *
   * @param other Source scope; left empty.
   *
   * @pre None.
   * @post This object owns \p other's former connection; \p other
   *       owns nothing.
   */
  scoped_connection(scoped_connection&& other) noexcept
      : m_conn{std::exchange(other.m_conn, connection{})} {}

  /**
   * @brief Move-assigns: disconnects the current connection, then
   *        adopts \p other's.
   *
   * @param other Source scope; left empty.
   *
   * @return Reference to \c *this.
   *
   * @pre None. Self-assignment is a no-op.
   * @post Any previously owned connection is disconnected; this object
   *       owns \p other's former connection and \p other owns nothing.
   */
  auto operator=(scoped_connection&& other) noexcept -> scoped_connection& {
    if (this != &other) {
      m_conn.disconnect();
      m_conn = std::exchange(other.m_conn, connection{});
    }
    return *this;
  }

  /**
   * @brief Destroys the scope, disconnecting the owned connection.
   *
   * @pre None.
   * @post The owned slot (if any) is disconnected.
   */
  ~scoped_connection() noexcept {
    m_conn.disconnect();
  }

  /**
   * @brief Releases ownership of the connection.
   *
   * @return The previously owned \c connection, which no longer
   *         auto-disconnects.
   *
   * @pre None.
   * @post This object owns nothing; the caller is responsible for the
   *       returned connection's lifetime.
   */
  [[nodiscard]] auto release() noexcept -> connection {
    return std::exchange(m_conn, connection{});
  }

  /**
   * @brief Disconnects the owned connection now, before scope end.
   *
   * @return \c true on first successful disconnect, \c false when
   *         already disconnected or the signal is gone.
   *
   * @pre None.
   * @post The owned slot will not fire again.
   */
  auto disconnect() noexcept -> bool {
    return m_conn.disconnect();
  }

  /**
   * @brief Read-only access to the owned connection.
   *
   * @return A const reference to the wrapped \c connection.
   *
   * @pre None.
   * @post This object is unchanged.
   */
  [[nodiscard]] auto get() const noexcept -> connection const& {
    return m_conn;
  }

  /**
   * @brief Reports whether the owned connection's signal is alive.
   *
   * @return \c connection::valid of the owned connection.
   *
   * @pre None.
   * @post This object is unchanged.
   */
  [[nodiscard]] auto valid() const noexcept -> bool {
    return m_conn.valid();
  }
};

}  // namespace nexenne::signal
