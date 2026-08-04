#pragma once

/**
 * @file
 * @brief Parsing a Vector DBC database file into a nexenne::can database.
 *
 * DBC is the de-facto text format for describing a CAN bus: each message is a
 * \c BO_ line and each of its signals an indented \c SG_ line. This parser reads
 * those into a \c database.hpp through the builders, marks IEEE-754 float signals
 * from \c SIG_VALTYPE_, and retains the descriptive metadata (value tables from
 * \c VAL_, comments from \c CM_, attributes from \c BA_) on the \c dbc_database.
 * Sections it does not model (\c VAL_TABLE_, node and environment records) are
 * skipped, so a full vehicle database loads without error.
 *
 * Because a \c signal.hpp holds non-owning name and unit views, the parsed
 * database must keep the source text alive: \c parse_dbc returns a \c dbc_database
 * that owns the text and the database, with every name and unit pointing into
 * that retained text. This is the only part of the module that allocates a string
 * of its own, and it is never on the hot path.
 *
 * Reference: the Vector DBC file format, the \c BO_ and \c SG_ record grammar.
 */

#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <nexenne/can/byte_order.hpp>
#include <nexenne/can/database.hpp>
#include <nexenne/can/database_builder.hpp>
#include <nexenne/can/error.hpp>
#include <nexenne/can/id.hpp>
#include <nexenne/can/message.hpp>
#include <nexenne/can/message_builder.hpp>
#include <nexenne/can/signal.hpp>
#include <nexenne/can/signal_builder.hpp>

namespace nexenne::can {

namespace detail {

/**
 * @brief Flag bit a DBC file sets on a message id to mark it extended.
 */
inline constexpr std::uint32_t dbc_extended_flag{0x8000'0000U};

/**
 * @brief Trims ASCII whitespace from both ends of a view.
 *
 * @param text View to trim.
 *
 * @return The view with leading and trailing spaces, tabs, and carriage returns
 *         removed.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] auto dbc_trim(std::string_view text) noexcept -> std::string_view;

/**
 * @brief Parses a whole view as an unsigned integer.
 *
 * @param text View to parse.
 * @param out Receives the parsed value on success.
 *
 * @return \c true when the entire trimmed view is a valid unsigned integer.
 *
 * @pre None.
 * @post \p out is set only when the result is \c true.
 */
[[nodiscard]] auto dbc_parse_u32(std::string_view text, std::uint32_t& out) noexcept -> bool;

/**
 * @brief Parses a whole view as a floating-point number.
 *
 * @param text View to parse.
 * @param out Receives the parsed value on success.
 *
 * @return \c true when the entire trimmed view is a valid number.
 *
 * @pre None.
 * @post \p out is set only when the result is \c true.
 */
[[nodiscard]] auto dbc_parse_double(std::string_view text, double& out) noexcept -> bool;

/**
 * @brief Parses a whole view as a signed integer.
 *
 * @param text View to parse.
 * @param out Receives the parsed value on success.
 *
 * @return \c true when the entire trimmed view is a valid signed integer.
 *
 * @pre None.
 * @post \p out is set only when the result is \c true.
 */
[[nodiscard]] auto dbc_parse_i64(std::string_view text, std::int64_t& out) noexcept -> bool;

/**
 * @brief A forward cursor over one DBC line for whitespace-delimited scanning.
 */
struct dbc_scanner {
  std::string_view text;  ///< The line being scanned.
  std::size_t pos{0};     ///< Current read position.

  /**
   * @brief Advances past spaces and tabs.
   *
   * @pre None.
   * @post The cursor sits on a non-space character or the end.
   */
  constexpr auto skip_spaces() noexcept -> void {
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t')) {
      ++pos;
    }
  }

  /**
   * @brief Reads the next whitespace-delimited token.
   *
   * @return The token, or an empty view at the end of the line.
   *
   * @pre None.
   * @post The cursor sits just past the token.
   */
  [[nodiscard]] constexpr auto token() noexcept -> std::string_view {
    skip_spaces();
    auto const start{pos};
    while (pos < text.size() && text[pos] != ' ' && text[pos] != '\t') {
      ++pos;
    }
    return text.substr(start, pos - start);
  }

  /**
   * @brief Reads characters up to, but not consuming, a delimiter.
   *
   * @param delimiter Character to stop at.
   *
   * @return The characters from the cursor up to \p delimiter or the end.
   *
   * @pre None.
   * @post The cursor sits on \p delimiter or at the end.
   */
  [[nodiscard]] constexpr auto until(char const delimiter) noexcept -> std::string_view {
    auto const start{pos};
    while (pos < text.size() && text[pos] != delimiter) {
      ++pos;
    }
    return text.substr(start, pos - start);
  }

  /**
   * @brief Consumes \p expected if it is the next non-space character.
   *
   * @param expected Character to match.
   *
   * @return \c true when \p expected was present and consumed.
   *
   * @pre None.
   * @post The cursor advances past \p expected when matched.
   */
  [[nodiscard]] constexpr auto consume(char const expected) noexcept -> bool {
    skip_spaces();
    if (pos < text.size() && text[pos] == expected) {
      ++pos;
      return true;
    }
    return false;
  }

  /**
   * @brief Reads a double-quoted string, returning its contents without the quotes.
   *
   * Does not interpret escape sequences; a backslash is a literal character. The
   * cursor advances past the closing quote on success.
   *
   * @return The quoted contents, or \c std::nullopt when a well-formed quoted
   *         string is not present at the cursor.
   *
   * @pre None.
   * @post On success the cursor sits just past the closing quote.
   */
  [[nodiscard]] constexpr auto read_quoted() noexcept -> std::optional<std::string_view> {
    if (!consume('"')) {
      return std::nullopt;
    }
    auto const contents{until('"')};
    if (!consume('"')) {
      return std::nullopt;
    }
    return contents;
  }
};

/**
 * @brief Parses a DBC \c BO_ message line into a builder.
 *
 * @param line The \c BO_ line.
 *
 * @return A message builder for the message, or \c can_error::parse_error.
 *
 * @pre \p line begins with \c "BO_".
 * @post None.
 */
[[nodiscard]] auto dbc_parse_message(std::string_view const line) -> result<message_builder>;

/**
 * @brief Parses a DBC \c SG_ signal line into a signal.
 *
 * @param line The \c SG_ line.
 *
 * @return The parsed signal, or \c can_error::parse_error on a malformed line.
 *
 * @pre \p line begins with \c "SG_".
 * @post None.
 */
[[nodiscard]] auto dbc_parse_signal(std::string_view const line) -> result<signal>;

}  // namespace detail

/**
 * @brief One raw-value-to-name mapping from a DBC \c VAL_ value table.
 *
 * A value table gives named meanings to a signal's raw integer values (for
 * example \c 0 "off", \c 1 "on"). The label is a non-owning view into the DBC
 * source text held by the \c dbc_database.
 */
struct dbc_enum_value {
  std::int64_t value{0};     ///< The raw signal value.
  std::string_view label{};  ///< The human-readable name; a view into the DBC source.

  /**
   * @brief Value equality over the raw value and label.
   *
   * @param lhs Left operand.
   * @param rhs Right operand.
   *
   * @return \c true when both the value and label are equal.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] friend auto
  operator==(dbc_enum_value const& lhs, dbc_enum_value const& rhs) noexcept -> bool = default;
};

/// @cond INTERNAL
namespace detail {

/**
 * @brief The scope a DBC comment or attribute is attached to.
 */
enum class dbc_scope : std::uint8_t {
  database,
  message,
  signal
};

/**
 * @brief The descriptive metadata parsed from a DBC file beyond the wire layout.
 *
 * Value tables, comments, and attributes are import-time descriptive data, not
 * needed to encode or decode a frame, so they live here on the parsed database
 * rather than bloating the trivially copyable \c signal and \c message wire
 * types. Every string is a non-owning view into the retained DBC source.
 */
struct dbc_metadata {
  struct value_table {
    std::uint32_t message_id{0};
    std::string_view signal{};
    std::vector<dbc_enum_value> values{};
  };

  struct comment {
    dbc_scope scope{dbc_scope::database};
    std::uint32_t message_id{0};
    std::string_view signal{};
    std::string_view text{};
  };

  struct attribute {
    dbc_scope scope{dbc_scope::database};
    std::uint32_t message_id{0};
    std::string_view signal{};
    std::string_view name{};
    std::string_view value{};
  };

  std::vector<value_table> value_tables{};
  std::vector<comment> comments{};
  std::vector<attribute> attributes{};
};

/**
 * @brief Converts a \c can_id to the raw DBC identifier word, extended flag included.
 *
 * @param id Identifier to convert.
 *
 * @return The raw DBC id: the identifier value, or-ed with the extended-frame flag
 *         bit when \p id is extended, matching the ids stored in the metadata.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] auto dbc_raw_id(can_id const id) noexcept -> std::uint32_t;

}  // namespace detail

/// @endcond

/**
 * @brief A database parsed from DBC text, owning the text its names point into.
 *
 * The signal and message names and units are views into the retained source, so
 * keep this object alive for as long as the database is used.
 *
 * The source is held through a \c std::unique_ptr<std::string> rather than a bare
 * \c std::string member on purpose: moving the \c dbc_database or a small-string
 * buffer would change the string's address and dangle every view into it. The
 * heap indirection keeps the text at a stable address across moves.
 */
class dbc_database {
public:
  using value_type = message;

private:
  std::unique_ptr<std::string> m_source;
  database m_database;
  detail::dbc_metadata m_metadata;

public:
  /**
   * @brief Constructs from owned source text, the parsed database, and metadata.
   *
   * @param source Heap-owned DBC text the database's views point into.
   * @param parsed The parsed database.
   * @param metadata The value tables, comments, and attributes parsed from the file.
   *
   * @pre The views in \p parsed and \p metadata point into \p source.
   * @post \c source() and \c db() return the given values.
   */
  dbc_database(
    std::unique_ptr<std::string> source, database parsed, detail::dbc_metadata metadata = {}
  ) noexcept;

  /**
   * @brief The retained DBC source text.
   *
   * @return A view of the owned source, or an empty view when none is held.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto source() const noexcept -> std::string_view;

  /**
   * @brief The parsed database.
   *
   * @return Const reference to the database.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto db() const noexcept -> database const&;

  /**
   * @brief Read-only view of the parsed messages.
   *
   * @return A span of the database's messages.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto messages() const noexcept -> std::span<message const>;

  /**
   * @brief The number of parsed messages.
   *
   * @return The message count.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto message_count() const noexcept -> std::size_t;

  /**
   * @brief Finds a parsed message by identifier.
   *
   * @param id Identifier to look up.
   *
   * @return A pointer to the matching message, or \c nullptr when none matches.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto find(can_id const id) const noexcept -> message const*;

  /**
   * @brief The \c VAL_ value table for a signal: its named raw values.
   *
   * @param id Identifier of the message the signal belongs to.
   * @param signal Signal name.
   *
   * @return A span of the signal's value-name pairs, empty when it has none.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto value_table(can_id const id, std::string_view const signal) const noexcept
    -> std::span<dbc_enum_value const>;

  /**
   * @brief The name a signal's value table gives to a raw value.
   *
   * @param id Identifier of the message the signal belongs to.
   * @param signal Signal name.
   * @param raw Raw signal value to name.
   *
   * @return The label for \p raw, or \c std::nullopt when the signal has no value
   *         table or the value is not named.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto
  value_name(can_id const id, std::string_view const signal, std::int64_t const raw) const noexcept
    -> std::optional<std::string_view>;

  /**
   * @brief The \c CM_ comment attached to a signal.
   *
   * @param id Identifier of the message the signal belongs to.
   * @param signal Signal name.
   *
   * @return The comment text, or an empty view when the signal has none.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto signal_comment(can_id const id, std::string_view const signal) const noexcept
    -> std::string_view;

  /**
   * @brief The \c CM_ comment attached to a message.
   *
   * @param id Identifier of the message.
   *
   * @return The comment text, or an empty view when the message has none.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto message_comment(can_id const id) const noexcept -> std::string_view;

  /**
   * @brief The file-level \c CM_ comment describing the database.
   *
   * @return The database comment, or an empty view when there is none.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto database_comment() const noexcept -> std::string_view;

  /**
   * @brief The value of a \c BA_ attribute on a signal.
   *
   * @param id Identifier of the message the signal belongs to.
   * @param signal Signal name.
   * @param name Attribute name.
   *
   * @return The attribute's value text, or \c std::nullopt when it is not set.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto signal_attribute(
    can_id const id, std::string_view const signal, std::string_view const name
  ) const noexcept -> std::optional<std::string_view>;

  /**
   * @brief The value of a \c BA_ attribute on a message.
   *
   * @param id Identifier of the message.
   * @param name Attribute name.
   *
   * @return The attribute's value text, or \c std::nullopt when it is not set.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto message_attribute(can_id const id, std::string_view const name) const noexcept
    -> std::optional<std::string_view>;

  /**
   * @brief The value of a file-level \c BA_ attribute.
   *
   * @param name Attribute name.
   *
   * @return The attribute's value text, or \c std::nullopt when it is not set.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto database_attribute(std::string_view const name) const noexcept
    -> std::optional<std::string_view>;

private:
  [[nodiscard]] auto comment_of(
    detail::dbc_scope const scope, std::uint32_t const raw, std::string_view const signal
  ) const noexcept -> std::string_view;

  [[nodiscard]] auto attribute_of(
    detail::dbc_scope const scope,
    std::uint32_t const raw,
    std::string_view const signal,
    std::string_view const name
  ) const noexcept -> std::optional<std::string_view>;
};

/**
 * @brief Parses DBC text into a database, retaining the source for its names.
 *
 * Reads the \c BO_ message and \c SG_ signal records and ignores the rest. A
 * message id with the high bit set is treated as extended, per the DBC convention.
 *
 * @param source DBC file contents.
 *
 * @return The parsed \c dbc_database, or \c can_error::parse_error on a malformed
 *         \c BO_ or \c SG_ line.
 *
 * @pre None.
 * @post On success every message and signal name and unit points into the
 *       returned object's retained source text.
 */
[[nodiscard]] auto parse_dbc(std::string_view const source) -> result<dbc_database>;

}  // namespace nexenne::can
