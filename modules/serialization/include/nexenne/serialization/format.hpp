#pragma once

/**
 * @file
 * @brief \c std::format support for the nexenne::serialization public types.
 *
 * Opt-in printing layer so callers can write \c std::format("{}", v) directly:
 *
 *   - \c error : prints its \c to_string enumerator name;
 *   - \c json::value : prints its compact \c serialize output;
 *   - \c json::value::kind : prints its kind name;
 *   - \c json::parse_error : prints a \c code at line, column diagnostic;
 *   - \c header : prints the versioned envelope fields;
 *   - \c cbor::type and \c msgpack::type : print the peeked token kind.
 *
 * The core headers stay free of the \c \<format\> include; pull this header in
 * only where the formatting hooks are wanted, keeping it off the hot path.
 */

#include <cstdint>
#include <expected>
#include <format>
#include <string>
#include <string_view>

#include <nexenne/serialization/cbor.hpp>
#include <nexenne/serialization/error.hpp>
#include <nexenne/serialization/json/parse.hpp>
#include <nexenne/serialization/json/serialize.hpp>
#include <nexenne/serialization/json/value.hpp>
#include <nexenne/serialization/msgpack.hpp>
#include <nexenne/serialization/versioned.hpp>

namespace nexenne::serialization::cbor {

/**
 * @brief Human-readable name of a CBOR token kind \p t.
 *
 * @param t  Token kind to name.
 *
 * @return Static string view naming \p t, or \c "?" for an unknown value.
 *
 * @pre None.
 * @post The returned view references storage with static lifetime.
 */
[[nodiscard]] constexpr auto to_string(type const t) noexcept -> std::string_view {
  switch (t) {
    case type::unsigned_int:
      return "unsigned_int";
    case type::negative_int:
      return "negative_int";
    case type::byte_string:
      return "byte_string";
    case type::text_string:
      return "text_string";
    case type::array_header:
      return "array_header";
    case type::map_header:
      return "map_header";
    case type::boolean:
      return "boolean";
    case type::null:
      return "null";
    case type::undefined:
      return "undefined";
    case type::floating:
      return "floating";
  }
  return "?";
}

}  // namespace nexenne::serialization::cbor

namespace nexenne::serialization::msgpack {

/**
 * @brief Human-readable name of a MessagePack token kind \p t.
 *
 * @param t  Token kind to name.
 *
 * @return Static string view naming \p t, or \c "?" for an unknown value.
 *
 * @pre None.
 * @post The returned view references storage with static lifetime.
 */
[[nodiscard]] constexpr auto to_string(type const t) noexcept -> std::string_view {
  switch (t) {
    case type::nil:
      return "nil";
    case type::boolean:
      return "boolean";
    case type::integer:
      return "integer";
    case type::floating:
      return "floating";
    case type::string:
      return "string";
    case type::binary:
      return "binary";
    case type::array_header:
      return "array_header";
    case type::map_header:
      return "map_header";
  }
  return "?";
}

}  // namespace nexenne::serialization::msgpack

namespace nexenne::serialization::json {

/**
 * @brief Human-readable name of a JSON value kind \p k.
 *
 * @param k  Value kind to name.
 *
 * @return Static string view naming \p k, or \c "?" for an unknown value.
 *
 * @pre None.
 * @post The returned view references storage with static lifetime.
 */
[[nodiscard]] constexpr auto to_string(value::kind const k) noexcept -> std::string_view {
  switch (k) {
    case value::kind::null_kind:
      return "null";
    case value::kind::boolean_kind:
      return "boolean";
    case value::kind::integer_kind:
      return "integer";
    case value::kind::floating_kind:
      return "floating";
    case value::kind::string_kind:
      return "string";
    case value::kind::array_kind:
      return "array";
    case value::kind::object_kind:
      return "object";
  }
  return "?";
}

/**
 * @brief Diagnostic string for a JSON parse failure \p e.
 *
 * Combines the error code with the failure location, for example
 * \c "invalid_string at line 3, column 12 (offset 41)".
 *
 * @param e  Parse error to render.
 *
 * @return A freshly built diagnostic string.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] inline auto to_string(parse_error const& e) -> std::string {
  return std::format(
    "{} at line {}, column {} (offset {})", to_string(e.code), e.line, e.column, e.offset
  );
}

}  // namespace nexenne::serialization::json

namespace nexenne::serialization {

/**
 * @brief Diagnostic string for a versioned envelope \p h.
 *
 * Renders the parsed magic tag (in hex) and schema version, for example
 * \c "{magic: 0x4e455801, version: 2}".
 *
 * @param h  Parsed envelope fields to render.
 *
 * @return A freshly built diagnostic string.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] inline auto to_string(header const& h) -> std::string {
  return std::format("{{magic: {:#010x}, version: {}}}", h.magic, h.version);
}

}  // namespace nexenne::serialization

/**
 * @brief \c std::format support for \c error: prints its \c to_string name.
 *
 * Inherits the string formatter so a spec (width, alignment) applies to the
 * name, and so \c std::format("{}", err) works directly on a value returned
 * from a \c std::expected without a manual \c to_string call.
 */
template <>
struct std::formatter<nexenne::serialization::error> : std::formatter<std::string_view> {
  /**
   * @brief Formats the error's \c to_string name through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param e Error to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The error name has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::serialization::error const e, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::serialization::to_string(e), ctx);
  }
};

/**
 * @brief \c std::format support for \c json::value: prints its compact JSON.
 *
 * Inherits the string formatter so a spec (width, alignment) applies to the
 * serialised text, and forwards to \c json::serialize, so
 * \c std::format("{}", v) yields the same one-line JSON a manual
 * \c serialize(v) would. Use \c serialize_pretty directly when indented
 * output is wanted, the format spec covers only the compact form.
 */
template <>
struct std::formatter<nexenne::serialization::json::value> : std::formatter<std::string_view> {
  /**
   * @brief Formats the value's compact \c serialize output as a string.
   *
   * @tparam FormatContext Deduced output context type.
   * @param v Value to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The serialised JSON has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::serialization::json::value const& v, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(
      nexenne::serialization::json::serialize(v), ctx
    );
  }
};

/**
 * @brief \c std::format support for \c cbor::type: prints its kind name.
 *
 * Inherits the string formatter so a width / alignment spec applies to the
 * name and \c std::format("{}", *r.peek_type()) works directly.
 */
template <>
struct std::formatter<nexenne::serialization::cbor::type> : std::formatter<std::string_view> {
  /**
   * @brief Formats the token kind's \c to_string name through the string
   *        formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param t Token kind to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The kind name has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::serialization::cbor::type const t, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(
      nexenne::serialization::cbor::to_string(t), ctx
    );
  }
};

/**
 * @brief \c std::format support for \c msgpack::type: prints its kind name.
 *
 * Inherits the string formatter so a width / alignment spec applies to the
 * name and \c std::format("{}", *r.peek_type()) works directly.
 */
template <>
struct std::formatter<nexenne::serialization::msgpack::type> : std::formatter<std::string_view> {
  /**
   * @brief Formats the token kind's \c to_string name through the string
   *        formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param t Token kind to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The kind name has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::serialization::msgpack::type const t, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(
      nexenne::serialization::msgpack::to_string(t), ctx
    );
  }
};

/**
 * @brief \c std::format support for \c json::value::kind: prints its name.
 *
 * Inherits the string formatter so a width / alignment spec applies to the
 * kind name.
 */
template <>
struct std::formatter<nexenne::serialization::json::value::kind>
    : std::formatter<std::string_view> {
  /**
   * @brief Formats the kind's \c to_string name through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param k Value kind to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The kind name has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::serialization::json::value::kind const k, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(
      nexenne::serialization::json::to_string(k), ctx
    );
  }
};

/**
 * @brief \c std::format support for \c json::parse_error: prints a diagnostic.
 *
 * Inherits the string formatter so a width / alignment spec applies to the
 * rendered \c code at line, column string.
 */
template <>
struct std::formatter<nexenne::serialization::json::parse_error>
    : std::formatter<std::string_view> {
  /**
   * @brief Formats the parse error's \c to_string diagnostic.
   *
   * @tparam FormatContext Deduced output context type.
   * @param e Parse error to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The diagnostic string has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::serialization::json::parse_error const& e, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(
      nexenne::serialization::json::to_string(e), ctx
    );
  }
};

/**
 * @brief \c std::format support for \c header: prints the envelope fields.
 *
 * Inherits the string formatter so a width / alignment spec applies to the
 * rendered magic and version.
 */
template <>
struct std::formatter<nexenne::serialization::header> : std::formatter<std::string_view> {
  /**
   * @brief Formats the header's \c to_string field dump.
   *
   * @tparam FormatContext Deduced output context type.
   * @param h Header to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The header fields have been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::serialization::header const& h, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::serialization::to_string(h), ctx);
  }
};
