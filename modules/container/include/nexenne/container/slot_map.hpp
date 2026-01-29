#pragma once

/**
 * @file
 * @brief Handle-based container with stable identity across insert and erase.
 *
 * \c slot_map<T> stores values in one growable buffer and hands each new value
 * an opaque \c key: a slot index plus a generation counter. A lookup returns the
 * live element or \c nullptr and is never undefined behaviour, even for a key
 * whose slot was erased and later recycled, because the generation counter
 * distinguishes the new occupant from the old (an ABA guard).
 *
 * The invariants: a key uniquely identifies its element and stays valid across
 * unrelated inserts, erases, and reallocations; erasing one element invalidates
 * only that element's key; a key to an erased (or recycled) slot reads as absent
 * via \c find / \c contains; and iteration walks only the live elements in slot
 * order, skipping vacancies. Reach for it for stable handles that must survive
 * reallocation, slotted registries (entities, resource managers, widget pools),
 * and anywhere a raw pointer would dangle. Storage is a single vector, so live
 * elements are roughly contiguous and iteration is cache-friendly. Insert is
 * amortised \c O(1), erase and lookup are \c O(1). Every operation is
 * \c noexcept; allocation failure terminates.
 */

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace nexenne::container {

/**
 * @brief Handle-based container with stable identity across insert and erase.
 *
 * @tparam T Element type. Intentionally unconstrained at the class level so
 *           \c slot_map<T>::key can be named where \p T is still incomplete (a
 *           type holding a handle into a slot map of itself); each operation
 *           enforces the constraint it actually needs.
 *
 * @pre None.
 * @post A default-constructed map is empty with no allocated storage.
 */
template <typename T>
class slot_map {
public:
  using value_type = T;
  using size_type = std::size_t;
  using generation_type = std::uint32_t;
  using index_type = std::uint32_t;

  /**
   * @brief Opaque handle returned by \c insert / \c emplace.
   *
   * Encodes a slot index and a generation counter; it is valid for lookup only
   * while the slot is occupied and its generation matches. Erasing the element
   * bumps the slot's generation, invalidating every outstanding key to it. A
   * default-constructed key (generation \c 0) never matches a live element.
   *
   * @pre None.
   * @post A default-constructed key never matches a live element; a key handed
   *       out by \c insert / \c emplace matches its element until it is erased.
   */
  class key {
  public:
    /**
     * @brief Constructs a null key that never matches a live element.
     *
     * @pre None.
     * @post \c index() and \c generation() are zero.
     */
    constexpr key() noexcept = default;

    /**
     * @brief Constructs a key from an explicit slot index and generation.
     *
     * @param index Slot index to reference.
     * @param generation Generation counter to tag the key with.
     *
     * @pre None.
     * @post \c index() equals \p index and \c generation() equals \p generation.
     */
    constexpr key(index_type const index, generation_type const generation) noexcept
        : m_index{index}, m_generation{generation} {}

    /**
     * @brief Slot index this key references.
     *
     * @return The slot index.
     *
     * @pre None.
     * @post None. The key is not modified.
     */
    [[nodiscard]] constexpr auto index() const noexcept -> index_type {
      return m_index;
    }

    /**
     * @brief Generation counter this key was tagged with.
     *
     * @return The generation counter.
     *
     * @pre None.
     * @post None. The key is not modified.
     */
    [[nodiscard]] constexpr auto generation() const noexcept -> generation_type {
      return m_generation;
    }

    [[nodiscard]] friend constexpr auto operator<=>(key const&, key const&) noexcept = default;

  private:
    index_type m_index{};
    generation_type m_generation{};
  };

private:
  std::vector<std::optional<T>> m_values;
  std::vector<generation_type> m_generations;
  std::vector<index_type> m_free_list;
  size_type m_size{};

  // True when k refers to a live element. The short-circuit guarantees the
  // generation and value reads happen only after the bounds check passes, so
  // this is the single validity check that find, contains, and erase share.
  [[nodiscard]] constexpr auto is_live(key const k) const noexcept -> bool {
    return k.index() < m_values.size() && m_generations[k.index()] == k.generation()
           && m_values[k.index()].has_value();
  }

  // Bump a slot's generation, skipping 0: generation 0 is reserved as the
  // null-key sentinel, so a wrapped counter can never collide with a default
  // key (or with a freshly allocated slot, which starts at 1).
  constexpr auto bump_generation(index_type const index) noexcept -> void {
    if (++m_generations[index] == 0) {
      m_generations[index] = generation_type{1};
    }
  }

  template <bool IsConst>
  class basic_iterator {
  private:
    using slot_iter = std::conditional_t<
      IsConst,
      typename std::vector<std::optional<T>>::const_iterator,
      typename std::vector<std::optional<T>>::iterator>;

  public:
    using value_type = T;
    using reference = std::conditional_t<IsConst, T const&, T&>;
    using pointer = std::conditional_t<IsConst, T const*, T*>;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::forward_iterator_tag;
    using iterator_concept = std::forward_iterator_tag;

    constexpr basic_iterator() noexcept = default;

    constexpr basic_iterator(slot_iter const current, slot_iter const end) noexcept
        : m_current{current}, m_end{end} {
      skip_vacant();
    }

    // Convert a mutable iterator to a const_iterator.
    template <bool OtherConst>
      requires(IsConst && !OtherConst)
    constexpr basic_iterator(basic_iterator<OtherConst> const& other) noexcept
        : m_current{other.m_current}, m_end{other.m_end} {}

    [[nodiscard]] constexpr auto operator*() const noexcept -> reference {
      return m_current->value();
    }

    [[nodiscard]] constexpr auto operator->() const noexcept -> pointer {
      return std::addressof(m_current->value());
    }

    constexpr auto operator++() noexcept -> basic_iterator& {
      ++m_current;
      skip_vacant();
      return *this;
    }

    constexpr auto operator++(int) noexcept -> basic_iterator {
      auto const tmp{*this};
      ++*this;
      return tmp;
    }

    [[nodiscard]] friend constexpr auto
    operator==(basic_iterator const& a, basic_iterator const& b) noexcept -> bool {
      return a.m_current == b.m_current;
    }

  private:
    slot_iter m_current{};
    slot_iter m_end{};

    constexpr auto skip_vacant() noexcept -> void {
      while (m_current != m_end && !m_current->has_value()) {
        ++m_current;
      }
    }

    template <bool>
    friend class basic_iterator;
  };

public:
  using iterator = basic_iterator<false>;
  using const_iterator = basic_iterator<true>;

  /**
   * @brief Default-constructs an empty map with no allocated storage.
   *
   * @pre None.
   * @post \c empty() is \c true and \c capacity() is zero.
   */
  constexpr slot_map() noexcept = default;

  /**
   * @brief Pre-allocates space for \p initial_capacity slots.
   *
   * @param initial_capacity Slot count to reserve up front.
   *
   * @pre None.
   * @post \c empty() is \c true and \c capacity() is at least
   *       \p initial_capacity.
   */
  explicit slot_map(size_type const initial_capacity) noexcept {
    m_values.reserve(initial_capacity);
    m_generations.reserve(initial_capacity);
  }

  /**
   * @brief Number of live elements in the map.
   *
   * @return Count of live elements, excluding vacant slots.
   *
   * @pre None.
   * @post None. The map is not modified.
   */
  [[nodiscard]] constexpr auto size() const noexcept -> size_type {
    return m_size;
  }

  /**
   * @brief Whether no live element exists.
   *
   * @return \c true when \c size() is zero.
   *
   * @pre None.
   * @post None. The map is not modified.
   */
  [[nodiscard]] constexpr auto empty() const noexcept -> bool {
    return m_size == 0;
  }

  /**
   * @brief Number of slots allocated, live plus vacant.
   *
   * @return Allocated slot count.
   *
   * @pre None.
   * @post None. The map is not modified.
   */
  [[nodiscard]] constexpr auto capacity() const noexcept -> size_type {
    return m_values.capacity();
  }

  /**
   * @brief Largest number of slots the map can ever hold.
   *
   * @return The maximum size of the backing vector.
   *
   * @pre None.
   * @post None. The map is not modified.
   */
  [[nodiscard]] constexpr auto max_size() const noexcept -> size_type {
    return m_values.max_size();
  }

  /**
   * @brief Releases unused capacity across all internal vectors.
   *
   * @pre None.
   * @post \c size() is unchanged; capacity may shrink. Existing keys remain
   *       valid.
   */
  constexpr auto shrink_to_fit() noexcept -> void {
    m_values.shrink_to_fit();
    m_generations.shrink_to_fit();
    m_free_list.shrink_to_fit();
  }

  /**
   * @brief Swaps contents with \p other.
   *
   * @param other Map to exchange state with.
   *
   * @pre None.
   * @post This map holds \p other's former elements and vice versa; keys issued
   *       by each map remain valid against that map's new owner.
   */
  constexpr auto swap(slot_map& other) noexcept -> void {
    using std::swap;
    m_values.swap(other.m_values);
    m_generations.swap(other.m_generations);
    m_free_list.swap(other.m_free_list);
    swap(m_size, other.m_size);
  }

  /**
   * @brief Swaps the contents of \p a and \p b.
   *
   * @param a First map.
   * @param b Second map.
   *
   * @pre None.
   * @post \p a and \p b have exchanged state.
   */
  friend constexpr auto swap(slot_map& a, slot_map& b) noexcept -> void {
    a.swap(b);
  }

  /**
   * @brief Reserves storage for at least \p n slots.
   *
   * @param n Minimum slot count to reserve.
   *
   * @pre None.
   * @post \c capacity() is at least \p n; \c size() is unchanged and existing
   *       keys remain valid.
   */
  auto reserve(size_type const n) noexcept -> void {
    m_values.reserve(n);
    m_generations.reserve(n);
  }

  /**
   * @brief Erases every element and resets internal state.
   *
   * @pre None.
   * @post \c empty() is \c true and every previously-issued key reads as absent.
   *       Capacity is retained.
   *
   * @complexity \c O(capacity).
   */
  auto clear() noexcept -> void {
    for (size_type i{0}; i < m_values.size(); ++i) {
      if (m_values[i].has_value()) {
        m_values[i].reset();
        bump_generation(static_cast<index_type>(i));
      }
    }
    m_free_list.clear();
    m_free_list.reserve(m_values.size());
    for (size_type i{m_values.size()}; i > 0; --i) {
      m_free_list.push_back(static_cast<index_type>(i - 1));
    }
    m_size = 0;
  }

  /**
   * @brief Inserts a copy of \p value and returns its key.
   *
   * @param value The value to copy in.
   *
   * @return Stable key referencing the newly inserted element.
   *
   * @pre None.
   * @post \c size() grew by one and the returned key refers to a live element
   *       holding \p value. Existing keys remain valid.
   *
   * @complexity Amortised \c O(1).
   */
  auto insert(T const& value) noexcept -> key {
    return emplace(value);
  }

  /**
   * @brief Inserts \p value by moving it and returns its key.
   *
   * @param value The value to move in.
   *
   * @return Stable key referencing the newly inserted element.
   *
   * @pre None.
   * @post \c size() grew by one and the returned key refers to a live element
   *       holding \p value. Existing keys remain valid.
   *
   * @complexity Amortised \c O(1).
   */
  auto insert(T&& value) noexcept -> key {
    return emplace(std::move(value));
  }

  /**
   * @brief Constructs an element in place, forwarding \p args to \p T.
   *
   * @tparam Args Constructor argument types.
   * @param args Arguments forwarded to \p T's constructor.
   *
   * @return Stable key referencing the newly constructed element.
   *
   * @pre None.
   * @post \c size() grew by one and the returned key refers to the new element.
   *       Existing keys remain valid.
   *
   * @complexity Amortised \c O(1).
   */
  template <typename... Args>
  auto emplace(Args&&... args) noexcept -> key {
    if (m_free_list.empty()) {
      auto const index{static_cast<index_type>(m_values.size())};
      m_values.emplace_back(std::in_place, std::forward<Args>(args)...);
      // Fresh slots start at generation 1; generation 0 is the null sentinel.
      m_generations.push_back(generation_type{1});
      ++m_size;
      return key{index, generation_type{1}};
    }
    auto const index{m_free_list.back()};
    m_free_list.pop_back();
    m_values[index].emplace(std::forward<Args>(args)...);
    ++m_size;
    return key{index, m_generations[index]};
  }

  /**
   * @brief Erases the element referenced by \p k.
   *
   * @param k Key to erase.
   *
   * @return \c true when an element was removed, \c false when \p k is stale or
   *         out of range (a no-op).
   *
   * @pre None.
   * @post On \c true \c size() shrank by one and \p k is permanently invalid;
   *       otherwise the map is unchanged.
   *
   * @complexity \c O(1).
   */
  auto erase(key const k) noexcept -> bool {
    if (!is_live(k)) {
      return false;
    }
    m_values[k.index()].reset();
    bump_generation(k.index());
    m_free_list.push_back(k.index());
    --m_size;
    return true;
  }

  /**
   * @brief Pointer to the element referenced by \p k, or \c nullptr on a miss.
   *
   * @param k Key to look up.
   *
   * @return Pointer to the live element, or \c nullptr when \p k is out of
   *         range, erased, or its generation no longer matches.
   *
   * @pre None.
   * @post None. The map is not modified.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] auto find(key const k) noexcept -> T* {
    return is_live(k) ? std::addressof(m_values[k.index()].value()) : nullptr;
  }

  /**
   * @brief Pointer to the const element referenced by \p k, or \c nullptr.
   *
   * @param k Key to look up.
   *
   * @return Pointer to the const live element, or \c nullptr when \p k is out of
   *         range, erased, or its generation no longer matches.
   *
   * @pre None.
   * @post None. The map is not modified.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] auto find(key const k) const noexcept -> T const* {
    return is_live(k) ? std::addressof(m_values[k.index()].value()) : nullptr;
  }

  /**
   * @brief Whether \p k refers to a live element.
   *
   * @param k Key to test.
   *
   * @return \c true when \c find(k) would return non-null.
   *
   * @pre None.
   * @post None. The map is not modified.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] auto contains(key const k) const noexcept -> bool {
    return is_live(k);
  }

  /**
   * @brief Iterator to the first live element.
   *
   * @return Iterator positioned at the first occupied slot.
   *
   * @pre None.
   * @post None. The map is not modified.
   */
  [[nodiscard]] auto begin() noexcept -> iterator {
    return iterator{m_values.begin(), m_values.end()};
  }

  /**
   * @brief Iterator one past the last live element.
   *
   * @return End iterator over the live elements.
   *
   * @pre None.
   * @post None. The map is not modified.
   */
  [[nodiscard]] auto end() noexcept -> iterator {
    return iterator{m_values.end(), m_values.end()};
  }

  /**
   * @brief Const iterator to the first live element.
   *
   * @return Const iterator positioned at the first occupied slot.
   *
   * @pre None.
   * @post None. The map is not modified.
   */
  [[nodiscard]] auto begin() const noexcept -> const_iterator {
    return const_iterator{m_values.begin(), m_values.end()};
  }

  /**
   * @brief Const iterator one past the last live element.
   *
   * @return End const iterator over the live elements.
   *
   * @pre None.
   * @post None. The map is not modified.
   */
  [[nodiscard]] auto end() const noexcept -> const_iterator {
    return const_iterator{m_values.end(), m_values.end()};
  }

  /**
   * @brief Const iterator to the first live element.
   *
   * @return Const iterator positioned at the first occupied slot.
   *
   * @pre None.
   * @post None. The map is not modified.
   */
  [[nodiscard]] auto cbegin() const noexcept -> const_iterator {
    return begin();
  }

  /**
   * @brief Const iterator one past the last live element.
   *
   * @return End const iterator over the live elements.
   *
   * @pre None.
   * @post None. The map is not modified.
   */
  [[nodiscard]] auto cend() const noexcept -> const_iterator {
    return end();
  }
};

}  // namespace nexenne::container
