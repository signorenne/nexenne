#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include <nexenne/can/j1939_transport.hpp>

namespace nexenne::can {

transport_message::transport_message(
  std::uint32_t const pgn,
  std::uint8_t const source,
  std::uint8_t const destination,
  container::small_vector<std::byte, 8> data
)
    : m_pgn{pgn}, m_source{source}, m_destination{destination}, m_data{std::move(data)} {}

auto transport_message::pgn() const noexcept -> std::uint32_t {
  return m_pgn;
}

auto transport_message::source() const noexcept -> std::uint8_t {
  return m_source;
}

auto transport_message::destination() const noexcept -> std::uint8_t {
  return m_destination;
}

auto transport_message::data() const noexcept -> std::span<std::byte const> {
  return std::span<std::byte const>{m_data.data(), m_data.size()};
}

auto transport_message::size() const noexcept -> std::size_t {
  return m_data.size();
}

auto transport_reassembler::evict_oldest() noexcept -> void {
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

auto transport_reassembler::find(std::uint8_t const source, std::uint8_t const destination) noexcept
  -> session* {
  for (session& s : m_sessions) {
    if (s.source == source && s.destination == destination) {
      return &s;
    }
  }
  return nullptr;
}

auto transport_reassembler::erase(
  std::uint8_t const source, std::uint8_t const destination
) noexcept -> void {
  for (std::size_t i{0}; i < m_sessions.size(); ++i) {
    if (m_sessions[i].source == source && m_sessions[i].destination == destination) {
      m_sessions[i] = std::move(m_sessions[m_sessions.size() - 1]);
      nexenne::utility::discard(m_sessions.pop_back());
      return;
    }
  }
}

auto transport_reassembler::accept(frame const& f) -> result<std::optional<transport_message>> {
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

}  // namespace nexenne::can
