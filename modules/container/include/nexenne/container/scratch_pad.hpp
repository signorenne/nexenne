#pragma once

/**
 * @file
 * @brief RAII checkpoint over a bump allocator: releases on scope exit.
 *
 * \c scratch_pad<Arena> snapshots an arena's \c bytes_used() at construction and
 * rewinds the arena to that offset in its destructor. Every allocation made
 * through the scratch_pad (or directly on the arena while it is alive) is
 * released when the scratch_pad leaves scope, which makes nested temporary
 * allocations safe and explicit:
 *
 * \code
 * linear_arena<4096> arena;
 * // ... long-lived allocations ...
 * {
 *   scratch_pad scratch{arena};
 *   auto const buf{scratch.allocate<int>(100)};
 *   // ... use buf ...
 * }                       // buf's memory is released; earlier allocations stay
 * \endcode
 *
 * Any arena exposing \c size_type, \c bytes_used(), and \c rewind_to() works;
 * the \c allocate / \c emplace facade forwards to the arena. It is non-copyable
 * and non-movable: the checkpoint is tied to one arena state, and copying it
 * would double-rewind. Every operation is \c noexcept, and thread safety is that
 * of the underlying arena (none).
 */

#include <concepts>
#include <utility>

namespace nexenne::container {

/**
 * @brief Satisfied by an arena that can be checkpointed and rewound.
 *
 * @tparam A Candidate arena type.
 */
template <typename A>
concept checkpointable_arena = requires(A& arena, typename A::size_type offset) {
  typename A::size_type;
  { arena.bytes_used() } -> std::same_as<typename A::size_type>;
  arena.rewind_to(offset);
};

/**
 * @brief RAII checkpoint that rewinds a bump allocator on scope exit.
 *
 * @tparam Arena Arena-like type, typically \c linear_arena<N>.
 *
 * @pre None.
 * @post Construction snapshots the arena offset; destruction rewinds to it.
 */
template <checkpointable_arena Arena>
class [[nodiscard]] scratch_pad final {
public:
  using arena_type = Arena;
  using size_type = typename Arena::size_type;

private:
  Arena& m_arena;
  size_type m_saved_offset;

public:
  /**
   * @brief Snapshots \p arena's current bump offset.
   *
   * @param arena Arena to checkpoint and later rewind.
   *
   * @pre None.
   * @post \c saved_offset() equals \p arena's \c bytes_used() at construction.
   */
  explicit scratch_pad(Arena& arena) noexcept
      : m_arena{arena}, m_saved_offset{arena.bytes_used()} {}

  /**
   * @brief Rewinds the arena to the snapshot, releasing everything since.
   *
   * @pre None.
   * @post The arena's \c bytes_used() is no greater than \c saved_offset().
   */
  ~scratch_pad() noexcept {
    m_arena.rewind_to(m_saved_offset);
  }

  scratch_pad(scratch_pad const&) = delete;
  auto operator=(scratch_pad const&) -> scratch_pad& = delete;
  scratch_pad(scratch_pad&&) = delete;
  auto operator=(scratch_pad&&) -> scratch_pad& = delete;

  /**
   * @brief Allocates \p size bytes aligned to \p alignment, via the arena.
   *
   * @param size Bytes to allocate.
   * @param alignment Required alignment, a power of two.
   *
   * @return The arena's result: a pointer to the block, or
   *         \c container_error::full when the arena lacks room.
   *
   * @pre \p alignment is a non-zero power of two.
   * @post On success the storage is released when this scratch_pad is
   *       destroyed.
   */
  [[nodiscard]] auto allocate(size_type const size, size_type const alignment) noexcept {
    return m_arena.allocate(size, alignment);
  }

  /**
   * @brief Allocates raw storage for \p count objects of \p T, via the arena.
   *
   * @tparam T Object type the storage is sized and aligned for.
   * @param count Number of objects to reserve space for.
   *
   * @return The arena's result: a \p T pointer, or \c container_error::full
   *         when the arena lacks room.
   *
   * @pre None.
   * @post On success the storage is released when this scratch_pad is
   *       destroyed.
   */
  template <typename T>
  [[nodiscard]] auto allocate(size_type const count = 1) noexcept {
    return m_arena.template allocate<T>(count);
  }

  /**
   * @brief Allocates storage and constructs a \p T in place, via the arena.
   *
   * @tparam T Type to construct.
   * @tparam Args Constructor argument types.
   * @param args Arguments forwarded to \p T's constructor.
   *
   * @return The arena's result: a pointer to the constructed object, or
   *         \c container_error::full when the arena lacks room.
   *
   * @pre None.
   * @post On success a \p T is alive in arena storage released when this
   *       scratch_pad is destroyed; the caller must \c std::destroy_at it before
   *       then for a non-trivial \p T.
   */
  template <typename T, typename... Args>
  [[nodiscard]] auto emplace(Args&&... args) noexcept {
    return m_arena.template emplace<T>(std::forward<Args>(args)...);
  }

  /**
   * @brief The underlying arena.
   *
   * @return A reference to the wrapped arena.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto arena() noexcept -> Arena& {
    return m_arena;
  }

  /// @copydoc arena()
  [[nodiscard]] auto arena() const noexcept -> Arena const& {
    return m_arena;
  }

  /**
   * @brief The offset the arena will rewind to on destruction.
   *
   * @return The arena \c bytes_used() captured at construction.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto saved_offset() const noexcept -> size_type {
    return m_saved_offset;
  }
};

/// @cond INTERNAL
template <typename Arena>
scratch_pad(Arena&) -> scratch_pad<Arena>;
/// @endcond

}  // namespace nexenne::container
