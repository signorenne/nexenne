#pragma once

/**
 * @file
 * @brief Sequence with a movable insertion gap: the classic text-editor buffer.
 *
 * \c gap_buffer<T> stores a logical sequence in two contiguous regions of one
 * backing vector separated by a gap of unused slots. Insert and erase at the
 * cursor are \c O(1) because they only resize the gap; the cost is paid when the
 * cursor moves (\c O(distance) to shift elements across the gap). Random access
 * stays \c O(1) (one branch plus index arithmetic).
 *
 * \verbatim
 *   [ pre ........... ][ ......gap...... ][ ...... post ........ ]
 *   ^                  ^                  ^                       ^
 *   begin              gap_begin (cursor) gap_end                end
 * \endverbatim
 *
 * The cursor sits at the start of the gap; inserting there consumes one gap
 * slot. Reach for it for editor and REPL input buffers, undo/redo timelines, and
 * any sequence whose edits cluster around one moving point. Versus \c std::vector
 * it makes cursor-local insert/erase \c O(1) instead of \c O(n), at the cost of
 * an \c O(distance) cursor move (done once per edit session, not per edit);
 * versus a rope it is far simpler but degrades to \c O(n) when edits scatter.
 *
 * The gap holds default-constructed elements, so \p T must be default
 * constructible as well as movable. Every operation is \c noexcept; allocation
 * failure terminates.
 */

#include <algorithm>
#include <compare>
#include <concepts>
#include <cstddef>
#include <expected>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include <nexenne/container/error.hpp>

namespace nexenne::container {

/**
 * @brief Sequence with a movable insertion gap.
 *
 * @tparam T Element type; must be default-constructible and movable.
 *
 * @pre None.
 * @post A default-constructed gap_buffer is empty.
 */
template <typename T>
  requires(std::default_initializable<T> && std::movable<T>)
class gap_buffer {
public:
  using value_type = T;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using reference = T&;
  using const_reference = T const&;
  using pointer = T*;
  using const_pointer = T const*;

  static constexpr size_type initial_gap{16};

  /// @brief Copies another buffer, including its gap layout.
  constexpr gap_buffer(gap_buffer const&) = default;

  /**
   * @brief Copy-assigns another buffer, including its gap layout.
   *
   * @param other Buffer to copy.
   *
   * @return A reference to this buffer.
   *
   * @pre None.
   * @post This buffer holds a copy of \p other.
   */
  constexpr auto operator=(gap_buffer const& other) -> gap_buffer& = default;

  /**
   * @brief Moves another buffer, leaving the source a valid empty buffer.
   *
   * The defaulted move would steal \p other's vector (emptying it) while copying
   * its gap indices, leaving \p other with a gap past its now-empty storage and
   * a \c size() that underflows. This resets the moved-from indices so the source
   * stays a usable empty buffer.
   *
   * @param other Buffer to move from; left empty.
   *
   * @pre None.
   * @post \p other is empty with no gap.
   */
  constexpr gap_buffer(gap_buffer&& other) noexcept
      : m_buffer{std::move(other.m_buffer)}
      , m_gap_begin{other.m_gap_begin}
      , m_gap_end{other.m_gap_end} {
    other.m_gap_begin = 0;
    other.m_gap_end = 0;
  }

  /**
   * @brief Move-assigns another buffer, leaving the source a valid empty buffer.
   *
   * @param other Buffer to move from; left empty unless it is \c *this.
   *
   * @return A reference to this buffer.
   *
   * @pre None.
   * @post \p other is empty with no gap (unless self-assigned).
   */
  constexpr auto operator=(gap_buffer&& other) noexcept -> gap_buffer& {
    if (this == &other) {
      return *this;
    }
    m_buffer = std::move(other.m_buffer);
    m_gap_begin = other.m_gap_begin;
    m_gap_end = other.m_gap_end;
    other.m_gap_begin = 0;
    other.m_gap_end = 0;
    return *this;
  }

private:
  std::vector<T> m_buffer;
  size_type m_gap_begin{0};  // index of the first gap slot
  size_type m_gap_end{0};    // index one past the last gap slot

  [[nodiscard]] constexpr auto gap_size() const noexcept -> size_type {
    return m_gap_end - m_gap_begin;
  }

  // Logical position to physical index: positions at or past the gap skip it.
  [[nodiscard]] constexpr auto physical(size_type const logical) const noexcept -> size_type {
    return logical < m_gap_begin ? logical : logical + gap_size();
  }

  // Ensures the gap holds at least min_gap slots, shifting the post region right
  // in place (destination is strictly above the source, so move back to front).
  constexpr auto grow_gap(size_type const min_gap) noexcept -> void {
    if (gap_size() >= min_gap) {
      return;
    }
    auto const post_count{m_buffer.size() - m_gap_end};
    auto const want_gap{std::max(min_gap, gap_size() * 2 + initial_gap)};
    auto const new_size{m_gap_begin + want_gap + post_count};
    m_buffer.resize(new_size);
    for (size_type j{post_count}; j > 0; --j) {
      m_buffer[new_size - post_count + (j - 1)] = std::move(m_buffer[m_gap_end + (j - 1)]);
    }
    m_gap_end = new_size - post_count;
  }

  template <bool IsConst>
  class basic_iterator {
  private:
    using owner_type = std::conditional_t<IsConst, gap_buffer const*, gap_buffer*>;
    owner_type m_owner{nullptr};
    size_type m_pos{0};

  public:
    using value_type = T;
    using reference = std::conditional_t<IsConst, T const&, T&>;
    using pointer = std::conditional_t<IsConst, T const*, T*>;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::random_access_iterator_tag;
    using iterator_concept = std::random_access_iterator_tag;

    constexpr basic_iterator() noexcept = default;

    constexpr basic_iterator(owner_type const owner, size_type const pos) noexcept
        : m_owner{owner}, m_pos{pos} {}

    template <bool OtherConst>
      requires(IsConst && !OtherConst)
    constexpr basic_iterator(basic_iterator<OtherConst> const& other) noexcept
        : m_owner{other.m_owner}, m_pos{other.m_pos} {}

    [[nodiscard]] constexpr auto operator*() const noexcept -> reference {
      return m_owner->m_buffer[m_owner->physical(m_pos)];
    }

    [[nodiscard]] constexpr auto operator->() const noexcept -> pointer {
      return std::addressof(**this);
    }

    [[nodiscard]] constexpr auto operator[](difference_type const n) const noexcept -> reference {
      auto const at{static_cast<size_type>(static_cast<difference_type>(m_pos) + n)};
      return m_owner->m_buffer[m_owner->physical(at)];
    }

    constexpr auto operator++() noexcept -> basic_iterator& {
      ++m_pos;
      return *this;
    }

    constexpr auto operator--() noexcept -> basic_iterator& {
      --m_pos;
      return *this;
    }

    constexpr auto operator++(int) noexcept -> basic_iterator {
      auto copy{*this};
      ++m_pos;
      return copy;
    }

    constexpr auto operator--(int) noexcept -> basic_iterator {
      auto copy{*this};
      --m_pos;
      return copy;
    }

    constexpr auto operator+=(difference_type const n) noexcept -> basic_iterator& {
      m_pos = static_cast<size_type>(static_cast<difference_type>(m_pos) + n);
      return *this;
    }

    constexpr auto operator-=(difference_type const n) noexcept -> basic_iterator& {
      return *this += -n;
    }

    [[nodiscard]] friend constexpr auto
    operator+(basic_iterator it, difference_type const n) noexcept -> basic_iterator {
      it += n;
      return it;
    }

    [[nodiscard]] friend constexpr auto
    operator+(difference_type const n, basic_iterator it) noexcept -> basic_iterator {
      it += n;
      return it;
    }

    [[nodiscard]] friend constexpr auto
    operator-(basic_iterator it, difference_type const n) noexcept -> basic_iterator {
      it -= n;
      return it;
    }

    [[nodiscard]] friend constexpr auto
    operator-(basic_iterator const& a, basic_iterator const& b) noexcept -> difference_type {
      return static_cast<difference_type>(a.m_pos) - static_cast<difference_type>(b.m_pos);
    }

    [[nodiscard]] friend constexpr auto
    operator==(basic_iterator const& a, basic_iterator const& b) noexcept -> bool {
      return a.m_pos == b.m_pos;
    }

    [[nodiscard]] friend constexpr auto
    operator<=>(basic_iterator const& a, basic_iterator const& b) noexcept -> std::strong_ordering {
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
   * @brief Constructs an empty gap_buffer.
   *
   * @pre None.
   * @post \c empty() is \c true.
   */
  constexpr gap_buffer() noexcept = default;

  /**
   * @brief Constructs from an initializer list, cursor at the end.
   *
   * @param init Elements to copy in.
   *
   * @pre None.
   * @post \c size() equals \c init.size() and \c cursor() equals \c size().
   */
  constexpr gap_buffer(std::initializer_list<T> const init) noexcept {
    m_buffer.reserve(init.size() + initial_gap);
    for (auto const& value : init) {
      m_buffer.push_back(value);
    }
    m_buffer.resize(init.size() + initial_gap);
    m_gap_begin = init.size();
    m_gap_end = m_buffer.size();
  }

  /**
   * @brief Number of logical elements (excludes the gap).
   *
   * @return The element count.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto size() const noexcept -> size_type {
    return m_buffer.size() - gap_size();
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
    return size() == 0;
  }

  /**
   * @brief Elements that fit without reallocating.
   *
   * @return The backing capacity.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto capacity() const noexcept -> size_type {
    return m_buffer.capacity();
  }

  /**
   * @brief The largest number of elements the buffer can hold.
   *
   * @return The maximum size.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto max_size() const noexcept -> size_type {
    return m_buffer.max_size();
  }

  /**
   * @brief Reserves storage for at least \p n elements.
   *
   * @param n Minimum element capacity to ensure.
   *
   * @pre None.
   * @post Capacity is at least \p n; element values and \c cursor() are
   *       unchanged.
   */
  constexpr auto reserve(size_type const n) noexcept -> void {
    if (n > m_buffer.capacity()) {
      grow_gap(n - size());
    }
  }

  /**
   * @brief Removes every element and the gap.
   *
   * @pre None.
   * @post \c empty() is \c true and \c cursor() is zero.
   */
  constexpr auto clear() noexcept -> void {
    m_buffer.clear();
    m_gap_begin = 0;
    m_gap_end = 0;
  }

  /**
   * @brief Closes the gap and releases unused capacity.
   *
   * @pre None.
   * @post \c size() is unchanged and \c cursor() equals \c size().
   */
  constexpr auto shrink_to_fit() noexcept -> void {
    auto const post_count{m_buffer.size() - m_gap_end};
    // Skip when the gap is already closed (m_gap_begin == m_gap_end): the post
    // elements are already packed and the move would self-assign, corrupting a
    // non-trivial T.
    if (m_gap_begin != m_gap_end) {
      for (size_type i{0}; i < post_count; ++i) {
        m_buffer[m_gap_begin + i] = std::move(m_buffer[m_gap_end + i]);
      }
    }
    m_buffer.resize(m_gap_begin + post_count);
    m_buffer.shrink_to_fit();
    m_gap_begin = m_buffer.size();
    m_gap_end = m_buffer.size();
  }

  /**
   * @brief Swaps contents with \p other.
   *
   * @param other Buffer to exchange state with.
   *
   * @pre None.
   * @post This buffer and \p other have exchanged elements and cursors.
   */
  constexpr auto swap(gap_buffer& other) noexcept -> void {
    using std::swap;
    m_buffer.swap(other.m_buffer);
    swap(m_gap_begin, other.m_gap_begin);
    swap(m_gap_end, other.m_gap_end);
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
  friend constexpr auto swap(gap_buffer& a, gap_buffer& b) noexcept -> void {
    a.swap(b);
  }

  /**
   * @brief The cursor position as a logical index.
   *
   * @return A value in \c [0, size()].
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto cursor() const noexcept -> size_type {
    return m_gap_begin;
  }

  /**
   * @brief Moves the cursor to logical position \p pos.
   *
   * @param pos Target position in \c [0, size()].
   *
   * @return Nothing on success, or \c container_error::out_of_range when \p pos
   *         exceeds \c size().
   *
   * @pre None.
   * @post On success \c cursor() equals \p pos and element values are unchanged;
   *       on failure the buffer is unchanged.
   *
   * @complexity \c O(|pos - cursor()|).
   */
  constexpr auto move_cursor_to(size_type const pos) noexcept -> result<void> {
    if (pos > size()) {
      return std::unexpected{container_error::out_of_range};
    }
    while (m_gap_begin > pos) {  // shift the gap left
      --m_gap_begin;
      --m_gap_end;
      // With an empty gap m_gap_begin == m_gap_end, so this would self-move
      // (a = std::move(a)), which empties a std::string and the like. Moving the
      // cursor across a zero-width gap only reclassifies the boundary element,
      // no relocation needed.
      if (m_gap_end != m_gap_begin) {
        m_buffer[m_gap_end] = std::move(m_buffer[m_gap_begin]);
      }
    }
    while (m_gap_begin < pos) {        // shift the gap right
      if (m_gap_begin != m_gap_end) {  // skip a self-move when the gap is empty
        m_buffer[m_gap_begin] = std::move(m_buffer[m_gap_end]);
      }
      ++m_gap_begin;
      ++m_gap_end;
    }
    return {};
  }

  /**
   * @brief Moves the cursor by \p delta positions.
   *
   * @param delta Signed offset to apply to the cursor.
   *
   * @return Nothing on success, or \c container_error::out_of_range when the
   *         destination leaves \c [0, size()].
   *
   * @pre None.
   * @post On success \c cursor() moved by \p delta; on failure the buffer is
   *       unchanged.
   *
   * @complexity \c O(|delta|).
   */
  constexpr auto move_cursor_by(difference_type const delta) noexcept -> result<void> {
    auto const target{static_cast<difference_type>(cursor()) + delta};
    if (target < 0 || static_cast<size_type>(target) > size()) {
      return std::unexpected{container_error::out_of_range};
    }
    return move_cursor_to(static_cast<size_type>(target));
  }

  /**
   * @brief Inserts a copy of \p value at the cursor; the cursor advances past
   *        it.
   *
   * @param value Value to copy in.
   *
   * @pre None.
   * @post \c size() and \c cursor() each grew by one.
   *
   * @complexity Amortised \c O(1).
   */
  constexpr auto insert(T const& value) noexcept -> void {
    if (gap_size() == 0) {
      grow_gap(initial_gap);
    }
    m_buffer[m_gap_begin] = value;
    ++m_gap_begin;
  }

  /**
   * @brief Inserts \p value at the cursor by moving it; the cursor advances.
   *
   * @param value Value to move in.
   *
   * @pre None.
   * @post \c size() and \c cursor() each grew by one.
   *
   * @complexity Amortised \c O(1).
   */
  constexpr auto insert(T&& value) noexcept -> void {
    if (gap_size() == 0) {
      grow_gap(initial_gap);
    }
    m_buffer[m_gap_begin] = std::move(value);
    ++m_gap_begin;
  }

  /**
   * @brief Constructs an element in place at the cursor.
   *
   * @tparam Args Constructor argument types.
   * @param args Arguments forwarded to \p T's constructor.
   *
   * @pre None.
   * @post \c size() and \c cursor() each grew by one.
   *
   * @complexity Amortised \c O(1).
   */
  template <typename... Args>
    requires std::constructible_from<T, Args...>
  constexpr auto emplace(Args&&... args) noexcept -> void {
    insert(T(std::forward<Args>(args)...));
  }

  /**
   * @brief Erases the element immediately after the cursor (forward delete).
   *
   * @return Nothing on success, or \c container_error::empty when no element
   *         follows the cursor.
   *
   * @pre None.
   * @post On success \c size() shrank by one and \c cursor() is unchanged; on
   *       failure the buffer is unchanged.
   *
   * @complexity \c O(1).
   */
  constexpr auto erase_forward() noexcept -> result<void> {
    if (m_gap_end >= m_buffer.size()) {
      return std::unexpected{container_error::empty};
    }
    ++m_gap_end;
    return {};
  }

  /**
   * @brief Erases the element immediately before the cursor (backspace).
   *
   * @return Nothing on success, or \c container_error::empty when no element
   *         precedes the cursor.
   *
   * @pre None.
   * @post On success \c size() and \c cursor() each shrank by one; on failure
   *       the buffer is unchanged.
   *
   * @complexity \c O(1).
   */
  constexpr auto erase_backward() noexcept -> result<void> {
    if (m_gap_begin == 0) {
      return std::unexpected{container_error::empty};
    }
    --m_gap_begin;
    return {};
  }

  /**
   * @brief Unchecked access by logical index.
   *
   * @param i Logical index.
   *
   * @return Reference to the element at logical index \p i.
   *
   * @pre \p i is less than \c size().
   * @post None.
   */
  [[nodiscard]] constexpr auto operator[](size_type const i) noexcept -> T& {
    return m_buffer[physical(i)];
  }

  /**
   * @brief Unchecked access by logical index (const overload).
   *
   * @param i Logical index.
   *
   * @return Const reference to the element at logical index \p i.
   *
   * @pre \p i is less than \c size().
   * @post None.
   */
  [[nodiscard]] constexpr auto operator[](size_type const i) const noexcept -> T const& {
    return m_buffer[physical(i)];
  }

  /**
   * @brief Checked access by logical index.
   *
   * @param i Logical index.
   *
   * @return Pointer to the element, or \c nullptr when \p i is out of range.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto at(size_type const i) noexcept -> T* {
    return i < size() ? std::addressof((*this)[i]) : nullptr;
  }

  /// @copydoc at(size_type)
  [[nodiscard]] constexpr auto at(size_type const i) const noexcept -> T const* {
    return i < size() ? std::addressof((*this)[i]) : nullptr;
  }

  /**
   * @brief Pointer to the first element, or \c nullptr when empty.
   *
   * @return A pointer to the front element, or \c nullptr.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto front() noexcept -> T* {
    return empty() ? nullptr : std::addressof((*this)[0]);
  }

  /// @copydoc front()
  [[nodiscard]] constexpr auto front() const noexcept -> T const* {
    return empty() ? nullptr : std::addressof((*this)[0]);
  }

  /**
   * @brief Pointer to the last element, or \c nullptr when empty.
   *
   * @return A pointer to the back element, or \c nullptr.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto back() noexcept -> T* {
    return empty() ? nullptr : std::addressof((*this)[size() - 1]);
  }

  /// @copydoc back()
  [[nodiscard]] constexpr auto back() const noexcept -> T const* {
    return empty() ? nullptr : std::addressof((*this)[size() - 1]);
  }

  [[nodiscard]] constexpr auto begin() noexcept -> iterator {
    return iterator{this, 0};
  }

  [[nodiscard]] constexpr auto end() noexcept -> iterator {
    return iterator{this, size()};
  }

  [[nodiscard]] constexpr auto begin() const noexcept -> const_iterator {
    return const_iterator{this, 0};
  }

  [[nodiscard]] constexpr auto end() const noexcept -> const_iterator {
    return const_iterator{this, size()};
  }

  [[nodiscard]] constexpr auto cbegin() const noexcept -> const_iterator {
    return begin();
  }

  [[nodiscard]] constexpr auto cend() const noexcept -> const_iterator {
    return end();
  }

  [[nodiscard]] constexpr auto rbegin() noexcept -> reverse_iterator {
    return reverse_iterator{end()};
  }

  [[nodiscard]] constexpr auto rend() noexcept -> reverse_iterator {
    return reverse_iterator{begin()};
  }

  [[nodiscard]] constexpr auto rbegin() const noexcept -> const_reverse_iterator {
    return const_reverse_iterator{end()};
  }

  [[nodiscard]] constexpr auto rend() const noexcept -> const_reverse_iterator {
    return const_reverse_iterator{begin()};
  }

  [[nodiscard]] constexpr auto crbegin() const noexcept -> const_reverse_iterator {
    return rbegin();
  }

  [[nodiscard]] constexpr auto crend() const noexcept -> const_reverse_iterator {
    return rend();
  }

  /**
   * @brief Equality over the logical sequences.
   *
   * @param a First buffer.
   * @param b Second buffer.
   *
   * @return \c true when both hold equal elements in the same order.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] friend constexpr auto
  operator==(gap_buffer const& a, gap_buffer const& b) noexcept -> bool
    requires std::equality_comparable<T>
  {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), b.end());
  }

  /**
   * @brief Lexicographical ordering over the logical sequences.
   *
   * @param a First buffer.
   * @param b Second buffer.
   *
   * @return The three-way comparison of the two sequences.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] friend constexpr auto operator<=>(gap_buffer const& a, gap_buffer const& b) noexcept
    requires std::three_way_comparable<T>
  {
    return std::lexicographical_compare_three_way(a.begin(), a.end(), b.begin(), b.end());
  }
};

}  // namespace nexenne::container
