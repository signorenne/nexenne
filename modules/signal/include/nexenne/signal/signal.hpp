#pragma once

/**
 * @file
 * @brief Header-only signal/slot library with no virtual dispatch,
 *        no heap on the slot path, embedded-friendly by construction.
 *
 * \c signal (the \c R(Args...) specialization) is a multicast callback
 * channel: any number of slots can be connected to it, and \c emit
 * invokes every alive slot in priority order (then connection order as
 * tie-break).
 *
 * Design choices:
 *
 *   - No virtual functions. Type erasure for stored slots uses
 *     \c nexenne::utility::in_place_function: fixed inline storage
 *     of \c SlotCapacity bytes (default 64); a callable that exceeds
 *     it is rejected at compile time. No heap, no vtable, no
 *     inheritance.
 *
 *   - Free-function fast path. When the callable is convertible
 *     to a raw function pointer (free function or captureless
 *     lambda), it's stored as such, so \c emit becomes one indirect
 *     call with no type-erasure thunk.
 *
 *   - Inline slot storage. \c small_vector (with inline capacity 4)
 *     keeps up to four slots inline, so typical signals (1 to 3 slots)
 *     pay zero heap allocations beyond the one for the signal's shared
 *     core.
 *
 *   - Reliable destroy-during-emit. The signal's mutable state
 *     lives in a heap-allocated \c core managed by \c shared_ptr.
 *     During \c emit the implementation pins a copy of that
 *     \c shared_ptr so the core survives even if a slot destroys
 *     the signal: the iteration completes safely on the pinned
 *     state, and outstanding connections see a null owner.
 *
 *   - Reentrancy-safe emit. Slots can disconnect themselves or
 *     other slots during \c emit; deletion is deferred to the end
 *     of the outermost emit. New \c connect calls during \c emit
 *     append to the slot list; the in-progress emit does NOT visit
 *     them.
 *
 *   - Priority ordering. \c connect(fn, priority): lower
 *     priorities fire first (priority 0 is default). Ties resolved
 *     by insertion order. Implemented as an \c O(n) insertion-bubble
 *     on connect, so the slot list stays sorted, no per-connect
 *     full sort.
 *
 *   - One-shot slots. \c connect_once(fn) marks the slot for
 *     removal after the first \c emit reaches it. Zero extra heap
 *     compared to \c connect (no wrapper, no holder).
 *
 *   - Read-only \c as_sink() interface. \c signal::as_sink()
 *     returns a \c sink that exposes every \c connect overload but
 *     hides \c emit. Used to publish a signal from a class without
 *     letting clients fire it.
 *
 *   - Lifetime-tied subscriptions. \c connect(fn, owner) registers
 *     the new connection with a \c slot; when the owner dies
 *     every tracked connection auto-disconnects (Qt's \c QObject
 *     pattern). \c slot has fixed inline capacity (zero heap).
 *
 *   - Blockable emission. \c block() / \c unblock() suppress
 *     \c emit without disconnecting slots. The RAII \c emit_blocker
 *     scopes a block to a code region with save-and-restore
 *     semantics, so guards nest correctly (matches \c QSignalBlocker).
 *
 *   - Member-function shortcut. \c connect with a \c &T::method
 *     template argument wraps the call without an explicit lambda.
 *
 *   - Non-void return collection. For signals with a non-void
 *     return type, \c emit_and_collect gathers each slot's return
 *     into a \c std::vector of \c R.
 *
 *   - Signals are pinned: non-copyable, non-movable. The shared
 *     core holds a back-pointer to the signal so its address must
 *     stay stable. If you need to relocate a signal, hold via
 *     \c std::unique_ptr.
 *
 * Argument forwarding: \c emit takes \c Args by value (matching the slot
 * signature), one copy from the caller, then passes them by const reference to
 * each slot, so a by-value slot parameter is copied once per slot (the copy at
 * the slot's own call boundary), not twice. A reference parameter is not copied
 * at all, so declare the signal with reference parameters (for example a
 * \c big \c const&) for arguments that are expensive to copy.
 *
 * Thread safety: single-threaded (standard library convention).
 * For thread-safe signals, wrap with external synchronisation.
 */

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include <nexenne/container/small_vector.hpp>
#include <nexenne/signal/connection.hpp>
#include <nexenne/signal/emit_blocker.hpp>
#include <nexenne/signal/slot.hpp>
#include <nexenne/utility/defer.hpp>
#include <nexenne/utility/in_place_function.hpp>

namespace nexenne::signal {

/**
 * @brief Primary template for the multicast signal; see the
 *        \c R(Args...) specialization.
 *
 * @tparam Signature Slot call signature \c R(Args...).
 * @tparam SlotCapacity Inline byte capacity for type-erased slots.
 */
template <typename Signature, std::size_t SlotCapacity = 64>
class signal;

/**
 * @brief Primary template for the connect-only sink; see the
 *        \c R(Args...) specialization.
 *
 * @tparam Signature Slot call signature \c R(Args...).
 * @tparam SlotCapacity Inline byte capacity for type-erased slots.
 */
template <typename Signature, std::size_t SlotCapacity = 64>
class sink;

/**
 * @brief Multicast callback channel: connect any number of slots and
 *        invoke them in priority order on \c emit.
 *
 * No virtual dispatch and no heap on the slot path; the mutable state
 * lives in a lazily-allocated shared core so the signal survives a slot
 * destroying it mid-emit. See the file-level documentation for the full
 * design rationale.
 *
 * @tparam R Slot return type. \c emit_and_collect requires a non-void
 *         \c R.
 * @tparam Args Slot argument types.
 * @tparam SlotCapacity Inline byte capacity for each type-erased slot;
 *         a connected callable that exceeds it is rejected at compile
 *         time.
 */
template <typename R, typename... Args, std::size_t SlotCapacity>
class signal<R(Args...), SlotCapacity> {
  // A multicast signal fans one argument out to every connected slot, so an
  // rvalue-reference parameter is meaningless (a value cannot be moved into more
  // than one slot). Reject it here with a clear message rather than let it fail
  // deep inside the by-const-reference forwarder only when emit is instantiated.
  static_assert(
    (... && !std::is_rvalue_reference_v<Args>), "signal parameters cannot be rvalue references"
  );

private:
  using slot_fn_type = nexenne::utility::in_place_function<R(Args...), SlotCapacity>;
  using raw_fn_ptr_type = R (*)(Args...);

  struct slot_entry {
    detail::slot_id_type id{};
    int priority{0};
    raw_fn_ptr_type fn_ptr{nullptr};
    slot_fn_type fn_obj{};
    bool alive{true};
    bool once{false};

    // Takes the arguments by const reference so a by-value signal parameter is
    // copied only once, at the slot's own call boundary, not again into this
    // forwarder. For a reference-typed parameter the const collapses away
    // (\c int& stays \c int&), so a slot taking a mutable reference still can.
    auto invoke(Args const&... args) -> R {
      if (fn_ptr != nullptr) {
        return fn_ptr(args...);
      }
      return fn_obj(args...);
    }
  };

  struct core {
    nexenne::container::small_vector<slot_entry, 4> slots{};
    /**
     * @brief Connections made while an emit is in progress.
     *
     * Merged into \c slots once the outermost emit finishes, so the live list
     * never moves mid-iteration.
     */
    nexenne::container::small_vector<slot_entry, 2> pending{};
    detail::slot_id_type next_id{1};
    int emit_depth{0};
    bool blocked{false};
    signal* owner{nullptr};

    static auto
    disconnect_thunk(void* const core_ptr, detail::slot_id_type const id) noexcept -> bool {
      auto* const c{static_cast<core*>(core_ptr)};
      if (c->owner == nullptr) {
        return false;
      }
      return c->owner->disconnect_by_id(id);
    }
  };

  std::shared_ptr<core> m_core{};

public:
  /**
   * @brief Constructs an empty signal with no connected slots.
   *
   * The shared core is allocated lazily on the first \c connect or
   * \c block, so a never-used signal pays no heap cost.
   *
   * @pre None.
   * @post \c empty() is \c true and \c is_blocked() is \c false.
   */
  signal() noexcept = default;

  /// @brief Deleted copy constructor; signals are pinned (non-copyable).
  signal(signal const&) = delete;
  /// @brief Deleted move constructor; signals are pinned (non-movable).
  signal(signal&&) = delete;
  /// @brief Deleted copy assignment; signals are pinned (non-copyable).
  auto operator=(signal const&) -> signal& = delete;
  /// @brief Deleted move assignment; signals are pinned (non-movable).
  auto operator=(signal&&) -> signal& = delete;

  /**
   * @brief Destroys the signal, severing it from outstanding handles.
   *
   * Clears the core's back-pointer to this signal. The shared core
   * itself survives as long as any \c connection or in-progress
   * \c emit pins it, but every outstanding connection then sees a
   * null owner and its \c disconnect becomes a no-op.
   *
   * @pre None.
   * @post Outstanding connections to this signal report \c valid() as
   *       \c false and no longer fire.
   */
  ~signal() noexcept {
    if (m_core) {
      m_core->owner = nullptr;
    }
  }

  /**
   * @brief Connects \p fn at the given \p priority.
   *
   * Lower priorities fire first; equal priorities preserve insertion
   * order. Allocates the shared core on first use. A free function or
   * captureless lambda is stored as a raw pointer (no type-erasure
   * thunk); other callables go through fixed inline storage.
   *
   * @tparam Fn Callable invocable as \c Fn(Args...).
   * @param fn Callable to connect.
   * @param priority Ordering key; lower fires first. Default 0.
   *
   * @return A \c connection handle that can later disconnect this slot.
   *
   * @pre \p fn fits in the signal's \c SlotCapacity inline storage
   *      when it is not convertible to a raw function pointer
   *      (rejected at compile time otherwise).
   * @post \c size() has increased by one. Connecting during an
   *       in-progress \c emit does not add \p fn to that emit's visit
   *       list.
   *
   * @complexity \c O(n) to insert into the priority-sorted slot list.
   */
  template <typename Fn>
    requires std::invocable<Fn&, Args...>
  [[nodiscard]] auto connect(Fn&& fn, int const priority = 0) -> connection {
    auto& c{ensure_core()};
    return connect_into(c, std::forward<Fn>(fn), priority, /*once=*/false);
  }

  /**
   * @brief Connects \p fn for exactly one invocation.
   *
   * The slot is auto-disconnected at the end of the emit that first
   * reaches it. Zero extra heap over \c connect.
   *
   * @tparam Fn Callable invocable as \c Fn(Args...).
   * @param fn Callable to connect.
   * @param priority Ordering key; lower fires first. Default 0.
   *
   * @return A \c connection handle. It reports invalid once the slot
   *         has fired and been swept.
   *
   * @pre Same storage constraint as \c connect.
   * @post \c size() has increased by one until the first emit reaches
   *       the slot, after which the slot is removed.
   *
   * @complexity \c O(n) to insert into the priority-sorted slot list.
   */
  template <typename Fn>
    requires std::invocable<Fn&, Args...>
  [[nodiscard]] auto connect_once(Fn&& fn, int const priority = 0) -> connection {
    auto& c{ensure_core()};
    return connect_into(c, std::forward<Fn>(fn), priority, /*once=*/true);
  }

  /**
   * @brief Connects \p fn and ties its lifetime to \p owner.
   *
   * The resulting connection is tracked in \p owner so it
   * auto-disconnects when \p owner is destroyed or cleared. If
   * \p owner is full the connection stays live and untracked.
   *
   * @tparam Fn Callable invocable as \c Fn(Args...).
   * @tparam Capacity Inline tracking capacity of \p owner.
   * @param fn Callable to connect.
   * @param owner Receiver-side \c slot to register the connection.
   * @param priority Ordering key; lower fires first. Default 0.
   *
   * @return A copy of the tracked \c connection.
   *
   * @pre Same storage constraint as \c connect.
   * @post The connection is owned by \p owner when it had room;
   *       \c size() has increased by one regardless.
   *
   * @complexity \c O(n) to insert into the priority-sorted slot list.
   */
  template <typename Fn, std::size_t Capacity>
    requires std::invocable<Fn&, Args...>
  auto connect(Fn&& fn, slot<Capacity>& owner, int const priority = 0) -> connection {
    auto c{connect(std::forward<Fn>(fn), priority)};
    [[maybe_unused]] auto const _{owner.track(c)};
    return c;
  }

  /**
   * @brief Member-function shortcut: connects \c obj.*MemberFn.
   *
   * Wraps the member call in a lambda capturing \p obj by reference.
   * Use the \c slot overload to tie the connection to \p obj's
   * lifetime.
   *
   * @tparam MemberFn Pointer-to-member-function to invoke.
   * @tparam T Owning object type.
   * @param obj Object the member function is called on.
   * @param priority Ordering key; lower fires first. Default 0.
   *
   * @return A \c connection handle for the wrapped slot.
   *
   * @pre \p obj outlives the connection (it is captured by reference).
   * @post \c size() has increased by one.
   *
   * @complexity \c O(n) to insert into the priority-sorted slot list.
   *
   * @warning No lifetime tie: if \p obj dies first, the slot dangles.
   *          Prefer the \c slot overload.
   */
  template <auto MemberFn, typename T>
  [[nodiscard]] auto connect(T& obj, int const priority = 0) -> connection {
    return connect(bind_member<MemberFn>(obj), priority);
  }

  /**
   * @brief Member-function shortcut with a lifetime-tracking owner.
   *
   * Like the other member-function \c connect but registers the
   * connection in \p owner so it auto-disconnects with \p owner.
   *
   * @tparam MemberFn Pointer-to-member-function to invoke.
   * @tparam T Owning object type.
   * @tparam Capacity Inline tracking capacity of \p owner.
   * @param obj Object the member function is called on.
   * @param owner Receiver-side \c slot to register the connection.
   * @param priority Ordering key; lower fires first. Default 0.
   *
   * @return A copy of the tracked \c connection.
   *
   * @pre \p obj outlives the connection. Typically \p owner is a
   *      member of \p obj so both die together.
   * @post The connection is owned by \p owner when it had room;
   *       \c size() has increased by one regardless.
   *
   * @complexity \c O(n) to insert into the priority-sorted slot list.
   */
  template <auto MemberFn, typename T, std::size_t Capacity>
  auto connect(T& obj, slot<Capacity>& owner, int const priority = 0) -> connection {
    return connect(bind_member<MemberFn>(obj), owner, priority);
  }

  /**
   * @brief Disconnects every slot from this signal.
   *
   * If called during an \c emit, slots are marked dead and swept when
   * the outermost emit finishes (the in-progress emit completes
   * safely); otherwise the slot list is cleared immediately.
   *
   * @pre None.
   * @post No slot will fire on a subsequent emit. After any
   *       in-progress emit completes, \c empty() is \c true.
   *
   * @complexity \c O(n).
   */
  auto disconnect_all() noexcept -> void {
    if (!m_core)
      return;
    if (m_core->emit_depth > 0) {
      for (auto& s : m_core->slots) {
        s.alive = false;
      }
      // Drop connects deferred earlier in this emit too, otherwise they would be
      // merged when the emit finishes and survive a disconnect_all.
      m_core->pending.clear();
      return;
    }
    m_core->slots.clear();
  }

  /**
   * @brief Suppresses emission until \c unblock.
   *
   * Lazily creates the core so blocking before the first \c connect
   * still takes effect (correctness is preferred over zero-alloc
   * here). Slots stay connected; \c emit becomes a no-op while blocked.
   *
   * @pre None.
   * @post \c is_blocked() is \c true.
   */
  auto block() noexcept -> void {
    ensure_core().blocked = true;
  }

  /**
   * @brief Re-enables emission after \c block.
   *
   * No-op when the core has never been created (a signal that was
   * never blocked or connected).
   *
   * @pre None.
   * @post \c is_blocked() is \c false.
   */
  auto unblock() noexcept -> void {
    if (m_core)
      m_core->blocked = false;
  }

  /**
   * @brief Reports whether emission is currently suppressed.
   *
   * @return \c true iff a \c block is in effect.
   *
   * @pre None.
   * @post The signal is unchanged.
   */
  [[nodiscard]] auto is_blocked() const noexcept -> bool {
    return m_core && m_core->blocked;
  }

  /**
   * @brief Invokes every alive slot in priority order with \p args.
   *
   * Pins the shared core for the duration so the signal can be safely
   * destroyed by one of its own slots. One-shot slots are marked dead as
   * they fire; dead slots are swept once the outermost emit finishes. A
   * no-op when there are no slots or the signal is blocked.
   *
   * @param args Arguments forwarded to each slot (by value, then as
   *        lvalue references to each slot).
   *
   * @pre None. Reentrant \c emit and self-disconnect from within a
   *      slot are supported.
   * @post Every slot alive at the start of this emit (and not a
   *       deferred addition) was invoked once. One-shot and explicitly
   *       disconnected slots are removed after the outermost emit.
   *
   * @note A plain \c emit allocates nothing. The exception is when a slot
   *       connects during the emit: the new slot is merged into the list when
   *       the outermost emit finishes, and that merge can grow the slot list,
   *       so a connect during emit moves a possible allocation into this
   *       otherwise allocation-free path. Since \c emit is \c noexcept, an
   *       allocation failure there terminates (the standard embedded policy);
   *       reserve the slot list or keep connects out of the emit path in
   *       hard-real-time code.
   *
   * @complexity \c O(n) in the number of slots.
   */
  auto emit(Args... args) noexcept -> void {
    run_emit([&](slot_entry& s) noexcept { s.invoke(args...); });
  }

  /**
   * @brief Call-operator alias for \c emit.
   *
   * @param args Arguments forwarded to \c emit.
   *
   * @pre Same as \c emit.
   * @post Same as \c emit.
   *
   * @complexity \c O(n) in the number of slots.
   */
  auto operator()(Args... args) noexcept -> void {
    emit(args...);
  }

  /**
   * @brief Emits and gathers each slot's return value.
   *
   * Available only for non-void return types. Invokes every alive slot
   * like \c emit and collects the returns, in fire order, into a
   * vector. Returns an empty vector when blocked or with no slots.
   *
   * @param args Arguments forwarded to each slot.
   *
   * @return A vector holding one \c R per slot that fired, in priority
   *         then insertion order.
   *
   * @pre Same reentrancy guarantees as \c emit.
   * @post Same slot-lifecycle effects as \c emit. The returned vector
   *       has one element per slot that fired.
   *
   * @complexity \c O(n) in the number of slots, plus the vector
   *             allocation.
   */
  [[nodiscard]] auto emit_and_collect(Args... args) noexcept -> std::vector<R>
    requires(!std::is_void_v<R>)
  {
    auto out{std::vector<R>{}};
    if (m_core && !m_core->blocked) {
      out.reserve(m_core->slots.size());
    }
    run_emit([&](slot_entry& s) { out.push_back(s.invoke(args...)); });
    return out;
  }

  /**
   * @brief Returns a connect-only \c sink view of this signal.
   *
   * Publish the sink to let clients connect without granting them
   * \c emit authority.
   *
   * @return A \c sink bound to this signal.
   *
   * @pre This signal outlives the returned sink (the sink holds a
   *      raw pointer to it).
   * @post The signal is unchanged.
   */
  [[nodiscard]] auto as_sink() noexcept -> sink<R(Args...), SlotCapacity>;

  /**
   * @brief Number of currently alive slots.
   *
   * @return The count of connected slots not yet swept.
   *
   * @pre None.
   * @post The signal is unchanged.
   *
   * @complexity \c O(n).
   */
  [[nodiscard]] auto size() const noexcept -> std::size_t {
    if (!m_core)
      return 0;
    return static_cast<std::size_t>(std::count_if(
      m_core->slots.begin(),
      m_core->slots.end(),
      [](slot_entry const& s) noexcept { return s.alive; }
    ));
  }

  /**
   * @brief Reports whether the signal has no alive slots.
   *
   * @return \c true iff \c size() is 0.
   *
   * @pre None.
   * @post The signal is unchanged.
   *
   * @complexity \c O(n).
   */
  [[nodiscard]] auto empty() const noexcept -> bool {
    return size() == 0;
  }

private:
  auto ensure_core() -> core& {
    if (!m_core) {
      m_core = std::make_shared<core>();
      m_core->owner = this;
    }
    return *m_core;
  }

  /**
   * @brief Wraps a pointer-to-member call as a slot-shaped callable bound to \p obj.
   *
   * Bound by reference and shared by both member-function connect overloads.
   * \p obj must outlive the resulting connection.
   */
  template <auto MemberFn, typename T>
  static auto bind_member(T& obj) noexcept {
    return [&obj](Args... args) noexcept(noexcept((obj.*MemberFn)(args...))) -> R {
      return (obj.*MemberFn)(args...);
    };
  }

  /**
   * @brief Shared emit engine for \c emit and \c emit_and_collect.
   *
   * Pins the core, iterates the slots alive at entry once each, and applies the
   * single source of the reentrancy rules (one-shot marked dead before invoke,
   * deferred connects merged and dead slots swept once the outermost emit ends).
   * The caller's \p visit handles each alive slot (discard or collect its
   * return).
   *
   * @tparam Visit Callable invoked as \c visit(slot_entry&) for each alive slot.
   * @param visit Per-slot action.
   *
   * @pre None. Reentrant from within a slot.
   * @post As documented on \c emit.
   *
   * @complexity \c O(n) in the number of slots.
   */
  template <typename Visit>
  auto run_emit(Visit visit) noexcept -> void {
    if (!m_core || m_core->blocked) {
      return;
    }
    auto pin{m_core};
    ++pin->emit_depth;
    // Decrement and merge deferred work when the outermost emit ends, even on an
    // early return from a slot.
    auto const at_exit{nexenne::utility::defer{[&pin] {
      if (--pin->emit_depth == 0 && pin->owner != nullptr) {
        apply_pending(*pin);
        sweep_dead(*pin);
      }
    }}};
    auto const n{pin->slots.size()};
    for (auto i{std::size_t{0}}; i < n; ++i) {
      auto& slot{pin->slots[i]};
      if (slot.alive) {
        // Mark a one-shot dead before invoking, not after: the callable may
        // re-enter emit on this same signal, and a still-alive once slot would
        // fire itself again without bound.
        if (slot.once) {
          slot.alive = false;
        }
        visit(slot);
      }
    }
  }

  template <typename Fn>
  auto connect_into(core& c, Fn&& fn, int const priority, bool const once) -> connection {
    auto const id{c.next_id++};
    auto entry{slot_entry{
      .id = id,
      .priority = priority,
      .alive = true,
      .once = once,
    }};
    if constexpr (std::is_convertible_v<Fn, raw_fn_ptr_type>) {
      entry.fn_ptr = static_cast<raw_fn_ptr_type>(std::forward<Fn>(fn));
    } else {
      entry.fn_obj = slot_fn_type{std::forward<Fn>(fn)};
    }
    // During an emit the live slot list must not move: a slot may be connecting
    // from inside its own invocation, so a reallocation would free the callable
    // mid-call. Park the entry and merge it after the outermost emit instead.
    if (c.emit_depth > 0) {
      c.pending.push_back(std::move(entry));
    } else {
      c.slots.push_back(std::move(entry));
      bubble_last_into_position(c);
    }
    return make_connection(id);
  }

  [[nodiscard]] auto make_connection(detail::slot_id_type const id) noexcept -> connection {
    return connection{
      std::weak_ptr<void>{std::static_pointer_cast<void>(m_core)}, &core::disconnect_thunk, id
    };
  }

  auto disconnect_by_id(detail::slot_id_type const id) noexcept -> bool {
    if (!m_core)
      return false;
    auto& slots{m_core->slots};
    auto found_at{slots.size()};
    for (auto i{std::size_t{0}}; i < slots.size(); ++i) {
      if (slots[i].id == id && slots[i].alive) {
        found_at = i;
        break;
      }
    }
    if (found_at == slots.size()) {
      // Not in the live list. It may be a connection made earlier in this same
      // emit and still parked in the pending list; mark it so the merge drops it.
      if (m_core->emit_depth > 0) {
        for (auto& p : m_core->pending) {
          if (p.id == id && p.alive) {
            p.alive = false;
            return true;
          }
        }
      }
      return false;
    }
    if (m_core->emit_depth > 0) {
      slots[found_at].alive = false;
      return true;
    }
    stable_erase_at(*m_core, found_at);
    return true;
  }

  /**
   * @brief Insertion-bubble the just-pushed slot into its priority-sorted position.
   *
   * O(n) worst case, O(1) when the new slot belongs at the end. Stable:
   * equal-priority entries keep insertion order because we only bubble past
   * strictly lower-priority neighbours.
   */
  static auto bubble_last_into_position(core& c) noexcept -> void {
    auto& s{c.slots};
    for (auto i{s.size()}; i > 1; --i) {
      if (s[i - 1].priority < s[i - 2].priority) {
        using std::swap;
        swap(s[i - 1], s[i - 2]);
      } else {
        break;
      }
    }
  }

  static auto stable_erase_at(core& c, std::size_t const pos) noexcept -> void {
    for (auto i{pos}; i + 1 < c.slots.size(); ++i) {
      c.slots[i] = std::move(c.slots[i + 1]);
    }
    [[maybe_unused]] auto const _{c.slots.pop_back()};
  }

  /**
   * @brief Merges connections deferred during an emit into the live slot list.
   *
   * Each is bubbled into priority position; dead (disconnected before the merge)
   * entries are dropped. Called once the outermost emit finishes.
   */
  static auto apply_pending(core& c) noexcept -> void {
    for (auto& entry : c.pending) {
      if (!entry.alive) {
        continue;
      }
      c.slots.push_back(std::move(entry));
      bubble_last_into_position(c);
    }
    c.pending.clear();
  }

  static auto sweep_dead(core& c) noexcept -> void {
    auto write{std::size_t{0}};
    for (auto read{std::size_t{0}}; read < c.slots.size(); ++read) {
      if (c.slots[read].alive) {
        if (write != read) {
          c.slots[write] = std::move(c.slots[read]);
        }
        ++write;
      }
    }
    while (c.slots.size() > write) {
      [[maybe_unused]] auto const _{c.slots.pop_back()};
    }
  }
};

/**
 * @brief Read-only connection interface to a signal.
 *
 * Mirrors every \c connect overload (raw, member-function shortcut,
 * \c slot-tracked, \c connect_once) but hides \c emit. Use this to
 * publish a signal from a class while keeping emit authority private.
 * Holds a raw pointer to the underlying signal, which must outlive the
 * sink.
 *
 * @tparam R Slot return type of the underlying signal.
 * @tparam Args Slot argument types of the underlying signal.
 * @tparam SlotCapacity Inline byte capacity of the underlying signal.
 */
template <typename R, typename... Args, std::size_t SlotCapacity>
class sink<R(Args...), SlotCapacity> {
private:
  signal<R(Args...), SlotCapacity>* m_signal{nullptr};

public:
  /**
   * @brief Binds the sink to signal \p s.
   *
   * @param s Signal to expose. Must outlive the sink.
   *
   * @pre \p s outlives this sink.
   * @post Connect calls on this sink forward to \p s.
   */
  explicit constexpr sink(signal<R(Args...), SlotCapacity>& s) noexcept : m_signal{&s} {}

  /**
   * @brief Forwards to \c signal::connect.
   *
   * @tparam Fn Callable invocable as \c Fn(Args...).
   * @param fn Callable to connect.
   * @param priority Ordering key; lower fires first. Default 0.
   *
   * @return The resulting \c connection.
   *
   * @pre As for \c signal::connect.
   * @post As for \c signal::connect.
   */
  template <typename Fn>
    requires std::invocable<Fn&, Args...>
  [[nodiscard]] auto connect(Fn&& fn, int const priority = 0) -> connection {
    return m_signal->connect(std::forward<Fn>(fn), priority);
  }

  /**
   * @brief Forwards to \c signal::connect_once.
   *
   * @tparam Fn Callable invocable as \c Fn(Args...).
   * @param fn Callable to connect for one invocation.
   * @param priority Ordering key; lower fires first. Default 0.
   *
   * @return The resulting \c connection.
   *
   * @pre As for \c signal::connect_once.
   * @post As for \c signal::connect_once.
   */
  template <typename Fn>
    requires std::invocable<Fn&, Args...>
  [[nodiscard]] auto connect_once(Fn&& fn, int const priority = 0) -> connection {
    return m_signal->connect_once(std::forward<Fn>(fn), priority);
  }

  /**
   * @brief Forwards to the lifetime-tracking \c signal::connect.
   *
   * @tparam Fn Callable invocable as \c Fn(Args...).
   * @tparam Capacity Inline tracking capacity of \p owner.
   * @param fn Callable to connect.
   * @param owner \c slot that auto-disconnects the connection.
   * @param priority Ordering key; lower fires first. Default 0.
   *
   * @return The resulting \c connection.
   *
   * @pre As for the tracking \c signal::connect.
   * @post As for the tracking \c signal::connect.
   */
  template <typename Fn, std::size_t Capacity>
    requires std::invocable<Fn&, Args...>
  auto connect(Fn&& fn, slot<Capacity>& owner, int const priority = 0) -> connection {
    return m_signal->connect(std::forward<Fn>(fn), owner, priority);
  }

  /**
   * @brief Forwards to the member-function \c signal::connect.
   *
   * @tparam MemberFn Pointer-to-member-function to invoke.
   * @tparam T Owning object type.
   * @param obj Object the member function is called on.
   * @param priority Ordering key; lower fires first. Default 0.
   *
   * @return The resulting \c connection.
   *
   * @pre \p obj outlives the connection.
   * @post As for the member-function \c signal::connect.
   */
  template <auto MemberFn, typename T>
  [[nodiscard]] auto connect(T& obj, int const priority = 0) -> connection {
    return m_signal->template connect<MemberFn>(obj, priority);
  }

  /**
   * @brief Forwards to the member-function tracking \c signal::connect.
   *
   * @tparam MemberFn Pointer-to-member-function to invoke.
   * @tparam T Owning object type.
   * @tparam Capacity Inline tracking capacity of \p owner.
   * @param obj Object the member function is called on.
   * @param owner \c slot that auto-disconnects the connection.
   * @param priority Ordering key; lower fires first. Default 0.
   *
   * @return The resulting \c connection.
   *
   * @pre \p obj outlives the connection.
   * @post As for the member-function tracking \c signal::connect.
   */
  template <auto MemberFn, typename T, std::size_t Capacity>
  auto connect(T& obj, slot<Capacity>& owner, int const priority = 0) -> connection {
    return m_signal->template connect<MemberFn>(obj, owner, priority);
  }

  /**
   * @brief Number of alive slots on the underlying signal.
   *
   * @return \c signal::size of the bound signal.
   *
   * @pre None.
   * @post The signal is unchanged.
   */
  [[nodiscard]] auto size() const noexcept -> std::size_t {
    return m_signal->size();
  }

  /**
   * @brief Reports whether the underlying signal has no alive slots.
   *
   * @return \c signal::empty of the bound signal.
   *
   * @pre None.
   * @post The signal is unchanged.
   */
  [[nodiscard]] auto empty() const noexcept -> bool {
    return m_signal->empty();
  }
};

template <typename R, typename... Args, std::size_t SlotCapacity>
[[nodiscard]] auto
signal<R(Args...), SlotCapacity>::as_sink() noexcept -> sink<R(Args...), SlotCapacity> {
  return sink<R(Args...), SlotCapacity>{*this};
}

}  // namespace nexenne::signal
