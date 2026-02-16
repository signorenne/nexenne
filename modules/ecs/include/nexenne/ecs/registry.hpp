#pragma once

/**
 * @file
 * @brief Sparse-set entity-component registry (EnTT-style ECS) with
 *        zero-virtual dispatch.
 *
 * The registry owns:
 *
 *   - A flat array of generation counters per entity index, plus a
 *     free list for index recycling. \c create() returns an opaque
 *     \c entity_id pairing the index and the current generation;
 *     \c destroy(e) bumps the generation so outstanding handles to
 *     that entity read as invalid. Same recycle-safe scheme as
 *     \c slot_map, unrolled inside the registry to avoid the
 *     \c optional<T> payload overhead.
 *
 *   - A vector of type-erased storage entries indexed by
 *     \c type_id<T>(). Each entry holds a \c void* to a heap-
 *     allocated \c component_storage<T> plus a small struct of
 *     function pointers (erase / clear / contains / size / destroy).
 *     No virtual dispatch: the function pointer call is direct, and
 *     for the hot path (per-component iteration) callers access
 *     \c storage<T>() directly with the concrete type, so the loop
 *     inlines fully.
 *
 * Each \c component_storage<T> is a pointer-stable \c detail::component_pool:
 * a reference to a component keeps its address for the component's whole
 * lifetime, so it is safe to add or remove components (even destroy whole
 * entities) while a \c view iterates, without dangling the references the
 * view hands out. Removal tombstones the slot in place rather than
 * compacting, so iterating one component type is:
 *
 *   \code
 *   for (auto& p : reg.storage<position>().values()) { ... }
 *   \endcode
 *
 *   a chunk-by-chunk walk that skips tombstoned slots: cache-friendly within
 *   each chunk and free of function pointer indirection, though not a single
 *   flat array (the price of pointer stability).
 *
 * Cold paths (\c destroy(e), \c clear()) call into every storage
 * through the function pointers (single indirect call, no vtable
 * lookup), and the compiler can often devirtualize the loop with PGO.
 *
 * Memory & performance:
 *
 *   - \c create() / \c destroy(): O(1) amortised.
 *   - Per-component \c add / \c remove / \c get / \c has: O(1).
 *   - \c destroy(e): O(C) where C is the number of registered
 *     component types.
 *   - Iteration over one component type: chunked and cache-friendly,
 *     visiting live slots plus any not-yet-reused tombstones.
 *
 * Exception policy: every operation is \c noexcept. Allocation
 * failures terminate. Out-of-bounds / stale-handle accesses return
 * \c nullptr or \c false, never UB.
 *
 * Thread safety: standard convention, concurrent reads safe,
 * concurrent mutation not.
 */

#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include <nexenne/container/error.hpp>
#include <nexenne/container/sparse_set.hpp>
#include <nexenne/ecs/component_pool.hpp>
#include <nexenne/ecs/type_id.hpp>
#include <nexenne/signal/connection.hpp>
#include <nexenne/signal/signal.hpp>

namespace nexenne::ecs {

using nexenne::container::container_error;
using nexenne::container::sparse_set;

namespace detail {

/**
 * @brief Compile-time pack of types used by \c basic_view and the
 *        \c query builder to separate include / exclude lists.
 */
template <typename... Ts>
struct type_list {};

}  // namespace detail

/**
 * @brief Forward declaration of the multi-component view, defined in
 *        \c <nexenne/ecs/view.hpp>. Declared here so the
 *        registry can return one from its \c view<> / \c query()
 *        methods. Include \c view.hpp to use them.
 */
template <typename IncludeList, typename ExcludeList = detail::type_list<>>
class basic_view;

/**
 * @brief Forward declaration of the fluent query builder, defined
 *        alongside \c basic_view.
 */
template <typename IncludeList, typename ExcludeList>
class typed_query_builder;

/**
 * @brief Opaque handle to an entity. {index, generation} pair.
 *
 * Default-constructed (generation 0) never matches a live entity:
 * \c registry::create() always returns generation >= 1.
 *
 * @warning A handle carries no registry identity, so it is only meaningful for
 *          the registry that minted it. Passing a handle from one registry to
 *          another is a logic error: the second registry may accept it if it
 *          happens to hold a live entity with the same index and generation,
 *          acting on the wrong entity. This matches the convention of other
 *          sparse-set registries; keep each handle with its own registry.
 */
class entity_id {
public:
  using index_type = std::uint32_t;       ///< Slot index into the registry's per-entity arrays.
  using generation_type = std::uint32_t;  ///< Recycle counter distinguishing reuses of an index.

private:
  index_type m_index{};  ///< Position in the registry; meaningful only with a matching generation.
  generation_type m_generation{
  };  ///< 0 for the default (invalid) handle; >= 1 for any live entity.

public:
  /**
   * @brief Constructs an invalid handle (index 0, generation 0).
   *
   * Generation 0 never matches a live entity, so a default-
   * constructed handle is always rejected by \c registry::valid.
   *
   * @pre None.
   * @post \c index() and \c generation() are both 0.
   */
  constexpr entity_id() noexcept = default;

  /**
   * @brief Constructs a handle from an explicit index and generation.
   *
   * Used by \c registry to mint handles; user code rarely calls this
   * directly. No validity is checked here: a handle is only "live"
   * relative to a registry, via \c registry::valid.
   *
   * @param index       Slot index into the registry.
   * @param generation  Recycle counter for that slot.
   *
   * @pre None.
   * @post \c index() equals \p index and \c generation() equals
   *       \p generation.
   */
  constexpr entity_id(index_type const index, generation_type const generation) noexcept
      : m_index{index}, m_generation{generation} {}

  /**
   * @brief Returns the slot index of this handle.
   *
   * @return The index passed at construction, used by the registry to
   *         address its per-entity arrays.
   *
   * @pre None.
   * @post Return value is unchanged for the lifetime of the handle.
   */
  [[nodiscard]] constexpr auto index() const noexcept -> index_type {
    return m_index;
  }

  /**
   * @brief Returns the generation counter of this handle.
   *
   * @return The generation passed at construction. A value of 0
   *         denotes the default (invalid) handle.
   *
   * @pre None.
   * @post Return value is unchanged for the lifetime of the handle.
   */
  [[nodiscard]] constexpr auto generation() const noexcept -> generation_type {
    return m_generation;
  }

  /**
   * @brief Defaulted total ordering and equality over (index,
   *        generation).
   *
   * Two handles are equal iff both their index and generation match,
   * so a recycled index with a bumped generation never compares equal
   * to the stale handle it replaced.
   *
   * @return The three-way comparison of the (index, generation) pairs.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] friend constexpr auto
  operator<=>(entity_id const&, entity_id const&) noexcept = default;
};

/**
 * @brief Typed per-component storage: a pointer-stable \c component_pool
 *        keyed by entity index.
 *
 * Used by the registry as the concrete storage type behind each
 * \c type_id<T>() slot. Exposed via \c registry::storage<T>() so callers
 * can iterate \c values() (live components) or walk the slot interface the
 * \c view drives off. Also owns the three lifecycle signals (construct /
 * update / destroy) that the registry fires as components are attached,
 * replaced, and removed.
 *
 * The backing \c detail::component_pool is pointer-stable: a reference to a
 * component keeps its address for the component's whole lifetime, even as
 * other components are added or removed. That is what lets a \c view add to
 * or remove from a storage while iterating it without dangling the
 * references it hands out. Removal tombstones the slot in place rather than
 * compacting, so the live components are not contiguous; iterate them with
 * \c values() or the \c slot_count / \c is_live / \c key_at / \c value_at
 * slot interface.
 *
 * Not derived from any base: the registry erases the type through function
 * pointers rather than virtual dispatch. Pinned in place (non-copyable,
 * non-movable) because the registry holds a raw pointer to it and the
 * signals back-reference their own storage.
 *
 * @tparam T  Component value type stored per entity index.
 */
template <typename T>
class component_storage {
public:
  using pool_type =
    detail::component_pool<T>;  ///< Pointer-stable backing pool keyed by entity index.
  using size_type = typename pool_type::size_type;  ///< Count and slot-index type.
  /// @brief Range over the live components, yielded by \c values() (mutable).
  using value_range = detail::pool_value_range<pool_type>;
  /// @brief Range over the live components, yielded by \c values() (const).
  using const_value_range = detail::pool_value_range<pool_type const>;
  /// @brief Signal type for the three lifecycle events, signature
  ///        \c (entity_id, T&).
  using signal_type = nexenne::signal::signal<void(entity_id, T&)>;
  /// @brief Connect-only sink published for each lifecycle signal.
  using sink_type = nexenne::signal::sink<void(entity_id, T&)>;

private:
  pool_type m_pool{};
  signal_type m_on_construct{};
  signal_type m_on_update{};
  signal_type m_on_destroy{};

public:
  /**
   * @brief Constructs empty storage with no components and no listeners.
   *
   * @pre None.
   * @post \c empty() is \c true.
   */
  component_storage() noexcept = default;

  /// @brief Deleted copy constructor; storage is pinned in place.
  component_storage(component_storage const&) = delete;
  /// @brief Deleted move constructor; storage is pinned in place.
  component_storage(component_storage&&) = delete;
  /// @brief Deleted copy assignment; storage is pinned in place.
  auto operator=(component_storage const&) -> component_storage& = delete;
  /// @brief Deleted move assignment; storage is pinned in place.
  auto operator=(component_storage&&) -> component_storage& = delete;

  /**
   * @brief Inserts a component at \p index, overwriting any existing one.
   *
   * Does not fire any signal; the registry emits lifecycle signals
   * around this call. An existing component is assigned in place, keeping
   * its address.
   *
   * @param index  Entity index key.
   * @param value  Component value, moved into the pool.
   *
   * @return \c true when a new entry was created, \c false when one
   *         already existed (in which case it is overwritten).
   *
   * @pre None.
   * @post \c contains(index) is \c true.
   */
  auto insert(std::uint32_t const index, T value) noexcept -> bool {
    return m_pool.insert_or_assign(index, std::move(value));
  }

  /**
   * @brief Erases the component at \p index if present.
   *
   * Does not fire any signal; the registry emits \c on_destroy before
   * calling this. Tombstones the slot in place, so every other
   * component keeps its address.
   *
   * @param index  Entity index key.
   *
   * @return \c true when an entry was removed, \c false when \p index
   *         was absent.
   *
   * @pre None.
   * @post \c contains(index) is \c false.
   */
  auto erase(std::uint32_t const index) noexcept -> bool {
    return m_pool.erase(index);
  }

  /**
   * @brief Pointer to the component at \p index, or \c nullptr (mutable).
   *
   * @param index  Entity index key.
   *
   * @return A stable pointer to the component, or \c nullptr when \p index
   *         is absent.
   *
   * @pre None.
   * @post The storage is unchanged.
   */
  [[nodiscard]] auto try_get(std::uint32_t const index) noexcept -> T* {
    return m_pool.try_get(index);
  }

  /**
   * @brief Pointer to the component at \p index, or \c nullptr (const).
   *
   * @param index  Entity index key.
   *
   * @return A stable pointer to the \c const component, or \c nullptr when
   *         \p index is absent.
   *
   * @pre None.
   * @post The storage is unchanged.
   */
  [[nodiscard]] auto try_get(std::uint32_t const index) const noexcept -> T const* {
    return m_pool.try_get(index);
  }

  /**
   * @brief Expected-style lookup (mutable overload).
   *
   * @param index  Entity index key.
   *
   * @return A reference wrapper to the component on hit, or
   *         \c container_error::not_found on miss.
   *
   * @pre None.
   * @post The storage is unchanged.
   */
  [[nodiscard]] auto at(std::uint32_t const index
  ) noexcept -> std::expected<std::reference_wrapper<T>, container_error> {
    if (auto* const ptr{m_pool.try_get(index)}; ptr != nullptr) {
      return std::ref(*ptr);
    }
    return std::unexpected{container_error::not_found};
  }

  /**
   * @brief Expected-style lookup (const overload).
   *
   * @param index  Entity index key.
   *
   * @return A reference wrapper to the \c const component on hit, or
   *         \c container_error::not_found on miss.
   *
   * @pre None.
   * @post The storage is unchanged.
   */
  [[nodiscard]] auto at(std::uint32_t const index
  ) const noexcept -> std::expected<std::reference_wrapper<T const>, container_error> {
    if (auto const* const ptr{m_pool.try_get(index)}; ptr != nullptr) {
      return std::cref(*ptr);
    }
    return std::unexpected{container_error::not_found};
  }

  /**
   * @brief Reports whether a component exists at \p index.
   *
   * @param index  Entity index key.
   *
   * @return \c true iff an entry exists for \p index.
   *
   * @pre None.
   * @post The storage is unchanged.
   */
  [[nodiscard]] auto contains(std::uint32_t const index) const noexcept -> bool {
    return m_pool.contains(index);
  }

  /**
   * @brief Number of entries at \p index (0 or 1).
   *
   * @param index  Entity index key.
   *
   * @return 1 when present, 0 otherwise. Mirrors \c std::map::count
   *         for a unique key.
   *
   * @pre None.
   * @post The storage is unchanged.
   */
  [[nodiscard]] auto count(std::uint32_t const index) const noexcept -> std::size_t {
    return m_pool.contains(index) ? std::size_t{1} : std::size_t{0};
  }

  /**
   * @brief Number of live (non-tombstoned) components.
   *
   * @return The live component count.
   *
   * @pre None.
   * @post The storage is unchanged.
   */
  [[nodiscard]] auto size() const noexcept -> std::size_t {
    return m_pool.size();
  }

  /**
   * @brief Reports whether the storage holds no live components.
   *
   * @return \c true iff \c size() is 0.
   *
   * @pre None.
   * @post The storage is unchanged.
   */
  [[nodiscard]] auto empty() const noexcept -> bool {
    return m_pool.empty();
  }

  /**
   * @brief Removes every component without firing signals.
   *
   * @pre None.
   * @post \c empty() is \c true. Listeners stay connected.
   */
  auto clear() noexcept -> void {
    m_pool.clear();
  }

  /**
   * @brief Total number of slots, live plus tombstone.
   *
   * Drives view iteration together with \c is_live: walk \c [0, slot_count())
   * and skip slots where \c is_live is \c false.
   *
   * @return The slot count.
   *
   * @pre None.
   * @post The storage is unchanged.
   */
  [[nodiscard]] auto slot_count() const noexcept -> size_type {
    return m_pool.slot_count();
  }

  /**
   * @brief Whether slot \p slot currently holds a live component.
   *
   * @param slot  Slot index, less than \c slot_count().
   *
   * @return \c true when the slot is live, \c false when it is a tombstone.
   *
   * @pre \p slot is less than \c slot_count().
   * @post The storage is unchanged.
   */
  [[nodiscard]] auto is_live(size_type const slot) const noexcept -> bool {
    return m_pool.is_live(slot);
  }

  /**
   * @brief Entity index owning the live component at \p slot.
   *
   * @param slot  Live slot index.
   *
   * @return The entity index key for that slot.
   *
   * @pre \c is_live(slot) is \c true.
   * @post The storage is unchanged.
   */
  [[nodiscard]] auto key_at(size_type const slot) const noexcept -> std::uint32_t {
    return m_pool.key_at(slot);
  }

  /**
   * @brief The component at live slot \p slot (mutable).
   *
   * @param slot  Live slot index.
   *
   * @return A stable reference to the component.
   *
   * @pre \c is_live(slot) is \c true.
   * @post The storage is unchanged.
   */
  [[nodiscard]] auto value_at(size_type const slot) noexcept -> T& {
    return m_pool.value_at(slot);
  }

  /**
   * @brief The component at live slot \p slot (const).
   *
   * @param slot  Live slot index.
   *
   * @return A stable reference to the \c const component.
   *
   * @pre \c is_live(slot) is \c true.
   * @post The storage is unchanged.
   */
  [[nodiscard]] auto value_at(size_type const slot) const noexcept -> T const& {
    return m_pool.value_at(slot);
  }

  /**
   * @brief Range over the live components, skipping tombstones (mutable).
   *
   * @return A forward range yielding each live component by reference.
   *
   * @pre None.
   * @post The storage is structurally unchanged. The range is invalidated
   *       by \c clear; pointer stability keeps individual references valid
   *       across other inserts and erases.
   */
  [[nodiscard]] auto values() noexcept -> value_range {
    return value_range{m_pool};
  }

  /**
   * @brief Range over the live components, skipping tombstones (const).
   *
   * @return A forward range yielding each live \c const component by
   *         reference.
   *
   * @pre None.
   * @post The storage is unchanged. The range is invalidated by \c clear.
   */
  [[nodiscard]] auto values() const noexcept -> const_value_range {
    return const_value_range{m_pool};
  }

  /**
   * @brief Read-only sink for the on-construct signal.
   *
   * Fired by the registry after a new \c T is attached to an entity.
   *
   * @return A connect-only sink over the on-construct signal.
   *
   * @pre None.
   * @post The storage is unchanged.
   */
  [[nodiscard]] auto on_construct() noexcept -> sink_type {
    return m_on_construct.as_sink();
  }

  /**
   * @brief Read-only sink for the on-update signal.
   *
   * Fired by the registry when an existing \c T is replaced via
   * \c add or mutated via \c patch.
   *
   * @return A connect-only sink over the on-update signal.
   *
   * @pre None.
   * @post The storage is unchanged.
   */
  [[nodiscard]] auto on_update() noexcept -> sink_type {
    return m_on_update.as_sink();
  }

  /**
   * @brief Read-only sink for the on-destroy signal.
   *
   * Fired by the registry just BEFORE a \c T is removed from an entity
   * (so listeners can read the final value).
   *
   * @return A connect-only sink over the on-destroy signal.
   *
   * @pre None.
   * @post The storage is unchanged.
   */
  [[nodiscard]] auto on_destroy() noexcept -> sink_type {
    return m_on_destroy.as_sink();
  }

  //
  // These are public to allow the type-erased registry dispatch to
  // fire signals without friending lambdas; user code should
  // normally subscribe via the sinks above and let the registry
  // mutate the storage. Directly calling these from outside the

  /**
   * @brief Internal: fires the on-construct signal for (\p e, \p v).
   *
   * @param e  Entity the component was attached to.
   * @param v  Reference to the just-stored component.
   *
   * @pre Called by the registry immediately after a new \c T is
   *       inserted for \p e; \p v refers to that stored component.
   * @post Every connected on-construct listener has run.
   *
   * @warning Internal entry point. Calling this directly from user
   *          code breaks the registry's before/after invariants.
   */
  auto emit_construct(entity_id const e, T& v) noexcept -> void {
    m_on_construct.emit(e, v);
  }

  /**
   * @brief Internal: fires the on-update signal for (\p e, \p v).
   *
   * @param e  Entity whose component changed.
   * @param v  Reference to the stored component after the change.
   *
   * @pre Called by the registry after a replacement or in-place
   *       mutation of \p e's \c T; \p v refers to that component.
   * @post Every connected on-update listener has run.
   *
   * @warning Internal entry point; see \c emit_construct.
   */
  auto emit_update(entity_id const e, T& v) noexcept -> void {
    m_on_update.emit(e, v);
  }

  /**
   * @brief Internal: fires the on-destroy signal for (\p e, \p v).
   *
   * @param e  Entity whose component is about to be removed.
   * @param v  Reference to the still-valid component.
   *
   * @pre Called by the registry just before the component is erased;
   *       \p v still refers to the live component.
   * @post Every connected on-destroy listener has run.
   *
   * @warning Internal entry point; see \c emit_construct.
   */
  auto emit_destroy(entity_id const e, T& v) noexcept -> void {
    m_on_destroy.emit(e, v);
  }
};

namespace detail {

/**
 * @brief Type-erased storage entry: data pointer + manual vtable.
 *
 * The function pointers are filled in once when the storage is
 * created. The registry calls through them for type-independent
 * operations (erase, clear, contains, destroy) without going through
 * a C++ virtual table.
 */
struct erased_storage {
  void* data{nullptr};
  auto (*erase_fn)(void*, std::uint32_t) noexcept -> bool{nullptr};
  auto (*contains_fn)(void const*, std::uint32_t) noexcept -> bool{nullptr};
  auto (*size_fn)(void const*) noexcept -> std::size_t{nullptr};
  auto (*clear_fn)(void*) noexcept -> void{nullptr};
  auto (*destroy_fn)(void*) noexcept -> void{nullptr};
  // Fires the typed on_destroy signal at \p e before the caller
  // (the registry) issues the actual \c erase_fn. Caller must check
  // \c contains_fn first.
  auto (*fire_on_destroy_fn)(void*, entity_id) noexcept -> void{nullptr};
};

template <typename T>
[[nodiscard]] inline auto make_erased(component_storage<T>* const storage
) noexcept -> erased_storage {
  return erased_storage{
    .data = storage,
    .erase_fn = +[](void* d, std::uint32_t idx) noexcept -> bool {
      return static_cast<component_storage<T>*>(d)->erase(idx);
    },
    .contains_fn = +[](void const* d, std::uint32_t idx) noexcept -> bool {
      return static_cast<component_storage<T> const*>(d)->contains(idx);
    },
    .size_fn = +[](void const* d) noexcept -> std::size_t {
      return static_cast<component_storage<T> const*>(d)->size();
    },
    .clear_fn = +[](void* d) noexcept -> void { static_cast<component_storage<T>*>(d)->clear(); },
    .destroy_fn = +[](void* d) noexcept -> void { delete static_cast<component_storage<T>*>(d); },
    .fire_on_destroy_fn = +[](void* d, entity_id e) noexcept -> void {
      auto* const s{static_cast<component_storage<T>*>(d)};
      if (auto* const value{s->try_get(e.index())}; value != nullptr) {
        s->emit_destroy(e, *value);
      }
    },
  };
}

}  // namespace detail

/**
 * @brief The ECS registry: owns entities and their components.
 *
 * Central object of the module. Hands out \c entity_id handles via
 * \c create, recycles their slots on \c destroy, and stores components
 * in per-type \c component_storage accessed by \c type_id<T>(). Provides
 * the add / remove / patch / get / has component API, lifecycle signals,
 * iteration over live entities, and the \c view / \c query builders.
 *
 * Non-copyable and move-only: the registry owns heap-allocated storages
 * and the move operations transfer that ownership. Every operation is
 * \c noexcept; out-of-range or stale-handle accesses return an error
 * value rather than invoking undefined behaviour.
 */
class registry {
public:
  using index_type = entity_id::index_type;            ///< Slot index type for per-entity arrays.
  using generation_type = entity_id::generation_type;  ///< Recycle-counter type per slot.

private:
  using generation_vector = std::vector<generation_type>;  ///< Per-slot generation counters.
  using index_vector = std::vector<index_type>;            ///< Free-list of reusable indices.
  using index_set = sparse_set<index_type>;                ///< Dense set of live indices.
  using storage_table =
    std::vector<detail::erased_storage>;  ///< Type-erased storages by \c type_id.

  generation_vector m_generations{};
  index_vector m_free_indices{};
  // Dense set of live entity indices. Size doubles as the alive
  // count and the iteration list backs \c begin() / \c end().
  index_set m_alive_indices{};
  storage_table m_storages{};

  template <typename T>
  [[nodiscard]] auto ensure_storage() noexcept -> component_storage<T>& {
    auto const id{type_id<T>()};
    if (id >= m_storages.size()) {
      m_storages.resize(id + 1);
    }
    if (m_storages[id].data == nullptr) {
      auto* const storage{new component_storage<T>()};
      m_storages[id] = detail::make_erased(storage);
    }
    return *static_cast<component_storage<T>*>(m_storages[id].data);
  }

  template <typename T>
  [[nodiscard]] auto find_storage() noexcept -> component_storage<T>* {
    auto const id{type_id<T>()};
    if (id >= m_storages.size() || m_storages[id].data == nullptr) {
      return nullptr;
    }
    return static_cast<component_storage<T>*>(m_storages[id].data);
  }

  template <typename T>
  [[nodiscard]] auto find_storage() const noexcept -> component_storage<T> const* {
    auto const id{type_id<T>()};
    if (id >= m_storages.size() || m_storages[id].data == nullptr) {
      return nullptr;
    }
    return static_cast<component_storage<T> const*>(m_storages[id].data);
  }

public:
  /**
   * @brief Constructs an empty registry with no entities or storages.
   *
   * @pre None.
   * @post \c alive() is 0 and no component storage exists yet.
   */
  constexpr registry() noexcept = default;

  /// @brief Deleted copy constructor; the registry is move-only.
  registry(registry const&) = delete;

  /// @brief Deleted copy assignment; the registry is move-only.
  auto operator=(registry const&) -> registry& = delete;

  /**
   * @brief Move-constructs from \p other, taking over its storages.
   *
   * @param other  Registry to move from.
   *
   * @pre None.
   * @post This registry owns \p other's entities and component
   *       storages; \p other is left empty and reusable.
   */
  registry(registry&& other) noexcept
      : m_generations{std::move(other.m_generations)}
      , m_free_indices{std::move(other.m_free_indices)}
      , m_alive_indices{std::move(other.m_alive_indices)}
      , m_storages{std::move(other.m_storages)} {}

  /**
   * @brief Move-assigns from \p other, destroying this registry's
   *        current storages first.
   *
   * @param other  Registry to move from.
   *
   * @return Reference to \c *this.
   *
   * @pre None. Self-assignment is a no-op.
   * @post This registry owns \p other's entities and component
   *       storages; any previously held storages have been freed and
   *       \p other is left empty.
   */
  auto operator=(registry&& other) noexcept -> registry& {
    if (this != &other) {
      destroy_storages();
      m_generations = std::move(other.m_generations);
      m_free_indices = std::move(other.m_free_indices);
      m_alive_indices = std::move(other.m_alive_indices);
      m_storages = std::move(other.m_storages);
    }
    return *this;
  }

  /**
   * @brief Destroys the registry, freeing every component storage.
   *
   * @pre None.
   * @post All heap-allocated component storages have been released.
   */
  ~registry() noexcept {
    destroy_storages();
  }

  /**
   * @brief Creates a new entity and returns a live handle to it.
   *
   * Recycles a free index from the free list when one is available
   * (reusing its current generation), otherwise appends a fresh index
   * with generation 1. The returned handle always has generation
   * >= 1, so it never collides with a default-constructed \c entity_id.
   *
   * @return A handle for which \c valid() is \c true.
   *
   * @pre None.
   * @post \c valid(result) is \c true and \c alive() has increased by
   *       one. \c result.generation() is >= 1.
   *
   * @complexity \c O(1) amortised.
   */
  auto create() noexcept -> entity_id {
    if (!m_free_indices.empty()) {
      auto const idx{m_free_indices.back()};
      m_free_indices.pop_back();
      m_alive_indices.insert(idx);
      return entity_id{idx, m_generations[idx]};
    }
    auto const idx{static_cast<index_type>(m_generations.size())};
    m_generations.push_back(1);
    m_alive_indices.insert(idx);
    return entity_id{idx, 1};
  }

  /**
   * @brief Destroys \p e, removing all of its components.
   *
   * Fires the on-destroy signal for every component \p e carries
   * (listeners see a still-valid reference), erases those components,
   * bumps the slot's generation so all outstanding handles to \p e
   * read as invalid, and returns the index to the free list for reuse.
   *
   * @param e  Handle to destroy. May be stale or default-constructed.
   *
   * @return \c true when \p e was live and is now destroyed, \c false
   *         when \p e was already dead or never valid.
   *
   * @pre None. A stale or invalid \p e is handled gracefully.
   * @post On a \c true result, \c valid(e) is \c false, \c alive() has
   *       decreased by one, and \p e carries no components. On a
   *       \c false result the registry is unchanged.
   *
   * @complexity \c O(C) where C is the number of registered component
   *             types.
   */
  auto destroy(entity_id const e) noexcept -> bool {
    if (!valid(e)) {
      return false;
    }
    // For every storage that holds \p e, fire on_destroy with the live value,
    // then erase. An on_destroy listener may register a new component type and
    // grow m_storages, reallocating the entry table, so copy this entry's
    // dispatch out before firing: the pointed-to storage objects are pinned, so
    // the copied data pointer and function pointers stay valid across the fire
    // even though a reference into m_storages would dangle. Indexed up to the
    // count captured here (not a range-for); a storage added mid-loop cannot
    // hold \p e (it is being destroyed), so skipping it is correct.
    auto const storage_count{m_storages.size()};
    for (auto i{std::size_t{0}}; i < storage_count; ++i) {
      auto* const data{m_storages[i].data};
      if (data == nullptr || !m_storages[i].contains_fn(data, e.index())) {
        continue;
      }
      auto const fire_on_destroy{m_storages[i].fire_on_destroy_fn};
      auto const erase{m_storages[i].erase_fn};
      // m_storages[i] must not be touched past here: fire may reallocate it.
      fire_on_destroy(data, e);
      erase(data, e.index());
    }
    // Bump the generation so stale handles read invalid; step over 0 on
    // wraparound so a recycled slot never mints the invalid generation-0 handle.
    auto& generation{m_generations[e.index()]};
    ++generation;
    if (generation == 0) {
      generation = 1;
    }
    m_free_indices.push_back(e.index());
    m_alive_indices.erase(e.index());
    return true;
  }

  /**
   * @brief Reports whether \p e refers to a live entity.
   *
   * A handle is valid when its index is in range, its generation is
   * non-zero, and that generation matches the registry's current
   * generation for the slot. This rejects default-constructed handles
   * and handles to since-destroyed (or recycled) entities.
   *
   * @param e  Handle to test. Any value is accepted.
   *
   * @return \c true iff \p e currently names a live entity.
   *
   * @pre None.
   * @post The registry is unchanged.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] auto valid(entity_id const e) const noexcept -> bool {
    return e.index() < m_generations.size() && e.generation() != 0
           && m_generations[e.index()] == e.generation();
  }

  /**
   * @brief Current generation at \p index, or 0 when no entity has
   *        ever occupied that slot.
   *
   * Used by \c view to reconstruct a full \c entity_id from a raw
   * index pulled out of a component storage. An out-of-range index
   * yields 0 rather than reading past the array.
   *
   * @param index  Slot index. Need not be in range.
   *
   * @return The slot's current generation, or 0 when \p index is out
   *         of range or the slot has never been used.
   *
   * @pre None.
   * @post The registry is unchanged.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] auto generation_at(index_type const index) const noexcept -> generation_type {
    if (index >= m_generations.size()) {
      return 0;
    }
    return m_generations[index];
  }

  /**
   * @brief Number of currently live entities.
   *
   * @return The count of entities created and not yet destroyed.
   *
   * @pre None.
   * @post The registry is unchanged.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] auto alive() const noexcept -> std::size_t {
    return m_alive_indices.size();
  }

  /**
   * @brief Adds (or replaces) a \c T component on \p e.
   *
   * Lazily creates the storage for \c T on first use. Fires
   * \c on_construct<T>() on a new attachment and \c on_update<T>() on
   * a replacement, passing the now-stored component by reference.
   *
   * @tparam T      Component type.
   * @param e      Target entity.
   * @param value  Component value to store. Moved into the storage.
   *
   * @return \c true when a new component was attached, \c false when
   *         an existing \c T was replaced or when \p e is invalid.
   *
   * @pre None. An invalid \p e is rejected with a \c false return.
   * @post When \c valid(e), \c has<T>(e) is \c true. A \c true result
   *       means exactly one \c on_construct<T>() fired; a \c false
   *       result with a valid \p e means one \c on_update<T>() fired.
   *
   * @warning A listener invoked by the fired signal must not remove this \c T
   *          from \p e, nor destroy \p e, while the signal is firing: that
   *          would invalidate the very reference still being delivered. Adding
   *          or removing components on other entities (and destroying other
   *          entities) is safe: the pointer-stable storage keeps the delivered
   *          reference valid even if its pool grows or tombstones a slot.
   *
   * @complexity \c O(1).
   */
  template <typename T>
  auto add(entity_id const e, T value) noexcept -> bool {
    if (!valid(e)) {
      return false;
    }
    auto& storage{ensure_storage<T>()};
    auto const inserted{storage.insert(e.index(), std::move(value))};
    // try_get never returns nullptr right after a successful insert.
    auto* const stored{storage.try_get(e.index())};
    if (inserted) {
      storage.emit_construct(e, *stored);
    } else {
      storage.emit_update(e, *stored);
    }
    return inserted;
  }

  /**
   * @brief Removes the \c T component from \p e.
   *
   * Fires \c on_destroy<T>() with the still-valid component reference
   * just before erasing it, so listeners can read the final value.
   *
   * @tparam T  Component type to remove.
   * @param e  Target entity.
   *
   * @return \c true when a \c T was removed, \c false when \p e is
   *         invalid, no storage for \c T exists, or \p e carried no
   *         \c T.
   *
   * @pre None.
   * @post On a \c true result, \c has<T>(e) is \c false and exactly
   *       one \c on_destroy<T>() fired. On a \c false result the
   *       registry is unchanged.
   *
   * @warning A listener invoked by the fired signal must not itself remove
   *          this \c T from \p e, nor destroy \p e: that would invalidate the
   *          reference still being delivered to the other listeners. Structural
   *          changes to other entities are safe; the storage is pointer-stable.
   *
   * @complexity \c O(1).
   */
  template <typename T>
  auto remove(entity_id const e) noexcept -> bool {
    if (!valid(e)) {
      return false;
    }
    auto* const storage{find_storage<T>()};
    if (storage == nullptr) {
      return false;
    }
    auto* const value{storage->try_get(e.index())};
    if (value == nullptr) {
      return false;
    }
    storage->emit_destroy(e, *value);
    return storage->erase(e.index());
  }

  /**
   * @brief Mutates \p e's \c T component in place via \p mutator, then
   *        fires \c on_update<T>().
   *
   * Invokes \p mutator with a mutable reference to the stored
   * component, then emits \c on_update<T>() with that same reference
   * so listeners observe the post-mutation value.
   *
   * @tparam T   Component type to mutate.
   * @tparam Fn  Callable invocable as \c Fn(T&).
   * @param e        Target entity.
   * @param mutator  Callback applied to the stored component.
   *
   * @return \c true on success, \c false when \p e is invalid, no
   *         storage for \c T exists, or \p e doesn't carry a \c T.
   *
   * @pre \p mutator does not detach the \c T from \p e (it receives a
   *       reference that must stay valid through the \c on_update emit).
   * @post On a \c true result, \p mutator ran exactly once and one
   *       \c on_update<T>() fired afterward. On a \c false result the
   *       registry is unchanged and \p mutator did not run.
   *
   * @warning Neither \p mutator nor a listener invoked by the fired signal may
   *          remove this \c T from \p e or destroy \p e: that would invalidate
   *          the reference both receive. Structural changes to other entities
   *          are safe; the storage is pointer-stable.
   *
   * @complexity \c O(1) plus the cost of \p mutator.
   */
  template <typename T, typename Fn>
    requires std::invocable<Fn&, T&>
  auto patch(entity_id const e, Fn&& mutator) noexcept -> bool {
    if (!valid(e)) {
      return false;
    }
    auto* const storage{find_storage<T>()};
    if (storage == nullptr) {
      return false;
    }
    auto* const value{storage->try_get(e.index())};
    if (value == nullptr) {
      return false;
    }
    mutator(*value);
    storage->emit_update(e, *value);
    return true;
  }

  /**
   * @brief Sink for the on-construct signal of component \c T.
   *
   * Lazily allocates the storage for \c T if it doesn't exist yet, so
   * listeners can subscribe before any entity holds a \c T. The signal
   * fires after \c add<T>() attaches a new component.
   *
   * @tparam T  Component type whose construction signal is wanted.
   *
   * @return A connect-only sink over the on-construct signal for \c T.
   *
   * @pre None.
   * @post Storage for \c T exists after this call.
   *
   * @complexity \c O(1).
   */
  template <typename T>
  [[nodiscard]] auto on_construct() noexcept -> typename component_storage<T>::sink_type {
    return ensure_storage<T>().on_construct();
  }

  /**
   * @brief Sink for the on-update signal of component \c T.
   *
   * Lazily allocates the storage for \c T if needed. The signal fires
   * when \c add<T>() replaces an existing component or \c patch<T>()
   * mutates one.
   *
   * @tparam T  Component type whose update signal is wanted.
   *
   * @return A connect-only sink over the on-update signal for \c T.
   *
   * @pre None.
   * @post Storage for \c T exists after this call.
   *
   * @complexity \c O(1).
   */
  template <typename T>
  [[nodiscard]] auto on_update() noexcept -> typename component_storage<T>::sink_type {
    return ensure_storage<T>().on_update();
  }

  /**
   * @brief Sink for the on-destroy signal of component \c T.
   *
   * Lazily allocates the storage for \c T if needed. The signal fires
   * just before \c remove<T>() or \c destroy() erases a component, so
   * listeners can read the final value.
   *
   * @tparam T  Component type whose destruction signal is wanted.
   *
   * @return A connect-only sink over the on-destroy signal for \c T.
   *
   * @pre None.
   * @post Storage for \c T exists after this call.
   *
   * @complexity \c O(1).
   */
  template <typename T>
  [[nodiscard]] auto on_destroy() noexcept -> typename component_storage<T>::sink_type {
    return ensure_storage<T>().on_destroy();
  }

  /**
   * @brief Looks up \p e's \c T component (mutable overload).
   *
   * Follows the library-wide \c std::expected error policy: callers
   * need no nullptr checks on the success path. Does not create
   * storage; a missing storage is reported as not-found.
   *
   * @tparam T  Component type to fetch.
   * @param e  Entity to query.
   *
   * @return A reference wrapper to the component on hit, or
   *         \c container_error::not_found when \p e is invalid, no
   *         storage exists for \c T yet, or \p e doesn't carry a \c T.
   *
   * @pre None.
   * @post The registry is unchanged. The returned reference, if any,
   *       stays valid until the component is removed or the storage
   *       reallocates.
   *
   * @complexity \c O(1).
   */
  template <typename T>
  [[nodiscard]] auto get(entity_id const e
  ) noexcept -> std::expected<std::reference_wrapper<T>, container_error> {
    if (!valid(e)) {
      return std::unexpected{container_error::not_found};
    }
    auto* const storage{find_storage<T>()};
    if (storage == nullptr) {
      return std::unexpected{container_error::not_found};
    }
    return storage->at(e.index());
  }

  /**
   * @brief Looks up \p e's \c T component (const overload).
   *
   * Const counterpart of the mutable \c get; yields a reference to a
   * \c const component. Same error policy and lookup semantics.
   *
   * @tparam T  Component type to fetch.
   * @param e  Entity to query.
   *
   * @return A reference wrapper to the \c const component on hit, or
   *         \c container_error::not_found when \p e is invalid, no
   *         storage exists for \c T yet, or \p e doesn't carry a \c T.
   *
   * @pre None.
   * @post The registry is unchanged.
   *
   * @complexity \c O(1).
   */
  template <typename T>
  [[nodiscard]] auto get(entity_id const e
  ) const noexcept -> std::expected<std::reference_wrapper<T const>, container_error> {
    if (!valid(e)) {
      return std::unexpected{container_error::not_found};
    }
    auto const* const storage{find_storage<T>()};
    if (storage == nullptr) {
      return std::unexpected{container_error::not_found};
    }
    return storage->at(e.index());
  }

  /**
   * @brief Reports whether \p e carries a \c T component.
   *
   * @tparam T  Component type to test for.
   * @param e  Entity to query.
   *
   * @return \c true iff \p e is valid and carries a \c T.
   *
   * @pre None.
   * @post The registry is unchanged.
   *
   * @complexity \c O(1).
   */
  template <typename T>
  [[nodiscard]] auto has(entity_id const e) const noexcept -> bool {
    if (!valid(e)) {
      return false;
    }
    auto const* const storage{find_storage<T>()};
    return storage != nullptr && storage->contains(e.index());
  }

  /**
   * @brief Reports whether \p e carries ALL of \p Cs (logical AND).
   *
   * Folds \c has<Cs>(e) with \c &&. An empty pack yields \c true.
   *
   * @tparam Cs  Component types to test for.
   * @param e   Entity to query.
   *
   * @return \c true iff \p e carries every listed component (or the
   *         pack is empty).
   *
   * @pre None.
   * @post The registry is unchanged.
   *
   * @complexity \c O(sizeof...(Cs)).
   */
  template <typename... Cs>
  [[nodiscard]] auto all_of(entity_id const e) const noexcept -> bool {
    return (has<Cs>(e) && ...);
  }

  /**
   * @brief Reports whether \p e carries ANY of \p Cs (logical OR).
   *
   * Folds \c has<Cs>(e) with \c ||. An empty pack yields \c false.
   *
   * @tparam Cs  Component types to test for.
   * @param e   Entity to query.
   *
   * @return \c true iff \p e carries at least one listed component.
   *         \c false for an empty parameter pack.
   *
   * @pre None.
   * @post The registry is unchanged.
   *
   * @complexity \c O(sizeof...(Cs)).
   */
  template <typename... Cs>
  [[nodiscard]] auto any_of(entity_id const e) const noexcept -> bool {
    return (has<Cs>(e) || ...);
  }

  /**
   * @brief Invokes \p f for every live entity (entity-only callback).
   *
   * Same coverage as a range-for over \c *this, packaged in callback
   * form for parity with EnTT's \c registry::each.
   *
   * @tparam Func  Callable invocable as \c Func(entity_id).
   * @param f     Callback invoked once per live entity.
   *
   * @pre \p f must not create or destroy entities for the duration of
   *       the loop (it iterates the live-entity set in place).
   * @post Every live entity at call time was passed to \p f exactly
   *       once. The registry is otherwise unchanged.
   *
   * @complexity \c O(alive()) plus the cost of \p f.
   */
  template <typename Func>
  auto each(Func&& f) const noexcept -> void {
    for (auto const e : *this) {
      f(e);
    }
  }

  /**
   * @brief Returns a multi-component view of this registry.
   *
   * Equivalent to constructing \c view<Includes...>{*this} but matches
   * EnTT's \c registry::view<>() API for ergonomics. The implementation
   * lives in \c view.hpp; include that header to use this method. The
   * include storages are created lazily as a side effect.
   *
   * @tparam Includes  Component types the view requires (at least one).
   *
   * @return A view over entities carrying all of \p Includes.
   *
   * @pre At least one include type is given (enforced by a
   *       \c requires clause on \c basic_view).
   * @post Storage exists for every type in \p Includes. The returned
   *       view is invalidated by later structural changes to those
   *       storages.
   *
   * @complexity \c O(sizeof...(Includes)) to construct.
   */
  template <typename... Includes>
  [[nodiscard]] auto
  view() noexcept -> basic_view<detail::type_list<Includes...>, detail::type_list<>>;

  /**
   * @brief Returns a Flecs-style fluent query builder.
   *
   * Chain \c .with<C>() to add include filters and \c .without<C>() to
   * add exclude filters, then \c .build() to obtain a view, or call
   * \c .each(callback) directly on the builder. Implementation lives
   * in \c view.hpp.
   *
   * @return An empty query builder bound to this registry.
   *
   * @pre None.
   * @post The registry is unchanged; storages are created only when
   *       the eventual view is built.
   *
   * @complexity \c O(1).
   *
   * @par Example
   * \code
   *   reg.query()
   *       .with<position>()
   *       .with<velocity>()
   *       .without<dead>()
   *       .each([](position& p, velocity const& v) { ... });
   * \endcode
   */
  [[nodiscard]] auto
  query() noexcept -> typed_query_builder<detail::type_list<>, detail::type_list<>>;

  /**
   * @brief Direct access to the typed storage for \p T (mutable).
   *
   * The hot path for iteration: \c values() on the returned storage walks
   * the live components with no indirection (chunked, skipping tombstones).
   * Creates the storage lazily on first call for type \p T.
   *
   * @tparam T  Component type whose storage is wanted.
   *
   * @return A reference to the live storage for \c T.
   *
   * @pre None.
   * @post Storage for \c T exists after this call.
   *
   * @complexity \c O(1).
   */
  template <typename T>
  [[nodiscard]] auto storage() noexcept -> component_storage<T>& {
    return ensure_storage<T>();
  }

  /**
   * @brief Read-only view of the storage for \p T (const overload).
   *
   * Unlike the mutable overload this never creates storage, so it can
   * be called on a \c const registry.
   *
   * @tparam T  Component type whose storage is wanted.
   *
   * @return A pointer to the storage for \c T, or \c nullptr if none
   *         has ever been registered.
   *
   * @pre None.
   * @post The registry is unchanged.
   *
   * @complexity \c O(1).
   */
  template <typename T>
  [[nodiscard]] auto storage() const noexcept -> component_storage<T> const* {
    return find_storage<T>();
  }

  /**
   * @brief Wipes every entity and every component.
   *
   * Clears all component storages, then bumps the generation of every
   * previously-used slot and returns all slots to the free list, so
   * any outstanding \c entity_id stays invalid forever.
   *
   * @pre None.
   * @post \c alive() is 0. Every \c entity_id obtained before this
   *       call is now invalid. Registered component types remain
   *       registered (their storages are empty, not destroyed).
   *
   * @complexity \c O(N + C) where N is the number of slots ever used
   *             and C the number of registered component types.
   */
  auto clear() noexcept -> void {
    for (auto& s : m_storages) {
      if (s.data != nullptr) {
        s.clear_fn(s.data);
      }
    }
    for (auto i{std::size_t{0}}; i < m_generations.size(); ++i) {
      if (m_generations[i] != 0) {
        ++m_generations[i];
        // Step over 0 on wraparound, like destroy: every slot is pushed to the
        // free list below, so its generation must stay >= 1 or create() would
        // mint an invalid (generation 0) handle.
        if (m_generations[i] == 0) {
          m_generations[i] = 1;
        }
      }
    }
    m_free_indices.clear();
    m_free_indices.reserve(m_generations.size());
    // Count with size_t (not index_type) so the loop terminates even if the
    // slot count reaches the index-type maximum; indices fit index_type.
    for (auto i{std::size_t{0}}; i < m_generations.size(); ++i) {
      m_free_indices.push_back(static_cast<index_type>(i));
    }
    m_alive_indices.clear();
  }

  /**
   * @brief Input-iterator yielding the \c entity_id of every live
   *        entity. Iteration order is "creation order with swap-pop
   *        on destroy", sparse_set's dense ordering, not stable
   *        across destroys.
   */
  class iterator {
  public:
    using value_type = entity_id;
    using reference = entity_id;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::input_iterator_tag;
    using iterator_concept = std::input_iterator_tag;

  private:
    registry const* m_registry{nullptr};
    std::size_t m_pos{0};

  public:
    /**
     * @brief Constructs a singular end-like iterator.
     *
     * @pre None.
     * @post The iterator is not bound to any registry and must not
     *       be dereferenced or incremented.
     */
    constexpr iterator() noexcept = default;

    /**
     * @brief Constructs an iterator into \p r at dense position \p pos.
     *
     * @param r    Registry being iterated.
     * @param pos  Position in the live-index list, in \c [0, alive()].
     *
     * @pre \p pos is <= \c r.alive().
     * @post The iterator yields the entity at \p pos when
     *       dereferenced (unless it equals \c end()).
     */
    constexpr iterator(registry const& r, std::size_t const pos) noexcept
        : m_registry{&r}, m_pos{pos} {}

    /**
     * @brief Returns the \c entity_id at the current position.
     *
     * @return A fully-formed handle (index plus current generation)
     *         for the live entity at this position.
     *
     * @pre \c *this is dereferenceable: it is bound to a registry
     *       and does not equal \c end().
     * @post The iterator is unchanged.
     */
    [[nodiscard]] auto operator*() const noexcept -> entity_id {
      auto const idx{m_registry->m_alive_indices.keys()[m_pos]};
      return entity_id{idx, m_registry->m_generations[idx]};
    }

    /**
     * @brief Pre-increment: advances to the next live entity.
     *
     * @return Reference to \c *this after advancing.
     *
     * @pre \c *this does not equal \c end().
     * @post The position has advanced by one.
     */
    auto operator++() noexcept -> iterator& {
      ++m_pos;
      return *this;
    }

    /**
     * @brief Post-increment: advances, returning the prior value.
     *
     * @return A copy of the iterator as it was before advancing.
     *
     * @pre \c *this does not equal \c end().
     * @post The position has advanced by one.
     */
    auto operator++(int) noexcept -> iterator {
      auto const t{*this};
      ++*this;
      return t;
    }

    /**
     * @brief Equality: same registry and same position.
     *
     * @param a  Left operand.
     * @param b  Right operand.
     *
     * @return \c true iff both iterators name the same position in
     *         the same registry.
     *
     * @pre None.
     * @post None.
     */
    [[nodiscard]] friend constexpr auto
    operator==(iterator const& a, iterator const& b) noexcept -> bool {
      return a.m_pos == b.m_pos && a.m_registry == b.m_registry;
    }
  };

  /**
   * @brief Iterator to the first live entity.
   *
   * @return An iterator at position 0, equal to \c end() when the
   *         registry holds no live entities.
   *
   * @pre None.
   * @post The registry is unchanged.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] auto begin() const noexcept -> iterator {
    return iterator{*this, 0};
  }

  /**
   * @brief Past-the-end iterator for the live-entity range.
   *
   * @return An iterator one past the last live entity.
   *
   * @pre None.
   * @post The registry is unchanged.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] auto end() const noexcept -> iterator {
    return iterator{*this, m_alive_indices.size()};
  }

private:
  auto destroy_storages() noexcept -> void {
    for (auto& s : m_storages) {
      if (s.data != nullptr && s.destroy_fn != nullptr) {
        s.destroy_fn(s.data);
      }
    }
    m_storages.clear();
  }
};

}  // namespace nexenne::ecs
