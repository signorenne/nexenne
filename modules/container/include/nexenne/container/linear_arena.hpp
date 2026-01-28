#pragma once

/**
 * @file
 * @brief Inline-buffer bump allocator: no heap, no virtual dispatch.
 *
 * \c linear_arena<N> holds an aligned \p N-byte buffer inline and a bump
 * pointer. \c allocate(size, alignment) returns a pointer into the buffer or
 * \c container_error::full when there is not enough room. There is no
 * per-allocation free; \c reset() releases everything in \c O(1).
 *
 * The arena hands out *raw memory*: the lifetime of objects placed in it is the
 * caller's responsibility. If you construct a non-trivially-destructible \p T in
 * an arena slot, \c std::destroy_at it before \c reset (or accept the leak,
 * which is fine for a trivial \p T). Reach for it for per-frame scratch
 * (allocate freely during a frame, reset at the boundary), parser/compiler
 * intermediate state discarded all at once, request-scoped allocations, and
 * freestanding work where the heap is forbidden. Compared with
 * \c std::pmr::monotonic_buffer_resource it is non-virtual (a direct call, no
 * vtable hop) with inline, stack-bufferable storage, but it has no type-erased
 * adapter for allocator-aware containers. Every operation is \c noexcept and
 * \c O(1); the arena is not thread-safe.
 */

#include <array>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <expected>
#include <memory>
#include <utility>

#include <nexenne/container/error.hpp>

namespace nexenne::container {

/**
 * @brief Inline-buffer bump allocator.
 *
 * @tparam N Inline buffer size in bytes; must be greater than zero.
 *
 * @pre None.
 * @post A default-constructed arena has the whole buffer free.
 */
template <std::size_t N>
  requires(N > 0)
class linear_arena {
public:
  using size_type = std::size_t;

  static constexpr size_type buffer_size{N};

private:
  alignas(std::max_align_t) std::array<std::byte, N> m_storage{};
  size_type m_offset{0};
  size_type m_high_water{0};

public:
  /**
   * @brief Constructs an empty arena with the whole buffer free.
   *
   * @pre None.
   * @post \c empty() is \c true and \c bytes_available() equals \p N.
   */
  constexpr linear_arena() noexcept = default;

  // An arena hands out pointers into itself; copying or moving would either
  // share the buffer (an aliasing bug) or dangle outstanding pointers.
  linear_arena(linear_arena const&) = delete;
  auto operator=(linear_arena const&) -> linear_arena& = delete;
  linear_arena(linear_arena&&) = delete;
  auto operator=(linear_arena&&) -> linear_arena& = delete;

  /**
   * @brief Total inline buffer size in bytes.
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
   * @brief Largest single allocation the arena can ever satisfy.
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
   * @brief Bytes currently handed out by the bump pointer.
   *
   * @return The bump offset from the buffer start.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto bytes_used() const noexcept -> size_type {
    return m_offset;
  }

  /**
   * @brief Bytes remaining before the arena is full, ignoring alignment.
   *
   * @return \p N minus \c bytes_used().
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto bytes_available() const noexcept -> size_type {
    return N - m_offset;
  }

  /**
   * @brief Reports whether nothing has been allocated since the last reset.
   *
   * @return \c true when \c bytes_used() is zero.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto empty() const noexcept -> bool {
    return m_offset == 0;
  }

  /**
   * @brief Peak \c bytes_used() since the last high-water reset.
   *
   * Useful for sizing arenas empirically: run the workload, then size to a
   * little above the observed high-water mark.
   *
   * @return The maximum \c bytes_used() recorded since
   *         \c clear_high_water_mark().
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto high_water_mark() const noexcept -> size_type {
    return m_high_water;
  }

  /**
   * @brief Resets the high-water mark to zero; live allocations are untouched.
   *
   * @pre None.
   * @post \c high_water_mark() is zero.
   */
  constexpr auto clear_high_water_mark() noexcept -> void {
    m_high_water = 0;
  }

  /**
   * @brief Allocates \p size bytes aligned to \p alignment.
   *
   * @param size Bytes to allocate.
   * @param alignment Required alignment, a power of two.
   *
   * @return A pointer to the block, or \c container_error::full when the arena
   *         lacks room.
   *
   * @pre \p alignment is a non-zero power of two no greater than
   *      \c alignof(std::max_align_t) (the inline buffer's alignment); both are
   *      asserted in debug. A larger alignment cannot be guaranteed.
   * @post On success \c bytes_used() grew by the padding plus \p size and
   *       \c high_water_mark() is at least the new \c bytes_used(); on failure
   *       the arena is unchanged.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] auto
  allocate(size_type const size, size_type const alignment) noexcept -> result<void*> {
    assert(
      alignment != 0 && (alignment & (alignment - 1)) == 0 && "alignment must be a power of two"
    );
    assert(
      alignment <= alignof(std::max_align_t) && "alignment must not exceed the buffer's alignment"
    );
    auto const mask{alignment - 1};
    auto const aligned{(m_offset + mask) & ~mask};
    if (aligned > N || size > N - aligned) {
      return std::unexpected{container_error::full};
    }
    m_offset = aligned + size;
    if (m_offset > m_high_water) {
      m_high_water = m_offset;
    }
    return static_cast<void*>(m_storage.data() + aligned);
  }

  /**
   * @brief Allocates raw storage for \p count objects of \p T, aligned for it.
   *
   * The storage is raw: use \c std::construct_at to create objects in it, and
   * \c std::destroy_at to clean up before \c reset for a non-trivial \p T.
   *
   * @tparam T Object type the storage is sized and aligned for.
   * @param count Number of objects to reserve space for.
   *
   * @return A pointer to raw storage for \p count objects, or
   *         \c container_error::full when the arena lacks room.
   *
   * @pre None.
   * @post On success \c bytes_used() grew; on failure the arena is unchanged.
   *
   * @complexity \c O(1).
   */
  template <typename T>
  [[nodiscard]] auto allocate(size_type const count = 1) noexcept -> result<T*> {
    static_assert(
      alignof(T) <= alignof(std::max_align_t),
      "T's alignment exceeds the arena buffer's guaranteed alignment"
    );
    auto const block{allocate(sizeof(T) * count, alignof(T))};
    if (!block.has_value()) {
      return std::unexpected{block.error()};
    }
    return static_cast<T*>(*block);
  }

  /**
   * @brief Allocates storage and constructs a \p T in place.
   *
   * The caller owns the lifetime: \c std::destroy_at the object before the
   * arena is reset if \p T has a non-trivial destructor.
   *
   * @tparam T Type to construct.
   * @tparam Args Constructor argument types.
   * @param args Arguments forwarded to \p T's constructor.
   *
   * @return A pointer to the constructed object, or \c container_error::full
   *         when the arena lacks room.
   *
   * @pre None.
   * @post On success a \p T is alive in arena storage and \c bytes_used() grew;
   *       on failure the arena is unchanged.
   *
   * @complexity \c O(1).
   */
  template <typename T, typename... Args>
    requires std::constructible_from<T, Args...>
  [[nodiscard]] auto emplace(Args&&... args) noexcept -> result<T*> {
    auto const storage{allocate<T>()};
    if (!storage.has_value()) {
      return std::unexpected{storage.error()};
    }
    return std::construct_at(*storage, std::forward<Args>(args)...);
  }

  /**
   * @brief Releases every allocation in \c O(1).
   *
   * Does not run destructors: destroy any non-trivial objects placed in the
   * arena before calling this.
   *
   * @pre No non-trivially-destructible object placed in the arena is still
   *      needed, or the caller accepts leaking it.
   * @post \c bytes_used() is zero; outstanding pointers into the arena are
   *       invalidated.
   *
   * @complexity \c O(1).
   */
  constexpr auto reset() noexcept -> void {
    m_offset = 0;
  }

  /**
   * @brief Rewinds the bump pointer to a previously saved offset.
   *
   * Used by \c scratch_pad for RAII checkpoints. An offset above the current
   * position is ignored.
   *
   * @param saved_offset Offset to rewind to, typically an earlier
   *                     \c bytes_used().
   *
   * @pre No non-trivially-destructible object above \p saved_offset is still
   *      needed, or the caller accepts leaking it.
   * @post \c bytes_used() is at most its prior value, and equals
   *       \p saved_offset when that was below it; pointers above the new offset
   *       are invalidated.
   *
   * @complexity \c O(1).
   */
  constexpr auto rewind_to(size_type const saved_offset) noexcept -> void {
    if (saved_offset < m_offset) {
      m_offset = saved_offset;
    }
  }
};

}  // namespace nexenne::container
