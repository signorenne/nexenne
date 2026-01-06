#pragma once

/**
 * @file
 * @brief Compile-time sorted map backed by a fixed-size array.
 *
 * Stores \p N key-value pairs in a \c std::array sorted by key, so lookups use
 * binary search. The constructor sorts the entries, so the caller may supply
 * them in any order. Suited to enum-to-string tables, name-keyed config
 * constants, and dispatch tables: any \c constexpr lookup where a \c std::map
 * would be overkill or unavailable at compile time.
 */

#include <algorithm>
#include <array>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <functional>
#include <utility>

namespace nexenne::utility {

/**
 * @brief Compile-time sorted map backed by a fixed-size array.
 *
 * @tparam K Key type, totally ordered via \c operator<.
 * @tparam V Value type.
 * @tparam N Number of entries.
 *
 * @pre None.
 * @post After construction the entries are sorted by ascending key.
 *
 * @par Example
 * \code
 * constexpr auto names{nexenne::utility::constexpr_map{std::array{
 *   std::pair{1, "one"},
 *   std::pair{3, "three"},
 *   std::pair{2, "two"},
 * }}};
 * static_assert(names.contains(2));
 * static_assert(*names.find(3) == std::string_view{"three"});
 * \endcode
 */
template <std::totally_ordered K, typename V, std::size_t N>
class constexpr_map {
public:
  using key_type = K;
  using mapped_type = V;
  using value_type = std::pair<K, V>;
  using size_type = std::size_t;

  static constexpr size_type capacity{N};

private:
  std::array<value_type, N> m_data{};

public:
  /**
   * @brief Constructs the map from an array of pairs and sorts it by key.
   *
   * Works in \c constexpr and at runtime. Only the key participates in the
   * ordering, so \p V need not be comparable. Duplicate keys are kept; which
   * one \c find returns is then unspecified.
   *
   * @param data Array of key-value pairs in any order, moved in.
   *
   * @pre None.
   * @post The stored entries are sorted by ascending key.
   *
   * @complexity \c O(N log N).
   */
  explicit constexpr constexpr_map(std::array<value_type, N> data) noexcept
      : m_data{std::move(data)} {
    std::ranges::sort(m_data, std::ranges::less{}, &value_type::first);
  }

  /**
   * @brief Looks up \p key by binary search.
   *
   * @param key Key to search for.
   *
   * @return Pointer to the mapped value on a hit, \c nullptr on a miss.
   *
   * @pre None.
   * @post None.
   *
   * @complexity \c O(log N) comparisons.
   */
  [[nodiscard]] constexpr auto find(key_type const& key) const noexcept -> mapped_type const* {
    auto const it{std::ranges::lower_bound(m_data, key, std::ranges::less{}, &value_type::first)};
    if (it != m_data.end() && !(key < it->first)) {
      return &it->second;
    }
    return nullptr;
  }

  /**
   * @brief Looks up \p key, asserting it is present.
   *
   * @param key Key that must be present.
   *
   * @return Const reference to the mapped value.
   *
   * @pre \p key is present; a missing key asserts in debug and dereferences
   *      \c nullptr in release.
   * @post None.
   *
   * @complexity \c O(log N) comparisons.
   */
  [[nodiscard]] constexpr auto operator[](key_type const& key
  ) const noexcept -> mapped_type const& {
    auto const* const found{find(key)};
    assert(found != nullptr && "constexpr_map: key not found");
    return *found;
  }

  /**
   * @brief Reports whether \p key is present.
   *
   * @param key Key to test for.
   *
   * @return \c true when the key is in the map.
   *
   * @pre None.
   * @post None.
   *
   * @complexity \c O(log N) comparisons.
   */
  [[nodiscard]] constexpr auto contains(key_type const& key) const noexcept -> bool {
    return find(key) != nullptr;
  }

  /**
   * @brief The number of entries.
   *
   * @return \p N.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto size() const noexcept -> size_type {
    return N;
  }

  /**
   * @brief Iterator to the first entry (lowest key).
   *
   * @return Iterator to the first key-value pair.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto begin() const noexcept {
    return m_data.begin();
  }

  /**
   * @brief Iterator one past the last entry.
   *
   * @return Iterator past the final key-value pair.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto end() const noexcept {
    return m_data.end();
  }

  /**
   * @brief The underlying sorted array of pairs.
   *
   * @return Const reference to the internal \c std::array.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto data() const noexcept -> std::array<value_type, N> const& {
    return m_data;
  }
};

/**
 * @brief Deduces \c constexpr_map's \p K, \p V, \p N from an array of pairs.
 *
 * @tparam K Key type.
 * @tparam V Value type.
 * @tparam N Number of entries.
 *
 * @pre None.
 * @post \c constexpr_map{arr} deduces \c constexpr_map<K, V, N>.
 */
template <typename K, typename V, std::size_t N>
constexpr_map(std::array<std::pair<K, V>, N>) -> constexpr_map<K, V, N>;

}  // namespace nexenne::utility
