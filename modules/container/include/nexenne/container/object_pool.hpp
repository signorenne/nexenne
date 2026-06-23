#pragma once

/**
 * @file
 * @brief Fixed-capacity typed pool of pre-allocated T slots.
 *
 * \c object_pool<T, N> reserves \p N aligned slots for \p T inline (no heap, no
 * allocator) and hands them out and reclaims them in \c O(1). A free list (a
 * \c static_vector of slot pointers) drives LIFO recycling, so the
 * most-recently-freed slot stays hot in cache for the next acquisition.
 *
 * There are two API tiers. The raw tier, \c acquire / \c release, hands out and
 * reclaims raw storage and leaves \p T's lifetime to the caller (construct with
 * \c std::construct_at, destroy with \c std::destroy_at). The object tier,
 * \c emplace / \c destroy, acquires a slot and constructs in place, then
 * destroys and releases in one call. Reach for it for particle systems, fixed
 * rosters of reusable buffers, and per-frame transient objects that want \c O(1)
 * granular recycling rather than a \c linear_arena 's all-or-nothing reset.
 *
 * Compared with \c std::pmr::unsynchronized_pool_resource it is non-virtual (a
 * direct call), typed (the slot is sized for \p T, no allocator-traits
 * ceremony), inline (stack-allocatable), and single-size (exactly \p T). Every
 * operation is \c noexcept; exhaustion is reported via \c result. It is not
 * thread-safe.
 */

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <utility>

#include <nexenne/container/error.hpp>
#include <nexenne/container/static_vector.hpp>
#include <nexenne/utility/discard.hpp>

namespace nexenne::container {

/**
 * @brief Fixed-capacity, allocation-free pool of reusable T slots.
 *
 * @tparam T Slot element type. The pool constructs and destroys it in place and
 *           never moves or copies it, so an immovable \p T (a mutex, an atomic)
 *           is fine.
 * @tparam N Number of slots; must be greater than zero.
 *
 * @pre None.
 * @post A freshly constructed pool has all \p N slots free.
 *
 * @warning The pool does not destroy objects still live at its own destruction:
 *          because the raw \c acquire tier leaves construction to the caller, the
 *          pool cannot know which slots hold a live \p T. Destroy (or \c destroy)
 *          every outstanding object before the pool goes out of scope, or accept
 *          leaking it, exactly as with \c linear_arena.
 */
template <typename T, std::size_t N>
  requires(N > 0)
class object_pool {
public:
  using value_type = T;
  using size_type = std::size_t;

  static constexpr size_type capacity_v{N};

private:
  struct slot {
    alignas(T) std::array<std::byte, sizeof(T)> storage{};

    [[nodiscard]] auto ptr() noexcept -> T* {
      return reinterpret_cast<T*>(storage.data());
    }

    [[nodiscard]] auto ptr() const noexcept -> T const* {
      return reinterpret_cast<T const*>(storage.data());
    }
  };

  std::array<slot, N> m_slots{};
  static_vector<slot*, N> m_free{};
  std::array<bool, N> m_acquired{};
  size_type m_high_water{0};

  // Maps a pointer handed out by acquire/emplace back to its slot index, or
  // returns N when ptr is not a currently-acquired slot of this pool: a foreign
  // pointer, an interior/misaligned pointer, or one already released. Byte
  // arithmetic avoids forming an out-of-bounds slot* for a stray pointer.
  [[nodiscard]] auto acquired_index(T const* const ptr) const noexcept -> size_type {
    // Compare and offset as integer addresses. Relational comparison and
    // subtraction of pointers into different objects (a foreign \p ptr versus the
    // pool storage) are not well-defined, but the same operations on their
    // \c uintptr_t addresses are.
    auto const base{reinterpret_cast<std::uintptr_t>(m_slots.data())};
    auto const p{reinterpret_cast<std::uintptr_t>(ptr)};
    if (p < base || p >= base + N * sizeof(slot)) {
      return N;  // outside this pool's storage
    }
    auto const offset{p - base};
    if (offset % sizeof(slot) != 0) {
      return N;  // interior or misaligned pointer, not a slot base
    }
    auto const index{offset / sizeof(slot)};
    if (!m_acquired[index]) {
      return N;  // never acquired, or already released
    }
    return index;
  }

public:
  /**
   * @brief Constructs a pool with all \p N slots free.
   *
   * Seeds the free list in reverse so the first acquisition pulls slot 0.
   *
   * @pre None.
   * @post \c size() is zero and every slot is on the free list.
   */
  object_pool() noexcept {
    for (auto i{N}; i > 0; --i) {
      nexenne::utility::discard(m_free.push_back(&m_slots[i - 1]));
    }
  }

  // A pool hands out interior pointers; copying or moving would either share the
  // storage (aliasing) or dangle outstanding handles.
  object_pool(object_pool const&) = delete;
  auto operator=(object_pool const&) -> object_pool& = delete;
  object_pool(object_pool&&) = delete;
  auto operator=(object_pool&&) -> object_pool& = delete;

  /**
   * @brief Total slot count.
   *
   * @return The template parameter \p N.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] static constexpr auto capacity() noexcept -> size_type {
    return N;
  }

  /**
   * @brief Largest number of objects the pool can hold at once.
   *
   * @return The template parameter \p N.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] static constexpr auto max_size() noexcept -> size_type {
    return N;
  }

  /**
   * @brief Number of slots currently acquired.
   *
   * @return The count of slots handed out and not yet released.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto size() const noexcept -> size_type {
    return N - m_free.size();
  }

  /**
   * @brief Reports whether no slots are acquired.
   *
   * @return \c true when \c size() is zero.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto empty() const noexcept -> bool {
    return size() == 0;
  }

  /**
   * @brief Reports whether every slot is acquired.
   *
   * @return \c true when no free slot remains.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto full() const noexcept -> bool {
    return m_free.empty();
  }

  /**
   * @brief Peak \c size() since the last high-water reset.
   *
   * Useful for sizing a pool empirically.
   *
   * @return The maximum \c size() recorded since \c clear_high_water_mark().
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto high_water_mark() const noexcept -> size_type {
    return m_high_water;
  }

  /**
   * @brief Resets the high-water mark to zero; acquired slots are unaffected.
   *
   * @pre None.
   * @post \c high_water_mark() is zero.
   */
  auto clear_high_water_mark() noexcept -> void {
    m_high_water = 0;
  }

  /**
   * @brief Pulls a free slot and returns a pointer to its raw storage.
   *
   * The storage is uninitialised: \c std::construct_at a \p T into it, or call
   * \c emplace for the combined operation.
   *
   * @return A pointer to raw slot storage, or \c container_error::full when the
   *         pool is exhausted.
   *
   * @pre None.
   * @post On success \c size() grew by one and \c high_water_mark() is at least
   *       the new \c size(); on failure the pool is unchanged.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] auto acquire() noexcept -> result<T*> {
    if (m_free.empty()) {
      return std::unexpected{container_error::full};
    }
    auto* const target{*m_free.back()};
    nexenne::utility::discard(m_free.pop_back());
    m_acquired[static_cast<size_type>(target - m_slots.data())] = true;
    auto const live{N - m_free.size()};
    if (live > m_high_water) {
      m_high_water = live;
    }
    return target->ptr();
  }

  /**
   * @brief Returns \p ptr's slot to the free list without destroying anything.
   *
   * @param ptr A pointer previously returned by \c acquire or \c emplace.
   *
   * @return Nothing on success, \c container_error::not_found when \p ptr is
   *         null, or \c container_error::out_of_range when \p ptr is not a
   *         currently-acquired slot of this pool (foreign, interior, or already
   *         released).
   *
   * @pre Any object constructed in the slot has already been destroyed.
   * @post On success the slot is free and \c size() shrank by one; on failure
   *       the pool is unchanged.
   *
   * @complexity \c O(1).
   */
  auto release(T* const ptr) noexcept -> result<void> {
    if (ptr == nullptr) {
      return std::unexpected{container_error::not_found};
    }
    auto const index{acquired_index(ptr)};
    if (index == N) {
      return std::unexpected{container_error::out_of_range};
    }
    m_acquired[index] = false;
    nexenne::utility::discard(m_free.push_back(&m_slots[index]));
    return {};
  }

  /**
   * @brief Pulls a free slot and constructs a \p T in place.
   *
   * @tparam Args Constructor argument types.
   * @param args Arguments forwarded to \p T's constructor.
   *
   * @return A pointer to the constructed object, or \c container_error::full
   *         when the pool is exhausted.
   *
   * @pre None.
   * @post On success a \p T is alive in a slot and \c size() grew by one; on
   *       failure the pool is unchanged.
   *
   * @complexity \c O(1).
   */
  template <typename... Args>
    requires std::constructible_from<T, Args...>
  [[nodiscard]] auto emplace(Args&&... args) noexcept -> result<T*> {
    auto const acquired{acquire()};
    if (!acquired.has_value()) {
      return std::unexpected{acquired.error()};
    }
    return std::construct_at(*acquired, std::forward<Args>(args)...);
  }

  /**
   * @brief Destroys the object at \p ptr and returns its slot to the free list.
   *
   * @param ptr A pointer to a live object previously returned by \c emplace, or
   *            constructed in an \c acquire slot.
   *
   * @return Nothing on success, \c container_error::not_found when \p ptr is
   *         null, or \c container_error::out_of_range when \p ptr is not a
   *         currently-acquired slot of this pool (foreign, interior, or already
   *         released).
   *
   * @pre \p ptr refers to a live object owned by this pool.
   * @post On success the object is destroyed, the slot is free, and \c size()
   *       shrank by one; on failure the pool is unchanged.
   *
   * @complexity \c O(1).
   */
  auto destroy(T* const ptr) noexcept -> result<void> {
    if (ptr == nullptr) {
      return std::unexpected{container_error::not_found};
    }
    auto const index{acquired_index(ptr)};
    if (index == N) {
      return std::unexpected{container_error::out_of_range};
    }
    m_acquired[index] = false;
    std::destroy_at(ptr);
    nexenne::utility::discard(m_free.push_back(&m_slots[index]));
    return {};
  }
};

}  // namespace nexenne::container
