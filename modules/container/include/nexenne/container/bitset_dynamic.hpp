#pragma once

/**
 * @file
 * @brief Runtime-sized bitset packed into 64-bit words.
 *
 * \c bitset_dynamic is the missing option between \c std::bitset<N> (size fixed
 * at compile time) and \c std::vector<bool> (no bitwise operators, no popcount,
 * no scan-for-set). Storage is a contiguous \c std::vector<std::uint64_t>, and
 * every bit operation maps to one or two word-level intrinsics.
 *
 * Reach for it for archetype/component masks whose width is known only at run
 * time, per-frame active/dirty flags over a dynamic list, visited sets in graph
 * traversals, and any dense "set of integer indices in a known range" that beats
 * a hash set on memory and speed. Every operation is \c noexcept and \c constexpr
 * (so a bitset can be built and queried at compile time); allocation failure
 * terminates. The logical bit count is stored separately, so the tail of the
 * last word may hold garbage and is masked off after every mutation. \c words()
 * exposes the raw storage and \c set_bits() yields a sparse-aware forward range
 * over the indices of set bits, for \c std::ranges interop. Thread safety is the
 * standard-library convention: concurrent reads are fine, concurrent mutation is
 * not.
 */

#include <bit>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <span>
#include <utility>
#include <vector>

#include <nexenne/container/error.hpp>

namespace nexenne::container {

/**
 * @brief Runtime-sized bitset packed into 64-bit words.
 *
 * @pre None.
 * @post A default-constructed bitset is empty.
 */
class bitset_dynamic {
public:
  using value_type = bool;
  using size_type = std::size_t;
  using word_type = std::uint64_t;

  static constexpr size_type bits_per_word{64};

private:
  std::vector<word_type> m_words;
  size_type m_size{};

  /**
   * @brief Index of the word holding bit \p i.
   *
   * A shift, because \c bits_per_word is a power of two.
   *
   * @param i Bit index.
   *
   * @return The index into \c m_words that holds bit \p i.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] static constexpr auto word_index(size_type const i) noexcept -> size_type {
    return i / bits_per_word;
  }

  /**
   * @brief Offset of bit \p i within its word.
   *
   * A mask, because \c bits_per_word is a power of two.
   *
   * @param i Bit index.
   *
   * @return The bit position of \p i within its word, in \c [0, bits_per_word).
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] static constexpr auto bit_index(size_type const i) noexcept -> size_type {
    return i % bits_per_word;
  }

  /**
   * @brief Number of 64-bit words needed to hold \p n bits.
   *
   * Terminates on a request past \c max_size(), whose round-up would otherwise
   * wrap and yield zero backing words for a huge logical size (a later checked
   * write would then corrupt the heap).
   *
   * @param n Bit count.
   *
   * @return \c ceil(n / bits_per_word).
   *
   * @pre None.
   * @post The returned count never wraps, or the process terminated.
   */
  [[nodiscard]] static constexpr auto word_count(size_type const n) noexcept -> size_type {
    // The round-up (n + bits_per_word - 1) wraps for n past
    // SIZE_MAX - (bits_per_word - 1), yielding zero backing words for a huge
    // logical size; a later checked write would then index an empty word vector
    // (heap corruption). Such a size is unallocatable anyway, so fail loudly.
    if (n > max_size()) {
      std::terminate();
    }
    return (n + bits_per_word - 1) / bits_per_word;
  }

  /**
   * @brief Masks off the bits beyond \c m_size in the final word.
   *
   * Maintaining this invariant after every mutation is what lets \c count,
   * \c any, and \c all stay correct without per-call masking.
   *
   * @pre None.
   * @post Every bit at or above \c m_size in the last word is 0.
   */
  constexpr auto clear_tail_bits() noexcept -> void {
    auto const tail{m_size % bits_per_word};
    if (tail != 0 && !m_words.empty()) {
      auto const mask{(word_type{1} << tail) - 1};
      m_words.back() &= mask;
    }
  }

public:
  /**
   * @brief Forward iterator over the indices of set bits.
   *
   * Skips zero bits with \c std::countr_zero per word, so a sparse bitset
   * iterates in proportion to the number of set bits, not the total length.
   *
   * @pre A dereferenced or incremented iterator refers to a live bitset whose
   *      storage has not been resized since the iterator was obtained.
   * @post A default-constructed iterator compares equal to the end of an empty
   *       range; dereferencing yields the index of a set bit.
   */
  class set_bit_iterator {
  private:
    word_type const* m_words{nullptr};
    size_type m_word_idx{};
    size_type m_word_count{};
    word_type m_remaining{};

    /**
     * @brief Advances to the next word that has a set bit, or to the end.
     *
     * @pre None.
     * @post \c m_remaining is non-zero, or \c m_word_idx equals \c m_word_count.
     */
    constexpr auto advance() noexcept -> void {
      while (m_remaining == 0) {
        ++m_word_idx;
        if (m_word_idx >= m_word_count) {
          m_word_idx = m_word_count;
          return;
        }
        m_remaining = m_words[m_word_idx];
      }
    }

  public:
    using value_type = size_type;
    using reference = size_type;
    using pointer = void;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::forward_iterator_tag;
    using iterator_concept = std::forward_iterator_tag;

    /**
     * @brief Constructs a past-the-end iterator over an empty range.
     *
     * @pre None.
     * @post The iterator compares equal to any other end iterator.
     */
    constexpr set_bit_iterator() noexcept = default;

    /**
     * @brief Constructs an iterator at the first set bit from \p word_idx on.
     *
     * @param words Pointer to the backing word storage.
     * @param word_idx Word index to begin scanning at.
     * @param word_count Number of backing words, the past-the-end word index.
     *
     * @pre \p words addresses \p word_count contiguous words, or \p word_count is
     *      zero.
     * @post The iterator refers to the first set bit at or after \p word_idx, or
     *       to the end.
     */
    constexpr set_bit_iterator(
      word_type const* words, size_type const word_idx, size_type const word_count
    ) noexcept
        : m_words{words}, m_word_idx{word_idx}, m_word_count{word_count} {
      if (m_word_idx < m_word_count) {
        m_remaining = m_words[m_word_idx];
        advance();
      }
    }

    /**
     * @brief The index of the bit the iterator refers to.
     *
     * @return The absolute index of the current set bit.
     *
     * @pre The iterator is not past the end.
     * @post None.
     */
    [[nodiscard]] constexpr auto operator*() const noexcept -> size_type {
      return m_word_idx * bits_per_word + static_cast<size_type>(std::countr_zero(m_remaining));
    }

    /**
     * @brief Advances to the next set bit.
     *
     * @return Reference to \c *this.
     *
     * @pre The iterator is not past the end.
     * @post The iterator refers to the next set bit, or to the end.
     */
    constexpr auto operator++() noexcept -> set_bit_iterator& {
      m_remaining &= m_remaining - 1;  // drop the lowest set bit, then scan on
      advance();
      return *this;
    }

    /**
     * @brief Advances to the next set bit, returning the prior position.
     *
     * @return A copy of the iterator before the increment.
     *
     * @pre The iterator is not past the end.
     * @post The iterator refers to the next set bit, or to the end.
     */
    constexpr auto operator++(int) noexcept -> set_bit_iterator {
      auto const previous{*this};
      ++*this;
      return previous;
    }

    /**
     * @brief Equality of two set-bit iterators.
     *
     * @param a Left iterator.
     * @param b Right iterator.
     *
     * @return \c true when both refer to the same position in the same storage.
     *
     * @pre None.
     * @post None.
     */
    [[nodiscard]] friend constexpr auto
    operator==(set_bit_iterator const& a, set_bit_iterator const& b) noexcept -> bool {
      return a.m_words == b.m_words && a.m_word_idx == b.m_word_idx
             && a.m_remaining == b.m_remaining;
    }
  };

  /**
   * @brief A lightweight forward range over the set-bit indices.
   *
   * @pre The backing storage outlives the range and is not resized while it is
   *      iterated.
   * @post \c begin() and \c end() delimit the set-bit indices of the storage.
   */
  class set_bits_range {
  private:
    word_type const* m_words{nullptr};
    size_type m_word_count{};

  public:
    /**
     * @brief Constructs an empty range.
     *
     * @pre None.
     * @post \c begin() equals \c end().
     */
    constexpr set_bits_range() noexcept = default;

    /**
     * @brief Constructs a range over \p word_count words of \p words.
     *
     * @param words Pointer to the backing word storage.
     * @param word_count Number of backing words.
     *
     * @pre \p words addresses \p word_count contiguous words, or \p word_count is
     *      zero.
     * @post The range spans the set-bit indices of \p words.
     */
    constexpr set_bits_range(word_type const* words, size_type const word_count) noexcept
        : m_words{words}, m_word_count{word_count} {}

    /**
     * @brief Iterator to the first set-bit index.
     *
     * @return Iterator to the first set bit, or \c end() when none is set.
     *
     * @pre None.
     * @post None.
     */
    [[nodiscard]] constexpr auto begin() const noexcept -> set_bit_iterator {
      return set_bit_iterator{m_words, 0, m_word_count};
    }

    /**
     * @brief Iterator one past the last set-bit index.
     *
     * @return The past-the-end iterator.
     *
     * @pre None.
     * @post None.
     */
    [[nodiscard]] constexpr auto end() const noexcept -> set_bit_iterator {
      return set_bit_iterator{m_words, m_word_count, m_word_count};
    }
  };

  /**
   * @brief Constructs an empty bitset.
   *
   * @pre None.
   * @post \c empty() is \c true.
   */
  constexpr bitset_dynamic() noexcept = default;

  /**
   * @brief Constructs \p n bits, each initialised to \p value.
   *
   * @param n Number of bits.
   * @param value Initial value of every bit.
   *
   * @pre None.
   * @post \c size() equals \p n and every bit equals \p value.
   */
  explicit constexpr bitset_dynamic(size_type const n, bool const value = false) noexcept
      : m_words(word_count(n), value ? ~word_type{0} : word_type{0}), m_size{n} {
    clear_tail_bits();
  }

  /**
   * @brief Constructs from an explicit list of bit values, in index order.
   *
   * @param init Bit values, bit \c i taken from the \c i-th element.
   *
   * @pre None.
   * @post \c size() equals \c init.size() and each bit matches \p init.
   */
  constexpr bitset_dynamic(std::initializer_list<bool> const init) noexcept
      : m_words(word_count(init.size()), 0), m_size{init.size()} {
    size_type i{0};
    for (auto const bit : init) {
      if (bit) {
        m_words[word_index(i)] |= word_type{1} << bit_index(i);
      }
      ++i;
    }
  }

  /// @brief Copy-constructs from another bitset.
  constexpr bitset_dynamic(bitset_dynamic const&) = default;

  /**
   * @brief Copy-assigns from another bitset.
   *
   * @return Reference to \c *this.
   *
   * @pre None.
   * @post This bitset holds a copy of the source's bits.
   */
  constexpr auto operator=(bitset_dynamic const&) -> bitset_dynamic& = default;

  /// @brief Destroys the bitset and frees its word storage.
  ~bitset_dynamic() noexcept = default;

  /**
   * @brief Move-constructs from \p other, leaving it empty.
   *
   * The custom move also zeroes \p other's bit count, so it stays consistent
   * with its moved-out word storage rather than reporting a stale size.
   *
   * @param other Source bitset, emptied after the move.
   *
   * @pre None.
   * @post This bitset holds \p other's former bits; \p other is empty.
   */
  constexpr bitset_dynamic(bitset_dynamic&& other) noexcept
      : m_words{std::move(other.m_words)}, m_size{other.m_size} {
    other.m_size = 0;
  }

  /**
   * @brief Move-assigns from \p other, leaving it empty.
   *
   * @param other Source bitset, emptied after the move.
   *
   * @return Reference to \c *this.
   *
   * @pre None.
   * @post This bitset holds \p other's former bits; \p other is empty.
   */
  constexpr auto operator=(bitset_dynamic&& other) noexcept -> bitset_dynamic& {
    if (this != &other) {
      m_words = std::move(other.m_words);
      m_size = other.m_size;
      other.m_size = 0;
    }
    return *this;
  }

  /**
   * @brief Number of bits.
   *
   * @return The logical bit count.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto size() const noexcept -> size_type {
    return m_size;
  }

  /**
   * @brief Reports whether the bitset holds no bits.
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
   * @brief The largest number of bits the bitset can address.
   *
   * Bounded so the backing word count \c ceil(size / 64) never wraps; a request
   * past it terminates rather than allocating zero words for a huge size.
   *
   * @return The maximum bit count.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] static constexpr auto max_size() noexcept -> size_type {
    return std::numeric_limits<size_type>::max() - (bits_per_word - 1);
  }

  /**
   * @brief Number of 64-bit words backing the bitset.
   *
   * @return The backing word count, \c ceil(size() / 64).
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto word_size() const noexcept -> size_type {
    return m_words.size();
  }

  /**
   * @brief Releases unused backing capacity.
   *
   * @pre None.
   * @post Bit values and \c size() are unchanged.
   */
  constexpr auto shrink_to_fit() noexcept -> void {
    m_words.shrink_to_fit();
  }

  /**
   * @brief Swaps contents with \p other in constant time.
   *
   * @param other Bitset to exchange state with.
   *
   * @pre None.
   * @post This bitset holds \p other's former bits and vice versa.
   *
   * @complexity \c O(1).
   */
  constexpr auto swap(bitset_dynamic& other) noexcept -> void {
    m_words.swap(other.m_words);
    using std::swap;
    swap(m_size, other.m_size);
  }

  /**
   * @brief Swaps the contents of \p a and \p b.
   *
   * @param a First bitset.
   * @param b Second bitset.
   *
   * @pre None.
   * @post \p a and \p b have exchanged state.
   */
  friend constexpr auto swap(bitset_dynamic& a, bitset_dynamic& b) noexcept -> void {
    a.swap(b);
  }

  /**
   * @brief Resizes to \p n bits; bits added on a grow are set to \p value.
   *
   * @param n New bit count.
   * @param value Value of bits added when growing.
   *
   * @pre None.
   * @post \c size() equals \p n; bits in \c [old_size, n) equal \p value when
   *       growing; bits at or above \p n are dropped when shrinking.
   */
  constexpr auto resize(size_type const n, bool const value = false) noexcept -> void {
    auto const old_size{m_size};
    auto const old_words{m_words.size()};

    m_words.resize(word_count(n), value ? ~word_type{0} : word_type{0});
    m_size = n;

    // Fill the gap inside the previously-partial last word when growing past it.
    if (value && n > old_size && old_words > 0) {
      auto const boundary{old_words * bits_per_word};
      auto const fill_to{n < boundary ? n : boundary};
      for (auto i{old_size}; i < fill_to; ++i) {
        m_words[word_index(i)] |= word_type{1} << bit_index(i);
      }
    }
    clear_tail_bits();
  }

  /**
   * @brief Appends a single bit.
   *
   * @param value Value of the appended bit.
   *
   * @pre None.
   * @post \c size() grew by one and the bit at the old \c size() equals
   *       \p value.
   *
   * @complexity Amortised \c O(1).
   */
  constexpr auto push_back(bool const value) noexcept -> void {
    auto const new_size{m_size + 1};
    if (word_count(new_size) > m_words.size()) {
      m_words.push_back(0);
    }
    if (value) {
      m_words[word_index(m_size)] |= word_type{1} << bit_index(m_size);
    }
    m_size = new_size;
  }

  /**
   * @brief Drops every bit; backing capacity is retained.
   *
   * @pre None.
   * @post \c size() is zero.
   */
  constexpr auto clear() noexcept -> void {
    m_words.clear();
    m_size = 0;
  }

  /**
   * @brief Reserves backing storage for at least \p n bits.
   *
   * @param n Lower bound on the bit capacity to allocate.
   *
   * @pre None.
   * @post Capacity is sufficient for \p n bits; size and bit values are
   *       unchanged.
   */
  constexpr auto reserve(size_type const n) noexcept -> void {
    m_words.reserve(word_count(n));
  }

  /**
   * @brief Sets bit \p index to 1.
   *
   * @param index Index of the bit.
   *
   * @return Nothing on success, or \c container_error::out_of_range when
   *         \p index is past the end.
   *
   * @pre None.
   * @post On success bit \p index is 1; on failure the bitset is unchanged.
   */
  [[nodiscard]] constexpr auto set(size_type const index) noexcept -> result<void> {
    if (index >= m_size) {
      return std::unexpected{container_error::out_of_range};
    }
    m_words[word_index(index)] |= word_type{1} << bit_index(index);
    return {};
  }

  /**
   * @brief Clears bit \p index to 0.
   *
   * @param index Index of the bit.
   *
   * @return Nothing on success, or \c container_error::out_of_range when
   *         \p index is past the end.
   *
   * @pre None.
   * @post On success bit \p index is 0; on failure the bitset is unchanged.
   */
  [[nodiscard]] constexpr auto reset(size_type const index) noexcept -> result<void> {
    if (index >= m_size) {
      return std::unexpected{container_error::out_of_range};
    }
    m_words[word_index(index)] &= ~(word_type{1} << bit_index(index));
    return {};
  }

  /**
   * @brief Flips bit \p index.
   *
   * @param index Index of the bit.
   *
   * @return Nothing on success, or \c container_error::out_of_range when
   *         \p index is past the end.
   *
   * @pre None.
   * @post On success bit \p index is the complement of its prior value; on
   *       failure the bitset is unchanged.
   */
  [[nodiscard]] constexpr auto flip(size_type const index) noexcept -> result<void> {
    if (index >= m_size) {
      return std::unexpected{container_error::out_of_range};
    }
    m_words[word_index(index)] ^= word_type{1} << bit_index(index);
    return {};
  }

  /**
   * @brief Reads bit \p index, bounds-checked.
   *
   * @param index Index of the bit.
   *
   * @return The bit value, or \c container_error::out_of_range when \p index is
   *         past the end.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto test(size_type const index) const noexcept -> result<bool> {
    if (index >= m_size) {
      return std::unexpected{container_error::out_of_range};
    }
    return static_cast<bool>((m_words[word_index(index)] >> bit_index(index)) & word_type{1});
  }

  /**
   * @brief Unchecked bit read.
   *
   * @param index Index of the bit.
   *
   * @return The bit value.
   *
   * @pre \p index is less than \c size(); a larger index is undefined
   *      behaviour. Use \c test for a checked read.
   * @post None.
   */
  [[nodiscard]] constexpr auto operator[](size_type const index) const noexcept -> bool {
    return static_cast<bool>((m_words[word_index(index)] >> bit_index(index)) & word_type{1});
  }

  /**
   * @brief Sets every bit to 1.
   *
   * @pre None.
   * @post Every bit in \c [0, size()) is 1.
   */
  constexpr auto set_all() noexcept -> void {
    for (auto& word : m_words) {
      word = ~word_type{0};
    }
    clear_tail_bits();
  }

  /**
   * @brief Clears every bit to 0.
   *
   * @pre None.
   * @post Every bit is 0.
   */
  constexpr auto reset_all() noexcept -> void {
    for (auto& word : m_words) {
      word = 0;
    }
  }

  /**
   * @brief Flips every bit.
   *
   * @pre None.
   * @post Every bit in \c [0, size()) is the complement of its prior value.
   */
  constexpr auto flip_all() noexcept -> void {
    for (auto& word : m_words) {
      word = ~word;
    }
    clear_tail_bits();
  }

  /**
   * @brief Number of bits set to 1.
   *
   * @return The set-bit count, summed with \c std::popcount per word.
   *
   * @pre None.
   * @post None.
   *
   * @complexity \c O(word_size()).
   */
  [[nodiscard]] constexpr auto count() const noexcept -> size_type {
    size_type total{0};
    for (auto const word : m_words) {
      total += static_cast<size_type>(std::popcount(word));
    }
    return total;
  }

  /**
   * @brief Reports whether any bit is set.
   *
   * @return \c true when at least one bit is 1.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto any() const noexcept -> bool {
    for (auto const word : m_words) {  // early-exit on the first non-zero word
      if (word != 0) {
        return true;
      }
    }
    return false;
  }

  /**
   * @brief Reports whether no bit is set.
   *
   * @return \c true when every bit is 0.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto none() const noexcept -> bool {
    return !any();
  }

  /**
   * @brief Reports whether every bit is set.
   *
   * @return \c true when all \c size() bits are 1 (vacuously \c true when
   *         empty).
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto all() const noexcept -> bool {
    return count() == m_size;
  }

  /**
   * @brief Index of the first set bit at or after \p from.
   *
   * @param from Index to start scanning from, inclusive.
   *
   * @return The index of the first set bit at or after \p from, or \c size()
   *         when none exists (the past-the-end sentinel).
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto
  find_first_set(size_type const from = 0) const noexcept -> size_type {
    if (from >= m_size) {
      return m_size;
    }
    auto index{word_index(from)};
    auto word{m_words[index] & (~word_type{0} << bit_index(from))};
    while (true) {
      if (word != 0) {
        auto const bit{index * bits_per_word + static_cast<size_type>(std::countr_zero(word))};
        return bit < m_size ? bit : m_size;
      }
      ++index;
      if (index >= m_words.size()) {
        return m_size;
      }
      word = m_words[index];
    }
  }

  /**
   * @brief In-place bitwise AND with \p other (the shorter width wins).
   *
   * @param other Right operand.
   *
   * @return Reference to \c *this.
   *
   * @pre None.
   * @post Each bit is the AND of the operands; bits past \p other's width
   *       become 0.
   */
  constexpr auto operator&=(bitset_dynamic const& other) noexcept -> bitset_dynamic& {
    auto const shared{
      m_words.size() < other.m_words.size() ? m_words.size() : other.m_words.size()
    };
    for (size_type i{0}; i < shared; ++i) {
      m_words[i] &= other.m_words[i];
    }
    for (auto i{shared}; i < m_words.size(); ++i) {
      m_words[i] = 0;
    }
    clear_tail_bits();
    return *this;
  }

  /**
   * @brief In-place bitwise OR with \p other.
   *
   * @param other Right operand.
   *
   * @return Reference to \c *this.
   *
   * @pre None.
   * @post Each bit is the OR of the operands over their common width.
   */
  constexpr auto operator|=(bitset_dynamic const& other) noexcept -> bitset_dynamic& {
    auto const shared{
      m_words.size() < other.m_words.size() ? m_words.size() : other.m_words.size()
    };
    for (size_type i{0}; i < shared; ++i) {
      m_words[i] |= other.m_words[i];
    }
    clear_tail_bits();
    return *this;
  }

  /**
   * @brief In-place bitwise XOR with \p other.
   *
   * @param other Right operand.
   *
   * @return Reference to \c *this.
   *
   * @pre None.
   * @post Each bit is the XOR of the operands over their common width.
   */
  constexpr auto operator^=(bitset_dynamic const& other) noexcept -> bitset_dynamic& {
    auto const shared{
      m_words.size() < other.m_words.size() ? m_words.size() : other.m_words.size()
    };
    for (size_type i{0}; i < shared; ++i) {
      m_words[i] ^= other.m_words[i];
    }
    clear_tail_bits();
    return *this;
  }

  /**
   * @brief Bitwise AND of \p a and \p b.
   *
   * @param a Left operand, taken by value.
   * @param b Right operand.
   *
   * @return The AND of \p a and \p b.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] friend constexpr auto
  operator&(bitset_dynamic a, bitset_dynamic const& b) noexcept -> bitset_dynamic {
    a &= b;
    return a;
  }

  /**
   * @brief Bitwise OR of \p a and \p b.
   *
   * @param a Left operand, taken by value.
   * @param b Right operand.
   *
   * @return The OR of \p a and \p b.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] friend constexpr auto
  operator|(bitset_dynamic a, bitset_dynamic const& b) noexcept -> bitset_dynamic {
    a |= b;
    return a;
  }

  /**
   * @brief Bitwise XOR of \p a and \p b.
   *
   * @param a Left operand, taken by value.
   * @param b Right operand.
   *
   * @return The XOR of \p a and \p b.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] friend constexpr auto
  operator^(bitset_dynamic a, bitset_dynamic const& b) noexcept -> bitset_dynamic {
    a ^= b;
    return a;
  }

  /**
   * @brief Equality: same size and same bits.
   *
   * @param a Left operand.
   * @param b Right operand.
   *
   * @return \c true when both have the same width and the same bits.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] friend constexpr auto
  operator==(bitset_dynamic const& a, bitset_dynamic const& b) noexcept -> bool {
    return a.m_size == b.m_size && a.m_words == b.m_words;
  }

  /**
   * @brief Lexicographic ordering on the packed word representation.
   *
   * Words are compared from index 0 upward, then by size; this is a total order
   * for sorting, not numeric magnitude.
   *
   * @param a Left operand.
   * @param b Right operand.
   *
   * @return The ordering of \p a relative to \p b.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] friend constexpr auto
  operator<=>(bitset_dynamic const& a, bitset_dynamic const& b) noexcept -> std::strong_ordering {
    if (auto const cmp{a.m_words <=> b.m_words}; cmp != 0) {
      return cmp;
    }
    return a.m_size <=> b.m_size;
  }

  /**
   * @brief Iterator to the first set-bit index.
   *
   * Iterates the indices of set bits (sparse-aware), not a bit-per-bit \c bool
   * sequence: that is the common "visit the live indices" use and needs no proxy
   * reference. For the raw words use \c words(); for the range use \c set_bits().
   *
   * @return Iterator to the first set bit, or \c end() when none is set.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto begin() const noexcept -> set_bit_iterator {
    return set_bits().begin();
  }

  /**
   * @brief Iterator one past the last set-bit index.
   *
   * @return The past-the-end iterator.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto end() const noexcept -> set_bit_iterator {
    return set_bits().end();
  }

  /// @copydoc begin()
  [[nodiscard]] constexpr auto cbegin() const noexcept -> set_bit_iterator {
    return begin();
  }

  /// @copydoc end()
  [[nodiscard]] constexpr auto cend() const noexcept -> set_bit_iterator {
    return end();
  }

  /**
   * @brief Read-only view of the raw word storage.
   *
   * @return A span over the \c word_size() backing words.
   *
   * @pre None.
   * @post The span is valid until the next operation that resizes the storage.
   */
  [[nodiscard]] constexpr auto words() const noexcept -> std::span<word_type const> {
    return std::span<word_type const>{m_words.data(), m_words.size()};
  }

  /**
   * @brief A sparse-aware forward range over the indices of every set bit.
   *
   * @return A forward range yielding each set-bit index in ascending order.
   *
   * @pre None.
   * @post The range is valid until the next operation that resizes the storage.
   */
  [[nodiscard]] constexpr auto set_bits() const noexcept -> set_bits_range {
    return set_bits_range{m_words.data(), m_words.size()};
  }
};

}  // namespace nexenne::container
