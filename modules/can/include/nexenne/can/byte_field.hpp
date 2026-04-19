#pragma once

/**
 * @file
 * @brief A byte-aligned field for raw byte arrays and text in a CAN payload.
 *
 * Not every payload field is a scaled number. Diagnostic and identification
 * messages carry raw byte blobs and ASCII text (a VIN, a part number, a status
 * string), usually byte-aligned. A \c byte_field names such a region by its start
 * byte and length, and the free functions read and write it as raw bytes or as
 * text. Reading returns a view into the frame, so nothing is allocated; the view
 * is valid only while the frame lives. This complements the numeric
 * \c signal.hpp; bit-granular numeric fields still use a signal and its
 * \c packing_plan.hpp.
 */

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include <nexenne/can/error.hpp>
#include <nexenne/can/frame.hpp>

namespace nexenne::can {

/**
 * @brief A named byte-aligned region of a CAN payload, for bytes or text.
 */
class byte_field {
public:
  using value_type = std::byte;

private:
  std::uint16_t m_start_byte{0};
  std::uint16_t m_length{1};
  std::string_view m_name{};

public:
  /**
   * @brief Constructs a single-byte field at byte 0.
   *
   * @pre None.
   * @post \c start_byte() is zero, \c length() is one, and \c name() is empty.
   */
  constexpr byte_field() noexcept = default;

  /**
   * @brief Constructs a byte field at a start byte with a length and name.
   *
   * @param start_byte Index of the first byte of the field in the payload.
   * @param length Number of bytes in the field.
   * @param name Non-owning view of the field name.
   *
   * @pre The string \p name refers to outlives the field.
   * @post The accessors return the given values.
   */
  constexpr byte_field(
    std::uint16_t const start_byte, std::uint16_t const length, std::string_view const name = {}
  ) noexcept
      : m_start_byte{start_byte}, m_length{length}, m_name{name} {}

  /**
   * @brief The index of the field's first byte.
   *
   * @return Mutable reference to the start byte.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto start_byte() noexcept -> std::uint16_t& {
    return m_start_byte;
  }

  /**
   * @brief The index of the field's first byte.
   *
   * @return Const reference to the start byte.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto start_byte() const noexcept -> std::uint16_t const& {
    return m_start_byte;
  }

  /**
   * @brief The field length in bytes.
   *
   * @return Mutable reference to the length.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto length() noexcept -> std::uint16_t& {
    return m_length;
  }

  /**
   * @brief The field length in bytes.
   *
   * @return Const reference to the length.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto length() const noexcept -> std::uint16_t const& {
    return m_length;
  }

  /**
   * @brief The field name.
   *
   * @return Mutable reference to the name view.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto name() noexcept -> std::string_view& {
    return m_name;
  }

  /**
   * @brief The field name, or an empty view when unset.
   *
   * @return Const reference to the name view.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto name() const noexcept -> std::string_view const& {
    return m_name;
  }

  /**
   * @brief The number of payload bytes the field needs.
   *
   * @return One past the field's last byte, so a payload of at least this many
   *         bytes holds the whole field.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto required_length() const noexcept -> std::uint32_t {
    // Computed in a wider type so a field near the 16-bit limit cannot wrap to a
    // small value and slip past the bounds check in read_bytes / write_bytes.
    return static_cast<std::uint32_t>(m_start_byte) + m_length;
  }
};

/**
 * @brief Returns a read-only view of a byte field within a frame.
 *
 * The span points into \p f, so it is valid only while \p f lives.
 *
 * @param field Field to read.
 * @param f Frame to read from.
 *
 * @return A span of \c field.length() bytes, or \c can_error::signal_out_of_range
 *         when the field runs past the frame's data length.
 *
 * @pre None.
 * @post On success the span size equals \c field.length().
 */
[[nodiscard]] inline auto
read_bytes(byte_field const& field, frame const& f) noexcept -> result<std::span<std::byte const>> {
  if (field.required_length() > f.length()) {
    return std::unexpected{can_error::signal_out_of_range};
  }
  return f.data().subspan(field.start_byte(), field.length());
}

/**
 * @brief Writes bytes into a byte field of a frame.
 *
 * Copies up to \c field.length() bytes from \p bytes; if \p bytes is shorter the
 * remaining field bytes are left unchanged (pre-fill the frame to control them).
 *
 * @param field Field to write.
 * @param f Frame to write into.
 * @param bytes Source bytes; only the first \c field.length() are used.
 *
 * @return Empty on success, or \c can_error::signal_out_of_range when the field
 *         runs past the frame's data length.
 *
 * @pre None.
 * @post On success the field holds the copied bytes.
 */
[[nodiscard]] inline auto write_bytes(
  byte_field const& field, frame& f, std::span<std::byte const> const bytes
) noexcept -> result<void> {
  if (field.required_length() > f.length()) {
    return std::unexpected{can_error::signal_out_of_range};
  }
  auto const destination{f.data().subspan(field.start_byte(), field.length())};
  auto const count{bytes.size() < destination.size() ? bytes.size() : destination.size()};
  for (std::size_t i{0}; i < count; ++i) {
    destination[i] = bytes[i];
  }
  return {};
}

/**
 * @brief Reads a byte field as text, trimming trailing NUL padding.
 *
 * The view points into \p f, so it is valid only while \p f lives.
 *
 * @param field Field to read.
 * @param f Frame to read from.
 *
 * @return A string view over the field with trailing NUL bytes removed, or
 *         \c can_error::signal_out_of_range when the field runs past the frame's
 *         data length.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] inline auto
read_text(byte_field const& field, frame const& f) noexcept -> result<std::string_view> {
  auto const bytes{read_bytes(field, f)};
  if (!bytes) {
    return std::unexpected{bytes.error()};
  }
  std::size_t size{bytes->size()};
  while (size > 0 && (*bytes)[size - 1] == std::byte{0}) {
    --size;
  }
  return std::string_view{reinterpret_cast<char const*>(bytes->data()), size};
}

/**
 * @brief Writes text into a byte field, NUL-padding the remainder.
 *
 * Copies up to \c field.length() characters from \p text; any remaining field
 * bytes are set to zero, so a shorter string clears stale data.
 *
 * @param field Field to write.
 * @param f Frame to write into.
 * @param text Source text; only the first \c field.length() characters are used.
 *
 * @return Empty on success, or \c can_error::signal_out_of_range when the field
 *         runs past the frame's data length.
 *
 * @pre None.
 * @post On success the field holds the text followed by NUL padding.
 */
[[nodiscard]] inline auto write_text(
  byte_field const& field, frame& f, std::string_view const text
) noexcept -> result<void> {
  if (field.required_length() > f.length()) {
    return std::unexpected{can_error::signal_out_of_range};
  }
  auto const destination{f.data().subspan(field.start_byte(), field.length())};
  for (std::size_t i{0}; i < destination.size(); ++i) {
    destination[i] = i < text.size() ? static_cast<std::byte>(text[i]) : std::byte{0};
  }
  return {};
}

}  // namespace nexenne::can
