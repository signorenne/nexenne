#pragma once

/**
 * @file
 * @brief Schema-versioned envelope around the binary writer / reader.
 *
 * Long-lived payloads (save files, config blobs, EEPROM dumps,
 * messages on a long-running bus) outlive a single release of the
 * code that produced them. \c versioned wraps a payload in a tiny
 * envelope that lets the consumer recognise the version and dispatch
 * to the correct decode routine, without parsing the body to find
 * out what version it is.
 *
 * Envelope layout (8 bytes total):
 *
 * \verbatim
 *   magic    : uint32   little-endian, your application's tag
 *   version  : uint16   the schema version of the payload
 *   reserved : uint16   zero, kept for future extension
 * \endverbatim
 *
 * Followed by the payload bytes themselves, in whatever format the
 * caller chose (binary, msgpack, cbor, json, raw).
 *
 * The reader side uses concept-constrained dispatch: pass an object
 * whose \c decode(reader&, version) returns
 * \c std::expected<T, error>, and \c versioned handles the envelope,
 * version check, and routing.
 *
 * Typical use, a save-file format that grows over time:
 *
 * \code
 * struct save_codec {
 *     auto decode(binary::reader& r, std::uint16_t v) const
 *         -> std::expected<game_state, error>
 *     {
 *         switch (v) {
 *             case 1: return decode_v1(r);
 *             case 2: return decode_v2(r);
 *             default: return std::unexpected{error::invalid_input};
 *         }
 *     }
 * };
 * \endcode
 */

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <span>

#include <nexenne/serialization/binary/reader.hpp>
#include <nexenne/serialization/binary/writer.hpp>
#include <nexenne/serialization/error.hpp>
#include <nexenne/utility/endian.hpp>

namespace nexenne::serialization {

// True when T is a std::expected whose error type is serialization::error,
// i.e. the shape every codec's decode must return.
template <typename T>
concept expected_with_error = requires {
  typename T::value_type;
  typename T::error_type;
} && std::same_as<typename T::error_type, error>;

/**
 * @brief Requirements for a codec usable with \c decode_with.
 *
 * A codec must expose a \c const \c decode(reader&, version) member that returns
 * a \c std::expected<T, error> for some payload \c T; the codec owns the
 * version-dispatch logic. The concept checks both that the call is well-formed
 * and that its result is an \c expected with \c error as the error type, so a
 * codec whose \c decode returns the wrong shape is rejected at the call site
 * rather than failing deeper.
 *
 * @tparam Codec  Candidate codec type.
 */
template <typename Codec>
concept versioned_decoder = requires(Codec const& c, binary::reader& r, std::uint16_t v) {
  { c.decode(r, v) } -> expected_with_error;
};

namespace detail {

inline auto store_le32(std::byte* const dst, std::uint32_t const v) noexcept -> void {
  nexenne::utility::write_le(
    std::span<std::byte, sizeof(std::uint32_t)>{dst, sizeof(std::uint32_t)}, v
  );
}

inline auto store_le16(std::byte* const dst, std::uint16_t const v) noexcept -> void {
  nexenne::utility::write_le(
    std::span<std::byte, sizeof(std::uint16_t)>{dst, sizeof(std::uint16_t)}, v
  );
}

inline auto load_le32(std::byte const* const src) noexcept -> std::uint32_t {
  return nexenne::utility::read_le<std::uint32_t>(
    std::span<std::byte const, sizeof(std::uint32_t)>{src, sizeof(std::uint32_t)}
  );
}

inline auto load_le16(std::byte const* const src) noexcept -> std::uint16_t {
  return nexenne::utility::read_le<std::uint16_t>(
    std::span<std::byte const, sizeof(std::uint16_t)>{src, sizeof(std::uint16_t)}
  );
}

}  // namespace detail

/// @brief Envelope size in bytes, same on every platform.
inline constexpr std::size_t versioned_header_size{8};

/**
 * @brief Stamp a versioned envelope at the head of a writer.
 *
 * Call once before writing the payload body. The header is fixed size
 * (\c versioned_header_size bytes: magic, version, and a zeroed reserved
 * field), so the caller never has to backfill anything.
 *
 * @param w        Writer positioned where the envelope should go,
 *                 normally at offset zero.
 * @param magic    Application tag identifying the payload kind.
 * @param version  Schema version of the body that follows.
 *
 * @return Empty on success.
 *
 * @pre None.
 * @post On success \p w has advanced by \c versioned_header_size bytes; on
 *       failure the writer is unchanged.
 *
 * @throws None. Returns \c error::buffer_full when the writer lacks room
 *         for the header.
 */
inline auto write_header(
  binary::writer& w, std::uint32_t const magic, std::uint16_t const version
) noexcept -> std::expected<void, error> {
  std::byte hdr[versioned_header_size]{};
  detail::store_le32(hdr, magic);
  detail::store_le16(hdr + 4, version);
  detail::store_le16(hdr + 6, 0);
  return w.write_bytes(std::span<std::byte const>{hdr, versioned_header_size});
}

/**
 * @brief Parsed envelope fields populated by \c read_header.
 */
struct header {
  std::uint32_t magic{};    ///< Application tag read from the envelope.
  std::uint16_t version{};  ///< Schema version of the body that follows.
};

/**
 * @brief Validate an envelope and return its parsed fields.
 *
 * Reads the fixed-size header, checks the magic against \p expected_magic,
 * and leaves the reader positioned at the start of the body. The reserved
 * field is not surfaced.
 *
 * @param r              Reader positioned at the start of the envelope.
 * @param expected_magic Application tag this payload must carry.
 *
 * @return The parsed header on success.
 *
 * @pre None.
 * @post On success \p r has advanced past the envelope and is ready to
 *       decode the body; on failure the cursor may have advanced over part
 *       of the header.
 *
 * @throws None. Returns \c error::buffer_underrun when fewer than
 *         \c versioned_header_size bytes remain, or
 *         \c error::invalid_input when the stored magic does not match
 *         \p expected_magic.
 */
[[nodiscard]] inline auto read_header(
  binary::reader& r, std::uint32_t const expected_magic
) noexcept -> std::expected<header, error> {
  if (r.bytes_remaining() < versioned_header_size) {
    return std::unexpected{error::buffer_underrun};
  }
  std::byte hdr[versioned_header_size]{};
  for (std::size_t i{0}; i < versioned_header_size; ++i) {
    auto b{r.template read<std::uint8_t>()};
    if (!b)
      return std::unexpected{b.error()};
    hdr[i] = static_cast<std::byte>(*b);
  }
  auto const magic{detail::load_le32(hdr)};
  if (magic != expected_magic) {
    return std::unexpected{error::invalid_input};
  }
  return header{.magic = magic, .version = detail::load_le16(hdr + 4)};
}

/**
 * @brief Read an envelope and hand the body to \p codec.
 *
 * Composition helper: validates the envelope with \c read_header, then
 * forwards the body and the parsed version to \c codec.decode so the codec
 * only owns version-dispatch. On envelope failure the error is repackaged
 * into the codec's own result type without calling \c decode.
 *
 * @tparam Codec  Codec type satisfying \c versioned_decoder.
 * @param r              Reader positioned at the start of the envelope.
 * @param expected_magic Application tag this payload must carry.
 * @param codec          Codec that decodes the body for a given version.
 *
 * @return The codec's result on success, or the codec's result type
 *         carrying the envelope error on failure.
 *
 * @pre None.
 * @post On envelope success \p r has advanced past the header and \p codec
 *       has consumed the body; on envelope failure \p r is left as
 *       \c read_header left it and \p codec is not invoked.
 *
 * @throws Propagates any exception \c codec.decode may throw. This wrapper
 *         is \c noexcept exactly when \c codec.decode is.
 *
 * @note The wrapper adds no exception of its own. A codec built on this
 *       module's readers (\c binary::reader and the codec readers, all
 *       \c noexcept) therefore makes \c decode_with \c noexcept and usable from
 *       an interrupt handler; only a user codec that itself throws makes the
 *       call potentially throwing.
 */
template <versioned_decoder Codec>
[[nodiscard]] auto decode_with(
  binary::reader& r, std::uint32_t const expected_magic, Codec const& codec
) noexcept(noexcept(codec.decode(r, std::uint16_t{}))) -> decltype(codec.decode(r, std::uint16_t{})
                                                       ) {
  auto const h{read_header(r, expected_magic)};
  if (!h) {
    using result_type = decltype(codec.decode(r, std::uint16_t{}));
    return result_type{std::unexpect, h.error()};
  }
  return codec.decode(r, h->version);
}

}  // namespace nexenne::serialization
