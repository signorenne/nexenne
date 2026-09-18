#pragma once

/**
 * @file
 * @brief The three formatting layers for the nexenne::serialization public types.
 *
 * Opt-in printing layer so callers can write \c std::format("{}", v), or stream
 * a value with \c operator<<, directly. Each type below has a \c to_string, an
 * \c operator<< and a \c std::formatter, all printing the same text:
 *
 *   - \c error : prints its \c to_string enumerator name;
 *   - \c json::value : prints its compact \c serialize output;
 *   - \c json::value::kind : prints its kind name;
 *   - \c json::parse_error : prints a \c code at line, column diagnostic;
 *   - \c header : prints the versioned envelope fields;
 *   - \c cbor::type and \c msgpack::type : print the peeked token kind;
 *   - \c json::parse_options and \c json::serialize_options : print every knob;
 *   - the \c binary, \c cbor and \c msgpack readers and writers, and the
 *     streaming \c json::writer : print the bytes consumed or produced, the
 *     bytes left, and (for the JSON writer) the open nesting depth.
 *
 * The core headers stay free of the \c \<format\> include; pull this header in
 * only where the formatting hooks are wanted, keeping it off the hot path.
 */

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <ostream>
#include <string>
#include <string_view>

#include <nexenne/serialization/binary/reader.hpp>
#include <nexenne/serialization/binary/writer.hpp>
#include <nexenne/serialization/cbor.hpp>
#include <nexenne/serialization/error.hpp>
#include <nexenne/serialization/json/parse.hpp>
#include <nexenne/serialization/json/serialize.hpp>
#include <nexenne/serialization/json/value.hpp>
#include <nexenne/serialization/json/writer.hpp>
#include <nexenne/serialization/msgpack.hpp>
#include <nexenne/serialization/versioned.hpp>

namespace nexenne::serialization::cbor {

/**
 * @brief Human-readable name of a CBOR token kind \p t.
 *
 * @param t Token kind to name.
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

/**
 * @brief Summary string for a CBOR \c writer \p w.
 *
 * Renders the bytes encoded so far and the room left, for example
 * \c "cbor::writer(bytes_written=3, bytes_remaining=13)".
 *
 * @param w Writer to render.
 *
 * @return A freshly built summary string.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] inline auto to_string(writer const& w) -> std::string {
  return std::format(
    "cbor::writer(bytes_written={}, bytes_remaining={})", w.bytes_written(), w.bytes_remaining()
  );
}

/**
 * @brief Summary string for a CBOR \c reader \p r.
 *
 * Renders the bytes consumed so far and the bytes left, for example
 * \c "cbor::reader(bytes_read=1, bytes_remaining=2)".
 *
 * @param r Reader to render.
 *
 * @return A freshly built summary string.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] inline auto to_string(reader const& r) -> std::string {
  return std::format(
    "cbor::reader(bytes_read={}, bytes_remaining={})", r.bytes_read(), r.bytes_remaining()
  );
}

}  // namespace nexenne::serialization::cbor

namespace nexenne::serialization::msgpack {

/**
 * @brief Human-readable name of a MessagePack token kind \p t.
 *
 * @param t Token kind to name.
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

/**
 * @brief Summary string for a MessagePack \c writer \p w.
 *
 * Renders the bytes encoded so far and the room left, for example
 * \c "msgpack::writer(bytes_written=3, bytes_remaining=13)".
 *
 * @param w Writer to render.
 *
 * @return A freshly built summary string.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] inline auto to_string(writer const& w) -> std::string {
  return std::format(
    "msgpack::writer(bytes_written={}, bytes_remaining={})", w.bytes_written(), w.bytes_remaining()
  );
}

/**
 * @brief Summary string for a MessagePack \c reader \p r.
 *
 * Renders the bytes consumed so far and the bytes left, for example
 * \c "msgpack::reader(bytes_read=1, bytes_remaining=2)".
 *
 * @param r Reader to render.
 *
 * @return A freshly built summary string.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] inline auto to_string(reader const& r) -> std::string {
  return std::format(
    "msgpack::reader(bytes_read={}, bytes_remaining={})", r.bytes_read(), r.bytes_remaining()
  );
}

}  // namespace nexenne::serialization::msgpack

namespace nexenne::serialization::binary {

/**
 * @brief Summary string for a binary \c writer \p w.
 *
 * Renders the bytes written so far and the room left, for example
 * \c "binary::writer(bytes_written=4, bytes_remaining=12)".
 *
 * @param w Writer to render.
 *
 * @return A freshly built summary string.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] inline auto to_string(writer const& w) -> std::string {
  return std::format(
    "binary::writer(bytes_written={}, bytes_remaining={})", w.bytes_written(), w.bytes_remaining()
  );
}

/**
 * @brief Summary string for a binary \c reader \p r.
 *
 * Renders the bytes consumed so far, the bytes left and the per-string size
 * cap, for example
 * \c "binary::reader(bytes_read=2, bytes_remaining=2, max_string_size=67108864)".
 *
 * @param r Reader to render.
 *
 * @return A freshly built summary string.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] inline auto to_string(reader const& r) -> std::string {
  return std::format(
    "binary::reader(bytes_read={}, bytes_remaining={}, max_string_size={})",
    r.bytes_read(),
    r.bytes_remaining(),
    r.max_string_size()
  );
}

}  // namespace nexenne::serialization::binary

namespace nexenne::serialization::json {

/**
 * @brief Human-readable name of a JSON value kind \p k.
 *
 * @param k Value kind to name.
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
 * @param e Parse error to render.
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

/**
 * @brief Summary string for the parser options \p o, every knob included.
 *
 * Renders, for example,
 * \c "json::parse_options(allow_comments=false, allow_trailing_commas=false, max_depth=128)".
 *
 * @param o Options to render.
 *
 * @return A freshly built summary string.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] inline auto to_string(parse_options const& o) -> std::string {
  return std::format(
    "json::parse_options(allow_comments={}, allow_trailing_commas={}, max_depth={})",
    o.allow_comments,
    o.allow_trailing_commas,
    o.max_depth
  );
}

/**
 * @brief Summary string for the serialiser options \p o, every knob included.
 *
 * Renders, for example, \c "json::serialize_options(indent=2, ascii_only=false)".
 *
 * @param o Options to render.
 *
 * @return A freshly built summary string.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] inline auto to_string(serialize_options const& o) -> std::string {
  return std::format("json::serialize_options(indent={}, ascii_only={})", o.indent, o.ascii_only);
}

/**
 * @brief Summary string for a streaming JSON \c writer \p w.
 *
 * Renders the characters written so far, the room left, the open container
 * depth against its limit and whether a complete document has been emitted,
 * for example
 * \c "json::writer(bytes_written=1, bytes_remaining=63, depth=1, max_depth=32, complete=false)".
 *
 * @tparam MaxDepth Maximum container nesting depth of the writer.
 * @param w Writer to render.
 *
 * @return A freshly built summary string.
 *
 * @pre None.
 * @post None.
 */
template <std::size_t MaxDepth>
[[nodiscard]] auto to_string(writer<MaxDepth> const& w) -> std::string {
  return std::format(
    "json::writer(bytes_written={}, bytes_remaining={}, depth={}, max_depth={}, complete={})",
    w.bytes_written(),
    w.bytes_remaining(),
    w.depth(),
    MaxDepth,
    w.is_complete()
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
 * @param h Parsed envelope fields to render.
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
   * @brief Formats the token kind's \c to_string name through the string formatter.
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
   * @brief Formats the token kind's \c to_string name through the string formatter.
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

/**
 * @brief \c std::format support for \c json::parse_options: prints every parser knob.
 *
 * Inherits the string formatter so a width / alignment spec applies to the
 * rendered summary.
 *
 * @pre None.
 * @post None.
 */
template <>
struct std::formatter<nexenne::serialization::json::parse_options>
    : std::formatter<std::string_view> {
  /**
   * @brief Formats the \c to_string summary of the options.
   *
   * @tparam FormatContext Deduced output context type.
   * @param o Options to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The summary has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::serialization::json::parse_options const& o, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(
      nexenne::serialization::json::to_string(o), ctx
    );
  }
};

/**
 * @brief \c std::format support for \c json::serialize_options: prints every serialiser knob.
 *
 * Inherits the string formatter so a width / alignment spec applies to the
 * rendered summary.
 *
 * @pre None.
 * @post None.
 */
template <>
struct std::formatter<nexenne::serialization::json::serialize_options>
    : std::formatter<std::string_view> {
  /**
   * @brief Formats the \c to_string summary of the options.
   *
   * @tparam FormatContext Deduced output context type.
   * @param o Options to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The summary has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::serialization::json::serialize_options const& o, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(
      nexenne::serialization::json::to_string(o), ctx
    );
  }
};

/**
 * @brief \c std::format support for \c binary::writer: prints the bytes written and left.
 *
 * Inherits the string formatter so a width / alignment spec applies to the
 * rendered summary.
 *
 * @pre None.
 * @post None.
 */
template <>
struct std::formatter<nexenne::serialization::binary::writer> : std::formatter<std::string_view> {
  /**
   * @brief Formats the writer's \c to_string summary.
   *
   * @tparam FormatContext Deduced output context type.
   * @param w Writer to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The summary has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::serialization::binary::writer const& w, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(
      nexenne::serialization::binary::to_string(w), ctx
    );
  }
};

/**
 * @brief \c std::format support for \c binary::reader: prints its progress and string cap.
 *
 * Inherits the string formatter so a width / alignment spec applies to the
 * rendered summary.
 *
 * @pre None.
 * @post None.
 */
template <>
struct std::formatter<nexenne::serialization::binary::reader> : std::formatter<std::string_view> {
  /**
   * @brief Formats the reader's \c to_string summary.
   *
   * @tparam FormatContext Deduced output context type.
   * @param r Reader to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The summary has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::serialization::binary::reader const& r, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(
      nexenne::serialization::binary::to_string(r), ctx
    );
  }
};

/**
 * @brief \c std::format support for \c cbor::writer: prints the bytes written and left.
 *
 * Inherits the string formatter so a width / alignment spec applies to the
 * rendered summary.
 *
 * @pre None.
 * @post None.
 */
template <>
struct std::formatter<nexenne::serialization::cbor::writer> : std::formatter<std::string_view> {
  /**
   * @brief Formats the writer's \c to_string summary.
   *
   * @tparam FormatContext Deduced output context type.
   * @param w Writer to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The summary has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::serialization::cbor::writer const& w, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(
      nexenne::serialization::cbor::to_string(w), ctx
    );
  }
};

/**
 * @brief \c std::format support for \c cbor::reader: prints the bytes read and left.
 *
 * Inherits the string formatter so a width / alignment spec applies to the
 * rendered summary.
 *
 * @pre None.
 * @post None.
 */
template <>
struct std::formatter<nexenne::serialization::cbor::reader> : std::formatter<std::string_view> {
  /**
   * @brief Formats the reader's \c to_string summary.
   *
   * @tparam FormatContext Deduced output context type.
   * @param r Reader to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The summary has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::serialization::cbor::reader const& r, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(
      nexenne::serialization::cbor::to_string(r), ctx
    );
  }
};

/**
 * @brief \c std::format support for \c msgpack::writer: prints the bytes written and left.
 *
 * Inherits the string formatter so a width / alignment spec applies to the
 * rendered summary.
 *
 * @pre None.
 * @post None.
 */
template <>
struct std::formatter<nexenne::serialization::msgpack::writer> : std::formatter<std::string_view> {
  /**
   * @brief Formats the writer's \c to_string summary.
   *
   * @tparam FormatContext Deduced output context type.
   * @param w Writer to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The summary has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::serialization::msgpack::writer const& w, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(
      nexenne::serialization::msgpack::to_string(w), ctx
    );
  }
};

/**
 * @brief \c std::format support for \c msgpack::reader: prints the bytes read and left.
 *
 * Inherits the string formatter so a width / alignment spec applies to the
 * rendered summary.
 *
 * @pre None.
 * @post None.
 */
template <>
struct std::formatter<nexenne::serialization::msgpack::reader> : std::formatter<std::string_view> {
  /**
   * @brief Formats the reader's \c to_string summary.
   *
   * @tparam FormatContext Deduced output context type.
   * @param r Reader to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The summary has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::serialization::msgpack::reader const& r, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(
      nexenne::serialization::msgpack::to_string(r), ctx
    );
  }
};

/**
 * @brief \c std::format support for a streaming \c json::writer: prints its state.
 *
 * Inherits the string formatter so a width / alignment spec applies to the
 * rendered summary.
 *
 * @tparam MaxDepth Maximum container nesting depth of the writer.
 *
 * @pre None.
 * @post None.
 */
template <std::size_t MaxDepth>
struct std::formatter<nexenne::serialization::json::writer<MaxDepth>>
    : std::formatter<std::string_view> {
  /**
   * @brief Formats the writer's \c to_string summary.
   *
   * @tparam FormatContext Deduced output context type.
   * @param w Writer to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The summary has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::serialization::json::writer<MaxDepth> const& w, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(
      nexenne::serialization::json::to_string(w), ctx
    );
  }
};

namespace nexenne::serialization {

/**
 * @brief Streams an \c error by its name via its \c to_string.
 *
 * @param os Output stream.
 * @param err Value to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The text of \p err has been written to \p os.
 */
inline auto operator<<(std::ostream& os, error const err) -> std::ostream& {
  return os << to_string(err);
}

/**
 * @brief Streams a versioned envelope via its \c to_string.
 *
 * @param os Output stream.
 * @param h Value to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The text of \p h has been written to \p os.
 */
inline auto operator<<(std::ostream& os, header const& h) -> std::ostream& {
  return os << to_string(h);
}

}  // namespace nexenne::serialization

namespace nexenne::serialization::cbor {

/**
 * @brief Streams a CBOR token kind by its name via its \c to_string.
 *
 * @param os Output stream.
 * @param t Value to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The text of \p t has been written to \p os.
 */
inline auto operator<<(std::ostream& os, type const t) -> std::ostream& {
  return os << to_string(t);
}

/**
 * @brief Streams a CBOR \c writer summary via its \c to_string.
 *
 * @param os Output stream.
 * @param w Value to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The text of \p w has been written to \p os.
 */
inline auto operator<<(std::ostream& os, writer const& w) -> std::ostream& {
  return os << to_string(w);
}

/**
 * @brief Streams a CBOR \c reader summary via its \c to_string.
 *
 * @param os Output stream.
 * @param r Value to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The text of \p r has been written to \p os.
 */
inline auto operator<<(std::ostream& os, reader const& r) -> std::ostream& {
  return os << to_string(r);
}

}  // namespace nexenne::serialization::cbor

namespace nexenne::serialization::msgpack {

/**
 * @brief Streams a MessagePack token kind by its name via its \c to_string.
 *
 * @param os Output stream.
 * @param t Value to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The text of \p t has been written to \p os.
 */
inline auto operator<<(std::ostream& os, type const t) -> std::ostream& {
  return os << to_string(t);
}

/**
 * @brief Streams a MessagePack \c writer summary via its \c to_string.
 *
 * @param os Output stream.
 * @param w Value to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The text of \p w has been written to \p os.
 */
inline auto operator<<(std::ostream& os, writer const& w) -> std::ostream& {
  return os << to_string(w);
}

/**
 * @brief Streams a MessagePack \c reader summary via its \c to_string.
 *
 * @param os Output stream.
 * @param r Value to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The text of \p r has been written to \p os.
 */
inline auto operator<<(std::ostream& os, reader const& r) -> std::ostream& {
  return os << to_string(r);
}

}  // namespace nexenne::serialization::msgpack

namespace nexenne::serialization::binary {

/**
 * @brief Streams a binary \c writer summary via its \c to_string.
 *
 * @param os Output stream.
 * @param w Value to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The text of \p w has been written to \p os.
 */
inline auto operator<<(std::ostream& os, writer const& w) -> std::ostream& {
  return os << to_string(w);
}

/**
 * @brief Streams a binary \c reader summary via its \c to_string.
 *
 * @param os Output stream.
 * @param r Value to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The text of \p r has been written to \p os.
 */
inline auto operator<<(std::ostream& os, reader const& r) -> std::ostream& {
  return os << to_string(r);
}

}  // namespace nexenne::serialization::binary

namespace nexenne::serialization::json {

/**
 * @brief Compact JSON text of a \c value, as \c serialize writes it.
 *
 * Constrained to exactly \c value: \c value converts implicitly from numbers
 * and strings, and a plain overload would turn an unqualified \c to_string(42)
 * in this namespace into JSON text.
 *
 * @tparam V Exactly \c value.
 * @param v Value to render.
 *
 * @return The compact JSON text.
 *
 * @pre None.
 * @post None.
 *
 * @throws std::bad_alloc if the string cannot be allocated.
 */
template <std::same_as<value> V>
[[nodiscard]] auto to_string(V const& v) -> std::string {
  return serialize(v);
}

/**
 * @brief Streams a \c value as compact JSON via its \c to_string.
 *
 * @tparam V Exactly \c value; deduction keeps other types from converting.
 * @param os Output stream.
 * @param v Value to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The text of \p v has been written to \p os.
 */
template <std::same_as<value> V>
auto operator<<(std::ostream& os, V const& v) -> std::ostream& {
  return os << to_string(v);
}

/**
 * @brief Streams a \c value kind by its name via its \c to_string.
 *
 * @param os Output stream.
 * @param k Value to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The text of \p k has been written to \p os.
 */
inline auto operator<<(std::ostream& os, value::kind const k) -> std::ostream& {
  return os << to_string(k);
}

/**
 * @brief Streams a JSON parse failure via its \c to_string.
 *
 * @param os Output stream.
 * @param e Value to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The text of \p e has been written to \p os.
 */
inline auto operator<<(std::ostream& os, parse_error const& e) -> std::ostream& {
  return os << to_string(e);
}

/**
 * @brief Streams the parser options via its \c to_string.
 *
 * @param os Output stream.
 * @param o Value to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The text of \p o has been written to \p os.
 */
inline auto operator<<(std::ostream& os, parse_options const& o) -> std::ostream& {
  return os << to_string(o);
}

/**
 * @brief Streams the serialiser options via its \c to_string.
 *
 * @param os Output stream.
 * @param o Value to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The text of \p o has been written to \p os.
 */
inline auto operator<<(std::ostream& os, serialize_options const& o) -> std::ostream& {
  return os << to_string(o);
}

/**
 * @brief Streams a streaming JSON \c writer summary via its \c to_string.
 *
 * @tparam MaxDepth Maximum container nesting depth of the writer.
 * @param os Output stream.
 * @param w Value to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The text of \p w has been written to \p os.
 */
template <std::size_t MaxDepth>
auto operator<<(std::ostream& os, writer<MaxDepth> const& w) -> std::ostream& {
  return os << to_string(w);
}

}  // namespace nexenne::serialization::json
