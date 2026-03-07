#pragma once

/**
 * @file
 * @brief Fixed-capacity vector with inline storage that never touches the heap.
 *
 * \c static_vector<T, N> behaves like \c std::vector except its capacity is
 * fixed at compile time to \p N. Storage is an inline array of \p N
 * manually-managed slots embedded in the object, so the container never
 * allocates; an operation that would exceed \p N fails via \c result rather
 * than growing.
 *
 * Reach for it on hot paths that can prove an upper bound on element count and
 * want zero allocation traffic (contact manifolds, scratch queues, command
 * builders), and in freestanding contexts where \c new is unavailable. Every
 * operation is \c noexcept; capacity exhaustion surfaces as
 * \c container_error::full. Construction, element access, and comparison are
 * \c constexpr, so a \c static_vector can be built and queried at compile time;
 * \c data() / \c span() / the iterators expose the contiguous live range for
 * \c std::span and \c std::ranges interop at run time.
 */

#include <array>
#include <compare>
#include <concepts>
#include <cstddef>
#include <expected>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <span>
#include <utility>

#include <nexenne/container/error.hpp>

namespace nexenne::container {

/**
 * @brief Fixed-capacity, heap-free vector with inline storage.
 *
 * @tparam T Element type; must be move-constructible.
 * @tparam N Capacity, fixed at compile time.
 *
 * @pre None.
 * @post A default-constructed vector is empty.
 */
template <std::move_constructible T, std::size_t N>
class static_vector {
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
  // A slot holds at most one live T. Its default member is a trivial byte so
  // that constructing the storage array does not construct any T; element
  // lifetimes are then managed by hand. Using a union (rather than a byte
  // buffer plus reinterpret_cast) keeps every element operation usable in a
  // constant expression.
  union slot {
    unsigned char none;
    T value;

    constexpr slot() noexcept : none{} {}

    constexpr ~slot() noexcept {}
  };

  // The runtime data()/iterator accessors expose a contiguous T* over m_slots by
  // reinterpreting the slot array. That is only sound if a slot has exactly T's
  // size and alignment (so the T* stride matches the slot stride); the union of
  // T with a single byte guarantees this, and asserting it here turns the layout
  // assumption into a compile-time invariant instead of a silent dependency.
  static_assert(sizeof(slot) == sizeof(T) && alignof(slot) == alignof(T));

  std::array<slot, N> m_slots{};
  size_type m_size{};

  [[nodiscard]] constexpr auto slot_at(size_type const index) noexcept -> T* {
    return std::addressof(m_slots[index].value);
  }

  [[nodiscard]] constexpr auto slot_at(size_type const index) const noexcept -> T const* {
    return std::addressof(m_slots[index].value);
  }

public:
  /**
   * @brief Default-constructs an empty vector.
   *
   * @pre None.
   * @post \c empty() is \c true.
   */
  constexpr static_vector() noexcept = default;

  /**
   * @brief Constructs from an initializer list, truncating past \p N.
   *
   * Elements past index \c N-1 are silently discarded so the constructor stays
   * \c noexcept; a caller needing to detect truncation compares \c size()
   * against the original list size.
   *
   * @param init Brace-enclosed list of values.
   *
   * @pre None.
   * @post \c size() is the smaller of \c init.size() and \p N, holding the
   *       first that many elements of \p init.
   */
  constexpr static_vector(std::initializer_list<T> const init) noexcept {
    auto const count{init.size() < N ? init.size() : N};
    auto it{init.begin()};
    for (size_type i{0}; i < count; ++i, ++it) {
      std::construct_at(slot_at(i), *it);
    }
    m_size = count;
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
  constexpr static_vector(static_vector const& other) noexcept {
    for (size_type i{0}; i < other.m_size; ++i) {
      std::construct_at(slot_at(i), *other.slot_at(i));
    }
    m_size = other.m_size;
  }

  /**
   * @brief Move-constructs from \p other, leaving it empty.
   *
   * @param other Source vector, emptied after the move.
   *
   * @pre None.
   * @post This vector holds \p other's former elements; \p other is empty.
   */
  constexpr static_vector(static_vector&& other) noexcept {
    for (size_type i{0}; i < other.m_size; ++i) {
      std::construct_at(slot_at(i), std::move(*other.slot_at(i)));
      std::destroy_at(other.slot_at(i));
    }
    m_size = other.m_size;
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
  constexpr auto operator=(static_vector other) noexcept -> static_vector& {
    swap(other);
    return *this;
  }

  constexpr ~static_vector() noexcept {
    clear();
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
   * @brief Reports whether the vector is at capacity.
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
   * @brief The largest number of elements the vector can ever hold.
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
   * @brief Swaps contents with \p other, element by element.
   *
   * @param other Vector to exchange state with.
   *
   * @pre None.
   * @post This vector holds \p other's former elements and vice versa.
   *
   * @complexity \c O(size).
   */
  constexpr auto swap(static_vector& other) noexcept -> void {
    if (this == &other) {
      return;
    }
    auto const shared{m_size < other.m_size ? m_size : other.m_size};
    using std::swap;
    for (size_type i{0}; i < shared; ++i) {
      swap(*slot_at(i), *other.slot_at(i));
    }
    // The longer half migrates element by element to the shorter side.
    if (m_size < other.m_size) {
      for (size_type i{shared}; i < other.m_size; ++i) {
        std::construct_at(slot_at(i), std::move(*other.slot_at(i)));
        std::destroy_at(other.slot_at(i));
      }
    } else {
      for (size_type i{shared}; i < m_size; ++i) {
        std::construct_at(other.slot_at(i), std::move(*slot_at(i)));
        std::destroy_at(slot_at(i));
      }
    }
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
  friend constexpr auto swap(static_vector& a, static_vector& b) noexcept -> void {
    a.swap(b);
  }

  /**
   * @brief Destroys every element; capacity stays \p N.
   *
   * @pre None.
   * @post \c empty() is \c true.
   */
  constexpr auto clear() noexcept -> void {
    for (size_type i{0}; i < m_size; ++i) {
      std::destroy_at(slot_at(i));
    }
    m_size = 0;
  }

  /**
   * @brief Appends a copy of \p value to the end.
   *
   * @param value Value to copy in.
   *
   * @return Nothing on success, or \c container_error::full when at capacity.
   *
   * @pre None.
   * @post On success \c size() grew by one and the new back equals \p value; on
   *       failure the vector is unchanged.
   *
   * @complexity \c O(1).
   */
  constexpr auto push_back(T const& value) noexcept -> result<void> {
    return emplace_back(value);
  }

  /**
   * @brief Appends \p value to the end by moving it.
   *
   * @param value Value to move in.
   *
   * @return Nothing on success, or \c container_error::full when at capacity.
   *
   * @pre None.
   * @post On success \c size() grew by one and \p value was moved into the new
   *       back; on failure the vector is unchanged.
   *
   * @complexity \c O(1).
   */
  constexpr auto push_back(T&& value) noexcept -> result<void> {
    return emplace_back(std::move(value));
  }

  /**
   * @brief Constructs an element in place at the end.
   *
   * @tparam Args Constructor argument types.
   * @param args Arguments forwarded to \p T's constructor.
   *
   * @return Nothing on success, or \c container_error::full when at capacity.
   *
   * @pre None.
   * @post On success \c size() grew by one; on failure the vector is unchanged.
   *
   * @complexity \c O(1).
   */
  template <typename... Args>
    requires std::constructible_from<T, Args...>
  constexpr auto emplace_back(Args&&... args) noexcept -> result<void> {
    if (m_size == N) {
      return std::unexpected{container_error::full};
    }
    std::construct_at(slot_at(m_size), std::forward<Args>(args)...);
    ++m_size;
    return {};
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
  constexpr auto pop_back() noexcept -> result<void> {
    if (m_size == 0) {
      return std::unexpected{container_error::empty};
    }
    --m_size;
    std::destroy_at(slot_at(m_size));
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
  [[nodiscard]] constexpr auto operator[](size_type const index) noexcept -> T& {
    return *slot_at(index);
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
  [[nodiscard]] constexpr auto operator[](size_type const index) const noexcept -> T const& {
    return *slot_at(index);
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
  [[nodiscard]] constexpr auto at(size_type const index) noexcept -> T* {
    return index < m_size ? slot_at(index) : nullptr;
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
  [[nodiscard]] constexpr auto at(size_type const index) const noexcept -> T const* {
    return index < m_size ? slot_at(index) : nullptr;
  }

  /**
   * @brief The first element.
   *
   * @return Pointer to the first element, or \c nullptr when empty.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto front() noexcept -> T* {
    return m_size == 0 ? nullptr : slot_at(0);
  }

  /**
   * @brief The first element (const overload).
   *
   * @return Const pointer to the first element, or \c nullptr when empty.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto front() const noexcept -> T const* {
    return m_size == 0 ? nullptr : slot_at(0);
  }

  /**
   * @brief The last element.
   *
   * @return Pointer to the last element, or \c nullptr when empty.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto back() noexcept -> T* {
    return m_size == 0 ? nullptr : slot_at(m_size - 1);
  }

  /**
   * @brief The last element (const overload).
   *
   * @return Const pointer to the last element, or \c nullptr when empty.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto back() const noexcept -> T const* {
    return m_size == 0 ? nullptr : slot_at(m_size - 1);
  }

  /**
   * @brief Raw pointer to the contiguous storage (run time only).
   *
   * Reinterprets the slot array as a \c T array. This is not a constant
   * expression (hence not \c constexpr) and relies on the slot having T's exact
   * size and alignment, which the \c static_assert above guarantees so the
   * pointer stride matches.
   *
   * @return Pointer to the first element slot of the inline buffer.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto data() noexcept -> T* {
    return reinterpret_cast<T*>(m_slots.data());
  }

  /**
   * @brief Raw pointer to the contiguous storage (const, run time only).
   *
   * @return Const pointer to the first element slot of the inline buffer.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto data() const noexcept -> T const* {
    return reinterpret_cast<T const*>(m_slots.data());
  }

  /**
   * @brief A \c std::span view over the live elements (run time only).
   *
   * @return A span covering the \c size() live elements.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto span() noexcept -> std::span<T> {
    return std::span<T>{data(), m_size};
  }

  /**
   * @brief A \c std::span view over the live elements (const, run time only).
   *
   * @return A span covering the \c size() live elements.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto span() const noexcept -> std::span<T const> {
    return std::span<T const>{data(), m_size};
  }

  [[nodiscard]] auto begin() noexcept -> iterator {
    return data();
  }

  [[nodiscard]] auto end() noexcept -> iterator {
    return data() + m_size;
  }

  [[nodiscard]] auto begin() const noexcept -> const_iterator {
    return data();
  }

  [[nodiscard]] auto end() const noexcept -> const_iterator {
    return data() + m_size;
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
  [[nodiscard]] friend constexpr auto
  operator==(static_vector const& a, static_vector const& b) noexcept -> bool
    requires std::equality_comparable<T>
  {
    if (a.m_size != b.m_size) {
      return false;
    }
    for (size_type i{0}; i < a.m_size; ++i) {
      if (!(*a.slot_at(i) == *b.slot_at(i))) {
        return false;
      }
    }
    return true;
  }

  /**
   * @brief Lexicographic three-way comparison of the elements.
   *
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
  [[nodiscard]] friend constexpr auto operator<=>(
    static_vector const& a, static_vector const& b
  ) noexcept -> std::compare_three_way_result_t<U> {
    auto const shared{a.m_size < b.m_size ? a.m_size : b.m_size};
    for (size_type i{0}; i < shared; ++i) {
      if (auto const cmp{*a.slot_at(i) <=> *b.slot_at(i)}; cmp != 0) {
        return cmp;
      }
    }
    return a.m_size <=> b.m_size;
  }
};

}  // namespace nexenne::container
