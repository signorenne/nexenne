#pragma once

/**
 * @file
 * @brief Fixed-capacity circular FIFO queue with inline storage.
 *
 * \c ring_buffer<T, N> stores up to \p N values in an inline array of slots,
 * using a logical \c [head, tail) window that wraps around. It never allocates.
 * It makes the producer/consumer overflow trade explicit: a push into a full
 * buffer either fails (\c push) or evicts the oldest element
 * (\c push_overwrite).
 *
 * Reach for it for bounded event queues (\c push fails when full so the
 * producer can sleep or drop), rolling windows of the most recent \p N samples
 * (\c push_overwrite), command history, and rolling statistics. Every operation
 * is \c noexcept and there is no allocation. Construction, the pushes, pops, and
 * element access are \c constexpr, so a \c ring_buffer can also be driven at
 * compile time. Iteration walks the live elements in FIFO order from the front.
 */

#include <array>
#include <concepts>
#include <cstddef>
#include <expected>
#include <iterator>
#include <memory>
#include <type_traits>
#include <utility>

#include <nexenne/container/error.hpp>

namespace nexenne::container {

/**
 * @brief Fixed-capacity, heap-free circular FIFO.
 *
 * @tparam T Element type; must be move-constructible.
 * @tparam N Capacity, fixed at compile time; must be at least one.
 *
 * @pre None.
 * @post A default-constructed buffer is empty.
 */
template <std::move_constructible T, std::size_t N>
  requires(N >= 1)
class ring_buffer {
public:
  using value_type = T;
  using size_type = std::size_t;
  using reference = T&;
  using const_reference = T const&;
  using pointer = T*;
  using const_pointer = T const*;

private:
  // A slot holds at most one live T. The trivial default member means
  // constructing the storage array constructs no T; lifetimes are managed by
  // hand. A union (rather than reinterpret_cast over bytes) keeps every element
  // operation usable in a constant expression.
  union slot {
    unsigned char none;
    T value;

    constexpr slot() noexcept : none{} {}

    constexpr ~slot() noexcept {}
  };

  std::array<slot, N> m_slots{};
  size_type m_head{};  // index of the oldest live element
  size_type m_tail{};  // index the next push fills
  size_type m_size{};

  [[nodiscard]] constexpr auto value_ptr(size_type const index) noexcept -> T* {
    return std::addressof(m_slots[index].value);
  }

  [[nodiscard]] constexpr auto value_ptr(size_type const index) const noexcept -> T const* {
    return std::addressof(m_slots[index].value);
  }

  // Reduces an index in [0, 2N) to [0, N) without a modulo: a power-of-two N
  // masks (a single AND), any other N uses a compare-subtract. Both keep the
  // hot push/pop/index paths off the division unit.
  static constexpr auto wrap(size_type const index) noexcept -> size_type {
    if constexpr ((N & (N - 1)) == 0) {
      return index & (N - 1);
    } else {
      return index >= N ? index - N : index;
    }
  }

  static constexpr auto advance(size_type const index) noexcept -> size_type {
    return wrap(index + 1);
  }

  template <typename... Args>
  constexpr auto emplace_overwrite(Args&&... args) noexcept -> void {
    if (m_size == N) {
      // Full: m_tail == m_head, so this slot holds the oldest element.
      std::destroy_at(value_ptr(m_tail));
      std::construct_at(value_ptr(m_tail), std::forward<Args>(args)...);
      m_head = advance(m_head);
      m_tail = advance(m_tail);
    } else {
      std::construct_at(value_ptr(m_tail), std::forward<Args>(args)...);
      m_tail = advance(m_tail);
      ++m_size;
    }
  }

  template <bool IsConst>
  class basic_iterator {
  public:
    using value_type = T;
    using reference = std::conditional_t<IsConst, T const&, T&>;
    using pointer = std::conditional_t<IsConst, T const*, T*>;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::forward_iterator_tag;
    using iterator_concept = std::forward_iterator_tag;

  private:
    using slot_ptr = std::conditional_t<IsConst, slot const*, slot*>;
    slot_ptr m_slots{};
    size_type m_head{};
    size_type m_offset{};

  public:
    constexpr basic_iterator() noexcept = default;

    constexpr basic_iterator(slot_ptr slots, size_type const head, size_type const offset) noexcept
        : m_slots{slots}, m_head{head}, m_offset{offset} {}

    template <bool OtherConst>
      requires(IsConst && !OtherConst)
    constexpr basic_iterator(basic_iterator<OtherConst> const& other) noexcept
        : m_slots{other.m_slots}, m_head{other.m_head}, m_offset{other.m_offset} {}

    [[nodiscard]] constexpr auto operator*() const noexcept -> reference {
      return m_slots[wrap(m_head + m_offset)].value;
    }

    [[nodiscard]] constexpr auto operator->() const noexcept -> pointer {
      return std::addressof(m_slots[wrap(m_head + m_offset)].value);
    }

    constexpr auto operator++() noexcept -> basic_iterator& {
      ++m_offset;
      return *this;
    }

    constexpr auto operator++(int) noexcept -> basic_iterator {
      auto const previous{*this};
      ++*this;
      return previous;
    }

    [[nodiscard]] friend constexpr auto
    operator==(basic_iterator const& a, basic_iterator const& b) noexcept -> bool {
      return a.m_slots == b.m_slots && a.m_head == b.m_head && a.m_offset == b.m_offset;
    }

    template <bool>
    friend class basic_iterator;
  };

public:
  using iterator = basic_iterator<false>;
  using const_iterator = basic_iterator<true>;

  /**
   * @brief Constructs an empty buffer.
   *
   * @pre None.
   * @post \c empty() is \c true.
   */
  constexpr ring_buffer() noexcept = default;

  /**
   * @brief Copy-constructs from \p other, preserving FIFO order.
   *
   * @param other Source buffer to copy.
   *
   * @pre None.
   * @post This buffer holds copies of \p other's elements in the same order;
   *       \p other is unchanged.
   *
   * @complexity \c O(size).
   */
  constexpr ring_buffer(ring_buffer const& other) noexcept {
    for (auto const& value : other) {
      push(value);
    }
  }

  /**
   * @brief Move-constructs from \p other, leaving it empty.
   *
   * @param other Source buffer, emptied after the move.
   *
   * @pre None.
   * @post This buffer holds \p other's former elements in FIFO order; \p other
   *       is empty.
   *
   * @complexity \c O(size).
   */
  constexpr ring_buffer(ring_buffer&& other) noexcept {
    move_from(other);
  }

  /**
   * @brief Copy-assigns from \p other, replacing the current contents.
   *
   * @param other Source buffer to copy.
   *
   * @return Reference to \c *this.
   *
   * @pre None.
   * @post This buffer holds copies of \p other's elements in FIFO order.
   *
   * @complexity \c O(size).
   */
  constexpr auto operator=(ring_buffer const& other) noexcept -> ring_buffer& {
    if (this != &other) {
      clear();
      for (auto const& value : other) {
        push(value);
      }
    }
    return *this;
  }

  /**
   * @brief Move-assigns from \p other, replacing the current contents.
   *
   * @param other Source buffer, emptied after the move.
   *
   * @return Reference to \c *this.
   *
   * @pre None.
   * @post This buffer holds \p other's former elements; \p other is empty.
   *
   * @complexity \c O(size).
   */
  constexpr auto operator=(ring_buffer&& other) noexcept -> ring_buffer& {
    if (this != &other) {
      clear();
      move_from(other);
    }
    return *this;
  }

  constexpr ~ring_buffer() noexcept {
    clear();
  }

private:
  // Moves other's elements into a freshly-cleared *this, canonicalising head to
  // zero, and leaves other empty.
  constexpr auto move_from(ring_buffer& other) noexcept -> void {
    auto src{other.m_head};
    for (size_type i{0}; i < other.m_size; ++i) {
      std::construct_at(value_ptr(i), std::move(*other.value_ptr(src)));
      std::destroy_at(other.value_ptr(src));
      src = other.advance(src);
    }
    m_head = 0;
    m_tail = wrap(other.m_size);
    m_size = other.m_size;
    other.m_head = 0;
    other.m_tail = 0;
    other.m_size = 0;
  }

public:
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
   * @brief Reports whether the buffer holds no elements.
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
   * @brief Reports whether the buffer is at capacity.
   *
   * @return \c true when \c size() equals \p N.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto full() const noexcept -> bool {
    return m_size == N;
  }

  /**
   * @brief The fixed capacity.
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
   * @brief The largest number of elements the buffer can ever hold.
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
   * @brief Swaps contents with \p other.
   *
   * @param other Buffer to exchange state with.
   *
   * @pre None.
   * @post This buffer holds \p other's former elements and vice versa.
   *
   * @complexity \c O(N).
   */
  constexpr auto swap(ring_buffer& other) noexcept -> void {
    if (this == &other) {
      return;
    }
    auto temp{std::move(*this)};
    *this = std::move(other);
    other = std::move(temp);
  }

  /**
   * @brief Swaps the contents of \p a and \p b.
   *
   * @param a First buffer.
   * @param b Second buffer.
   *
   * @pre None.
   * @post \p a and \p b have exchanged state.
   */
  friend constexpr auto swap(ring_buffer& a, ring_buffer& b) noexcept -> void {
    a.swap(b);
  }

  /**
   * @brief Destroys every element; capacity stays \p N.
   *
   * @pre None.
   * @post \c empty() is \c true.
   */
  constexpr auto clear() noexcept -> void {
    auto index{m_head};
    for (size_type i{0}; i < m_size; ++i) {
      std::destroy_at(value_ptr(index));
      index = advance(index);
    }
    m_head = 0;
    m_tail = 0;
    m_size = 0;
  }

  /**
   * @brief Pushes a copy of \p value onto the back.
   *
   * @param value Value to copy in.
   *
   * @return Nothing on success, or \c container_error::full when at capacity.
   *
   * @pre None.
   * @post On success \c size() grew by one and \p value is the new back; on
   *       failure the buffer is unchanged.
   *
   * @complexity \c O(1).
   */
  constexpr auto push(T const& value) noexcept -> result<void> {
    return emplace(value);
  }

  /**
   * @brief Pushes \p value onto the back by moving it.
   *
   * @param value Value to move in.
   *
   * @return Nothing on success, or \c container_error::full when at capacity.
   *
   * @pre None.
   * @post On success \c size() grew by one and \p value was moved into the new
   *       back; on failure the buffer is unchanged.
   *
   * @complexity \c O(1).
   */
  constexpr auto push(T&& value) noexcept -> result<void> {
    return emplace(std::move(value));
  }

  /**
   * @brief Constructs an element in place at the back.
   *
   * @tparam Args Constructor argument types.
   * @param args Arguments forwarded to \p T's constructor.
   *
   * @return Nothing on success, or \c container_error::full when at capacity.
   *
   * @pre None.
   * @post On success \c size() grew by one; on failure the buffer is unchanged.
   *
   * @complexity \c O(1).
   */
  template <typename... Args>
    requires std::constructible_from<T, Args...>
  constexpr auto emplace(Args&&... args) noexcept -> result<void> {
    if (m_size == N) {
      return std::unexpected{container_error::full};
    }
    std::construct_at(value_ptr(m_tail), std::forward<Args>(args)...);
    m_tail = advance(m_tail);
    ++m_size;
    return {};
  }

  /**
   * @brief Pushes a copy of \p value, evicting the oldest element when full.
   *
   * @param value Value to copy in.
   *
   * @pre None.
   * @post \p value is the new back; when the buffer was full the oldest element
   *       was dropped and \c size() is unchanged, otherwise \c size() grew by
   *       one.
   *
   * @complexity \c O(1).
   */
  constexpr auto push_overwrite(T const& value) noexcept -> void {
    emplace_overwrite(value);
  }

  /**
   * @brief Pushes \p value by moving it, evicting the oldest element when full.
   *
   * @param value Value to move in.
   *
   * @pre None.
   * @post \p value is the new back; when the buffer was full the oldest element
   *       was dropped and \c size() is unchanged, otherwise \c size() grew by
   *       one.
   *
   * @complexity \c O(1).
   */
  constexpr auto push_overwrite(T&& value) noexcept -> void {
    emplace_overwrite(std::move(value));
  }

  /**
   * @brief Pops and returns the oldest element.
   *
   * @return The popped value, or \c container_error::empty when empty.
   *
   * @pre None.
   * @post On success \c size() shrank by one and the former front was
   *       destroyed; on failure the buffer is unchanged.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] constexpr auto pop() noexcept -> result<T> {
    if (m_size == 0) {
      return std::unexpected{container_error::empty};
    }
    result<T> value{std::move(*value_ptr(m_head))};
    std::destroy_at(value_ptr(m_head));
    m_head = advance(m_head);
    --m_size;
    return value;
  }

  /**
   * @brief The oldest element (next to be popped).
   *
   * @return Pointer to the front, or \c nullptr when empty.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto front() noexcept -> T* {
    return m_size == 0 ? nullptr : value_ptr(m_head);
  }

  /// @copydoc front()
  [[nodiscard]] constexpr auto front() const noexcept -> T const* {
    return m_size == 0 ? nullptr : value_ptr(m_head);
  }

  /**
   * @brief The most recently pushed element.
   *
   * @return Pointer to the back, or \c nullptr when empty.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto back() noexcept -> T* {
    return m_size == 0 ? nullptr : value_ptr(wrap(m_tail + N - 1));
  }

  /// @copydoc back()
  [[nodiscard]] constexpr auto back() const noexcept -> T const* {
    return m_size == 0 ? nullptr : value_ptr(wrap(m_tail + N - 1));
  }

  /**
   * @brief Unchecked logical-index access from the front (\c [0] is oldest).
   *
   * @param index Logical index from the front.
   *
   * @return Reference to the element.
   *
   * @pre \p index is less than \c size(); a larger index is undefined
   *      behaviour. Use \c at for a checked lookup.
   * @post None.
   */
  [[nodiscard]] constexpr auto operator[](size_type const index) noexcept -> T& {
    return *value_ptr(wrap(m_head + index));
  }

  /**
   * @brief Unchecked logical-index access from the front (const overload).
   *
   * @param index Logical index from the front.
   *
   * @return Const reference to the element.
   *
   * @pre \p index is less than \c size(); a larger index is undefined
   *      behaviour. Use \c at for a checked lookup.
   * @post None.
   */
  [[nodiscard]] constexpr auto operator[](size_type const index) const noexcept -> T const& {
    return *value_ptr(wrap(m_head + index));
  }

  /**
   * @brief Checked logical-index access from the front.
   *
   * @param index Logical index from the front.
   *
   * @return Pointer to the element, or \c nullptr when \p index is out of range.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto at(size_type const index) noexcept -> T* {
    return index < m_size ? value_ptr(wrap(m_head + index)) : nullptr;
  }

  /**
   * @brief Checked logical-index access from the front (const overload).
   *
   * @param index Logical index from the front.
   *
   * @return Const pointer to the element, or \c nullptr when out of range.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto at(size_type const index) const noexcept -> T const* {
    return index < m_size ? value_ptr(wrap(m_head + index)) : nullptr;
  }

  /**
   * @brief Iterator to the oldest element, walking in FIFO order.
   *
   * @return Iterator to the front, or \c end() when empty.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto begin() noexcept -> iterator {
    return iterator{m_slots.data(), m_head, 0};
  }

  /**
   * @brief Iterator one past the most recently pushed element.
   *
   * @return The past-the-end iterator.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto end() noexcept -> iterator {
    return iterator{m_slots.data(), m_head, m_size};
  }

  /// @copydoc begin()
  [[nodiscard]] constexpr auto begin() const noexcept -> const_iterator {
    return const_iterator{m_slots.data(), m_head, 0};
  }

  /// @copydoc end()
  [[nodiscard]] constexpr auto end() const noexcept -> const_iterator {
    return const_iterator{m_slots.data(), m_head, m_size};
  }

  /// @copydoc begin()
  [[nodiscard]] constexpr auto cbegin() const noexcept -> const_iterator {
    return begin();
  }

  /// @copydoc end()
  [[nodiscard]] constexpr auto cend() const noexcept -> const_iterator {
    return end();
  }
};

}  // namespace nexenne::container
