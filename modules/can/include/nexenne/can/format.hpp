#pragma once

/**
 * @file
 * @brief Debug printing and formatting for nexenne::can types.
 *
 * Three layers, like the rest of the library's format headers: \c to_string(x)
 * builds a readable string, \c operator<<(std::ostream&, x) streams it, and a
 * \c std::formatter specialization makes \c std::format("{}", x) work. The output
 * is for diagnostics, not serialisation, and is not stable across versions. This
 * header is the single owner of the standard format header for the module, so
 * error.hpp and the value-type headers stay free of it. Each \c std::formatter inherits
 * \c std::formatter<std::string_view>, so a width or alignment spec applies to
 * the whole rendered string.
 */

#include <cstddef>
#include <format>
#include <ostream>
#include <string>
#include <string_view>

#include <nexenne/can/bus.hpp>
#include <nexenne/can/byte_field.hpp>
#include <nexenne/can/byte_order.hpp>
#include <nexenne/can/database.hpp>
#include <nexenne/can/dbc.hpp>
#include <nexenne/can/error.hpp>
#include <nexenne/can/error_frame.hpp>
#include <nexenne/can/filter.hpp>
#include <nexenne/can/frame.hpp>
#include <nexenne/can/id.hpp>
#include <nexenne/can/j1939_id.hpp>
#include <nexenne/can/j1939_transport.hpp>
#include <nexenne/can/message.hpp>
#include <nexenne/can/packing_plan.hpp>
#include <nexenne/can/registry.hpp>
#include <nexenne/can/signal.hpp>
#include <nexenne/can/socket_options.hpp>

namespace nexenne::can {

/**
 * @brief Streams a \c can_error by its \c to_string name.
 *
 * @param os Output stream.
 * @param err Error to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The error name has been written to \p os.
 */
inline auto operator<<(std::ostream& os, can_error const err) -> std::ostream& {
  return os << to_string(err);
}

/**
 * @brief Debug string for a \c can_id.
 *
 * Examples: \c "0x123 std", or \c "0x18FEF100 ext rtr" with the flags shown.
 *
 * @param id Identifier to print.
 *
 * @return The debug string: the identifier in hex, a \c std or \c ext tag, then
 *         \c rtr or \c err when those flags are set.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] inline auto to_string(can_id const id) -> std::string {
  std::string out{
    id.extended() ? std::format("0x{:08X} ext", id.identifier())
                  : std::format("0x{:03X} std", id.identifier())
  };
  if (id.remote()) {
    out += " rtr";
  }
  if (id.error_frame()) {
    out += " err";
  }
  return out;
}

/**
 * @brief Streams a \c can_id via its \c to_string.
 *
 * @param os Output stream.
 * @param id Identifier to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The formatted identifier has been written to \p os.
 */
inline auto operator<<(std::ostream& os, can_id const id) -> std::ostream& {
  return os << to_string(id);
}

/**
 * @brief Debug string for a \c frame.
 *
 * Example: \c "frame(0x123 std, len=2, [DE AD])", with the FD flags shown when
 * present.
 *
 * @param f Frame to print.
 *
 * @return The debug string: the identifier, the length, any FD flags, and the
 *         payload bytes in hex.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] inline auto to_string(frame const& f) -> std::string {
  std::string flags;
  if (f.is_fd()) {
    flags += " fd";
    if (f.flags().has(fd_flag::brs)) {
      flags += " brs";
    }
    if (f.flags().has(fd_flag::esi)) {
      flags += " esi";
    }
  }
  std::string bytes;
  auto const payload{f.data()};
  for (std::size_t i{0}; i < payload.size(); ++i) {
    if (i != 0) {
      bytes += ' ';
    }
    bytes += std::format("{:02X}", std::to_integer<unsigned>(payload[i]));
  }
  return std::format("frame({}{}, len={}, [{}])", to_string(f.id()), flags, f.length(), bytes);
}

/**
 * @brief Streams a \c frame via its \c to_string.
 *
 * @param os Output stream.
 * @param f Frame to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The formatted frame has been written to \p os.
 */
inline auto operator<<(std::ostream& os, frame const& f) -> std::ostream& {
  return os << to_string(f);
}

/**
 * @brief The name of a \c byte_order, either \c "little_endian" or \c "big_endian".
 *
 * @param order Byte order to name.
 *
 * @return A static string view naming the byte order.
 *
 * @pre None.
 * @post The returned view refers to a string with program lifetime.
 */
[[nodiscard]] constexpr auto to_string(byte_order const order) noexcept -> std::string_view {
  switch (order) {
    case byte_order::little_endian:
      return "little_endian";
    case byte_order::big_endian:
      return "big_endian";
  }
  return "unknown";
}

/**
 * @brief Streams a \c byte_order by its name.
 *
 * @param os Output stream.
 * @param order Byte order to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The byte order name has been written to \p os.
 */
inline auto operator<<(std::ostream& os, byte_order const order) -> std::ostream& {
  return os << to_string(order);
}

/**
 * @brief The name of a single \c fd_flag bit, one of \c "fdf", \c "brs", \c "esi".
 *
 * @param flag CAN FD flag bit to name.
 *
 * @return A static string view naming the flag.
 *
 * @pre None.
 * @post The returned view refers to a string with program lifetime.
 */
[[nodiscard]] constexpr auto to_string(fd_flag const flag) noexcept -> std::string_view {
  switch (flag) {
    case fd_flag::fdf:
      return "fdf";
    case fd_flag::brs:
      return "brs";
    case fd_flag::esi:
      return "esi";
  }
  return "unknown";
}

/**
 * @brief Streams a single \c fd_flag by its name.
 *
 * @param os Output stream.
 * @param flag Flag to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The flag name has been written to \p os.
 */
inline auto operator<<(std::ostream& os, fd_flag const flag) -> std::ostream& {
  return os << to_string(flag);
}

/**
 * @brief Debug string for an \c fd_flag set, the space-joined names of its bits.
 *
 * Example: \c "fdf brs" for a frame that is FD with bit-rate switch. An empty set
 * renders as the empty string.
 *
 * @param flags Flag set to describe.
 *
 * @return The set bits joined by spaces in \c fdf, \c brs, \c esi order.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] inline auto to_string(utility::flags<fd_flag> const flags) -> std::string {
  std::string out;
  for (auto const bit : {fd_flag::fdf, fd_flag::brs, fd_flag::esi}) {
    if (flags.has(bit)) {
      if (!out.empty()) {
        out += ' ';
      }
      out += to_string(bit);
    }
  }
  return out;
}

/**
 * @brief Streams an \c fd_flag set via its \c to_string.
 *
 * @param os Output stream.
 * @param flags Flag set to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The flag set has been written to \p os.
 */
inline auto operator<<(std::ostream& os, utility::flags<fd_flag> const flags) -> std::ostream& {
  return os << to_string(flags);
}

/**
 * @brief Debug string for a \c signal.
 *
 * Example: \c "signal(speed @0:16 little_endian unsigned *0.01+0 km/h)".
 *
 * @param sig Signal to print.
 *
 * @return The debug string with the name, bit layout, scaling, and unit.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] inline auto to_string(signal const& sig) -> std::string {
  return std::format(
    "signal({} @{}:{} {} {} *{}+{} {})",
    sig.name(),
    sig.start_bit(),
    sig.length(),
    to_string(sig.order()),
    sig.is_float() ? "float" : (sig.is_signed() ? "signed" : "unsigned"),
    sig.scale(),
    sig.offset(),
    sig.unit()
  );
}

/**
 * @brief Streams a \c signal via its \c to_string.
 *
 * @param os Output stream.
 * @param sig Signal to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The formatted signal has been written to \p os.
 */
inline auto operator<<(std::ostream& os, signal const& sig) -> std::ostream& {
  return os << to_string(sig);
}

/**
 * @brief The name of a \c multiplex_role: \c "none", \c "selector", or
 *        \c "multiplexed".
 *
 * @param role Multiplex role to name.
 *
 * @return A static string view naming the role.
 *
 * @pre None.
 * @post The returned view refers to a string with program lifetime.
 */
[[nodiscard]] constexpr auto to_string(multiplex_role const role) noexcept -> std::string_view {
  switch (role) {
    case multiplex_role::none:
      return "none";
    case multiplex_role::selector:
      return "selector";
    case multiplex_role::multiplexed:
      return "multiplexed";
  }
  return "unknown";
}

/**
 * @brief Streams a \c multiplex_role by its name.
 *
 * @param os Output stream.
 * @param role Multiplex role to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The role name has been written to \p os.
 */
inline auto operator<<(std::ostream& os, multiplex_role const role) -> std::ostream& {
  return os << to_string(role);
}

/**
 * @brief The name of an \c invalid_value policy.
 *
 * @param policy Invalid-value policy to name.
 *
 * @return A static string view naming the policy.
 *
 * @pre None.
 * @post The returned view refers to a string with program lifetime.
 */
[[nodiscard]] constexpr auto to_string(invalid_value const policy) noexcept -> std::string_view {
  switch (policy) {
    case invalid_value::none:
      return "none";
    case invalid_value::all_ones:
      return "all_ones";
    case invalid_value::all_zeros:
      return "all_zeros";
    case invalid_value::all_ones_or_zeros:
      return "all_ones_or_zeros";
  }
  return "unknown";
}

/**
 * @brief Streams an \c invalid_value policy by its name.
 *
 * @param os Output stream.
 * @param policy Invalid-value policy to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The policy name has been written to \p os.
 */
inline auto operator<<(std::ostream& os, invalid_value const policy) -> std::ostream& {
  return os << to_string(policy);
}

/**
 * @brief Debug string for a \c byte_field, such as \c "byte_field(vin @0:17)".
 *
 * @param field Field to print.
 *
 * @return The debug string with the name, start byte, and length.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] inline auto to_string(byte_field const& field) -> std::string {
  return std::format("byte_field({} @{}:{})", field.name(), field.start_byte(), field.length());
}

/**
 * @brief Streams a \c byte_field via its \c to_string.
 *
 * @param os Output stream.
 * @param field Field to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The formatted field has been written to \p os.
 */
inline auto operator<<(std::ostream& os, byte_field const& field) -> std::ostream& {
  return os << to_string(field);
}

/**
 * @brief Debug string for one \c plan_chunk.
 *
 * Example: \c "byte0[0+8]<<8" meaning eight bits at bit 0 of byte 0, shifted to
 * value position 8.
 *
 * @param chunk Chunk to print.
 *
 * @return The debug string for the chunk.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] inline auto to_string(plan_chunk const chunk) -> std::string {
  return std::format(
    "byte{}[{}+{}]<<{}", chunk.byte, chunk.lsb_in_byte, chunk.num_bits, chunk.dest_shift
  );
}

/**
 * @brief Streams a \c plan_chunk via its \c to_string.
 *
 * @param os Output stream.
 * @param chunk Chunk to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The formatted chunk has been written to \p os.
 */
inline auto operator<<(std::ostream& os, plan_chunk const chunk) -> std::ostream& {
  return os << to_string(chunk);
}

/**
 * @brief Debug string for a \c packing_plan.
 *
 * Example: \c "packing_plan(16b unsigned: byte0[0+8]<<8, byte1[0+8]<<0)".
 *
 * @param plan Plan to print.
 *
 * @return The debug string with the width, signedness, and chunks.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] inline auto to_string(packing_plan const& plan) -> std::string {
  std::string chunks;
  auto const list{plan.chunks()};
  for (std::size_t i{0}; i < list.size(); ++i) {
    if (i != 0) {
      chunks += ", ";
    }
    chunks += to_string(list[i]);
  }
  return std::format(
    "packing_plan({}b {}: {})", plan.bit_length(), plan.is_signed() ? "signed" : "unsigned", chunks
  );
}

/**
 * @brief Streams a \c packing_plan via its \c to_string.
 *
 * @param os Output stream.
 * @param plan Plan to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The formatted plan has been written to \p os.
 */
inline auto operator<<(std::ostream& os, packing_plan const& plan) -> std::ostream& {
  return os << to_string(plan);
}

/**
 * @brief Debug string for a \c filter, such as \c "filter(id=0x100, mask=0x7FF)".
 *
 * @param f Filter to print.
 *
 * @return The debug string with the id and mask in hex.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] inline auto to_string(filter const f) -> std::string {
  return std::format("filter(id=0x{:X}, mask=0x{:X})", f.id(), f.mask());
}

/**
 * @brief Streams a \c filter via its \c to_string.
 *
 * @param os Output stream.
 * @param f Filter to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The formatted filter has been written to \p os.
 */
inline auto operator<<(std::ostream& os, filter const f) -> std::ostream& {
  return os << to_string(f);
}

/**
 * @brief Debug string for a \c message.
 *
 * Example: \c "message(status @0x100 std, 2 signals)".
 *
 * @param m Message to print.
 *
 * @return The debug string with the name, identifier, and signal count.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] inline auto to_string(message const& m) -> std::string {
  return std::format(
    "message({} @{}, {} bytes, {} signals)",
    m.name(),
    to_string(m.id()),
    m.byte_length(),
    m.signal_count()
  );
}

/**
 * @brief Debug string for a \c signal_entry, its signal and compiled plan.
 *
 * @param entry Entry to print.
 *
 * @return The debug string delegating to the signal and its packing plan.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] inline auto to_string(signal_entry const& entry) -> std::string {
  return std::format("signal_entry({}, {})", to_string(entry.definition), to_string(entry.plan));
}

/**
 * @brief Streams a \c signal_entry via its \c to_string.
 *
 * @param os Output stream.
 * @param entry Entry to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The formatted entry has been written to \p os.
 */
inline auto operator<<(std::ostream& os, signal_entry const& entry) -> std::ostream& {
  return os << to_string(entry);
}

/**
 * @brief Streams a \c message via its \c to_string.
 *
 * @param os Output stream.
 * @param m Message to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The formatted message has been written to \p os.
 */
inline auto operator<<(std::ostream& os, message const& m) -> std::ostream& {
  return os << to_string(m);
}

/**
 * @brief Debug string for a \c database listing its messages.
 *
 * Example: \c "database(2 messages: [message(...), message(...)])".
 *
 * @param db Database to print.
 *
 * @return The debug string with the count and each message.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] inline auto to_string(database const& db) -> std::string {
  std::string body;
  auto const messages{db.messages()};
  for (std::size_t i{0}; i < messages.size(); ++i) {
    if (i != 0) {
      body += ", ";
    }
    body += to_string(messages[i]);
  }
  return std::format("database({} messages: [{}])", db.message_count(), body);
}

/**
 * @brief Streams a \c database via its \c to_string.
 *
 * @param os Output stream.
 * @param db Database to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The formatted database has been written to \p os.
 */
inline auto operator<<(std::ostream& os, database const& db) -> std::ostream& {
  return os << to_string(db);
}

/**
 * @brief Debug string for a \c registry, its indexed count and filter count.
 *
 * Example: \c "registry(indexed=2, filters=0)".
 *
 * @param reg Registry to print.
 *
 * @return The debug string with the indexed message and filter counts.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] inline auto to_string(registry const& reg) -> std::string {
  return std::format("registry(indexed={}, filters={})", reg.message_count(), reg.filter_count());
}

/**
 * @brief Streams a \c registry via its \c to_string.
 *
 * @param os Output stream.
 * @param reg Registry to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The formatted registry has been written to \p os.
 */
inline auto operator<<(std::ostream& os, registry const& reg) -> std::ostream& {
  return os << to_string(reg);
}

/**
 * @brief Debug string for a \c dbc_database, its message count and source size.
 *
 * Example: \c "dbc_database(2 messages, 128 source bytes)".
 *
 * @param db Parsed DBC database to print.
 *
 * @return The debug string with the message count and retained source size.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] inline auto to_string(dbc_database const& db) -> std::string {
  return std::format(
    "dbc_database({} messages, {} source bytes)", db.messages().size(), db.source().size()
  );
}

/**
 * @brief Debug string for a \c dbc_enum_value, its raw value and label.
 *
 * Example: \c "1: \"drive\"".
 *
 * @param entry Value-table entry to print.
 *
 * @return The debug string with the value and quoted label.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] inline auto to_string(dbc_enum_value const& entry) -> std::string {
  return std::format("{}: \"{}\"", entry.value, entry.label);
}

/**
 * @brief Streams a \c dbc_enum_value via its \c to_string.
 *
 * @param os Output stream.
 * @param entry Value-table entry to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The formatted entry has been written to \p os.
 */
inline auto operator<<(std::ostream& os, dbc_enum_value const& entry) -> std::ostream& {
  return os << to_string(entry);
}

/**
 * @brief Streams a \c dbc_database via its \c to_string.
 *
 * @param os Output stream.
 * @param db Parsed DBC database to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The formatted database has been written to \p os.
 */
inline auto operator<<(std::ostream& os, dbc_database const& db) -> std::ostream& {
  return os << to_string(db);
}

/**
 * @brief The name of a \c bus_state.
 *
 * @param state Bus state to name.
 *
 * @return A static string view naming the state.
 *
 * @pre None.
 * @post The returned view refers to a string with program lifetime.
 */
[[nodiscard]] constexpr auto to_string(bus_state const state) noexcept -> std::string_view {
  switch (state) {
    case bus_state::error_active:
      return "error_active";
    case bus_state::error_passive:
      return "error_passive";
    case bus_state::bus_off:
      return "bus_off";
  }
  return "unknown";
}

/**
 * @brief Streams a \c bus_state by its name.
 *
 * @param os Output stream.
 * @param state Bus state to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The state name has been written to \p os.
 */
inline auto operator<<(std::ostream& os, bus_state const state) -> std::ostream& {
  return os << to_string(state);
}

/**
 * @brief Debug string for \c error_counters, such as \c "tx=0 rx=0".
 *
 * @param counters Counters to print.
 *
 * @return The debug string with the transmit and receive counts.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] inline auto to_string(error_counters const counters) -> std::string {
  return std::format("tx={} rx={}", counters.transmit, counters.receive);
}

/**
 * @brief Streams \c error_counters via its \c to_string.
 *
 * @param os Output stream.
 * @param counters Counters to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The formatted counters have been written to \p os.
 */
inline auto operator<<(std::ostream& os, error_counters const counters) -> std::ostream& {
  return os << to_string(counters);
}

/**
 * @brief Debug string for an \c error_report.
 *
 * Example: \c "error_report(bus_off, tx=255 rx=130, classes=0x40)".
 *
 * @param report Report to print.
 *
 * @return The debug string with the state, counters, and class mask.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] inline auto to_string(error_report const& report) -> std::string {
  return std::format(
    "error_report({}, {}, classes=0x{:X})",
    to_string(report.state),
    to_string(report.counters),
    report.classes
  );
}

/**
 * @brief Streams an \c error_report via its \c to_string.
 *
 * @param os Output stream.
 * @param report Report to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The formatted report has been written to \p os.
 */
inline auto operator<<(std::ostream& os, error_report const& report) -> std::ostream& {
  return os << to_string(report);
}

/**
 * @brief Debug string for \c socket_options.
 *
 * Example: \c "socket_options(fd=0, recv_own=1, nonblocking=1, timeout=0ms)".
 *
 * @param options Options to print.
 *
 * @return The debug string with each option.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] inline auto to_string(socket_options const& options) -> std::string {
  return std::format(
    "socket_options(fd={}, recv_own={}, nonblocking={}, timeout={}ms)",
    static_cast<int>(options.fd_enabled),
    static_cast<int>(options.receive_own_messages),
    static_cast<int>(options.nonblocking),
    options.read_timeout_ms
  );
}

/**
 * @brief Streams \c socket_options via its \c to_string.
 *
 * @param os Output stream.
 * @param options Options to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The formatted options have been written to \p os.
 */
inline auto operator<<(std::ostream& os, socket_options const& options) -> std::ostream& {
  return os << to_string(options);
}

/**
 * @brief Debug string for a \c j1939_id.
 *
 * Example: \c "j1939(prio=6, pgn=0x0FEF1, sa=0x00, da=0xFF)".
 *
 * @param id J1939 identifier to print.
 *
 * @return The debug string with the priority, PGN, source, and destination.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] inline auto to_string(j1939_id const id) -> std::string {
  return std::format(
    "j1939(prio={}, pgn=0x{:05X}, sa=0x{:02X}, da=0x{:02X})",
    id.priority(),
    id.pgn(),
    id.source_address(),
    id.destination_address()
  );
}

/**
 * @brief Streams a \c j1939_id via its \c to_string.
 *
 * @param os Output stream.
 * @param id J1939 identifier to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The formatted identifier has been written to \p os.
 */
inline auto operator<<(std::ostream& os, j1939_id const id) -> std::ostream& {
  return os << to_string(id);
}

/**
 * @brief Debug string for a reassembled \c transport_message.
 *
 * Example: \c "transport(pgn=0x0FECA, sa=0x11, da=0xFF, 20 bytes)".
 *
 * @param message Transport message to print.
 *
 * @return The debug string with the PGN, source, destination, and byte count.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] inline auto to_string(transport_message const& message) -> std::string {
  return std::format(
    "transport(pgn=0x{:05X}, sa=0x{:02X}, da=0x{:02X}, {} bytes)",
    message.pgn(),
    message.source(),
    message.destination(),
    message.size()
  );
}

/**
 * @brief Streams a \c transport_message via its \c to_string.
 *
 * @param os Output stream.
 * @param message Transport message to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The formatted message has been written to \p os.
 */
inline auto operator<<(std::ostream& os, transport_message const& message) -> std::ostream& {
  return os << to_string(message);
}

}  // namespace nexenne::can

/**
 * @brief \c std::format support for \c can_error.
 *
 * Prints the error's \c to_string name, so \c std::format("{}", err) works on a
 * value taken from a \c result<T>. Inherits the string formatter, so a spec
 * (width, alignment) applies to the name.
 */
template <>
struct std::formatter<nexenne::can::can_error> : std::formatter<std::string_view> {
  /**
   * @brief Formats the error's \c to_string name through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param err Error to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The error name has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::can::can_error const err, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::can::to_string(err), ctx);
  }
};

/**
 * @brief \c std::format support for \c can_id, printing its \c to_string form.
 *
 * Inherits the string formatter, so a spec (width, alignment) applies to the text.
 */
template <>
struct std::formatter<nexenne::can::can_id> : std::formatter<std::string_view> {
  /**
   * @brief Formats the identifier's \c to_string through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param id Identifier to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted identifier has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::can::can_id const id, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::can::to_string(id), ctx);
  }
};

/**
 * @brief \c std::format support for \c frame, printing its \c to_string form.
 *
 * Inherits the string formatter, so a spec (width, alignment) applies to the text.
 */
template <>
struct std::formatter<nexenne::can::frame> : std::formatter<std::string_view> {
  /**
   * @brief Formats the frame's \c to_string through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param f Frame to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted frame has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::can::frame const& f, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::can::to_string(f), ctx);
  }
};

/**
 * @brief \c std::format support for \c byte_order, printing its name.
 *
 * Inherits the string formatter, so a spec (width, alignment) applies to the name.
 */
template <>
struct std::formatter<nexenne::can::byte_order> : std::formatter<std::string_view> {
  /**
   * @brief Formats the byte order's name through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param order Byte order to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The byte order name has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::can::byte_order const order, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::can::to_string(order), ctx);
  }
};

/**
 * @brief \c std::format support for a single \c fd_flag, printing its name.
 *
 * Inherits the string formatter, so a spec (width, alignment) applies to the name.
 */
template <>
struct std::formatter<nexenne::can::fd_flag> : std::formatter<std::string_view> {
  /**
   * @brief Formats the flag's name through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param flag Flag to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The flag name has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::can::fd_flag const flag, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::can::to_string(flag), ctx);
  }
};

/**
 * @brief \c std::format support for an \c fd_flag set, printing its space-joined names.
 *
 * Inherits the string formatter, so a spec (width, alignment) applies to the text.
 */
template <>
struct std::formatter<nexenne::utility::flags<nexenne::can::fd_flag>>
    : std::formatter<std::string> {
  /**
   * @brief Formats the flag set's \c to_string through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param flags Flag set to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The flag set has been written to \p ctx.
   */
  template <typename FormatContext>
  auto
  format(nexenne::utility::flags<nexenne::can::fd_flag> const flags, FormatContext& ctx) const {
    return std::formatter<std::string>::format(nexenne::can::to_string(flags), ctx);
  }
};

/**
 * @brief \c std::format support for \c signal, printing its \c to_string form.
 *
 * Inherits the string formatter, so a spec (width, alignment) applies to the text.
 */
template <>
struct std::formatter<nexenne::can::signal> : std::formatter<std::string_view> {
  /**
   * @brief Formats the signal's \c to_string through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param sig Signal to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted signal has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::can::signal const& sig, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::can::to_string(sig), ctx);
  }
};

/**
 * @brief \c std::format support for \c packing_plan, printing its \c to_string form.
 *
 * Inherits the string formatter, so a spec (width, alignment) applies to the text.
 */
template <>
struct std::formatter<nexenne::can::packing_plan> : std::formatter<std::string_view> {
  /**
   * @brief Formats the plan's \c to_string through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param plan Plan to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted plan has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::can::packing_plan const& plan, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::can::to_string(plan), ctx);
  }
};

/**
 * @brief \c std::format support for one \c plan_chunk, printing its \c to_string form.
 *
 * Inherits the string formatter, so a spec (width, alignment) applies to the text.
 */
template <>
struct std::formatter<nexenne::can::plan_chunk> : std::formatter<std::string_view> {
  /**
   * @brief Formats the chunk's \c to_string through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param chunk Chunk to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted chunk has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::can::plan_chunk const chunk, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::can::to_string(chunk), ctx);
  }
};

/**
 * @brief \c std::format support for \c filter, printing its \c to_string form.
 *
 * Inherits the string formatter, so a spec (width, alignment) applies to the text.
 */
template <>
struct std::formatter<nexenne::can::filter> : std::formatter<std::string_view> {
  /**
   * @brief Formats the filter's \c to_string through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param f Filter to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted filter has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::can::filter const f, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::can::to_string(f), ctx);
  }
};

/**
 * @brief \c std::format support for \c message, printing its \c to_string form.
 *
 * Inherits the string formatter, so a spec (width, alignment) applies to the text.
 */
template <>
struct std::formatter<nexenne::can::message> : std::formatter<std::string_view> {
  /**
   * @brief Formats the message's \c to_string through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param m Message to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted message has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::can::message const& m, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::can::to_string(m), ctx);
  }
};

/**
 * @brief \c std::format support for \c database, printing its \c to_string form.
 *
 * Inherits the string formatter, so a spec (width, alignment) applies to the text.
 */
template <>
struct std::formatter<nexenne::can::database> : std::formatter<std::string_view> {
  /**
   * @brief Formats the database's \c to_string through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param db Database to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted database has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::can::database const& db, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::can::to_string(db), ctx);
  }
};

/**
 * @brief \c std::format support for \c signal_entry, printing its \c to_string form.
 *
 * Inherits the string formatter, so a spec (width, alignment) applies to the text.
 */
template <>
struct std::formatter<nexenne::can::signal_entry> : std::formatter<std::string_view> {
  /**
   * @brief Formats the entry's \c to_string through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param entry Entry to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted entry has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::can::signal_entry const& entry, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::can::to_string(entry), ctx);
  }
};

/**
 * @brief \c std::format support for \c registry, printing its \c to_string form.
 *
 * Inherits the string formatter, so a spec (width, alignment) applies to the text.
 */
template <>
struct std::formatter<nexenne::can::registry> : std::formatter<std::string_view> {
  /**
   * @brief Formats the registry's \c to_string through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param reg Registry to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted registry has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::can::registry const& reg, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::can::to_string(reg), ctx);
  }
};

/**
 * @brief \c std::format support for \c dbc_database, printing its \c to_string form.
 *
 * Inherits the string formatter, so a spec (width, alignment) applies to the text.
 */
template <>
struct std::formatter<nexenne::can::dbc_database> : std::formatter<std::string_view> {
  /**
   * @brief Formats the parsed database's \c to_string through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param db Parsed DBC database to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted database has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::can::dbc_database const& db, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::can::to_string(db), ctx);
  }
};

/**
 * @brief \c std::format support for \c dbc_enum_value, printing its \c to_string form.
 *
 * Inherits the string formatter, so a spec (width, alignment) applies to the text.
 */
template <>
struct std::formatter<nexenne::can::dbc_enum_value> : std::formatter<std::string_view> {
  /**
   * @brief Formats the entry's \c to_string through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param entry Value-table entry to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted entry has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::can::dbc_enum_value const& entry, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::can::to_string(entry), ctx);
  }
};

/**
 * @brief \c std::format support for \c bus_state, printing its name.
 *
 * Inherits the string formatter, so a spec (width, alignment) applies to the name.
 */
template <>
struct std::formatter<nexenne::can::bus_state> : std::formatter<std::string_view> {
  /**
   * @brief Formats the bus state's name through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param state Bus state to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The state name has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::can::bus_state const state, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::can::to_string(state), ctx);
  }
};

/**
 * @brief \c std::format support for \c error_counters, printing its \c to_string.
 *
 * Inherits the string formatter, so a spec (width, alignment) applies to the text.
 */
template <>
struct std::formatter<nexenne::can::error_counters> : std::formatter<std::string_view> {
  /**
   * @brief Formats the counters' \c to_string through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param counters Counters to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted counters have been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::can::error_counters const counters, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::can::to_string(counters), ctx);
  }
};

/**
 * @brief \c std::format support for \c error_report, printing its \c to_string.
 *
 * Inherits the string formatter, so a spec (width, alignment) applies to the text.
 */
template <>
struct std::formatter<nexenne::can::error_report> : std::formatter<std::string_view> {
  /**
   * @brief Formats the report's \c to_string through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param report Report to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted report has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::can::error_report const& report, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::can::to_string(report), ctx);
  }
};

/**
 * @brief \c std::format support for \c socket_options, printing its \c to_string.
 *
 * Inherits the string formatter, so a spec (width, alignment) applies to the text.
 */
template <>
struct std::formatter<nexenne::can::socket_options> : std::formatter<std::string_view> {
  /**
   * @brief Formats the options' \c to_string through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param options Options to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted options have been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::can::socket_options const& options, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::can::to_string(options), ctx);
  }
};

/**
 * @brief \c std::format support for \c j1939_id, printing its \c to_string form.
 *
 * Inherits the string formatter, so a spec (width, alignment) applies to the text.
 */
template <>
struct std::formatter<nexenne::can::j1939_id> : std::formatter<std::string_view> {
  /**
   * @brief Formats the identifier's \c to_string through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param id J1939 identifier to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted identifier has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::can::j1939_id const id, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::can::to_string(id), ctx);
  }
};

/**
 * @brief \c std::format support for \c transport_message, printing its \c to_string form.
 *
 * Inherits the string formatter, so a spec (width, alignment) applies to the text.
 */
template <>
struct std::formatter<nexenne::can::transport_message> : std::formatter<std::string_view> {
  /**
   * @brief Formats the message's \c to_string through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param message Transport message to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted message has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::can::transport_message const& message, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::can::to_string(message), ctx);
  }
};

/**
 * @brief \c std::format support for \c multiplex_role, printing its name.
 *
 * Inherits the string formatter, so a spec (width, alignment) applies to the name.
 */
template <>
struct std::formatter<nexenne::can::multiplex_role> : std::formatter<std::string_view> {
  /**
   * @brief Formats the role's name through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param role Multiplex role to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The role name has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::can::multiplex_role const role, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::can::to_string(role), ctx);
  }
};

/**
 * @brief \c std::format support for \c invalid_value, printing its name.
 *
 * Inherits the string formatter, so a spec (width, alignment) applies to the name.
 */
template <>
struct std::formatter<nexenne::can::invalid_value> : std::formatter<std::string_view> {
  /**
   * @brief Formats the policy's name through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param policy Invalid-value policy to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The policy name has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::can::invalid_value const policy, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::can::to_string(policy), ctx);
  }
};

/**
 * @brief \c std::format support for \c byte_field, printing its \c to_string form.
 *
 * Inherits the string formatter, so a spec (width, alignment) applies to the text.
 */
template <>
struct std::formatter<nexenne::can::byte_field> : std::formatter<std::string_view> {
  /**
   * @brief Formats the field's \c to_string through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param field Field to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted field has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::can::byte_field const& field, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::can::to_string(field), ctx);
  }
};
