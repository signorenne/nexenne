#pragma once

/**
 * @file
 * @brief Growable double-ended queue: a contiguous power-of-two ring.
 *
 * \c deque<T> is a double-ended queue backed by a single contiguous,
 * power-of-two buffer addressed as a ring. Push and pop at either end are
 * amortised \c O(1), and indexed access is \c O(1) and cache-friendly. When the
 * buffer fills it doubles and re-packs the elements (with the front at index 0),
 * so unlike \c ring_buffer the capacity is not fixed, and unlike \c std::deque
 * the storage is one block rather than a map of segments (faster random access,
 * but a push that grows invalidates references).
 *
 * Reach for it as a work queue or sliding window where you add and remove at
 * both ends and want contiguous random access. It is copyable when \p T is (a
 * deep copy that re-packs from the front) and always movable. Allocation uses
 * the over-aligned global allocation function, so an over-aligned \p T is handled
 * correctly. Every operation is \c noexcept: a boundary failure (pop from empty)
 * returns \c result, and allocation failure terminates. It is not thread-safe.
 */

#include <algorithm>
#include <array>
#include <bit>
#include <compare>
#include <concepts>
#include <cstddef>
#include <exception>
#include <expected>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <utility>

#include <nexenne/container/error.hpp>

namespace nexenne::container {

/**
 * @brief Growable double-ended queue over a power-of-two ring buffer.
 *
 * @tparam T Element type; must be move-constructible.
 *
 * @pre None.
 * @post A default-constructed deque is empty with no allocated storage.
 */
template <std::move_constructible T>
class deque {
public:
  using value_type = T;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using reference = T&;
  using const_reference = T const&;
  using pointer = T*;
  using const_pointer = T const*;

private:
  T* m_data{nullptr};
  size_type m_cap{0};
  size_type m_head{0};
  size_type m_size{0};

  /**
   * @brief Physical slot backing logical index \p i, with \c 0 the front.
   *
   * \c m_cap is a power of two, so the wrap is a bitmask. When \c m_cap is zero
   * the deque is empty and this helper is unused.
   *
   * @param i Logical index from the front.
   *
   * @return The physical buffer index of logical element \p i.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto slot_of(size_type const i) const noexcept -> size_type {
    return (m_head + i) & (m_cap - 1);
  }

  /**
   * @brief Grows to hold at least \p want elements, re-packing from the front.
   *
   * Rounds \p want up to a power of two and re-packs the elements with the front
   * at index \c 0. A request at or below the current capacity is a no-op, and an
   * unsatisfiable request terminates.
   *
   * @param want Minimum number of elements the buffer must hold.
   *
   * @pre None.
   * @post \c capacity() is at least \p want rounded up to a power of two, or the
   *       process terminated.
   */
  auto grow(size_type const want) noexcept -> void {
    if (want <= m_cap) {
      return;
    }
    // std::bit_ceil is undefined when the rounded-up power of two is not
    // representable (want past 2^63 on a 64-bit size_type), and even a
    // representable new_cap must satisfy new_cap * sizeof(T) <= SIZE_MAX or the
    // byte count wraps into an undersized allocation. Both are unsatisfiable
    // requests, so fail loudly rather than silently corrupt the heap.
    if (want > max_size()) {
      std::terminate();
    }
    auto const new_cap{std::bit_ceil(want)};
    if (new_cap > max_size()) {
      std::terminate();
    }
    auto* const new_data{
      static_cast<T*>(::operator new(sizeof(T) * new_cap, std::align_val_t{alignof(T)}))
    };
    for (size_type i{0}; i < m_size; ++i) {
      auto* const old_slot{m_data + slot_of(i)};
      std::construct_at(new_data + i, std::move(*old_slot));
      std::destroy_at(old_slot);
    }
    if (m_data != nullptr) {
      ::operator delete(m_data, std::align_val_t{alignof(T)});
    }
    m_data = new_data;
    m_cap = new_cap;
    m_head = 0;
  }

  /**
   * @brief The next capacity for a one-past-full growth.
   *
   * Starts at \c 8 for an empty buffer and doubles otherwise, keeping the
   * capacity a power of two.
   *
   * @param cap The current capacity.
   *
   * @return The capacity to grow to on the next push.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] static auto grown_capacity(size_type const cap) noexcept -> size_type {
    return cap == 0 ? 8 : cap * 2;
  }

  // Random-access iterator over the logical sequence. It holds the owning deque
  // plus a logical position (0 is the front) and maps to a physical slot through
  // slot_of, so it walks the masked ring in front-to-back order. Like the sibling
  // owner+index iterators it follows the container object: a container move
  // invalidates outstanding iterators and a swap retargets them.
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
    using owner_ptr = std::conditional_t<IsConst, deque const*, deque*>;
    owner_ptr m_owner{nullptr};
    size_type m_pos{0};

    /**
     * @brief Pointer to the element at logical position \p pos.
     *
     * @param pos Logical position from the front.
     *
     * @return Pointer to the element backing logical position \p pos.
     *
     * @pre None.
     * @post None.
     */
    [[nodiscard]] auto element(size_type const pos) const noexcept -> pointer {
      return m_owner->m_data + m_owner->slot_of(pos);
    }

  public:
    /**
     * @brief Constructs a singular iterator that refers to no deque.
     *
     * @pre None.
     * @post The iterator has no owning deque and must be assigned before use.
     */
    basic_iterator() noexcept = default;

    /**
     * @brief Constructs an iterator over \p owner at logical position \p pos.
     *
     * @param owner The deque the iterator walks.
     * @param pos Logical position from the front.
     *
     * @pre None.
     * @post The iterator refers to \p owner at logical position \p pos.
     */
    basic_iterator(owner_ptr const owner, size_type const pos) noexcept
        : m_owner{owner}, m_pos{pos} {}

    /**
     * @brief Converts a mutable iterator to a const iterator.
     *
     * @tparam OtherConst Constness of the source iterator; the overload is
     *                    viable only when converting mutable to const.
     * @param other The mutable iterator to convert.
     *
     * @pre None.
     * @post The iterator refers to the same deque and logical position as
     *       \p other.
     */
    template <bool OtherConst>
      requires(IsConst && !OtherConst)
    basic_iterator(basic_iterator<OtherConst> const& other) noexcept
        : m_owner{other.m_owner}, m_pos{other.m_pos} {}

    /**
     * @brief Dereferences the iterator.
     *
     * @return Reference to the element at the current position.
     *
     * @pre The iterator refers to a valid element, not \c end().
     * @post None.
     */
    [[nodiscard]] auto operator*() const noexcept -> reference {
      return *element(m_pos);
    }

    /**
     * @brief Member access through the iterator.
     *
     * @return Pointer to the element at the current position.
     *
     * @pre The iterator refers to a valid element, not \c end().
     * @post None.
     */
    [[nodiscard]] auto operator->() const noexcept -> pointer {
      return element(m_pos);
    }

    /**
     * @brief Access the element \p n positions from the current one.
     *
     * @param n Signed offset from the current position.
     *
     * @return Reference to the element at the offset position.
     *
     * @pre The offset position refers to a valid element.
     * @post None.
     */
    [[nodiscard]] auto operator[](difference_type const n) const noexcept -> reference {
      return *element(static_cast<size_type>(static_cast<difference_type>(m_pos) + n));
    }

    /**
     * @brief Advances the iterator to the next element.
     *
     * @return Reference to this iterator after advancing.
     *
     * @pre None.
     * @post The iterator refers to the next logical position.
     */
    auto operator++() noexcept -> basic_iterator& {
      ++m_pos;
      return *this;
    }

    /**
     * @brief Moves the iterator to the previous element.
     *
     * @return Reference to this iterator after moving back.
     *
     * @pre None.
     * @post The iterator refers to the previous logical position.
     */
    auto operator--() noexcept -> basic_iterator& {
      --m_pos;
      return *this;
    }

    /**
     * @brief Advances the iterator, returning its prior value.
     *
     * @return A copy of the iterator before advancing.
     *
     * @pre None.
     * @post The iterator refers to the next logical position.
     */
    auto operator++(int) noexcept -> basic_iterator {
      auto previous{*this};
      ++m_pos;
      return previous;
    }

    /**
     * @brief Moves the iterator back, returning its prior value.
     *
     * @return A copy of the iterator before moving back.
     *
     * @pre None.
     * @post The iterator refers to the previous logical position.
     */
    auto operator--(int) noexcept -> basic_iterator {
      auto previous{*this};
      --m_pos;
      return previous;
    }

    /**
     * @brief Advances the iterator by \p n positions.
     *
     * @param n Signed number of positions to advance.
     *
     * @return Reference to this iterator after advancing.
     *
     * @pre None.
     * @post The iterator moved \p n logical positions forward.
     */
    auto operator+=(difference_type const n) noexcept -> basic_iterator& {
      m_pos = static_cast<size_type>(static_cast<difference_type>(m_pos) + n);
      return *this;
    }

    /**
     * @brief Moves the iterator back by \p n positions.
     *
     * @param n Signed number of positions to move back.
     *
     * @return Reference to this iterator after moving back.
     *
     * @pre None.
     * @post The iterator moved \p n logical positions backward.
     */
    auto operator-=(difference_type const n) noexcept -> basic_iterator& {
      return *this += -n;
    }

    /**
     * @brief Returns the iterator advanced by \p n positions.
     *
     * @param it The iterator to advance.
     * @param n Signed number of positions to advance.
     *
     * @return A copy of \p it advanced by \p n positions.
     *
     * @pre None.
     * @post None.
     */
    [[nodiscard]] friend auto
    operator+(basic_iterator it, difference_type const n) noexcept -> basic_iterator {
      it += n;
      return it;
    }

    /**
     * @brief Returns the iterator advanced by \p n positions.
     *
     * @param n Signed number of positions to advance.
     * @param it The iterator to advance.
     *
     * @return A copy of \p it advanced by \p n positions.
     *
     * @pre None.
     * @post None.
     */
    [[nodiscard]] friend auto
    operator+(difference_type const n, basic_iterator it) noexcept -> basic_iterator {
      it += n;
      return it;
    }

    /**
     * @brief Returns the iterator moved back by \p n positions.
     *
     * @param it The iterator to move back.
     * @param n Signed number of positions to move back.
     *
     * @return A copy of \p it moved back by \p n positions.
     *
     * @pre None.
     * @post None.
     */
    [[nodiscard]] friend auto
    operator-(basic_iterator it, difference_type const n) noexcept -> basic_iterator {
      it -= n;
      return it;
    }

    /**
     * @brief The signed distance between two iterators.
     *
     * @param a The left iterator.
     * @param b The right iterator.
     *
     * @return The number of positions from \p b to \p a.
     *
     * @pre \p a and \p b refer to the same deque.
     * @post None.
     */
    [[nodiscard]] friend auto
    operator-(basic_iterator const& a, basic_iterator const& b) noexcept -> difference_type {
      return static_cast<difference_type>(a.m_pos) - static_cast<difference_type>(b.m_pos);
    }

    /**
     * @brief Equality: the iterators are at the same position.
     *
     * @param a The left iterator.
     * @param b The right iterator.
     *
     * @return \c true when both are at the same logical position.
     *
     * @pre \p a and \p b refer to the same deque.
     * @post None.
     */
    [[nodiscard]] friend auto
    operator==(basic_iterator const& a, basic_iterator const& b) noexcept -> bool {
      return a.m_pos == b.m_pos;
    }

    /**
     * @brief Three-way comparison of iterator positions.
     *
     * @param a The left iterator.
     * @param b The right iterator.
     *
     * @return The ordering of the two logical positions.
     *
     * @pre \p a and \p b refer to the same deque.
     * @post None.
     */
    [[nodiscard]] friend auto
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
   * @brief Constructs an empty deque with no allocated storage.
   *
   * @pre None.
   * @post \c empty() is \c true and \c capacity() is zero.
   */
  deque() noexcept = default;

  /**
   * @brief Constructs an empty deque with storage for at least \p initial_cap
   *        elements.
   *
   * @param initial_cap Minimum number of elements to reserve.
   *
   * @pre None.
   * @post \c empty() is \c true and \c capacity() is at least \p initial_cap
   *       (rounded up to a power of two).
   */
  explicit deque(size_type const initial_cap) noexcept {
    if (initial_cap > 0) {
      grow(initial_cap);
    }
  }

  /**
   * @brief Constructs a deque holding the elements of \p init, front to back.
   *
   * @param init Elements to copy in.
   *
   * @pre None.
   * @post \c size() equals \c init.size() and ordering matches \p init.
   */
  deque(std::initializer_list<T> const init) noexcept {
    if (init.size() > 0) {
      grow(init.size());
      for (auto const& value : init) {
        push_back(value);
      }
    }
  }

  /**
   * @brief Deep-copies \p other, re-packing the copy from the front.
   *
   * @param other Source deque to copy.
   *
   * @pre None.
   * @post This deque holds copies of \p other's elements in the same order.
   */
  deque(deque const& other) noexcept
    requires std::copy_constructible<T>
  {
    if (other.m_size > 0) {
      grow(other.m_size);
      for (size_type i{0}; i < other.m_size; ++i) {
        std::construct_at(m_data + i, other[i]);
      }
      m_size = other.m_size;
    }
  }

  /**
   * @brief Move-constructs from \p other, taking its storage.
   *
   * @param other Source deque, left empty.
   *
   * @pre None.
   * @post This deque owns \p other's former elements; \p other is empty with
   *       zero capacity.
   *
   * @note Iterators into \p other are invalidated by the move; they follow the
   *       deque object, not the elements.
   */
  deque(deque&& other) noexcept
      : m_data{other.m_data}, m_cap{other.m_cap}, m_head{other.m_head}, m_size{other.m_size} {
    other.m_data = nullptr;
    other.m_cap = 0;
    other.m_head = 0;
    other.m_size = 0;
  }

  /**
   * @brief Copy-and-swap assignment from \p other (copy or move).
   *
   * @param other Source deque, taken by value so it is copy- or
   *              move-constructed at the call site.
   *
   * @return Reference to this deque.
   *
   * @pre None.
   * @post This deque holds \p other's elements; the prior contents are
   *       released. Self-assignment is safe.
   */
  auto operator=(deque other) noexcept -> deque& {
    swap(other);
    return *this;
  }

  /**
   * @brief Destroys every element and frees the storage.
   *
   * @pre None.
   * @post None.
   */
  ~deque() noexcept {
    clear();
    if (m_data != nullptr) {
      ::operator delete(m_data, std::align_val_t{alignof(T)});
    }
  }

  /**
   * @brief Swaps contents with \p other in constant time.
   *
   * @param other Deque to exchange state with.
   *
   * @pre None.
   * @post This deque and \p other have exchanged elements and storage.
   *
   * @note Outstanding iterators retarget on swap: they keep their owner and
   *       logical position, so they now refer to the other deque's contents.
   *
   * @complexity \c O(1).
   */
  auto swap(deque& other) noexcept -> void {
    using std::swap;
    swap(m_data, other.m_data);
    swap(m_cap, other.m_cap);
    swap(m_head, other.m_head);
    swap(m_size, other.m_size);
  }

  /**
   * @brief Swaps the contents of \p a and \p b.
   *
   * @param a First deque.
   * @param b Second deque.
   *
   * @pre None.
   * @post \p a and \p b have exchanged state.
   */
  friend auto swap(deque& a, deque& b) noexcept -> void {
    a.swap(b);
  }

  /**
   * @brief Number of elements currently stored.
   *
   * @return The element count.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto size() const noexcept -> size_type {
    return m_size;
  }

  /**
   * @brief Elements that fit without reallocating.
   *
   * @return The capacity, a power of two or zero.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto capacity() const noexcept -> size_type {
    return m_cap;
  }

  /**
   * @brief The largest number of elements the deque can hold.
   *
   * The bound is the smaller of the element count an allocation can address
   * (\c SIZE_MAX / \c sizeof(T)) and the largest power-of-two capacity
   * \c std::bit_ceil can produce (\c 2^63 on a 64-bit \c size_type), so a
   * request past it terminates rather than wrapping the byte count.
   *
   * @return The maximum element count.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] static constexpr auto max_size() noexcept -> size_type {
    constexpr auto byte_bound{std::numeric_limits<size_type>::max() / sizeof(T)};
    constexpr auto ceil_bound{size_type{1} << (std::numeric_limits<size_type>::digits - 1)};
    return byte_bound < ceil_bound ? byte_bound : ceil_bound;
  }

  /**
   * @brief Reports whether the deque holds no elements.
   *
   * @return \c true when \c size() is zero.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto empty() const noexcept -> bool {
    return m_size == 0;
  }

  /**
   * @brief Reserves storage for at least \p n elements.
   *
   * @param n Minimum capacity to ensure.
   *
   * @pre None.
   * @post \c capacity() is at least \p n; references are invalidated if a
   *       reallocation occurred.
   */
  auto reserve(size_type const n) noexcept -> void {
    grow(n);
  }

  /**
   * @brief Pointer to the front (oldest) element, or \c nullptr when empty.
   *
   * @return A pointer to the front element, or \c nullptr.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto front() noexcept -> T* {
    return m_size == 0 ? nullptr : m_data + m_head;
  }

  /// @copydoc front()
  [[nodiscard]] auto front() const noexcept -> T const* {
    return m_size == 0 ? nullptr : m_data + m_head;
  }

  /**
   * @brief Pointer to the back (newest) element, or \c nullptr when empty.
   *
   * @return A pointer to the back element, or \c nullptr.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto back() noexcept -> T* {
    return m_size == 0 ? nullptr : m_data + slot_of(m_size - 1);
  }

  /// @copydoc back()
  [[nodiscard]] auto back() const noexcept -> T const* {
    return m_size == 0 ? nullptr : m_data + slot_of(m_size - 1);
  }

  /**
   * @brief Unchecked indexed access, \c 0 being the front.
   *
   * @param i Logical index from the front.
   *
   * @return Reference to the element at logical index \p i.
   *
   * @pre \p i is less than \c size().
   * @post None.
   */
  [[nodiscard]] auto operator[](size_type const i) noexcept -> reference {
    return m_data[slot_of(i)];
  }

  /**
   * @brief Unchecked indexed access (const overload).
   *
   * @param i Logical index from the front.
   *
   * @return Const reference to the element at logical index \p i.
   *
   * @pre \p i is less than \c size().
   * @post None.
   */
  [[nodiscard]] auto operator[](size_type const i) const noexcept -> const_reference {
    return m_data[slot_of(i)];
  }

  /**
   * @brief Checked indexed access, \c 0 being the front.
   *
   * @param i Logical index from the front.
   *
   * @return Pointer to the element at logical index \p i, or
   *         \c container_error::out_of_range when \p i is not less than
   *         \c size().
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto at(size_type const i) noexcept -> result<T*> {
    if (i >= m_size) {
      return std::unexpected{container_error::out_of_range};
    }
    return m_data + slot_of(i);
  }

  /**
   * @brief Checked indexed access (const overload).
   *
   * @param i Logical index from the front.
   *
   * @return Const pointer to the element at logical index \p i, or
   *         \c container_error::out_of_range when \p i is not less than
   *         \c size().
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto at(size_type const i) const noexcept -> result<T const*> {
    if (i >= m_size) {
      return std::unexpected{container_error::out_of_range};
    }
    return m_data + slot_of(i);
  }

  /**
   * @brief Constructs an element in place at the back, growing if needed.
   *
   * @tparam Args Constructor argument types.
   * @param args Arguments forwarded to \p T's constructor.
   *
   * @return Reference to the new back element.
   *
   * @pre None.
   * @post \c size() grew by one; references are invalidated if a reallocation
   *       occurred.
   *
   * @complexity Amortised \c O(1).
   */
  template <typename... Args>
    requires std::constructible_from<T, Args...>
  auto emplace_back(Args&&... args) noexcept -> reference {
    if (m_size >= m_cap) {
      // Cold grow path. Stage the element in raw storage with the same
      // direct-initialization semantics as the in-capacity path's
      // std::construct_at (parenthesized, not braced), so an
      // initializer_list-greedy or narrowing-convertible argument yields an
      // identical element on both paths (a fresh deque has capacity 0, so the
      // very first emplace runs here). Staging before grow frees the old buffer
      // also keeps an argument aliasing an existing element (push_back(d[0]))
      // valid across the reallocation.
      alignas(T) std::array<std::byte, sizeof(T)> staging{};
      auto* const staged{
        std::construct_at(reinterpret_cast<T*>(staging.data()), std::forward<Args>(args)...)
      };
      grow(grown_capacity(m_cap));
      auto* const target{m_data + slot_of(m_size)};
      std::construct_at(target, std::move(*staged));
      std::destroy_at(staged);
      ++m_size;
      return *target;
    }
    auto* const target{m_data + slot_of(m_size)};
    std::construct_at(target, std::forward<Args>(args)...);
    ++m_size;
    return *target;
  }

  /**
   * @brief Constructs an element in place at the front, growing if needed.
   *
   * @tparam Args Constructor argument types.
   * @param args Arguments forwarded to \p T's constructor.
   *
   * @return Reference to the new front element.
   *
   * @pre None.
   * @post \c size() grew by one; references are invalidated if a reallocation
   *       occurred.
   *
   * @complexity Amortised \c O(1).
   */
  template <typename... Args>
    requires std::constructible_from<T, Args...>
  auto emplace_front(Args&&... args) noexcept -> reference {
    if (m_size >= m_cap) {
      // Cold grow path. Stage with std::construct_at semantics (see emplace_back)
      // so both paths build an identical element, and so an argument aliasing an
      // existing element (push_front(d[0])) survives the reallocation.
      alignas(T) std::array<std::byte, sizeof(T)> staging{};
      auto* const staged{
        std::construct_at(reinterpret_cast<T*>(staging.data()), std::forward<Args>(args)...)
      };
      grow(grown_capacity(m_cap));
      m_head = (m_head + m_cap - 1) & (m_cap - 1);
      auto* const target{m_data + m_head};
      std::construct_at(target, std::move(*staged));
      std::destroy_at(staged);
      ++m_size;
      return *target;
    }
    m_head = (m_head + m_cap - 1) & (m_cap - 1);
    auto* const target{m_data + m_head};
    std::construct_at(target, std::forward<Args>(args)...);
    ++m_size;
    return *target;
  }

  /**
   * @brief Appends a copy of \p value at the back.
   *
   * @param value Element to copy in.
   *
   * @pre None.
   * @post \c size() grew by one and \p value is the new back.
   *
   * @complexity Amortised \c O(1).
   */
  auto push_back(T const& value) noexcept -> void {
    emplace_back(value);
  }

  /**
   * @brief Appends \p value at the back by moving it.
   *
   * @param value Element to move in.
   *
   * @pre None.
   * @post \c size() grew by one and \p value is the new back.
   *
   * @complexity Amortised \c O(1).
   */
  auto push_back(T&& value) noexcept -> void {
    emplace_back(std::move(value));
  }

  /**
   * @brief Prepends a copy of \p value at the front.
   *
   * @param value Element to copy in.
   *
   * @pre None.
   * @post \c size() grew by one and \p value is the new front.
   *
   * @complexity Amortised \c O(1).
   */
  auto push_front(T const& value) noexcept -> void {
    emplace_front(value);
  }

  /**
   * @brief Prepends \p value at the front by moving it.
   *
   * @param value Element to move in.
   *
   * @pre None.
   * @post \c size() grew by one and \p value is the new front.
   *
   * @complexity Amortised \c O(1).
   */
  auto push_front(T&& value) noexcept -> void {
    emplace_front(std::move(value));
  }

  /**
   * @brief Removes and returns the back element.
   *
   * @return The removed element, or \c container_error::empty when the deque is
   *         empty.
   *
   * @pre None.
   * @post On success \c size() shrank by one; on failure the deque is
   *       unchanged.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] auto pop_back() noexcept -> result<T> {
    if (m_size == 0) {
      return std::unexpected{container_error::empty};
    }
    --m_size;
    auto* const target{m_data + slot_of(m_size)};
    result<T> removed{std::move(*target)};
    std::destroy_at(target);
    return removed;
  }

  /**
   * @brief Removes and returns the front element.
   *
   * @return The removed element, or \c container_error::empty when the deque is
   *         empty.
   *
   * @pre None.
   * @post On success \c size() shrank by one; on failure the deque is
   *       unchanged.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] auto pop_front() noexcept -> result<T> {
    if (m_size == 0) {
      return std::unexpected{container_error::empty};
    }
    auto* const target{m_data + m_head};
    result<T> removed{std::move(*target)};
    std::destroy_at(target);
    m_head = (m_head + 1) & (m_cap - 1);
    --m_size;
    return removed;
  }

  /**
   * @brief Destroys every element, leaving the deque empty.
   *
   * @pre None.
   * @post \c empty() is \c true; allocated capacity is retained.
   */
  auto clear() noexcept -> void {
    for (size_type i{0}; i < m_size; ++i) {
      std::destroy_at(m_data + slot_of(i));
    }
    m_size = 0;
    m_head = 0;
  }

  /**
   * @brief Iterator to the front element, walking front to back.
   *
   * @return Iterator to the front, or \c end() when empty.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto begin() noexcept -> iterator {
    return iterator{this, 0};
  }

  /**
   * @brief Iterator one past the back element.
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
   * @brief Equality: same size and element-wise equal, front to back.
   *
   * @param a Left deque.
   * @param b Right deque.
   *
   * @return \c true when both hold equal elements in the same order.
   *
   * @pre None.
   * @post None.
   *
   * @complexity \c O(size).
   */
  [[nodiscard]] friend auto operator==(deque const& a, deque const& b) noexcept -> bool
    requires std::equality_comparable<T>
  {
    return a.m_size == b.m_size && std::equal(a.begin(), a.end(), b.begin(), b.end());
  }

  /**
   * @brief Lexicographic three-way comparison of the elements, front to back.
   *
   * @tparam U Deduced as \p T; keeps the ordering type unevaluated unless \p T
   *           is three-way comparable.
   * @param a Left deque.
   * @param b Right deque.
   *
   * @return The lexicographic ordering of the element sequences.
   *
   * @pre None.
   * @post None.
   *
   * @complexity \c O(size).
   */
  template <std::three_way_comparable U = T>
  [[nodiscard]] friend auto operator<=>(deque const& a, deque const& b) noexcept
    -> std::compare_three_way_result_t<U> {
    return std::lexicographical_compare_three_way(a.begin(), a.end(), b.begin(), b.end());
  }
};

}  // namespace nexenne::container
