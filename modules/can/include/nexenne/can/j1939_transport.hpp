#pragma once

/**
 * @file
 * @brief J1939 transport protocol: reassembling and segmenting multi-packet messages.
 *
 * A J1939 parameter group larger than the 8 data bytes of one frame is carried by
 * the transport protocol (SAE J1939-21). The sender announces the message with a
 * TP.CM frame and streams the payload in TP.DT frames of 7 data bytes each, up to
 * 1785 bytes total. Two modes exist: BAM (broadcast announce message), which
 * pushes the packets to every node with no handshake, and CMDT (connection mode),
 * which adds an RTS/CTS flow-control handshake for point-to-point transfers.
 *
 * This header gives the assembled message a type, a \c transport_reassembler that
 * feeds on received frames and yields a completed message, and a BAM segmenter
 * that splits a payload into the frames to send. The reassembler is receive-side:
 * it rebuilds the payload of BAM transfers and of any CMDT data it observes, but
 * it does not generate the CMDT CTS flow control, which is a live-bus timing
 * concern left to a higher layer.
 *
 * Reference: SAE J1939-21, the transport protocol functions TP.CM and TP.DT.
 */

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>

#include <nexenne/can/dlc.hpp>
#include <nexenne/can/error.hpp>
#include <nexenne/can/frame.hpp>
#include <nexenne/can/id.hpp>
#include <nexenne/can/j1939_id.hpp>
#include <nexenne/container/small_vector.hpp>
#include <nexenne/utility/discard.hpp>

namespace nexenne::can {

/**
 * @brief PGN of the transport protocol connection management frame (TP.CM).
 */
inline constexpr std::uint32_t j1939_pgn_tp_cm{0x00EC00};

/**
 * @brief PGN of the transport protocol data transfer frame (TP.DT).
 */
inline constexpr std::uint32_t j1939_pgn_tp_dt{0x00EB00};

/**
 * @brief TP.CM control byte for a broadcast announce message (BAM).
 */
inline constexpr std::uint8_t j1939_tp_bam{32};

/**
 * @brief TP.CM control byte for a connection-mode request to send (RTS).
 */
inline constexpr std::uint8_t j1939_tp_rts{16};

/**
 * @brief The largest payload the J1939 transport protocol can carry, in bytes.
 */
inline constexpr std::uint16_t j1939_max_transport{1785};

/**
 * @brief Data bytes carried in one TP.DT frame.
 */
inline constexpr std::uint8_t j1939_tp_dt_payload{7};

/**
 * @brief A reassembled multi-packet J1939 message.
 */
class transport_message {
public:
  using value_type = std::byte;

private:
  std::uint32_t m_pgn{0};
  std::uint8_t m_source{0};
  std::uint8_t m_destination{j1939_global_address};
  container::small_vector<std::byte, 8> m_data{};

public:
  /**
   * @brief Constructs an empty transport message.
   *
   * @pre None.
   * @post The message has no data.
   */
  transport_message() = default;

  /**
   * @brief Constructs a transport message from its fields.
   *
   * @param pgn Parameter Group Number of the assembled message.
   * @param source Source address of the sender.
   * @param destination Destination address, or the global address for BAM.
   * @param data Reassembled payload bytes.
   *
   * @pre None.
   * @post The accessors return the given values.
   */
  transport_message(
    std::uint32_t const pgn,
    std::uint8_t const source,
    std::uint8_t const destination,
    container::small_vector<std::byte, 8> data
  )
      : m_pgn{pgn}, m_source{source}, m_destination{destination}, m_data{std::move(data)} {}

  /**
   * @brief The Parameter Group Number of the message.
   *
   * @return The PGN.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto pgn() const noexcept -> std::uint32_t {
    return m_pgn;
  }

  /**
   * @brief The source address of the sender.
   *
   * @return The source address.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto source() const noexcept -> std::uint8_t {
    return m_source;
  }

  /**
   * @brief The destination address, or the global address for a broadcast.
   *
   * @return The destination address.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto destination() const noexcept -> std::uint8_t {
    return m_destination;
  }

  /**
   * @brief The reassembled payload.
   *
   * @return A read-only span over the payload bytes.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto data() const noexcept -> std::span<std::byte const> {
    return std::span<std::byte const>{m_data.data(), m_data.size()};
  }

  /**
   * @brief The payload length in bytes.
   *
   * @return The number of payload bytes.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto size() const noexcept -> std::size_t {
    return m_data.size();
  }
};

/**
 * @brief Reassembles multi-packet J1939 messages from received frames.
 *
 * Feed every received frame to \c accept; when a transport transfer completes,
 * \c accept returns the assembled \c transport_message. The reassembler tracks
 * one active transfer per (source, destination) pair, validates each announce
 * frame's size and packet count, and checks each data packet's sequence number,
 * dropping the session on a gap, a duplicate, or a malformed frame rather than
 * assembling corrupt data.
 */
class transport_reassembler {
public:
  using value_type = transport_message;

  /// @brief Number of concurrent transfers tracked before the oldest is dropped.
  static constexpr std::size_t max_sessions{8};

private:
  struct session {
    std::uint8_t source{0};
    std::uint8_t destination{j1939_global_address};
    std::uint32_t pgn{0};
    std::uint16_t size{0};
    std::uint8_t packets{0};
    std::uint8_t received{0};
    std::uint32_t opened_at{0};  ///< Monotonic open order, used to evict the oldest.
    container::small_vector<std::byte, 8> buffer{};
  };

  container::small_vector<session, max_sessions> m_sessions{};
  std::uint32_t m_next_order{0};

  // Drops the least-recently-opened session. erase() swaps with the last element,
  // so m_sessions is not insertion-ordered; the open order is tracked explicitly.
  auto evict_oldest() noexcept -> void {
    if (m_sessions.empty()) {
      return;
    }
    session* oldest{&m_sessions[0]};
    for (session& s : m_sessions) {
      if (s.opened_at < oldest->opened_at) {
        oldest = &s;
      }
    }
    erase(oldest->source, oldest->destination);
  }

  // Sessions are keyed by the (source, destination) pair, not the source alone,
  // so one node can run a broadcast (BAM) and a destination-specific transfer at
  // once, and a TP.DT frame only advances the session that shares its own
  // destination address.
  [[nodiscard]] auto find(std::uint8_t const source, std::uint8_t const destination) noexcept
    -> session* {
    for (session& s : m_sessions) {
      if (s.source == source && s.destination == destination) {
        return &s;
      }
    }
    return nullptr;
  }

  auto erase(std::uint8_t const source, std::uint8_t const destination) noexcept -> void {
    for (std::size_t i{0}; i < m_sessions.size(); ++i) {
      if (m_sessions[i].source == source && m_sessions[i].destination == destination) {
        m_sessions[i] = std::move(m_sessions[m_sessions.size() - 1]);
        nexenne::utility::discard(m_sessions.pop_back());
        return;
      }
    }
  }

public:
  /**
   * @brief Feeds one received frame, returning a message when a transfer ends.
   *
   * A TP.CM announce frame (BAM or RTS) opens a session; each TP.DT frame appends
   * its seven data bytes; the message is returned when the last packet arrives.
   * Frames that are not transport frames, and intermediate packets, return
   * \c std::nullopt.
   *
   * @param f Received frame.
   *
   * @return The assembled message when a transfer completes, or \c std::nullopt.
   *
   * @pre None.
   * @post A completed session has been removed from the reassembler.
   */
  [[nodiscard]] auto accept(frame const& f) -> result<std::optional<transport_message>> {
    if (!f.id().extended()) {
      return std::optional<transport_message>{};
    }
    auto const decoded{j1939_id::decode(f.id())};
    if (!decoded) {
      return std::optional<transport_message>{};
    }
    auto const pgn{decoded->pgn()};
    auto const source{decoded->source_address()};
    auto const payload{f.data()};

    auto const destination{decoded->destination_address()};

    if (pgn == j1939_pgn_tp_cm) {
      if (payload.size() < max_classic_length) {
        return std::optional<transport_message>{};
      }
      auto const control{std::to_integer<std::uint8_t>(payload[0])};
      if (control == j1939_tp_bam || control == j1939_tp_rts) {
        auto const size{static_cast<std::uint16_t>(
          std::to_integer<unsigned>(payload[1]) | (std::to_integer<unsigned>(payload[2]) << 8U)
        )};
        auto const packets{std::to_integer<std::uint8_t>(payload[3])};
        // Validate the announce before trusting it: a hostile or corrupt CM can
        // claim any size or packet count. Reject an impossible transfer instead
        // of opening a session that would exceed the 1785-byte bound or complete
        // with an inconsistent size.
        auto const expected{
          static_cast<std::uint16_t>((size + j1939_tp_dt_payload - 1U) / j1939_tp_dt_payload)
        };
        erase(source, destination);
        if (size == 0U || size > j1939_max_transport || packets == 0U || packets != expected) {
          return std::optional<transport_message>{};
        }
        if (m_sessions.size() >= max_sessions) {
          evict_oldest();
        }
        session opened;
        opened.source = source;
        opened.destination = destination;
        opened.size = size;
        opened.packets = packets;
        opened.opened_at = m_next_order++;
        opened.pgn = std::to_integer<std::uint32_t>(payload[5])
                     | (std::to_integer<std::uint32_t>(payload[6]) << 8U)
                     | (std::to_integer<std::uint32_t>(payload[7]) << 16U);
        m_sessions.push_back(std::move(opened));
      }
      return std::optional<transport_message>{};
    }

    if (pgn == j1939_pgn_tp_dt) {
      session* const active{find(source, destination)};
      if (active == nullptr) {
        return std::optional<transport_message>{};
      }
      // A TP.DT frame is always 8-byte Classic CAN (a sequence byte plus seven
      // data bytes). A short frame is a protocol error that drops the session; an
      // oversized (mis-tagged FD) frame contributes only its seven data bytes.
      if (payload.size() < max_classic_length) {
        erase(source, destination);
        return std::optional<transport_message>{};
      }
      // The first data byte is the 1-based packet number. A gap or duplicate means
      // a lost or reordered packet, so the session is dropped rather than
      // assembling corrupt data.
      auto const sequence{std::to_integer<std::uint8_t>(payload[0])};
      if (sequence != static_cast<std::uint8_t>(active->received + 1U)) {
        erase(source, destination);
        return std::optional<transport_message>{};
      }
      for (std::uint8_t i{0}; i < j1939_tp_dt_payload; ++i) {
        active->buffer.push_back(payload[i + 1U]);
      }
      ++active->received;
      if (active->received >= active->packets) {
        container::small_vector<std::byte, 8> bytes;
        auto const total{
          active->size <= active->buffer.size() ? active->size : active->buffer.size()
        };
        for (std::size_t i{0}; i < total; ++i) {
          bytes.push_back(active->buffer[i]);
        }
        transport_message message{active->pgn, source, destination, std::move(bytes)};
        erase(source, destination);
        return std::optional<transport_message>{std::move(message)};
      }
      return std::optional<transport_message>{};
    }

    return std::optional<transport_message>{};
  }
};

/**
 * @brief Splits a payload into the BAM frames that broadcast it.
 *
 * Produces the TP.CM announce frame followed by the TP.DT data frames; the last
 * data frame is padded with \c 0xFF. The frames are broadcast (destination is the
 * global address), as BAM requires.
 *
 * @param priority Message priority for the transport frames.
 * @param pgn Parameter Group Number of the message being sent.
 * @param source Source address of the sender.
 * @param data Payload to broadcast; at most 1785 bytes.
 *
 * @return The frames to send in order, or \c can_error::payload_too_large when
 *         \p data exceeds 1785 bytes.
 *
 * @pre \p data is non-empty; a payload that already fits one frame does not need
 *      the transport protocol.
 * @post On success the first frame is TP.CM and the rest are TP.DT.
 */
[[nodiscard]] inline auto segment_bam(
  std::uint8_t const priority,
  std::uint32_t const pgn,
  std::uint8_t const source,
  std::span<std::byte const> const data
) -> result<container::small_vector<frame, 4>> {
  assert(!data.empty() && "segment_bam: payload must be non-empty");
  if (data.size() > j1939_max_transport) {
    return std::unexpected{can_error::payload_too_large};
  }
  auto const packets{
    static_cast<std::uint8_t>((data.size() + j1939_tp_dt_payload - 1) / j1939_tp_dt_payload)
  };
  container::small_vector<frame, 4> frames;

  std::array<std::byte, max_classic_length> cm{};
  cm[0] = std::byte{j1939_tp_bam};
  cm[1] = static_cast<std::byte>(data.size() & 0xFFU);
  cm[2] = static_cast<std::byte>((data.size() >> 8U) & 0xFFU);
  cm[3] = std::byte{packets};
  cm[4] = std::byte{0xFF};
  cm[5] = static_cast<std::byte>(pgn & 0xFFU);
  cm[6] = static_cast<std::byte>((pgn >> 8U) & 0xFFU);
  cm[7] = static_cast<std::byte>((pgn >> 16U) & 0xFFU);
  auto const cm_id{j1939_id::make(priority, j1939_pgn_tp_cm, source).identifier()};
  auto const cm_frame{frame::classic(cm_id, cm)};
  if (!cm_frame) {
    return std::unexpected{cm_frame.error()};
  }
  frames.push_back(*cm_frame);

  auto const dt_id{j1939_id::make(priority, j1939_pgn_tp_dt, source).identifier()};
  for (std::uint8_t packet{0}; packet < packets; ++packet) {
    std::array<std::byte, max_classic_length> dt{};
    dt[0] = std::byte{static_cast<std::uint8_t>(packet + 1U)};
    for (std::uint8_t i{0}; i < j1939_tp_dt_payload; ++i) {
      auto const index{static_cast<std::size_t>(packet) * j1939_tp_dt_payload + i};
      dt[i + 1U] = index < data.size() ? data[index] : std::byte{0xFF};
    }
    auto const dt_frame{frame::classic(dt_id, dt)};
    if (!dt_frame) {
      return std::unexpected{dt_frame.error()};
    }
    frames.push_back(*dt_frame);
  }
  return frames;
}

}  // namespace nexenne::can
