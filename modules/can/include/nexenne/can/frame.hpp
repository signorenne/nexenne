#pragma once

/**
 * @file
 * @brief The CAN wire frame for Classic CAN and CAN FD.
 *
 * A \c frame is the unit that travels on the bus: an identifier, a data length,
 * the data bytes, and, for CAN FD, the bit-rate-switch and error-state-indicator
 * flags. One trivially copyable type covers both Classic CAN (up to 8 bytes) and
 * CAN FD (up to 64 bytes) so there is no class hierarchy and no branch on a
 * variant. The payload is a fixed 64-byte array, the CAN FD maximum, so building
 * or copying a frame never allocates. The identifier word uses the SocketCAN
 * layout (see \c id.hpp), which lets a later socket backend convert to and
 * from the kernel struct by copying fields rather than re-packing bits.
 *
 * An optional nanosecond timestamp records when a frame was received; it is zero
 * on a frame built for transmission and is not part of frame equality.
 */

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

#include <nexenne/can/dlc.hpp>
#include <nexenne/can/error.hpp>
#include <nexenne/can/id.hpp>
#include <nexenne/utility/flags.hpp>

namespace nexenne::can {

/**
 * @brief CAN FD frame flags, distinct from the Classic CAN form.
 *
 * The \c fdf bit marks a frame as CAN FD and is what \c frame::is_fd queries;
 * \c brs and \c esi are meaningful only when \c fdf is set.
 */
enum class fd_flag : std::uint8_t {
  fdf = 1U << 0U,  ///< FD format: the frame is CAN FD and may carry up to 64 bytes.
  brs = 1U << 1U,  ///< Bit rate switch: the data phase is sent at the faster rate.
  esi = 1U << 2U,  ///< Error state indicator: the transmitter is error-passive.
};

/**
 * @brief A CAN frame: identifier, data length, payload bytes, and FD flags.
 *
 * Construct through the \c classic and \c fd factories, which reject a payload
 * the frame type cannot hold. The default-constructed frame is an empty Classic
 * CAN frame with the zero identifier. The field accessors come in mutable and
 * const pairs; setting the length past the payload capacity is a precondition
 * violation the caller must avoid.
 */
class frame {
public:
  using value_type = std::byte;

private:
  can_id m_id{};
  std::uint8_t m_length{0};
  utility::flags<fd_flag> m_flags{};
  std::uint64_t m_timestamp_ns{0};
  std::array<std::byte, max_fd_length> m_data{};

public:
  /**
   * @brief Constructs an empty Classic CAN frame with the zero identifier.
   *
   * @pre None.
   * @post \c length() is zero, \c is_fd() is \c false, and \c id() is the zero
   *       standard identifier.
   */
  constexpr frame() noexcept = default;

  /**
   * @brief Builds a Classic CAN frame.
   *
   * @param id Identifier for the frame.
   * @param payload Data bytes; at most 8.
   *
   * @return The frame on success, or \c can_error::payload_too_large when
   *         \p payload holds more than 8 bytes.
   *
   * @pre None.
   * @post On success \c is_fd() is \c false, \c length() equals
   *       \c payload.size(), and the first \c length() data bytes equal
   *       \p payload.
   */
  [[nodiscard]] static constexpr auto
  classic(can_id const id, std::span<std::byte const> const payload) noexcept -> result<frame> {
    if (payload.size() > max_classic_length) {
      return std::unexpected{can_error::payload_too_large};
    }
    frame f;
    f.m_id = id;
    f.m_length = static_cast<std::uint8_t>(payload.size());
    std::ranges::copy(payload, f.m_data.begin());
    return f;
  }

  /**
   * @brief Builds a CAN FD frame.
   *
   * The payload keeps its exact byte count; the on-wire padded length is a
   * transmit-time concern computed from the DLC helpers in \c dlc.hpp.
   *
   * @param id Identifier for the frame.
   * @param payload Data bytes; at most 64.
   * @param flags FD flags to set alongside the implied \c fdf bit, typically
   *              \c brs or \c esi.
   *
   * @return The frame on success, or \c can_error::payload_too_large when
   *         \p payload holds more than 64 bytes.
   *
   * @pre None.
   * @post On success \c is_fd() is \c true, \c length() equals
   *       \c payload.size(), and the first \c length() data bytes equal
   *       \p payload.
   */
  [[nodiscard]] static constexpr auto fd(
    can_id const id,
    std::span<std::byte const> const payload,
    utility::flags<fd_flag> const flags = {}
  ) noexcept -> result<frame> {
    if (payload.size() > max_fd_length) {
      return std::unexpected{can_error::payload_too_large};
    }
    frame f;
    f.m_id = id;
    f.m_length = static_cast<std::uint8_t>(payload.size());
    f.m_flags = flags | fd_flag::fdf;
    std::ranges::copy(payload, f.m_data.begin());
    return f;
  }

  /**
   * @brief Builds a Classic CAN frame with every data byte set to a fill value.
   *
   * Useful before packing signals: J1939 leaves unused bytes as \c 0xFF ("not
   * available"), so a transmitter fills with \c 0xFF and then packs the signals
   * it has values for.
   *
   * @param id Identifier for the frame.
   * @param length Number of data bytes; at most 8.
   * @param fill Value written to every data byte.
   *
   * @return The filled frame, or \c can_error::payload_too_large when \p length
   *         exceeds 8.
   *
   * @pre None.
   * @post On success \c length() equals \p length and every data byte equals
   *       \p fill.
   */
  [[nodiscard]] static constexpr auto
  filled(can_id const id, std::uint8_t const length, std::byte const fill) noexcept
    -> result<frame> {
    if (length > max_classic_length) {
      return std::unexpected{can_error::payload_too_large};
    }
    frame f;
    f.m_id = id;
    f.m_length = length;
    std::ranges::fill(std::span<std::byte>{f.m_data.data(), length}, fill);
    return f;
  }

  /**
   * @brief Builds a CAN FD frame with every data byte set to a fill value.
   *
   * @param id Identifier for the frame.
   * @param length Number of data bytes; at most 64.
   * @param fill Value written to every data byte.
   * @param flags FD flags to set alongside the implied \c fdf bit.
   *
   * @return The filled frame, or \c can_error::payload_too_large when \p length
   *         exceeds 64.
   *
   * @pre None.
   * @post On success \c is_fd() is \c true, \c length() equals \p length, and
   *       every data byte equals \p fill.
   */
  [[nodiscard]] static constexpr auto fd_filled(
    can_id const id,
    std::uint8_t const length,
    std::byte const fill,
    utility::flags<fd_flag> const flags = {}
  ) noexcept -> result<frame> {
    if (length > max_fd_length) {
      return std::unexpected{can_error::payload_too_large};
    }
    frame f;
    f.m_id = id;
    f.m_length = length;
    f.m_flags = flags | fd_flag::fdf;
    std::ranges::fill(std::span<std::byte>{f.m_data.data(), length}, fill);
    return f;
  }

  /**
   * @brief The frame's identifier.
   *
   * @return Mutable reference to the stored \c can_id.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto id() & noexcept -> can_id& {
    return m_id;
  }

  /**
   * @brief The frame's identifier.
   *
   * @return Const reference to the stored \c can_id.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto id() const& noexcept -> can_id const& {
    return m_id;
  }

  /**
   * @brief The data length in bytes.
   *
   * @return Mutable reference to the data length.
   *
   * @pre A length written through this reference is at most 64.
   * @post None.
   */
  [[nodiscard]] constexpr auto length() & noexcept -> std::uint8_t& {
    return m_length;
  }

  /**
   * @brief The data length in bytes.
   *
   * @return Const reference to the data length, 0 through 64.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto length() const& noexcept -> std::uint8_t const& {
    return m_length;
  }

  /**
   * @brief The FD flag set (\c fdf, \c brs, \c esi).
   *
   * @return Mutable reference to the flags.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto flags() & noexcept -> utility::flags<fd_flag>& {
    return m_flags;
  }

  /**
   * @brief The FD flag set (\c fdf, \c brs, \c esi).
   *
   * @return Const reference to the flags.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto flags() const& noexcept -> utility::flags<fd_flag> const& {
    return m_flags;
  }

  /**
   * @brief The receive timestamp in nanoseconds, or zero when unset.
   *
   * @return Mutable reference to the timestamp.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto timestamp_ns() & noexcept -> std::uint64_t& {
    return m_timestamp_ns;
  }

  /**
   * @brief The receive timestamp in nanoseconds, or zero when unset.
   *
   * @return Const reference to the timestamp.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto timestamp_ns() const& noexcept -> std::uint64_t const& {
    return m_timestamp_ns;
  }

  /**
   * @brief Reports whether this is a CAN FD frame.
   *
   * @return \c true when the \c fdf flag is set.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto is_fd() const noexcept -> bool {
    return m_flags.has(fd_flag::fdf);
  }

  /**
   * @brief Mutable view of the valid payload bytes.
   *
   * @return A span of exactly \c length() bytes over the payload, for a codec to
   *         pack signal bits into.
   *
   * @pre \c length() is at most 64 (the payload capacity).
   * @post The span size equals \c length().
   */
  [[nodiscard]] constexpr auto data() & noexcept -> std::span<std::byte> {
    assert(m_length <= max_fd_length && "frame length exceeds payload capacity");
    return std::span<std::byte>{m_data.data(), m_length};
  }

  /**
   * @brief Read-only view of the valid payload bytes.
   *
   * @return A span of exactly \c length() bytes over the payload.
   *
   * @pre \c length() is at most 64 (the payload capacity).
   * @post The span size equals \c length().
   */
  [[nodiscard]] constexpr auto data() const& noexcept -> std::span<std::byte const> {
    assert(m_length <= max_fd_length && "frame length exceeds payload capacity");
    return std::span<std::byte const>{m_data.data(), m_length};
  }

  /**
   * @brief Content equality: identifier, FD flags, length, and the valid bytes.
   *
   * The timestamp is ignored, so two frames carrying the same message compare
   * equal regardless of when each was received. Padding past \c length() is
   * always zero and does not affect the result.
   *
   * @param other Frame to compare against.
   *
   * @return \c true when both frames carry the same identifier, flags, length,
   *         and payload bytes.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto operator==(frame const& other) const noexcept -> bool {
    if (m_id != other.m_id || m_flags != other.m_flags || m_length != other.m_length) {
      return false;
    }
    return std::ranges::equal(data(), other.data());
  }
};

static_assert(std::is_trivially_copyable_v<frame>, "frame must stay trivially copyable");

}  // namespace nexenne::can
