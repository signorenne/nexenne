#pragma once

/**
 * @file
 * @brief Fixed-capacity, allocation-free signal/slot, the heap-free sibling of
 *        \c signal.
 *
 * \c static_signal is to \c signal what \c container::static_vector is to
 * \c container::small_vector: the same multicast callback channel, but with all
 * state held inline so it never touches the heap. The slot list is a
 * fixed-capacity \c container::static_vector<slot_entry, MaxSlots>, each slot is
 * an inline \c utility::in_place_function, and there is no shared core, so the
 * whole signal is a value with a bounded, predictable footprint.
 *
 * What it keeps from \c signal: priority-ordered \c emit, \c connect_once,
 * lifetime-tracked subscriptions through \c static_slot, a connect-only
 * \c static_sink, the RAII \c emit_blocker, and reentrancy safety (connect or
 * disconnect from inside a slot during \c emit).
 *
 * What it trades for being heap-free:
 *
 *   - Bounded slots. \c connect returns an invalid connection once \c MaxSlots
 *     slots are live, instead of growing. Size the capacity for the worst case.
 *   - No destroy-during-emit. A slot must not destroy the signal it is running
 *     on: the slot storage is the signal, so freeing it mid-iteration is
 *     undefined. (\c signal supports this via its heap core.)
 *   - Token connections. A \c static_connection is a small copyable handle that
 *     does NOT detect the signal's death: it must not outlive the signal.
 *     Use \c static_scoped_connection or \c static_slot for receiver-side
 *     teardown while the signal is alive, and keep the signal longer-lived than
 *     anything connected to it. (\c signal's \c connection stays safe after the
 *     signal dies via a \c weak_ptr; the heap-free handle cannot.)
 *
 * Reach for \c static_signal on a tight target where a heap allocation per
 * signal is unwanted and the slot count is known; reach for \c signal when you
 * need unbounded slots, a connection that survives the signal, or a slot that
 * tears down the signal.
 *
 * Thread safety: single-threaded, like \c signal.
 */

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

#include <nexenne/container/static_vector.hpp>
#include <nexenne/signal/emit_blocker.hpp>
#include <nexenne/utility/defer.hpp>
#include <nexenne/utility/in_place_function.hpp>

namespace nexenne::signal {

/**
 * @brief Token handle to a slot on a \c static_signal.
 *
 * A small copyable value: a type-erased pointer to the signal, a disconnect
 * dispatcher, and the slot id. Copies share one logical connection (the first
 * \c disconnect wins; later calls report \c false). Unlike \c connection it does
 * NOT track the signal's lifetime, so it must not be used after the signal is
 * destroyed.
 */
class static_connection {
public:
  using id_type = std::uint64_t;  ///< Slot identifier type.
  using disconnect_fn_type = auto (*)(void*, id_type) noexcept -> bool;

private:
  void* m_signal{nullptr};
  disconnect_fn_type m_disconnect_fn{nullptr};
  id_type m_id{0};

public:
  /**
   * @brief Constructs an invalid connection.
   *
   * @pre None.
   * @post \c disconnect() is a no-op returning \c false.
   */
  constexpr static_connection() noexcept = default;

  /**
   * @brief Constructs a handle to a specific slot. Minted by \c static_signal.
   *
   * @param signal Type-erased pointer to the owning signal.
   * @param fn Disconnect dispatcher for the signal type.
   * @param id Slot identifier.
   *
   * @pre \p signal outlives every copy of this handle.
   * @post \c id() equals \p id.
   */
  constexpr static_connection(
    void* const signal, disconnect_fn_type const fn, id_type const id
  ) noexcept
      : m_signal{signal}, m_disconnect_fn{fn}, m_id{id} {}

  /**
   * @brief Disconnects the slot.
   *
   * @return \c true on the first successful disconnect, \c false when already
   *         disconnected.
   *
   * @pre The owning signal is still alive.
   * @post The referenced slot will not fire again.
   */
  auto disconnect() noexcept -> bool {
    if (m_signal == nullptr || m_disconnect_fn == nullptr) {
      return false;
    }
    return m_disconnect_fn(m_signal, m_id);
  }

  /**
   * @brief Slot identifier within the owning signal.
   *
   * @return The slot id assigned at connect time, or \c 0 when invalid.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto id() const noexcept -> id_type {
    return m_id;
  }

  /**
   * @brief Reports whether this handle refers to a slot (was ever connected).
   *
   * @return \c true when minted from a \c connect, \c false when default
   *         constructed. Does NOT report whether the signal is still alive.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto has_target() const noexcept -> bool {
    return m_signal != nullptr;
  }
};

/**
 * @brief RAII wrapper that disconnects its \c static_connection on destruction.
 *
 * Move-only, the heap-free counterpart of \c scoped_connection. The signal must
 * outlive this object.
 */
class static_scoped_connection {
private:
  static_connection m_conn{};

public:
  /**
   * @brief Constructs an empty scope owning no connection.
   *
   * @pre None.
   * @post Destruction disconnects nothing.
   */
  constexpr static_scoped_connection() noexcept = default;

  /**
   * @brief Takes ownership of \p c, disconnecting it at scope end.
   *
   * @param c Connection to own.
   *
   * @pre The owning signal outlives this object.
   * @post This object disconnects \p c when destroyed or reassigned.
   */
  static_scoped_connection(static_connection c) noexcept : m_conn{c} {}

  static_scoped_connection(static_scoped_connection const&) = delete;
  auto operator=(static_scoped_connection const&) -> static_scoped_connection& = delete;

  /**
   * @brief Move-constructs, transferring ownership from \p other.
   *
   * @param other Source scope, left empty.
   *
   * @pre None.
   * @post This object owns \p other's former connection.
   */
  static_scoped_connection(static_scoped_connection&& other) noexcept
      : m_conn{std::exchange(other.m_conn, static_connection{})} {}

  /**
   * @brief Move-assigns: disconnects the current connection, then adopts
   *        \p other's.
   *
   * @param other Source scope, left empty.
   *
   * @return Reference to \c *this.
   *
   * @pre None. Self-assignment is a no-op.
   * @post Any previously owned connection is disconnected.
   */
  auto operator=(static_scoped_connection&& other) noexcept -> static_scoped_connection& {
    if (this != &other) {
      m_conn.disconnect();
      m_conn = std::exchange(other.m_conn, static_connection{});
    }
    return *this;
  }

  /**
   * @brief Destroys the scope, disconnecting the owned connection.
   *
   * @pre The owning signal is still alive.
   * @post The owned slot (if any) is disconnected.
   */
  ~static_scoped_connection() noexcept {
    m_conn.disconnect();
  }

  /**
   * @brief Releases ownership of the connection.
   *
   * @return The previously owned connection, which no longer auto-disconnects.
   *
   * @pre None.
   * @post This object owns nothing.
   */
  [[nodiscard]] auto release() noexcept -> static_connection {
    return std::exchange(m_conn, static_connection{});
  }

  /**
   * @brief Disconnects the owned connection now.
   *
   * @return \c true on the first successful disconnect, \c false otherwise.
   *
   * @pre The owning signal is still alive.
   * @post The owned slot will not fire again.
   */
  auto disconnect() noexcept -> bool {
    return m_conn.disconnect();
  }
};

/**
 * @brief Receiver-side aggregator of up to \p Capacity tracked subscriptions,
 *        the heap-free counterpart of \c slot.
 *
 * Holds its connections in fixed inline storage and disconnects all of them on
 * destruction. The tracked signal must outlive this object.
 *
 * @tparam Capacity Maximum number of connections tracked inline.
 */
template <std::size_t Capacity = 8>
class static_slot {
public:
  using size_type = std::size_t;  ///< Count type.

private:
  container::static_vector<static_scoped_connection, Capacity> m_owned{};

public:
  /// @brief Constructs an empty tracker.
  constexpr static_slot() noexcept = default;

  static_slot(static_slot const&) = delete;
  auto operator=(static_slot const&) -> static_slot& = delete;
  static_slot(static_slot&&) noexcept = default;
  auto operator=(static_slot&&) noexcept -> static_slot& = default;
  ~static_slot() noexcept = default;

  /**
   * @brief Tracks \p c so it auto-disconnects with this tracker.
   *
   * @param c Connection to own.
   *
   * @return \c true when tracked, \c false when full (in which case \p c stays
   *         live and untracked).
   *
   * @pre None.
   * @post On \c true, \c size() has increased by one.
   */
  [[nodiscard]] auto track(static_connection c) noexcept -> bool {
    if (m_owned.size() == Capacity) {
      return false;
    }
    static_cast<void>(m_owned.push_back(static_scoped_connection{c}));
    return true;
  }

  /**
   * @brief Disconnects every tracked connection, keeping the tracker usable.
   *
   * @pre None.
   * @post \c empty() is \c true and every previously tracked connection is
   *       disconnected.
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
   * @post None.
   */
  [[nodiscard]] constexpr auto size() const noexcept -> size_type {
    return m_owned.size();
  }

  /**
   * @brief Reports whether no connections are tracked.
   *
   * @return \c true when \c size() is zero.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto empty() const noexcept -> bool {
    return m_owned.size() == 0;
  }

  /**
   * @brief Reports whether the tracker is at capacity.
   *
   * @return \c true when \c size() equals \c Capacity, so the next \c track
   *         returns \c false.
   *
   * @pre None.
   * @post None.
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

template <typename Signature, std::size_t MaxSlots = 8, std::size_t SlotCapacity = 32>
class static_signal;

template <typename Signature, std::size_t MaxSlots = 8, std::size_t SlotCapacity = 32>
class static_sink;

/**
 * @brief Fixed-capacity, allocation-free multicast callback channel.
 *
 * See the file-level documentation for the design and the trade-offs against
 * \c signal. Slots fire in priority order (lower first, then insertion order);
 * \c emit performs no allocation.
 *
 * @tparam R Slot return type (\c emit discards returns).
 * @tparam Args Slot argument types.
 * @tparam MaxSlots Inline slot capacity; \c connect fails past this.
 * @tparam SlotCapacity Inline byte capacity for each type-erased slot.
 */
template <typename R, typename... Args, std::size_t MaxSlots, std::size_t SlotCapacity>
class static_signal<R(Args...), MaxSlots, SlotCapacity> {
public:
  using id_type = static_connection::id_type;

private:
  using slot_fn_type = nexenne::utility::in_place_function<R(Args...), SlotCapacity>;

  struct slot_entry {
    id_type id{};
    int priority{0};
    bool alive{true};
    bool once{false};
    slot_fn_type fn_obj{};

    // By const reference: a by-value slot parameter is copied once, at the
    // slot's own call boundary, not again here.
    auto invoke(Args const&... args) -> R {
      return fn_obj(args...);
    }
  };

  container::static_vector<slot_entry, MaxSlots> m_slots{};
  id_type m_next_id{1};
  int m_emit_depth{0};
  bool m_blocked{false};
  bool m_dirty{false};  ///< slots appended during an emit need a re-sort

public:
  /// @brief Constructs an empty signal.
  constexpr static_signal() noexcept = default;

  static_signal(static_signal const&) = delete;
  static_signal(static_signal&&) = delete;
  auto operator=(static_signal const&) -> static_signal& = delete;
  auto operator=(static_signal&&) -> static_signal& = delete;
  ~static_signal() noexcept = default;

  /**
   * @brief Connects \p fn at the given \p priority.
   *
   * Lower priorities fire first; equal priorities preserve insertion order.
   *
   * @tparam Fn Callable invocable as \c Fn(Args...).
   * @param fn Callable to connect.
   * @param priority Ordering key; lower fires first. Default 0.
   *
   * @return A connection handle, or an invalid handle when the signal is full
   *         (\c MaxSlots slots) or the callable exceeds \c SlotCapacity.
   *
   * @pre \p fn fits in \c SlotCapacity inline storage.
   * @post On success \c size() has increased by one. A connect during an
   *       in-progress emit is not visited by that emit.
   *
   * @complexity \c O(MaxSlots) to insert into the priority-sorted list.
   */
  template <typename Fn>
    requires std::invocable<Fn&, Args...>
  [[nodiscard]] auto connect(Fn&& fn, int const priority = 0) -> static_connection {
    return connect_impl(std::forward<Fn>(fn), priority, /*once=*/false);
  }

  /**
   * @brief Connects \p fn for exactly one invocation.
   *
   * @tparam Fn Callable invocable as \c Fn(Args...).
   * @param fn Callable to connect.
   * @param priority Ordering key; lower fires first. Default 0.
   *
   * @return A connection handle, or an invalid handle when full.
   *
   * @pre Same storage constraint as \c connect.
   * @post The slot is removed after the first emit reaches it.
   *
   * @complexity \c O(MaxSlots).
   */
  template <typename Fn>
    requires std::invocable<Fn&, Args...>
  [[nodiscard]] auto connect_once(Fn&& fn, int const priority = 0) -> static_connection {
    return connect_impl(std::forward<Fn>(fn), priority, /*once=*/true);
  }

  /**
   * @brief Connects \p fn and tracks it in \p owner for auto-disconnect.
   *
   * @tparam Fn Callable invocable as \c Fn(Args...).
   * @tparam Capacity Inline tracking capacity of \p owner.
   * @param fn Callable to connect.
   * @param owner Receiver-side tracker.
   * @param priority Ordering key; lower fires first. Default 0.
   *
   * @return The connection (also tracked in \p owner when it had room).
   *
   * @pre Same storage constraint as \c connect.
   * @post The connection is owned by \p owner when it had room.
   *
   * @complexity \c O(MaxSlots).
   */
  template <typename Fn, std::size_t Capacity>
    requires std::invocable<Fn&, Args...>
  auto connect(Fn&& fn, static_slot<Capacity>& owner, int const priority = 0) -> static_connection {
    auto c{connect(std::forward<Fn>(fn), priority)};
    static_cast<void>(owner.track(c));
    return c;
  }

  /**
   * @brief Member-function shortcut: connects \c obj.*MemberFn.
   *
   * @tparam MemberFn Pointer-to-member-function to invoke.
   * @tparam T Owning object type.
   * @param obj Object the member function is called on.
   * @param priority Ordering key; lower fires first. Default 0.
   *
   * @return A connection handle, or an invalid handle when full.
   *
   * @pre \p obj outlives the connection.
   * @post On success \c size() has increased by one.
   *
   * @complexity \c O(MaxSlots).
   *
   * @warning No lifetime tie: prefer the \c static_slot overload.
   */
  template <auto MemberFn, typename T>
  [[nodiscard]] auto connect(T& obj, int const priority = 0) -> static_connection {
    return connect(bind_member<MemberFn>(obj), priority);
  }

  /**
   * @brief Member-function shortcut tracked in \p owner.
   *
   * @tparam MemberFn Pointer-to-member-function to invoke.
   * @tparam T Owning object type.
   * @tparam Capacity Inline tracking capacity of \p owner.
   * @param obj Object the member function is called on.
   * @param owner Receiver-side tracker.
   * @param priority Ordering key; lower fires first. Default 0.
   *
   * @return The connection (also tracked in \p owner when it had room).
   *
   * @pre \p obj outlives the connection.
   * @post The connection is owned by \p owner when it had room.
   *
   * @complexity \c O(MaxSlots).
   */
  template <auto MemberFn, typename T, std::size_t Capacity>
  auto connect(T& obj, static_slot<Capacity>& owner, int const priority = 0) -> static_connection {
    return connect(bind_member<MemberFn>(obj), owner, priority);
  }

  /**
   * @brief Disconnects every slot.
   *
   * @pre None.
   * @post No slot fires on a subsequent emit; after any in-progress emit,
   *       \c empty() is \c true.
   *
   * @complexity \c O(MaxSlots).
   */
  auto disconnect_all() noexcept -> void {
    if (m_emit_depth > 0) {
      for (auto& s : m_slots) {
        s.alive = false;
      }
      return;
    }
    m_slots.clear();
  }

  /**
   * @brief Suppresses emission until \c unblock.
   *
   * @pre None.
   * @post \c is_blocked() is \c true; slots stay connected.
   */
  auto block() noexcept -> void {
    m_blocked = true;
  }

  /**
   * @brief Re-enables emission after \c block.
   *
   * @pre None.
   * @post \c is_blocked() is \c false.
   */
  auto unblock() noexcept -> void {
    m_blocked = false;
  }

  /**
   * @brief Reports whether emission is currently suppressed.
   *
   * @return \c true when a \c block is in effect.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto is_blocked() const noexcept -> bool {
    return m_blocked;
  }

  /**
   * @brief Invokes every alive slot in priority order with \p args.
   *
   * Allocates nothing. One-shot slots are marked dead as they fire and swept
   * once the outermost emit finishes. Reentrant connect and disconnect from
   * within a slot are supported; a slot must NOT destroy the signal.
   *
   * @param args Arguments forwarded to each slot.
   *
   * @pre The signal is not destroyed by any slot during the emit.
   * @post Every slot alive at the start was invoked once.
   *
   * @complexity \c O(MaxSlots), plus an in-place re-sort if a slot connected.
   */
  auto emit(Args... args) noexcept -> void {
    if (m_blocked) {
      return;
    }
    ++m_emit_depth;
    auto const at_exit{nexenne::utility::defer{[this] {
      if (--m_emit_depth == 0) {
        if (m_dirty) {
          sort_by_priority();
          m_dirty = false;
        }
        sweep_dead();
      }
    }}};
    auto const n{m_slots.size()};
    for (auto i{std::size_t{0}}; i < n; ++i) {
      auto& slot{m_slots[i]};
      if (slot.alive) {
        if (slot.once) {
          slot.alive = false;
        }
        static_cast<void>(slot.invoke(args...));
      }
    }
  }

  /**
   * @brief Call-operator alias for \c emit.
   *
   * @param args Arguments forwarded to \c emit.
   *
   * @pre Same as \c emit.
   * @post Same as \c emit.
   *
   * @complexity \c O(MaxSlots).
   */
  auto operator()(Args... args) noexcept -> void {
    emit(args...);
  }

  /**
   * @brief Returns a connect-only \c static_sink view of this signal.
   *
   * @return A sink bound to this signal.
   *
   * @pre This signal outlives the returned sink.
   * @post None.
   */
  [[nodiscard]] auto as_sink() noexcept -> static_sink<R(Args...), MaxSlots, SlotCapacity>;

  /**
   * @brief Number of currently alive slots.
   *
   * @return The count of connected slots not yet swept.
   *
   * @pre None.
   * @post None.
   *
   * @complexity \c O(MaxSlots).
   */
  [[nodiscard]] auto size() const noexcept -> std::size_t {
    auto count{std::size_t{0}};
    for (auto const& s : m_slots) {
      if (s.alive) {
        ++count;
      }
    }
    return count;
  }

  /**
   * @brief Reports whether the signal has no alive slots.
   *
   * @return \c true when \c size() is zero.
   *
   * @pre None.
   * @post None.
   *
   * @complexity \c O(MaxSlots).
   */
  [[nodiscard]] auto empty() const noexcept -> bool {
    return size() == 0;
  }

  /**
   * @brief Reports whether the slot list is at capacity.
   *
   * @return \c true when \c MaxSlots slot entries are held, so the next
   *         \c connect fails.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto full() const noexcept -> bool {
    return m_slots.size() == MaxSlots;
  }

  /**
   * @brief The fixed maximum number of slots.
   *
   * @return The \c MaxSlots template argument.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] static constexpr auto capacity() noexcept -> std::size_t {
    return MaxSlots;
  }

private:
  template <auto MemberFn, typename T>
  static auto bind_member(T& obj) noexcept {
    return [&obj](Args... args) noexcept(noexcept((obj.*MemberFn)(args...))) -> R {
      return (obj.*MemberFn)(args...);
    };
  }

  template <typename Fn>
  auto connect_impl(Fn&& fn, int const priority, bool const once) -> static_connection {
    if (m_slots.size() == MaxSlots) {
      return static_connection{};  // full: invalid handle, callable not stored
    }
    auto const id{m_next_id++};
    auto entry{slot_entry{.id = id, .priority = priority, .alive = true, .once = once}};
    entry.fn_obj = slot_fn_type{std::forward<Fn>(fn)};
    static_cast<void>(m_slots.push_back(std::move(entry)));
    if (m_emit_depth > 0) {
      // Append only: the live prefix must not move while an emit iterates it.
      // The new slot sits past the captured length, so this emit skips it, and
      // the outermost emit re-sorts before the next one.
      m_dirty = true;
    } else {
      bubble_last_into_position();
    }
    return static_connection{this, &disconnect_thunk, id};
  }

  static auto disconnect_thunk(void* const self, id_type const id) noexcept -> bool {
    return static_cast<static_signal*>(self)->disconnect_by_id(id);
  }

  auto disconnect_by_id(id_type const id) noexcept -> bool {
    for (auto i{std::size_t{0}}; i < m_slots.size(); ++i) {
      if (m_slots[i].id == id && m_slots[i].alive) {
        if (m_emit_depth > 0) {
          m_slots[i].alive = false;  // deferred removal
        } else {
          erase_at(i);
        }
        return true;
      }
    }
    return false;
  }

  /// @brief Bubbles the just-appended slot left into priority position. Stable:
  ///        only past strictly lower-priority neighbours.
  auto bubble_last_into_position() noexcept -> void {
    for (auto i{m_slots.size()}; i > 1; --i) {
      if (m_slots[i - 1].priority < m_slots[i - 2].priority) {
        using std::swap;
        swap(m_slots[i - 1], m_slots[i - 2]);
      } else {
        break;
      }
    }
  }

  /// @brief In-place stable insertion sort by priority (no allocation), for
  ///        slots appended during an emit. O(MaxSlots^2), MaxSlots is small.
  auto sort_by_priority() noexcept -> void {
    for (auto i{std::size_t{1}}; i < m_slots.size(); ++i) {
      for (auto j{i}; j > 0 && m_slots[j].priority < m_slots[j - 1].priority; --j) {
        using std::swap;
        swap(m_slots[j], m_slots[j - 1]);
      }
    }
  }

  auto erase_at(std::size_t const pos) noexcept -> void {
    for (auto i{pos}; i + 1 < m_slots.size(); ++i) {
      m_slots[i] = std::move(m_slots[i + 1]);
    }
    static_cast<void>(m_slots.pop_back());
  }

  auto sweep_dead() noexcept -> void {
    auto write{std::size_t{0}};
    for (auto read{std::size_t{0}}; read < m_slots.size(); ++read) {
      if (m_slots[read].alive) {
        if (write != read) {
          m_slots[write] = std::move(m_slots[read]);
        }
        ++write;
      }
    }
    while (m_slots.size() > write) {
      static_cast<void>(m_slots.pop_back());
    }
  }
};

/**
 * @brief Connect-only view of a \c static_signal.
 *
 * Mirrors every \c connect overload but hides \c emit, \c block, and
 * \c disconnect_all. Holds a raw pointer to the signal, which must outlive it.
 *
 * @tparam R Slot return type.
 * @tparam Args Slot argument types.
 * @tparam MaxSlots Inline slot capacity of the underlying signal.
 * @tparam SlotCapacity Inline byte capacity of the underlying signal's slots.
 */
template <typename R, typename... Args, std::size_t MaxSlots, std::size_t SlotCapacity>
class static_sink<R(Args...), MaxSlots, SlotCapacity> {
private:
  using signal_type = static_signal<R(Args...), MaxSlots, SlotCapacity>;
  signal_type* m_signal{nullptr};

public:
  /**
   * @brief Binds the sink to signal \p s.
   *
   * @param s Signal to expose. Must outlive the sink.
   *
   * @pre \p s outlives this sink.
   * @post Connect calls forward to \p s.
   */
  explicit constexpr static_sink(signal_type& s) noexcept : m_signal{&s} {}

  /**
   * @brief Forwards to \c static_signal::connect.
   *
   * @tparam Fn Callable invocable as \c Fn(Args...).
   * @param fn Callable to connect.
   * @param priority Ordering key; lower fires first. Default 0.
   *
   * @return The resulting connection (invalid when the signal is full).
   *
   * @pre As for \c static_signal::connect.
   * @post As for \c static_signal::connect.
   */
  template <typename Fn>
    requires std::invocable<Fn&, Args...>
  [[nodiscard]] auto connect(Fn&& fn, int const priority = 0) -> static_connection {
    return m_signal->connect(std::forward<Fn>(fn), priority);
  }

  /**
   * @brief Forwards to \c static_signal::connect_once.
   *
   * @tparam Fn Callable invocable as \c Fn(Args...).
   * @param fn Callable to connect for one invocation.
   * @param priority Ordering key; lower fires first. Default 0.
   *
   * @return The resulting connection (invalid when the signal is full).
   *
   * @pre As for \c static_signal::connect_once.
   * @post As for \c static_signal::connect_once.
   */
  template <typename Fn>
    requires std::invocable<Fn&, Args...>
  [[nodiscard]] auto connect_once(Fn&& fn, int const priority = 0) -> static_connection {
    return m_signal->connect_once(std::forward<Fn>(fn), priority);
  }

  /**
   * @brief Forwards to the tracked \c static_signal::connect.
   *
   * @tparam Fn Callable invocable as \c Fn(Args...).
   * @tparam Capacity Inline tracking capacity of \p owner.
   * @param fn Callable to connect.
   * @param owner Receiver-side tracker.
   * @param priority Ordering key; lower fires first. Default 0.
   *
   * @return The resulting connection.
   *
   * @pre As for the tracked \c static_signal::connect.
   * @post As for the tracked \c static_signal::connect.
   */
  template <typename Fn, std::size_t Capacity>
    requires std::invocable<Fn&, Args...>
  auto connect(Fn&& fn, static_slot<Capacity>& owner, int const priority = 0) -> static_connection {
    return m_signal->connect(std::forward<Fn>(fn), owner, priority);
  }

  /**
   * @brief Forwards to the member-function \c static_signal::connect.
   *
   * @tparam MemberFn Pointer-to-member-function to invoke.
   * @tparam T Owning object type.
   * @param obj Object the member function is called on.
   * @param priority Ordering key; lower fires first. Default 0.
   *
   * @return The resulting connection.
   *
   * @pre \p obj outlives the connection.
   * @post As for the member-function \c static_signal::connect.
   */
  template <auto MemberFn, typename T>
  [[nodiscard]] auto connect(T& obj, int const priority = 0) -> static_connection {
    return m_signal->template connect<MemberFn>(obj, priority);
  }

  /**
   * @brief Forwards to the tracked member-function \c static_signal::connect.
   *
   * @tparam MemberFn Pointer-to-member-function to invoke.
   * @tparam T Owning object type.
   * @tparam Capacity Inline tracking capacity of \p owner.
   * @param obj Object the member function is called on.
   * @param owner Receiver-side tracker.
   * @param priority Ordering key; lower fires first. Default 0.
   *
   * @return The resulting connection.
   *
   * @pre \p obj outlives the connection.
   * @post As for the tracked member-function \c static_signal::connect.
   */
  template <auto MemberFn, typename T, std::size_t Capacity>
  auto connect(T& obj, static_slot<Capacity>& owner, int const priority = 0) -> static_connection {
    return m_signal->template connect<MemberFn>(obj, owner, priority);
  }

  /**
   * @brief Number of alive slots on the underlying signal.
   *
   * @return \c static_signal::size of the bound signal.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto size() const noexcept -> std::size_t {
    return m_signal->size();
  }

  /**
   * @brief Reports whether the underlying signal has no alive slots.
   *
   * @return \c static_signal::empty of the bound signal.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto empty() const noexcept -> bool {
    return m_signal->empty();
  }
};

template <typename R, typename... Args, std::size_t MaxSlots, std::size_t SlotCapacity>
[[nodiscard]] auto static_signal<R(Args...), MaxSlots, SlotCapacity>::as_sink() noexcept
  -> static_sink<R(Args...), MaxSlots, SlotCapacity> {
  return static_sink<R(Args...), MaxSlots, SlotCapacity>{*this};
}

}  // namespace nexenne::signal
