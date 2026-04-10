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

#include <bit>
#include <concepts>
#include <cstddef>
#include <expected>
#include <initializer_list>
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

  // Physical slot of logical index i (0 is the front). m_cap is a power of two,
  // so the wrap is a mask. When m_cap is 0 the deque is empty and this is unused.
  [[nodiscard]] auto slot_of(size_type const i) const noexcept -> size_type {
    return (m_head + i) & (m_cap - 1);
  }

  // Grows to hold at least want elements (rounded to a power of two), re-packing
  // with the front at index 0. A no-op when the buffer already fits.
  auto grow(size_type const want) noexcept -> void {
    auto const new_cap{std::bit_ceil(want)};
    if (new_cap <= m_cap) {
      return;
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

  [[nodiscard]] static auto grown_capacity(size_type const cap) noexcept -> size_type {
    return cap == 0 ? 8 : cap * 2;
  }

public:
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
      // Materialize before grow frees the old buffer, so an argument aliasing an
      // existing element (push_back(d[0])) stays valid across the reallocation.
      T value{std::forward<Args>(args)...};
      grow(grown_capacity(m_cap));
      auto* const target{m_data + slot_of(m_size)};
      std::construct_at(target, std::move(value));
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
      // Materialize before grow frees the old buffer, so an argument aliasing an
      // existing element (push_front(d[0])) stays valid across the reallocation.
      T value{std::forward<Args>(args)...};
      grow(grown_capacity(m_cap));
      m_head = (m_head + m_cap - 1) & (m_cap - 1);
      auto* const target{m_data + m_head};
      std::construct_at(target, std::move(value));
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
};

}  // namespace nexenne::container
