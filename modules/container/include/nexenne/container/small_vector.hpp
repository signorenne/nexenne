#pragma once

/**
 * @file
 * @brief Vector with inline storage for the first \p N elements and automatic
 *        heap fallback beyond that.
 *
 * \c small_vector<T, N> is the standard "usually small, occasionally larger"
 * container: while its size stays at or below \p N nothing is allocated, the
 * elements living in an inline buffer. Once it grows past \p N it moves to heap
 * storage like \c std::vector and stays there for the rest of its lifetime
 * (until \c shrink_to_fit migrates back).
 *
 * Reach for it for per-call temporaries that are usually short (search results,
 * parser child lists, command queues) and for members that are usually small
 * but occasionally unbounded, anywhere \c std::vector would do but the
 * allocation traffic on small inputs is measurable. All non-allocating
 * operations are \c noexcept; an operation that grows past the inline buffer
 * calls \c ::operator new and, per the module policy, treats allocation failure
 * as fatal (\c std::terminate) rather than throwing. \c data() / \c size() /
 * \c span() expose the contiguous live range for \c std::span and
 * \c std::ranges interop.
 */

#include <algorithm>
#include <array>
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
#include <span>
#include <utility>

#include <nexenne/container/error.hpp>

namespace nexenne::container {

/**
 * @brief Vector with \p N inline slots and heap fallback beyond them.
 *
 * @tparam T Element type; must be move-constructible.
 * @tparam N Inline capacity, fixed at compile time.
 *
 * @pre None.
 * @post A default-constructed vector is empty and uses inline storage.
 */
template <std::move_constructible T, std::size_t N>
class small_vector {
public:
  using value_type = T;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using reference = T&;
  using const_reference = T const&;
  using pointer = T*;
  using const_pointer = T const*;
  using iterator = T*;
  using const_iterator = T const*;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

private:
  alignas(T) std::array<std::byte, sizeof(T) * N> m_inline{};
  T* m_data{inline_storage()};
  size_type m_size{};
  size_type m_capacity{N};

  [[nodiscard]] auto inline_storage() noexcept -> T* {
    return reinterpret_cast<T*>(m_inline.data());
  }

  [[nodiscard]] auto inlined() const noexcept -> bool {
    return m_data == reinterpret_cast<T const*>(m_inline.data());
  }

  auto deallocate_if_heap() noexcept -> void {
    if (!inlined()) {
      ::operator delete(m_data, m_capacity * sizeof(T), std::align_val_t{alignof(T)});
    }
  }

  // Grows capacity to at least new_capacity, migrating inline to heap as needed.
  auto grow_to(size_type const new_capacity) noexcept -> void {
    if (new_capacity <= m_capacity) {
      return;
    }
    if (new_capacity > max_size()) {
      // new_capacity * sizeof(T) would overflow the byte count and yield an
      // undersized allocation; the request is unsatisfiable, so fail loudly
      // rather than silently corrupt the heap. Allocation failure is fatal.
      std::terminate();
    }
    auto* const fresh{
      static_cast<T*>(::operator new(new_capacity * sizeof(T), std::align_val_t{alignof(T)}))
    };
    for (size_type i{0}; i < m_size; ++i) {
      std::construct_at(fresh + i, std::move(m_data[i]));
      std::destroy_at(m_data + i);
    }
    deallocate_if_heap();
    m_data = fresh;
    m_capacity = new_capacity;
  }

  // The next capacity for a one-past-full growth: double, but clamp to
  // max_size() so the doubling cannot itself overflow size_type (which would
  // wrap to a value <= m_capacity and turn the grow into a silent no-op).
  [[nodiscard]] auto next_capacity() const noexcept -> size_type {
    if (m_capacity == 0) {
      return size_type{1};
    }
    return m_capacity > max_size() / 2 ? max_size() : m_capacity * 2;
  }

  // Takes ownership of other's elements into a freshly-reset *this (m_data is
  // inline storage, m_size is zero): steals the heap block, or moves inline.
  auto adopt(small_vector&& other) noexcept -> void {
    if (other.inlined()) {
      for (size_type i{0}; i < other.m_size; ++i) {
        std::construct_at(m_data + i, std::move(other.m_data[i]));
        std::destroy_at(other.m_data + i);
      }
      m_size = other.m_size;
      other.m_size = 0;
    } else {
      m_data = other.m_data;
      m_size = other.m_size;
      m_capacity = other.m_capacity;
      other.m_data = other.inline_storage();
      other.m_size = 0;
      other.m_capacity = N;
    }
  }

public:
  /**
   * @brief Default-constructs an empty vector with inline storage.
   *
   * @pre None.
   * @post \c size() is zero, \c capacity() equals \p N, and no heap allocation
   *       has occurred.
   */
  small_vector() noexcept = default;

  /**
   * @brief Constructs from an initializer list.
   *
   * @param init Brace-enclosed list of values.
   *
   * @pre None.
   * @post \c size() equals \c init.size() with a copy of each element.
   */
  small_vector(std::initializer_list<T> const init) noexcept {
    reserve(init.size());
    for (auto const& value : init) {
      std::construct_at(m_data + m_size, value);
      ++m_size;
    }
  }

  /**
   * @brief Copy-constructs from \p other.
   *
   * @param other Source vector to copy.
   *
   * @pre None.
   * @post This vector holds copies of \p other's elements; \p other is
   *       unchanged.
   */
  small_vector(small_vector const& other) noexcept {
    reserve(other.m_size);
    for (size_type i{0}; i < other.m_size; ++i) {
      std::construct_at(m_data + i, other.m_data[i]);
    }
    m_size = other.m_size;
  }

  /**
   * @brief Move-constructs from \p other.
   *
   * Steals the heap allocation when \p other is on the heap; otherwise
   * move-constructs each inline element.
   *
   * @param other Source vector, left empty with inline storage.
   *
   * @pre None.
   * @post This vector holds \p other's former elements; \p other is empty.
   */
  small_vector(small_vector&& other) noexcept {
    adopt(std::move(other));
  }

  /**
   * @brief Copy-assigns from \p other.
   *
   * @param other Source vector to copy.
   *
   * @return Reference to \c *this.
   *
   * @pre None.
   * @post This vector holds copies of \p other's elements; the prior contents
   *       are destroyed; \p other is unchanged.
   */
  auto operator=(small_vector const& other) noexcept -> small_vector& {
    if (this != &other) {
      clear();
      reserve(other.m_size);
      for (size_type i{0}; i < other.m_size; ++i) {
        std::construct_at(m_data + i, other.m_data[i]);
      }
      m_size = other.m_size;
    }
    return *this;
  }

  /**
   * @brief Move-assigns from \p other.
   *
   * @param other Source vector, left empty with inline storage.
   *
   * @return Reference to \c *this.
   *
   * @pre None.
   * @post This vector holds \p other's former elements; the prior contents are
   *       destroyed; \p other is empty.
   */
  auto operator=(small_vector&& other) noexcept -> small_vector& {
    if (this != &other) {
      clear();
      deallocate_if_heap();
      m_data = inline_storage();
      m_capacity = N;
      adopt(std::move(other));
    }
    return *this;
  }

  /**
   * @brief Destroys every element and releases any heap storage.
   *
   * @pre None.
   * @post All elements are destroyed and any heap allocation is released.
   */
  ~small_vector() noexcept {
    clear();
    deallocate_if_heap();
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
   * @brief Current storage capacity (inline or heap).
   *
   * @return The number of elements that fit without reallocating.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto capacity() const noexcept -> size_type {
    return m_capacity;
  }

  /**
   * @brief Reports whether the elements currently live in the inline buffer.
   *
   * @return \c true when no heap allocation backs the elements.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto is_inline() const noexcept -> bool {
    return inlined();
  }

  /**
   * @brief The inline capacity \p N.
   *
   * @return The template parameter \p N.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] static constexpr auto inline_capacity() noexcept -> size_type {
    return N;
  }

  /**
   * @brief The largest number of elements the vector can hold.
   *
   * @return The element count an allocation could address.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] static constexpr auto max_size() noexcept -> size_type {
    return std::numeric_limits<size_type>::max() / sizeof(T);
  }

  /**
   * @brief Swaps contents with \p other.
   *
   * Implemented through moves because a pointer swap is impossible while either
   * side uses inline storage.
   *
   * @param other Vector to exchange contents with.
   *
   * @pre None.
   * @post This vector holds \p other's former contents and vice versa.
   */
  auto swap(small_vector& other) noexcept -> void {
    if (this == &other) {
      return;
    }
    if (!inlined() && !other.inlined()) {
      // Both on the heap: an O(1) swap of the pointers, no element moves.
      using std::swap;
      swap(m_data, other.m_data);
      swap(m_size, other.m_size);
      swap(m_capacity, other.m_capacity);
      return;
    }
    auto temp{std::move(*this)};
    *this = std::move(other);
    other = std::move(temp);
  }

  /**
   * @brief Swaps the contents of \p a and \p b.
   *
   * @param a First vector.
   * @param b Second vector.
   *
   * @pre None.
   * @post \p a and \p b have exchanged contents.
   */
  friend auto swap(small_vector& a, small_vector& b) noexcept -> void {
    a.swap(b);
  }

  /**
   * @brief Reserves storage for at least \p n elements.
   *
   * @param n Minimum capacity to ensure.
   *
   * @pre None.
   * @post \c capacity() is at least \p n; size and element values are
   *       unchanged.
   */
  auto reserve(size_type const n) noexcept -> void {
    grow_to(n);
  }

  /**
   * @brief Releases unused heap capacity, migrating back inline when it fits.
   *
   * @pre None.
   * @post \c capacity() equals \c max(N, size()); element values and order are
   *       unchanged.
   */
  auto shrink_to_fit() noexcept -> void {
    if (inlined() || m_size == m_capacity) {
      return;
    }
    if (m_size <= N) {
      auto* const target{inline_storage()};
      for (size_type i{0}; i < m_size; ++i) {
        std::construct_at(target + i, std::move(m_data[i]));
        std::destroy_at(m_data + i);
      }
      deallocate_if_heap();
      m_data = target;
      m_capacity = N;
    } else {
      auto* const fresh{
        static_cast<T*>(::operator new(m_size * sizeof(T), std::align_val_t{alignof(T)}))
      };
      for (size_type i{0}; i < m_size; ++i) {
        std::construct_at(fresh + i, std::move(m_data[i]));
        std::destroy_at(m_data + i);
      }
      deallocate_if_heap();
      m_data = fresh;
      m_capacity = m_size;
    }
  }

  /**
   * @brief Destroys every element; capacity is preserved.
   *
   * @pre None.
   * @post \c size() is zero; capacity and any heap allocation are unchanged.
   */
  auto clear() noexcept -> void {
    for (size_type i{0}; i < m_size; ++i) {
      std::destroy_at(m_data + i);
    }
    m_size = 0;
  }

  /**
   * @brief Replaces the contents with the elements of \p init.
   *
   * @param init Brace-enclosed list of values to copy in.
   *
   * @pre None.
   * @post \c size() equals \c init.size(); the prior contents are destroyed.
   */
  auto assign(std::initializer_list<T> const init) noexcept -> void {
    clear();
    reserve(init.size());
    for (auto const& value : init) {
      std::construct_at(m_data + m_size, value);
      ++m_size;
    }
  }

  /**
   * @brief Replaces the contents with \p count copies of \p value.
   *
   * @param count Number of copies to store.
   * @param value Value to replicate.
   *
   * @pre None.
   * @post \c size() equals \p count and every element equals \p value; the
   *       prior contents are destroyed.
   */
  auto assign(size_type const count, T const& value) noexcept -> void {
    clear();
    reserve(count);
    for (size_type i{0}; i < count; ++i) {
      std::construct_at(m_data + i, value);
    }
    m_size = count;
  }

  /**
   * @brief Replaces the contents with the range \c [first, last).
   *
   * @tparam It Input iterator type.
   * @param first Iterator to the first source element.
   * @param last Iterator one past the last source element.
   *
   * @pre \c [first, last) is a valid range that does not alias this vector.
   * @post \c size() equals \c std::distance(first, last); the prior contents
   *       are destroyed.
   */
  template <std::input_iterator It>
  auto assign(It first, It const last) noexcept -> void {
    clear();
    if constexpr (std::forward_iterator<It>) {
      reserve(static_cast<size_type>(std::distance(first, last)));  // one allocation
    }
    for (; first != last; ++first) {
      push_back(*first);
    }
  }

  /**
   * @brief Appends a copy of \p value, growing past inline capacity as needed.
   *
   * @param value Value to copy in.
   *
   * @pre None.
   * @post \c size() grew by one and the new back equals \p value.
   *
   * @complexity Amortised \c O(1).
   */
  auto push_back(T const& value) noexcept -> void {
    emplace_back(value);
  }

  /**
   * @brief Appends \p value by moving it, growing past inline capacity as
   *        needed.
   *
   * @param value Value to move in.
   *
   * @pre None.
   * @post \c size() grew by one and \p value was moved into the new back.
   *
   * @complexity Amortised \c O(1).
   */
  auto push_back(T&& value) noexcept -> void {
    emplace_back(std::move(value));
  }

  /**
   * @brief Constructs an element in place at the end.
   *
   * @tparam Args Constructor argument types.
   * @param args Arguments forwarded to \p T's constructor.
   *
   * @return Reference to the newly constructed element.
   *
   * @pre None.
   * @post \c size() grew by one; the returned reference is valid until the next
   *       growth or erasure.
   *
   * @complexity Amortised \c O(1).
   */
  template <typename... Args>
    requires std::constructible_from<T, Args...>
  auto emplace_back(Args&&... args) noexcept -> T& {
    if (m_size == m_capacity) {
      // Cold grow path: materialize the value before grow_to frees the old
      // block, so an argument aliasing an existing element (push_back(v[i]))
      // stays valid across the reallocation.
      T value{std::forward<Args>(args)...};
      grow_to(next_capacity());
      std::construct_at(m_data + m_size, std::move(value));
    } else {
      std::construct_at(m_data + m_size, std::forward<Args>(args)...);
    }
    ++m_size;
    return m_data[m_size - 1];
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
  auto pop_back() noexcept -> result<void> {
    if (m_size == 0) {
      return std::unexpected{container_error::empty};
    }
    --m_size;
    std::destroy_at(m_data + m_size);
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
    return m_data[index];
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
    return m_data[index];
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
    return index < m_size ? m_data + index : nullptr;
  }

  /**
   * @brief Checked access by index (const overload).
   *
   * @param index Logical index.
   *
   * @return Const pointer to the element, or \c nullptr when \p index is out of
   *         range.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto at(size_type const index) const noexcept -> T const* {
    return index < m_size ? m_data + index : nullptr;
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
    return m_size == 0 ? nullptr : m_data;
  }

  /// @copydoc front()
  [[nodiscard]] auto front() const noexcept -> T const* {
    return m_size == 0 ? nullptr : m_data;
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
    return m_size == 0 ? nullptr : m_data + (m_size - 1);
  }

  /// @copydoc back()
  [[nodiscard]] auto back() const noexcept -> T const* {
    return m_size == 0 ? nullptr : m_data + (m_size - 1);
  }

  /**
   * @brief Raw pointer to the contiguous storage.
   *
   * @return Pointer to the first element (or inline storage when empty).
   *
   * @pre None.
   * @post The pointer is valid until the next growth or migration between
   *       inline and heap storage.
   */
  [[nodiscard]] auto data() noexcept -> T* {
    return m_data;
  }

  /// @copydoc data()
  [[nodiscard]] auto data() const noexcept -> T const* {
    return m_data;
  }

  /**
   * @brief A \c std::span view over the live elements.
   *
   * @return A span covering the \c size() live elements.
   *
   * @pre None.
   * @post The span is valid until the next growth or migration between inline
   *       and heap storage.
   */
  [[nodiscard]] auto span() noexcept -> std::span<T> {
    return std::span<T>{m_data, m_size};
  }

  /// @copydoc span()
  [[nodiscard]] auto span() const noexcept -> std::span<T const> {
    return std::span<T const>{m_data, m_size};
  }

  [[nodiscard]] auto begin() noexcept -> iterator {
    return m_data;
  }

  [[nodiscard]] auto end() noexcept -> iterator {
    return m_data + m_size;
  }

  [[nodiscard]] auto begin() const noexcept -> const_iterator {
    return m_data;
  }

  [[nodiscard]] auto end() const noexcept -> const_iterator {
    return m_data + m_size;
  }

  [[nodiscard]] auto cbegin() const noexcept -> const_iterator {
    return begin();
  }

  [[nodiscard]] auto cend() const noexcept -> const_iterator {
    return end();
  }

  [[nodiscard]] auto rbegin() noexcept -> reverse_iterator {
    return reverse_iterator{end()};
  }

  [[nodiscard]] auto rend() noexcept -> reverse_iterator {
    return reverse_iterator{begin()};
  }

  [[nodiscard]] auto rbegin() const noexcept -> const_reverse_iterator {
    return const_reverse_iterator{end()};
  }

  [[nodiscard]] auto rend() const noexcept -> const_reverse_iterator {
    return const_reverse_iterator{begin()};
  }

  [[nodiscard]] auto crbegin() const noexcept -> const_reverse_iterator {
    return rbegin();
  }

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
  [[nodiscard]] friend auto
  operator==(small_vector const& a, small_vector const& b) noexcept -> bool
    requires std::equality_comparable<T>
  {
    return a.m_size == b.m_size && std::equal(a.begin(), a.end(), b.begin(), b.end());
  }

  /**
   * @brief Lexicographic three-way comparison of the elements.
   *
   * @tparam U Deduced as \p T; lets the ordering type stay unevaluated unless
   *           \p T is three-way comparable.
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
  [[nodiscard]] friend auto operator<=>(small_vector const& a, small_vector const& b) noexcept
    -> std::compare_three_way_result_t<U> {
    return std::lexicographical_compare_three_way(a.begin(), a.end(), b.begin(), b.end());
  }
};

}  // namespace nexenne::container
