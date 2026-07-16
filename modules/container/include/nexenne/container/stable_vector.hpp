#pragma once

/**
 * @file
 * @brief Append-only sequence whose element addresses never change.
 *
 * \c stable_vector<T, ChunkSize> stores elements inside fixed-size,
 * heap-allocated chunks held by a \c std::vector of owning pointers. A push
 * lands in the trailing chunk or allocates a fresh one; existing chunks are
 * never reallocated or moved. So a \c T* or \c T& obtained from \c at,
 * \c operator[], or an iterator stays valid for the element's whole lifetime,
 * even across growth, unlike \c std::vector.
 *
 * Reach for it for entity/component pools and long-lived caches where
 * downstream code keeps raw pointers that must not dangle after a push, and for
 * append-only arenas whose elements outlive the container's growth. Iteration
 * is random-access but not contiguous: elements are contiguous within a chunk,
 * and chunks are scattered on the heap. Allocation failure terminates, per the
 * module policy. \c ChunkSize must be a power of two, so the chunk and slot of
 * an index are a shift and a mask rather than a divide.
 */

#include <algorithm>
#include <array>
#include <compare>
#include <concepts>
#include <cstddef>
#include <expected>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include <nexenne/container/error.hpp>

namespace nexenne::container {

/**
 * @brief Append-only sequence with stable element addresses.
 *
 * @tparam T Element type; must be move-constructible.
 * @tparam ChunkSize Elements per chunk; must be a power of two.
 *
 * @pre None.
 * @post A default-constructed vector is empty.
 */
template <std::move_constructible T, std::size_t ChunkSize = 64>
  requires(ChunkSize > 0 && (ChunkSize & (ChunkSize - 1)) == 0)
class stable_vector {
public:
  using value_type = T;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using reference = T&;
  using const_reference = T const&;
  using pointer = T*;
  using const_pointer = T const*;

  static constexpr size_type chunk_size{ChunkSize};

private:
  // A chunk is ChunkSize manually-managed slots. A union slot (rather than a
  // byte buffer plus reinterpret_cast) keeps element access free of the
  // launder/aliasing grey area.
  struct chunk {
    union slot {
      unsigned char none;
      T value;

      /**
       * @brief Constructs an inactive slot that holds no \c T.
       *
       * @pre None.
       * @post The slot holds no live element.
       */
      constexpr slot() noexcept : none{} {}

      /**
       * @brief Trivially destroys the slot; a live \c T is destroyed by hand.
       *
       * @pre None.
       * @post None.
       */
      constexpr ~slot() noexcept {}
    };

    std::array<slot, ChunkSize> storage{};

    /**
     * @brief Pointer to the \c T storage of slot \p index in this chunk.
     *
     * @param index Slot index within the chunk.
     *
     * @return Pointer to the element storage at \p index.
     *
     * @pre None.
     * @post None.
     */
    [[nodiscard]] auto at(size_type const index) noexcept -> T* {
      return std::addressof(storage[index].value);
    }

    /**
     * @brief Const pointer to the \c T storage of slot \p index in this chunk.
     *
     * @param index Slot index within the chunk.
     *
     * @return Const pointer to the element storage at \p index.
     *
     * @pre None.
     * @post None.
     */
    [[nodiscard]] auto at(size_type const index) const noexcept -> T const* {
      return std::addressof(storage[index].value);
    }
  };

  std::vector<std::unique_ptr<chunk>> m_chunks;
  size_type m_size{};

  /**
   * @brief The chunk index that holds logical element \p index.
   *
   * \c ChunkSize is a power of two, so this reduces to a right shift.
   *
   * @param index Logical element index.
   *
   * @return The index of the owning chunk in \c m_chunks.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] static constexpr auto chunk_of(size_type const index) noexcept -> size_type {
    return index / ChunkSize;
  }

  /**
   * @brief The slot offset of logical element \p index within its chunk.
   *
   * \c ChunkSize is a power of two, so this reduces to a bitwise mask.
   *
   * @param index Logical element index.
   *
   * @return The offset of the element inside its chunk.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] static constexpr auto slot_of(size_type const index) noexcept -> size_type {
    return index % ChunkSize;
  }

  /**
   * @brief Ensures storage exists for at least \p desired elements.
   *
   * Allocates fresh chunks until the chunk vector covers \p desired slots; an
   * already-sufficient request adds nothing. An unsatisfiable chunk count fails
   * loudly rather than wrapping to a silent no-op.
   *
   * @param desired Minimum element count to back with allocated chunks.
   *
   * @pre None.
   * @post \c capacity() is at least \p desired, or the process terminated.
   */
  auto ensure_capacity(size_type const desired) noexcept -> void {
    // Compute ceil(desired / ChunkSize) without the additive round-up, which
    // would wrap for desired near SIZE_MAX and make reserve(huge) a silent
    // no-op; an unsatisfiable chunk count instead fails loudly when the pointer
    // vector cannot be reserved.
    auto const needed{
      desired / ChunkSize + (desired % ChunkSize == 0 ? size_type{0} : size_type{1})
    };
    m_chunks.reserve(needed);  // size the pointer vector once, not per chunk
    while (m_chunks.size() < needed) {
      m_chunks.push_back(std::make_unique<chunk>());
    }
  }

  /**
   * @brief Destroys every live element without touching the chunk vector.
   *
   * @pre None.
   * @post All \c size() elements are destroyed; the chunks stay allocated and
   *       \c m_size is left for the caller to reset.
   */
  auto destroy_all() noexcept -> void {
    for (size_type i{0}; i < m_size; ++i) {
      std::destroy_at(m_chunks[chunk_of(i)]->at(slot_of(i)));
    }
  }

  /**
   * @brief Random-access iterator over the logical element sequence.
   *
   * Holds the owning vector plus a logical position rather than a raw element
   * pointer, so increments walk across chunk boundaries transparently. The
   * \c IsConst parameter selects the const or mutable reference and pointer
   * types, and a mutable iterator converts implicitly to its const twin.
   *
   * @tparam IsConst Whether the iterator yields \c const access to elements.
   *
   * @pre None.
   * @post A default-constructed iterator is singular.
   */
  template <bool IsConst>
  class basic_iterator {
  public:
    using value_type = T;
    using reference = std::conditional_t<IsConst, T const&, T&>;
    using pointer = std::conditional_t<IsConst, T const*, T*>;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::random_access_iterator_tag;
    using iterator_concept = std::random_access_iterator_tag;

  private:
    using owner_ptr = std::conditional_t<IsConst, stable_vector const*, stable_vector*>;
    owner_ptr m_owner{nullptr};
    size_type m_pos{0};

    /**
     * @brief Pointer to the element at logical position \p pos.
     *
     * @param pos Logical element position within the owning vector.
     *
     * @return Pointer to the element storage at \p pos.
     *
     * @pre \p pos is a valid position in the owning vector.
     * @post None.
     */
    [[nodiscard]] constexpr auto element(size_type const pos) const noexcept -> pointer {
      return m_owner->m_chunks[chunk_of(pos)]->at(slot_of(pos));
    }

  public:
    /**
     * @brief Constructs a singular iterator with no owner.
     *
     * @pre None.
     * @post The iterator is singular and not dereferenceable.
     */
    constexpr basic_iterator() noexcept = default;

    /**
     * @brief Constructs an iterator over \p owner at logical position \p pos.
     *
     * @param owner Owning vector the iterator walks.
     * @param pos Initial logical position.
     *
     * @pre None.
     * @post The iterator refers to \p owner at position \p pos.
     */
    constexpr basic_iterator(owner_ptr owner, size_type const pos) noexcept
        : m_owner{owner}, m_pos{pos} {}

    /**
     * @brief Converts a mutable iterator to a const iterator.
     *
     * @tparam OtherConst Constness of the source iterator; the overload is
     *                    constrained to the mutable to const direction only.
     * @param other Source iterator to copy the owner and position from.
     *
     * @pre None.
     * @post This iterator refers to the same owner and position as \p other.
     */
    template <bool OtherConst>
      requires(IsConst && !OtherConst)
    constexpr basic_iterator(basic_iterator<OtherConst> const& other) noexcept
        : m_owner{other.m_owner}, m_pos{other.m_pos} {}

    /**
     * @brief Accesses the element at the current position.
     *
     * @return Reference to the current element.
     *
     * @pre The iterator is dereferenceable (not the past-the-end position).
     * @post None.
     */
    [[nodiscard]] constexpr auto operator*() const noexcept -> reference {
      return *element(m_pos);
    }

    /**
     * @brief Member access on the element at the current position.
     *
     * @return Pointer to the current element.
     *
     * @pre The iterator is dereferenceable (not the past-the-end position).
     * @post None.
     */
    [[nodiscard]] constexpr auto operator->() const noexcept -> pointer {
      return element(m_pos);
    }

    /**
     * @brief Accesses the element \p n positions from the current one.
     *
     * @param n Signed offset from the current position.
     *
     * @return Reference to the element at the offset position.
     *
     * @pre The offset position is dereferenceable.
     * @post None.
     */
    [[nodiscard]] constexpr auto operator[](difference_type const n) const noexcept -> reference {
      return *element(static_cast<size_type>(static_cast<difference_type>(m_pos) + n));
    }

    /**
     * @brief Advances to the next element.
     *
     * @return Reference to \c *this after advancing.
     *
     * @pre None.
     * @post The position advanced by one.
     */
    constexpr auto operator++() noexcept -> basic_iterator& {
      ++m_pos;
      return *this;
    }

    /**
     * @brief Steps back to the previous element.
     *
     * @return Reference to \c *this after stepping back.
     *
     * @pre None.
     * @post The position decreased by one.
     */
    constexpr auto operator--() noexcept -> basic_iterator& {
      --m_pos;
      return *this;
    }

    /**
     * @brief Advances to the next element, returning the prior value.
     *
     * @return A copy of the iterator before advancing.
     *
     * @pre None.
     * @post The position advanced by one.
     */
    constexpr auto operator++(int) noexcept -> basic_iterator {
      auto previous{*this};
      ++m_pos;
      return previous;
    }

    /**
     * @brief Steps back to the previous element, returning the prior value.
     *
     * @return A copy of the iterator before stepping back.
     *
     * @pre None.
     * @post The position decreased by one.
     */
    constexpr auto operator--(int) noexcept -> basic_iterator {
      auto previous{*this};
      --m_pos;
      return previous;
    }

    /**
     * @brief Advances the iterator by \p n positions.
     *
     * @param n Signed number of positions to advance.
     *
     * @return Reference to \c *this after the shift.
     *
     * @pre None.
     * @post The position moved by \p n.
     */
    constexpr auto operator+=(difference_type const n) noexcept -> basic_iterator& {
      m_pos = static_cast<size_type>(static_cast<difference_type>(m_pos) + n);
      return *this;
    }

    /**
     * @brief Moves the iterator back by \p n positions.
     *
     * @param n Signed number of positions to move back.
     *
     * @return Reference to \c *this after the shift.
     *
     * @pre None.
     * @post The position moved by \c -n.
     */
    constexpr auto operator-=(difference_type const n) noexcept -> basic_iterator& {
      return *this += -n;
    }

    /**
     * @brief Returns \p it advanced by \p n positions.
     *
     * @param it Iterator to offset.
     * @param n Signed number of positions to advance.
     *
     * @return The advanced iterator.
     *
     * @pre None.
     * @post None.
     */
    [[nodiscard]] friend constexpr auto
    operator+(basic_iterator it, difference_type const n) noexcept -> basic_iterator {
      it += n;
      return it;
    }

    /**
     * @brief Returns \p it advanced by \p n positions.
     *
     * @param n Signed number of positions to advance.
     * @param it Iterator to offset.
     *
     * @return The advanced iterator.
     *
     * @pre None.
     * @post None.
     */
    [[nodiscard]] friend constexpr auto
    operator+(difference_type const n, basic_iterator it) noexcept -> basic_iterator {
      it += n;
      return it;
    }

    /**
     * @brief Returns \p it moved back by \p n positions.
     *
     * @param it Iterator to offset.
     * @param n Signed number of positions to move back.
     *
     * @return The moved iterator.
     *
     * @pre None.
     * @post None.
     */
    [[nodiscard]] friend constexpr auto
    operator-(basic_iterator it, difference_type const n) noexcept -> basic_iterator {
      it -= n;
      return it;
    }

    /**
     * @brief The signed distance from \p b to \p a.
     *
     * @param a Left iterator.
     * @param b Right iterator.
     *
     * @return The number of positions from \p b to \p a.
     *
     * @pre \p a and \p b refer to the same owning vector.
     * @post None.
     */
    [[nodiscard]] friend constexpr auto
    operator-(basic_iterator const& a, basic_iterator const& b) noexcept -> difference_type {
      return static_cast<difference_type>(a.m_pos) - static_cast<difference_type>(b.m_pos);
    }

    /**
     * @brief Equality: same logical position.
     *
     * @param a Left iterator.
     * @param b Right iterator.
     *
     * @return \c true when both hold the same position.
     *
     * @pre \p a and \p b refer to the same owning vector.
     * @post None.
     */
    [[nodiscard]] friend constexpr auto
    operator==(basic_iterator const& a, basic_iterator const& b) noexcept -> bool {
      return a.m_pos == b.m_pos;
    }

    /**
     * @brief Orders two iterators by logical position.
     *
     * @param a Left iterator.
     * @param b Right iterator.
     *
     * @return The ordering of the two positions.
     *
     * @pre \p a and \p b refer to the same owning vector.
     * @post None.
     */
    [[nodiscard]] friend constexpr auto
    operator<=>(basic_iterator const& a, basic_iterator const& b) noexcept {
      return a.m_pos <=> b.m_pos;
    }

    template <bool>
    friend class basic_iterator;
  };

public:
  using iterator = basic_iterator<false>;
  using const_iterator = basic_iterator<true>;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  /**
   * @brief Constructs an empty vector.
   *
   * @pre None.
   * @post \c empty() is \c true.
   */
  stable_vector() noexcept = default;

  /**
   * @brief Constructs from an initializer list.
   *
   * @param init Brace-enclosed list of values.
   *
   * @pre None.
   * @post \c size() equals \c init.size() with a copy of each element.
   */
  stable_vector(std::initializer_list<T> const init) noexcept {
    for (auto const& value : init) {
      push_back(value);
    }
  }

  /**
   * @brief Copy-constructs from \p other (a deep clone of every element).
   *
   * @param other Source vector to copy.
   *
   * @pre None.
   * @post This vector holds copies of \p other's elements; \p other is
   *       unchanged.
   */
  stable_vector(stable_vector const& other) noexcept {
    for (size_type i{0}; i < other.m_size; ++i) {
      push_back(*other.m_chunks[chunk_of(i)]->at(slot_of(i)));
    }
  }

  /**
   * @brief Move-constructs from \p other by stealing its chunks.
   *
   * @param other Source vector, left empty after the move.
   *
   * @pre None.
   * @post This vector owns \p other's chunks; \p other is empty. Element
   *       addresses are preserved.
   *
   * @note Raw pointers and references from \c at, \c operator[], or \c push_back
   *       stay valid across the move (that is the container's guarantee), but
   *       iterators into \p other are invalidated: an iterator holds the owning
   *       vector plus a position, so it follows the container object, not the
   *       elements.
   */
  stable_vector(stable_vector&& other) noexcept
      : m_chunks{std::move(other.m_chunks)}, m_size{other.m_size} {
    other.m_size = 0;
  }

  /**
   * @brief Assigns from \p other (copy-and-swap; routes copy and move).
   *
   * @param other Source vector, taken by value so the compiler routes a
   *              copy-assignment through the copy constructor and a
   *              move-assignment through the move constructor.
   *
   * @return Reference to \c *this.
   *
   * @pre None.
   * @post This vector holds \p other's elements.
   */
  auto operator=(stable_vector other) noexcept -> stable_vector& {
    swap(other);
    return *this;
  }

  /**
   * @brief Destroys every element and releases the chunk storage.
   *
   * @pre None.
   * @post All elements are destroyed and every chunk is freed.
   */
  ~stable_vector() noexcept {
    destroy_all();
  }

  /**
   * @brief Number of live elements.
   *
   * @return The element count.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto size() const noexcept -> size_type {
    return m_size;
  }

  /**
   * @brief Reports whether the vector holds no elements.
   *
   * @return \c true when \c size() is zero.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto empty() const noexcept -> bool {
    return m_size == 0;
  }

  /**
   * @brief Total slot capacity across all allocated chunks.
   *
   * @return \c chunk_count() times \c ChunkSize.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto capacity() const noexcept -> size_type {
    return m_chunks.size() * ChunkSize;
  }

  /**
   * @brief Number of allocated chunks.
   *
   * @return The chunk count.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto chunk_count() const noexcept -> size_type {
    return m_chunks.size();
  }

  /**
   * @brief The largest number of elements the vector can address.
   *
   * @return The maximum element count.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] static constexpr auto max_size() noexcept -> size_type {
    return std::numeric_limits<size_type>::max();
  }

  /**
   * @brief Swaps contents with \p other in constant time.
   *
   * @param other Vector to exchange state with.
   *
   * @pre None.
   * @post This vector holds \p other's former elements and vice versa; element
   *       addresses are preserved.
   *
   * @note Element addresses (raw pointers and references) survive the swap, but
   *       outstanding iterators retarget: they keep their owner and position, so
   *       they now refer to the other vector's contents.
   *
   * @complexity \c O(1).
   */
  auto swap(stable_vector& other) noexcept -> void {
    m_chunks.swap(other.m_chunks);
    using std::swap;
    swap(m_size, other.m_size);
  }

  /**
   * @brief Swaps the contents of \p a and \p b.
   *
   * @param a First vector.
   * @param b Second vector.
   *
   * @pre None.
   * @post \p a and \p b have exchanged state.
   */
  friend auto swap(stable_vector& a, stable_vector& b) noexcept -> void {
    a.swap(b);
  }

  /**
   * @brief Reserves storage for at least \p n elements.
   *
   * @param n Minimum element count to ensure storage for.
   *
   * @pre None.
   * @post \c capacity() is at least \p n; size and element addresses are
   *       unchanged.
   */
  auto reserve(size_type const n) noexcept -> void {
    ensure_capacity(n);
  }

  /**
   * @brief Releases chunks that lie wholly past the live element range.
   *
   * @pre None.
   * @post \c chunk_count() equals \c ceil(size() / ChunkSize); addresses of
   *       live elements are unchanged.
   */
  auto shrink_to_fit() noexcept -> void {
    auto const needed{(m_size + ChunkSize - 1) / ChunkSize};
    while (m_chunks.size() > needed) {
      m_chunks.pop_back();
    }
    m_chunks.shrink_to_fit();
  }

  /**
   * @brief Destroys every element; chunk storage is retained.
   *
   * @pre None.
   * @post \c size() is zero; \c capacity() is unchanged.
   */
  auto clear() noexcept -> void {
    destroy_all();
    m_size = 0;
  }

  /**
   * @brief Appends a copy of \p value, preserving every existing address.
   *
   * @param value Value to copy in.
   *
   * @return Pointer to the new element, stable until it is destroyed.
   *
   * @pre None.
   * @post \c size() grew by one; addresses of existing elements are unchanged.
   *
   * @complexity Amortised \c O(1).
   */
  auto push_back(T const& value) noexcept -> T* {
    return std::addressof(emplace_back(value));
  }

  /**
   * @brief Appends \p value by moving it, preserving every existing address.
   *
   * @param value Value to move in.
   *
   * @return Pointer to the new element, stable until it is destroyed.
   *
   * @pre None.
   * @post \c size() grew by one; addresses of existing elements are unchanged.
   *
   * @complexity Amortised \c O(1).
   */
  auto push_back(T&& value) noexcept -> T* {
    return std::addressof(emplace_back(std::move(value)));
  }

  /**
   * @brief Constructs an element in place at the end.
   *
   * @tparam Args Constructor argument types.
   * @param args Arguments forwarded to \p T's constructor.
   *
   * @return Reference to the new element, whose address is stable until it is
   *         destroyed.
   *
   * @pre None.
   * @post \c size() grew by one; addresses of existing elements are unchanged.
   *
   * @complexity Amortised \c O(1).
   */
  template <typename... Args>
    requires std::constructible_from<T, Args...>
  auto emplace_back(Args&&... args) noexcept -> T& {
    ensure_capacity(m_size + 1);
    auto* const slot{m_chunks[chunk_of(m_size)]->at(slot_of(m_size))};
    std::construct_at(slot, std::forward<Args>(args)...);
    ++m_size;
    return *slot;
  }

  /**
   * @brief Removes the last element.
   *
   * @return Nothing on success, or \c container_error::empty when empty.
   *
   * @pre None.
   * @post On success \c size() shrank by one; on failure the vector is
   *       unchanged.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] auto pop_back() noexcept -> result<void> {
    if (m_size == 0) {
      return std::unexpected{container_error::empty};
    }
    --m_size;
    std::destroy_at(m_chunks[chunk_of(m_size)]->at(slot_of(m_size)));
    return {};
  }

  /**
   * @brief Unchecked access by index.
   *
   * @param index Logical index.
   *
   * @return Reference to the element at \p index.
   *
   * @pre \p index is less than \c size(); a larger index is undefined
   *      behaviour. Use \c at for a checked lookup.
   * @post None.
   */
  [[nodiscard]] auto operator[](size_type const index) noexcept -> T& {
    return *m_chunks[chunk_of(index)]->at(slot_of(index));
  }

  /**
   * @brief Unchecked access by index (const overload).
   *
   * @param index Logical index.
   *
   * @return Const reference to the element at \p index.
   *
   * @pre \p index is less than \c size(); a larger index is undefined
   *      behaviour. Use \c at for a checked lookup.
   * @post None.
   */
  [[nodiscard]] auto operator[](size_type const index) const noexcept -> T const& {
    return *m_chunks[chunk_of(index)]->at(slot_of(index));
  }

  /**
   * @brief Checked access by index.
   *
   * @param index Logical index.
   *
   * @return Pointer to the element, or \c nullptr when \p index is out of range.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto at(size_type const index) noexcept -> T* {
    return index < m_size ? m_chunks[chunk_of(index)]->at(slot_of(index)) : nullptr;
  }

  /**
   * @brief Checked access by index (const overload).
   *
   * @param index Logical index.
   *
   * @return Const pointer to the element, or \c nullptr when out of range.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto at(size_type const index) const noexcept -> T const* {
    return index < m_size ? m_chunks[chunk_of(index)]->at(slot_of(index)) : nullptr;
  }

  /**
   * @brief The first element.
   *
   * @return Pointer to the first element, or \c nullptr when empty.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto front() noexcept -> T* {
    return m_size == 0 ? nullptr : m_chunks.front()->at(0);
  }

  /// @copydoc front()
  [[nodiscard]] auto front() const noexcept -> T const* {
    return m_size == 0 ? nullptr : m_chunks.front()->at(0);
  }

  /**
   * @brief The last element.
   *
   * @return Pointer to the last element, or \c nullptr when empty.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto back() noexcept -> T* {
    return m_size == 0 ? nullptr : m_chunks[chunk_of(m_size - 1)]->at(slot_of(m_size - 1));
  }

  /// @copydoc back()
  [[nodiscard]] auto back() const noexcept -> T const* {
    return m_size == 0 ? nullptr : m_chunks[chunk_of(m_size - 1)]->at(slot_of(m_size - 1));
  }

  /**
   * @brief Iterator to the first element.
   *
   * @return Iterator to the first element, or \c end() when empty.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto begin() noexcept -> iterator {
    return iterator{this, 0};
  }

  /**
   * @brief Iterator one past the last element.
   *
   * @return The past-the-end iterator.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto end() noexcept -> iterator {
    return iterator{this, m_size};
  }

  /// @copydoc begin()
  [[nodiscard]] auto begin() const noexcept -> const_iterator {
    return const_iterator{this, 0};
  }

  /// @copydoc end()
  [[nodiscard]] auto end() const noexcept -> const_iterator {
    return const_iterator{this, m_size};
  }

  /// @copydoc begin()
  [[nodiscard]] auto cbegin() const noexcept -> const_iterator {
    return begin();
  }

  /// @copydoc end()
  [[nodiscard]] auto cend() const noexcept -> const_iterator {
    return end();
  }

  /**
   * @brief Reverse iterator to the last element.
   *
   * @return Reverse iterator to the last element, or \c rend() when empty.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto rbegin() noexcept -> reverse_iterator {
    return reverse_iterator{end()};
  }

  /**
   * @brief Reverse iterator one before the first element.
   *
   * @return The past-the-end reverse iterator.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto rend() noexcept -> reverse_iterator {
    return reverse_iterator{begin()};
  }

  /// @copydoc rbegin()
  [[nodiscard]] auto rbegin() const noexcept -> const_reverse_iterator {
    return const_reverse_iterator{end()};
  }

  /// @copydoc rend()
  [[nodiscard]] auto rend() const noexcept -> const_reverse_iterator {
    return const_reverse_iterator{begin()};
  }

  /// @copydoc rbegin()
  [[nodiscard]] auto crbegin() const noexcept -> const_reverse_iterator {
    return rbegin();
  }

  /// @copydoc rend()
  [[nodiscard]] auto crend() const noexcept -> const_reverse_iterator {
    return rend();
  }

  /**
   * @brief Equality: same size and element-wise equal.
   *
   * @param a Left vector.
   * @param b Right vector.
   *
   * @return \c true when both hold equal elements in order.
   *
   * @pre None.
   * @post None.
   *
   * @complexity \c O(size).
   */
  [[nodiscard]] friend auto operator==(stable_vector const& a, stable_vector const& b) noexcept
    -> bool
    requires std::equality_comparable<T>
  {
    return a.m_size == b.m_size && std::equal(a.begin(), a.end(), b.begin(), b.end());
  }

  /**
   * @brief Lexicographic three-way comparison of the elements.
   *
   * @tparam U Deduced as \p T; keeps the ordering type unevaluated unless \p T
   *           is three-way comparable.
   * @param a Left vector.
   * @param b Right vector.
   *
   * @return The lexicographic ordering of the element sequences.
   *
   * @pre None.
   * @post None.
   *
   * @complexity \c O(size).
   */
  template <std::three_way_comparable U = T>
  [[nodiscard]] friend auto operator<=>(stable_vector const& a, stable_vector const& b) noexcept
    -> std::compare_three_way_result_t<U> {
    return std::lexicographical_compare_three_way(a.begin(), a.end(), b.begin(), b.end());
  }
};

}  // namespace nexenne::container
