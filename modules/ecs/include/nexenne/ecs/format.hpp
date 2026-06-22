#pragma once

/**
 * @file
 * @brief Debug printing and formatting for \c entity_id.
 *
 * Three interlocking layers, matching the math module's convention:
 *   - \c to_string(e) producing a readable representation;
 *   - \c operator<<(std::ostream&, e) for stream output (delegates to to_string);
 *   - a \c std::formatter specialization so \c std::format("{}", e) works.
 *
 * A live handle prints as \c "entity(index, generation)"; the default
 * (invalid) handle, which carries index 0 and generation 0, prints as
 * \c "entity(invalid)" so the two are never confused.
 *
 * This support is opt-in: \c registry.hpp stays free of \c \<format\>, and only
 * code that includes this header pays for \c \<format\> / \c \<ostream\>.
 */

#include <format>
#include <ostream>
#include <string>
#include <string_view>

#include <nexenne/ecs/registry.hpp>

namespace nexenne::ecs {

/**
 * @brief Debug string for an \c entity_id.
 *
 * Produces \c "entity(index, generation)" for any handle other than the
 * default one, and \c "entity(invalid)" for the default handle (index 0,
 * generation 0) since generation 0 never names a live entity.
 *
 * @param e Handle to print.
 *
 * @return The debug string.
 *
 * @pre None.
 * @post Names the index and generation, or "invalid" for the default handle.
 */
[[nodiscard]] inline auto to_string(entity_id const e) -> std::string {
  if (e.index() == 0 && e.generation() == 0) {
    return "entity(invalid)";
  }
  return std::format("entity({}, {})", e.index(), e.generation());
}

/**
 * @brief Streams an \c entity_id via its debug string.
 *
 * @param os Output stream.
 * @param e Handle to print.
 *
 * @return Reference to \p os, so calls chain.
 *
 * @pre \p os is in a good state.
 * @post The textual handle has been appended to \p os.
 */
inline auto operator<<(std::ostream& os, entity_id const e) -> std::ostream& {
  return os << to_string(e);
}

}  // namespace nexenne::ecs

/**
 * @brief \c std::format support for \c entity_id: prints its \c to_string form.
 *
 * Inherits the string formatter so a spec (width, alignment) applies to the
 * whole \c "entity(...)" rendering, and so \c std::format("{}", e) works
 * directly without a manual \c to_string call.
 */
template <>
struct std::formatter<nexenne::ecs::entity_id> : std::formatter<std::string_view> {
  /**
   * @brief Formats the handle's \c to_string form through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param e Handle to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The handle's textual form has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::ecs::entity_id const e, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::ecs::to_string(e), ctx);
  }
};
