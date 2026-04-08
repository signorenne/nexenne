#pragma once

/**
 * @file
 * @brief A bounds-aware read/write position within a contiguous buffer.
 */

#include <cstddef>
#include <span>
#include <type_traits>

namespace nexenne::utility {

/**
 * @brief Tracks a position within a byte span and answers bounds queries.
 *
 * Bundles the "span plus current offset plus is-there-room check" trio that
 * every hand-rolled serializer or parser repeats. Use \c const \c Byte for a
 * read cursor and a mutable \c Byte for a write cursor; the same \c has query
 * answers both "are there \p n more bytes to read" and "does \p n more fit".
 * It owns no storage, only a view and an index, so copying is trivial.
 *
 * @tparam Byte Element type, typically \c std::byte or \c std::byte \c const.
 *
 * @pre None.
 * @post A freshly constructed cursor is positioned at offset zero.
 *
 * @par Example
 * \code
 * auto cur{nexenne::utility::buffer_cursor{std::span{buf}}};
 * while (cur.has(1)) {
 *   handle(cur.next());
 * }
 * \endcode
 */
template <typename Byte>
class buffer_cursor {
public:
  using value_type = Byte;
  using size_type = std::size_t;

  /**
   * @brief Constructs a cursor over \p buffer, positioned at offset zero.
   *
   * @param buffer View the cursor walks; it must outlive the cursor.
   *
   * @pre None.
   * @post \c position() is zero.
   */
  constexpr explicit buffer_cursor(std::span<Byte> const buffer) noexcept : m_buf{buffer} {}

  /**
   * @brief The total size of the underlying buffer in elements.
   *
   * @return The buffer size.
   */
  [[nodiscard]] constexpr auto size() const noexcept -> size_type {
    return m_buf.size();
  }

  /**
   * @brief The current offset from the start of the buffer.
   *
   * @return The number of elements already consumed.
   */
  [[nodiscard]] constexpr auto position() const noexcept -> size_type {
    return m_pos;
  }

  /**
   * @brief The number of elements between the current position and the end.
   *
   * @return \c size() minus \c position().
   */
  [[nodiscard]] constexpr auto remaining() const noexcept -> size_type {
    return m_buf.size() - m_pos;
  }

  /**
   * @brief Reports whether the cursor has reached the end.
   *
   * @return \c true when no elements remain.
   */
  [[nodiscard]] constexpr auto exhausted() const noexcept -> bool {
    return m_pos >= m_buf.size();
  }

  /**
   * @brief Reports whether at least \p n more elements are available.
   *
   * Equally the write-side "do \p n more elements fit" query.
   *
   * @param n Element count to test for.
   *
   * @return \c true when \p n is at or below \c remaining().
   */
  [[nodiscard]] constexpr auto has(size_type const n) const noexcept -> bool {
    return n <= remaining();
  }

  /**
   * @brief A pointer to the element at the current position.
   *
   * @return Address of the current element (one past the end when exhausted).
   */
  [[nodiscard]] constexpr auto data() const noexcept -> Byte* {
    return m_buf.data() + m_pos;
  }

  /**
   * @brief The next \p n elements as a sub-span, without advancing.
   *
   * @param n Element count to view.
   *
   * @return A span over \c [position(), position() + n).
   *
   * @pre \c has(n) is \c true.
   * @post The position is unchanged.
   */
  [[nodiscard]] constexpr auto peek(size_type const n) const noexcept -> std::span<Byte> {
    return m_buf.subspan(m_pos, n);
  }

  /**
   * @brief Advances the position by \p n elements.
   *
   * @param n Element count to skip.
   *
   * @pre \c has(n) is \c true.
   * @post \c position() has grown by \p n.
   */
  constexpr auto advance(size_type const n) noexcept -> void {
    m_pos += n;
  }

  /**
   * @brief Moves the position to the absolute offset \p pos.
   *
   * @param pos New offset from the start of the buffer.
   *
   * @pre \p pos is at or below \c size().
   * @post \c position() equals \p pos.
   */
  constexpr auto seek(size_type const pos) noexcept -> void {
    m_pos = pos;
  }

  /**
   * @brief Moves the position back by \p n elements.
   *
   * The inverse of \c advance, for ungetting an element peeked or consumed by
   * \c next; \c retreat() with no argument steps back one.
   *
   * @param n Element count to move back. Defaults to one.
   *
   * @pre \p n is at or below \c position().
   * @post \c position() has shrunk by \p n.
   */
  constexpr auto retreat(size_type const n = 1) noexcept -> void {
    m_pos -= n;
  }

  /**
   * @brief Returns the next \p n elements as a sub-span and advances past them.
   *
   * @param n Element count to consume.
   *
   * @return A span over the consumed range.
   *
   * @pre \c has(n) is \c true.
   * @post \c position() has grown by \p n.
   */
  [[nodiscard]] constexpr auto take(size_type const n) noexcept -> std::span<Byte> {
    auto const out{m_buf.subspan(m_pos, n)};
    m_pos += n;
    return out;
  }

  /**
   * @brief Reads the element at the current position and advances past it.
   *
   * @return The element value.
   *
   * @pre \c has(1) is \c true.
   * @post \c position() has grown by one.
   */
  [[nodiscard]] constexpr auto next() noexcept -> std::remove_const_t<Byte> {
    auto const value{m_buf[m_pos]};
    ++m_pos;
    return value;
  }

  /**
   * @brief Writes \p value at the current position and advances past it.
   *
   * Available only on a write cursor (a mutable \c Byte).
   *
   * @param value Element to store.
   *
   * @pre \c has(1) is \c true.
   * @post \c position() has grown by one.
   */
  constexpr auto put(std::remove_const_t<Byte> const value) noexcept -> void
    requires(!std::is_const_v<Byte>)
  {
    m_buf[m_pos] = value;
    ++m_pos;
  }

  /**
   * @brief Resets the position to the start of the buffer.
   *
   * @post \c position() is zero.
   */
  constexpr auto rewind() noexcept -> void {
    m_pos = 0;
  }

  /**
   * @brief The whole underlying buffer view.
   *
   * @return The span the cursor was constructed with.
   */
  [[nodiscard]] constexpr auto buffer() const noexcept -> std::span<Byte> {
    return m_buf;
  }

  /**
   * @brief The elements before the current position.
   *
   * For a write cursor this is the portion already written; for a read cursor,
   * the portion already consumed.
   *
   * @return A span over \c [0, position()).
   */
  [[nodiscard]] constexpr auto consumed() const noexcept -> std::span<Byte> {
    return m_buf.first(m_pos);
  }

private:
  std::span<Byte> m_buf;
  size_type m_pos{0};
};

/**
 * @brief Deduces \c buffer_cursor's \c Byte from a span of any extent.
 *
 * @tparam Byte Element type of the span.
 * @tparam Extent Static or dynamic extent of the span (collapsed to dynamic).
 */
template <typename Byte, std::size_t Extent>
buffer_cursor(std::span<Byte, Extent>) -> buffer_cursor<Byte>;

}  // namespace nexenne::utility
