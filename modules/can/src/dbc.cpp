#include <algorithm>
#include <charconv>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <nexenne/can/dbc.hpp>

namespace nexenne::can {

namespace detail {

auto dbc_trim(std::string_view text) noexcept -> std::string_view {
  std::size_t begin{0};
  while (begin < text.size()
         && (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r')) {
    ++begin;
  }
  std::size_t end{text.size()};
  while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r')) {
    --end;
  }
  return text.substr(begin, end - begin);
}

auto dbc_parse_u32(std::string_view text, std::uint32_t& out) noexcept -> bool {
  text = dbc_trim(text);
  auto const result{std::from_chars(text.data(), text.data() + text.size(), out)};
  return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

auto dbc_parse_double(std::string_view text, double& out) noexcept -> bool {
  text = dbc_trim(text);
  auto const result{std::from_chars(text.data(), text.data() + text.size(), out)};
  return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

auto dbc_parse_i64(std::string_view text, std::int64_t& out) noexcept -> bool {
  text = dbc_trim(text);
  auto const result{std::from_chars(text.data(), text.data() + text.size(), out)};
  return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

auto dbc_parse_message(std::string_view const line) -> result<message_builder> {
  dbc_scanner scanner{line};
  if (scanner.token() != "BO_") {
    return std::unexpected{can_error::parse_error};
  }
  std::uint32_t raw{0};
  if (!dbc_parse_u32(scanner.token(), raw)) {
    return std::unexpected{can_error::parse_error};
  }
  scanner.skip_spaces();
  auto const name{dbc_trim(scanner.until(':'))};
  if (name.empty() || !scanner.consume(':')) {
    return std::unexpected{can_error::parse_error};
  }
  // The payload byte count follows the colon: "BO_ <id> <name>: <dlc> <transmitter>".
  std::uint32_t dlc{0};
  if (!dbc_parse_u32(scanner.token(), dlc) || dlc > max_fd_length) {
    return std::unexpected{can_error::parse_error};
  }
  // Validate the identifier instead of silently masking it: a standard id above
  // 0x7FF or an extended id above 0x1FFFFFFF (reserved bits set) is a bad record.
  bool const extended{(raw & dbc_extended_flag) != 0U};
  std::uint32_t const ident{raw & ~dbc_extended_flag};
  if (extended ? ident > extended_id_mask : ident > standard_id_mask) {
    return std::unexpected{can_error::parse_error};
  }
  auto const id{
    extended ? can_id::extended(ident) : can_id::standard(static_cast<std::uint16_t>(ident))
  };
  return message_builder{id, name}.byte_length(static_cast<std::uint8_t>(dlc));
}

auto dbc_parse_signal(std::string_view const line) -> result<signal> {
  dbc_scanner scanner{line};
  if (scanner.token() != "SG_") {
    return std::unexpected{can_error::parse_error};
  }
  auto const name{scanner.token()};
  if (name.empty()) {
    return std::unexpected{can_error::parse_error};
  }
  signal_builder builder;
  builder.name(name);

  // Optional multiplexing indicator: "M" (selector), "m<N>" (multiplexed), or
  // "m<N>M" (extended multiplexing: both). Our model has a single role, so an
  // extended-multiplexed signal is imported as multiplexed rather than failing.
  auto marker{scanner.token()};
  if (marker == "M") {
    builder.mux_role(multiplex_role::selector);
    marker = scanner.token();
  } else if (marker.size() >= 2 && marker[0] == 'm') {
    auto digits{marker.substr(1)};
    if (digits.back() == 'M') {
      digits = digits.substr(0, digits.size() - 1);
    }
    std::uint32_t mux{0};
    if (!dbc_parse_u32(digits, mux)) {
      return std::unexpected{can_error::parse_error};
    }
    builder.mux_role(multiplex_role::multiplexed).mux_value(mux);
    marker = scanner.token();
  }
  if (marker != ":") {
    return std::unexpected{can_error::parse_error};
  }

  // Bit layout: "<start>|<length>@<order><sign>", e.g. "24|16@1+". The order
  // character is at at+1 and the sign character at at+2, so both must be present.
  auto const layout{scanner.token()};
  auto const bar{layout.find('|')};
  auto const at{layout.find('@')};
  if (bar == std::string_view::npos || at == std::string_view::npos || at < bar
      || at + 2 >= layout.size()) {
    return std::unexpected{can_error::parse_error};
  }
  std::uint32_t start{0};
  std::uint32_t length{0};
  if (!dbc_parse_u32(layout.substr(0, bar), start)
      || !dbc_parse_u32(layout.substr(bar + 1, at - bar - 1), length)) {
    return std::unexpected{can_error::parse_error};
  }
  // Validate the raw numeric ranges before narrowing: an out-of-range length
  // would overflow the packing plan's chunk array, and a start bit above 16 bits
  // would truncate to a fabricated layout.
  if (length < 1U || length > 64U || start > 0xFFFFU) {
    return std::unexpected{can_error::parse_error};
  }
  char const order_char{layout[at + 1]};
  char const sign_char{layout[at + 2]};
  if ((order_char != '0' && order_char != '1') || (sign_char != '+' && sign_char != '-')) {
    return std::unexpected{can_error::parse_error};
  }
  builder.start_bit(static_cast<std::uint16_t>(start)).length(static_cast<std::uint16_t>(length));
  builder.endianness(order_char == '1' ? byte_order::little_endian : byte_order::big_endian);
  builder.is_signed(sign_char == '-');

  // Scaling: "(<factor>,<offset>)".
  if (!scanner.consume('(')) {
    return std::unexpected{can_error::parse_error};
  }
  auto const factor_text{scanner.until(',')};
  if (!scanner.consume(',')) {
    return std::unexpected{can_error::parse_error};
  }
  auto const offset_text{scanner.until(')')};
  double factor{1.0};
  double offset{0.0};
  if (!scanner.consume(')') || !dbc_parse_double(factor_text, factor)
      || !dbc_parse_double(offset_text, offset) || !std::isfinite(factor)
      || !std::isfinite(offset)) {
    return std::unexpected{can_error::parse_error};
  }
  builder.scale(factor).offset(offset);

  // Range: "[<min>|<max>]". By the DBC convention (and cantools), "[0|0]" means
  // no range was specified, so the signal is left unbounded rather than clamped
  // to zero. A degenerate range where maximum is not above minimum is treated the
  // same way.
  if (scanner.consume('[')) {
    auto const min_text{scanner.until('|')};
    double minimum{0.0};
    double maximum{0.0};
    if (scanner.consume('|') && dbc_parse_double(min_text, minimum)) {
      auto const max_text{scanner.until(']')};
      if (scanner.consume(']') && dbc_parse_double(max_text, maximum) && maximum > minimum) {
        builder.minimum(minimum).maximum(maximum);
      }
    }
  }

  // Unit: "<unit>" in quotes.
  if (scanner.consume('"')) {
    auto const unit{scanner.until('"')};
    if (scanner.consume('"')) {
      builder.unit(unit);
    }
  }

  return builder.build();
}

auto dbc_raw_id(can_id const id) noexcept -> std::uint32_t {
  return id.extended() ? (id.identifier() | dbc_extended_flag) : id.identifier();
}

}  // namespace detail

dbc_database::dbc_database(
  std::unique_ptr<std::string> source, database parsed, detail::dbc_metadata metadata
) noexcept
    : m_source{std::move(source)}, m_database{std::move(parsed)}, m_metadata{std::move(metadata)} {}

auto dbc_database::source() const noexcept -> std::string_view {
  return m_source ? std::string_view{*m_source} : std::string_view{};
}

auto dbc_database::db() const noexcept -> database const& {
  return m_database;
}

auto dbc_database::messages() const noexcept -> std::span<message const> {
  return m_database.messages();
}

auto dbc_database::message_count() const noexcept -> std::size_t {
  return m_database.message_count();
}

auto dbc_database::find(can_id const id) const noexcept -> message const* {
  return m_database.find(id);
}

auto dbc_database::value_table(can_id const id, std::string_view const signal) const noexcept
  -> std::span<dbc_enum_value const> {
  auto const raw{detail::dbc_raw_id(id)};
  for (auto const& table : m_metadata.value_tables) {
    if (table.message_id == raw && table.signal == signal) {
      return table.values;
    }
  }
  return {};
}

auto dbc_database::value_name(
  can_id const id, std::string_view const signal, std::int64_t const raw
) const noexcept -> std::optional<std::string_view> {
  for (auto const& entry : value_table(id, signal)) {
    if (entry.value == raw) {
      return entry.label;
    }
  }
  return std::nullopt;
}

auto dbc_database::signal_comment(can_id const id, std::string_view const signal) const noexcept
  -> std::string_view {
  return comment_of(detail::dbc_scope::signal, detail::dbc_raw_id(id), signal);
}

auto dbc_database::message_comment(can_id const id) const noexcept -> std::string_view {
  return comment_of(detail::dbc_scope::message, detail::dbc_raw_id(id), {});
}

auto dbc_database::database_comment() const noexcept -> std::string_view {
  return comment_of(detail::dbc_scope::database, 0, {});
}

auto dbc_database::signal_attribute(
  can_id const id, std::string_view const signal, std::string_view const name
) const noexcept -> std::optional<std::string_view> {
  return attribute_of(detail::dbc_scope::signal, detail::dbc_raw_id(id), signal, name);
}

auto dbc_database::message_attribute(can_id const id, std::string_view const name) const noexcept
  -> std::optional<std::string_view> {
  return attribute_of(detail::dbc_scope::message, detail::dbc_raw_id(id), {}, name);
}

auto dbc_database::database_attribute(std::string_view const name) const noexcept
  -> std::optional<std::string_view> {
  return attribute_of(detail::dbc_scope::database, 0, {}, name);
}

auto dbc_database::comment_of(
  detail::dbc_scope const scope, std::uint32_t const raw, std::string_view const signal
) const noexcept -> std::string_view {
  for (auto const& entry : m_metadata.comments) {
    if (entry.scope == scope
        && (scope == detail::dbc_scope::database || (entry.message_id == raw && entry.signal == signal))) {
      return entry.text;
    }
  }
  return {};
}

auto dbc_database::attribute_of(
  detail::dbc_scope const scope,
  std::uint32_t const raw,
  std::string_view const signal,
  std::string_view const name
) const noexcept -> std::optional<std::string_view> {
  for (auto const& entry : m_metadata.attributes) {
    if (entry.scope == scope && entry.name == name
        && (scope == detail::dbc_scope::database || (entry.message_id == raw && entry.signal == signal))) {
      return entry.value;
    }
  }
  return std::nullopt;
}

auto parse_dbc(std::string_view const source) -> result<dbc_database> {
  auto owned{std::make_unique<std::string>(source)};
  std::string_view const text{*owned};
  std::vector<message> messages;
  std::optional<message_builder> current;

  // SIG_VALTYPE_ lines can follow all the messages, so they are collected here
  // and applied in a second pass once every message and signal exists.
  struct pending_value_type {
    std::uint32_t raw_id{0};
    std::string_view signal{};
    std::uint32_t kind{0};
  };

  std::vector<pending_value_type> value_types;
  detail::dbc_metadata metadata;

  // Reads a BA_ attribute value up to the terminating ';', stripping the quotes
  // from a string value so the caller sees the text either way.
  auto read_attribute_value{[](detail::dbc_scanner& scanner) -> std::string_view {
    auto value{detail::dbc_trim(scanner.until(';'))};
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
      value = value.substr(1, value.size() - 2);
    }
    return value;
  }};

  std::size_t pos{0};
  while (pos <= text.size()) {
    auto const newline{text.find('\n', pos)};
    auto const line{detail::dbc_trim(
      text.substr(pos, newline == std::string_view::npos ? std::string_view::npos : newline - pos)
    )};
    pos = newline == std::string_view::npos ? text.size() + 1 : newline + 1;

    if (line.starts_with("BO_ ")) {
      if (current) {
        messages.push_back(current->build());
      }
      auto parsed{detail::dbc_parse_message(line)};
      if (!parsed) {
        return std::unexpected{parsed.error()};
      }
      current = std::move(*parsed);
    } else if (line.starts_with("SG_ ")) {
      if (!current) {
        continue;
      }
      auto parsed{detail::dbc_parse_signal(line)};
      if (!parsed) {
        return std::unexpected{parsed.error()};
      }
      current->add(*parsed);
    } else if (line.starts_with("SIG_VALTYPE_ ")) {
      // "SIG_VALTYPE_ <msgid> <signal> : <kind>;": kind 1 is a 32-bit float and 2
      // a 64-bit double. Collected here and applied after every signal exists.
      detail::dbc_scanner value_type{line};
      [[maybe_unused]] auto const keyword{value_type.token()};
      auto const message_id{value_type.token()};
      auto const signal_name{value_type.token()};
      std::uint32_t raw_id{0};
      if (value_type.consume(':') && detail::dbc_parse_u32(message_id, raw_id)) {
        std::uint32_t kind{0};
        if (detail::dbc_parse_u32(detail::dbc_trim(value_type.until(';')), kind) && kind != 0U) {
          value_types.push_back(pending_value_type{raw_id, signal_name, kind});
        }
      }
    } else if (line.starts_with("VAL_ ")) {
      // "VAL_ <msgid> <signal> <value> "<label>" ... ;": a signal's value table.
      detail::dbc_scanner scanner{line};
      [[maybe_unused]] auto const keyword{scanner.token()};
      std::uint32_t raw_id{0};
      if (detail::dbc_parse_u32(scanner.token(), raw_id)) {
        detail::dbc_metadata::value_table table{raw_id, scanner.token(), {}};
        while (!scanner.consume(';')) {
          std::int64_t entry_value{0};
          if (!detail::dbc_parse_i64(scanner.token(), entry_value)) {
            break;
          }
          auto const label{scanner.read_quoted()};
          if (!label) {
            break;
          }
          table.values.push_back(dbc_enum_value{entry_value, *label});
        }
        if (!table.values.empty()) {
          metadata.value_tables.push_back(std::move(table));
        }
      }
    } else if (line.starts_with("CM_ ")) {
      // "CM_ [SG_ <msgid> <signal> | BO_ <msgid>] "<text>";": a comment.
      detail::dbc_scanner scanner{line};
      [[maybe_unused]] auto const keyword{scanner.token()};
      if (auto const database_text{scanner.read_quoted()}) {
        metadata.comments.push_back({detail::dbc_scope::database, 0, {}, *database_text});
      } else {
        auto const kind{scanner.token()};
        std::uint32_t raw_id{0};
        if (kind == "SG_" && detail::dbc_parse_u32(scanner.token(), raw_id)) {
          auto const signal{scanner.token()};
          if (auto const comment_text{scanner.read_quoted()}) {
            metadata.comments.push_back({detail::dbc_scope::signal, raw_id, signal, *comment_text});
          }
        } else if (kind == "BO_" && detail::dbc_parse_u32(scanner.token(), raw_id)) {
          if (auto const comment_text{scanner.read_quoted()}) {
            metadata.comments.push_back({detail::dbc_scope::message, raw_id, {}, *comment_text});
          }
        }
      }
    } else if (line.starts_with("BA_ ")) {
      // "BA_ "<name>" [SG_ <msgid> <signal> | BO_ <msgid>] <value>;": an attribute.
      detail::dbc_scanner scanner{line};
      [[maybe_unused]] auto const keyword{scanner.token()};
      if (auto const name{scanner.read_quoted()}) {
        detail::dbc_scanner probe{scanner};
        auto const kind{probe.token()};
        std::uint32_t raw_id{0};
        if (kind == "SG_") {
          [[maybe_unused]] auto const sg{scanner.token()};
          if (detail::dbc_parse_u32(scanner.token(), raw_id)) {
            auto const signal{scanner.token()};
            metadata.attributes.push_back(
              {detail::dbc_scope::signal, raw_id, signal, *name, read_attribute_value(scanner)}
            );
          }
        } else if (kind == "BO_") {
          [[maybe_unused]] auto const bo{scanner.token()};
          if (detail::dbc_parse_u32(scanner.token(), raw_id)) {
            metadata.attributes.push_back(
              {detail::dbc_scope::message, raw_id, {}, *name, read_attribute_value(scanner)}
            );
          }
        } else if (kind != "BU_" && kind != "EV_") {
          metadata.attributes.push_back(
            {detail::dbc_scope::database, 0, {}, *name, read_attribute_value(scanner)}
          );
        }
      }
    }
  }
  if (current) {
    messages.push_back(current->build());
  }

  // Second pass: mark the float signals named by the SIG_VALTYPE_ lines. A float
  // value type on a signal whose width is not 32 or 64 bits is a malformed file.
  for (auto const& value_type : value_types) {
    bool const extended{(value_type.raw_id & detail::dbc_extended_flag) != 0U};
    std::uint32_t const ident{value_type.raw_id & ~detail::dbc_extended_flag};
    for (message& msg : messages) {
      if (msg.id().extended() != extended || msg.id().identifier() != ident) {
        continue;
      }
      auto const index{msg.find_signal(value_type.signal)};
      if (!index) {
        continue;
      }
      auto& definition{msg.signals()[*index].definition};
      if (definition.length() != 32U && definition.length() != 64U) {
        return std::unexpected{can_error::parse_error};
      }
      definition.is_float() = true;
    }
  }

  database_builder builder;
  for (message& msg : messages) {
    builder.add_message(std::move(msg));
  }
  return dbc_database{std::move(owned), builder.build(), std::move(metadata)};
}

}  // namespace nexenne::can
