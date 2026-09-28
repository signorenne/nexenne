#pragma once

/**
 * @file
 * @brief Debug printing and formatting for the ecs module's public types.
 *
 * Three interlocking layers, matching the math module's convention:
 *   - \c to_string(x) producing a readable representation;
 *   - \c operator<<(std::ostream&, x) for stream output (delegates to to_string);
 *   - a \c std::formatter specialization so \c std::format("{}", x) works.
 *
 * Covered types: \c entity_id, \c registry, \c component_storage and
 * \c typed_query_builder. A live handle prints as
 * \c "entity(index, generation)"; the default (invalid) handle, which carries
 * index 0 and generation 0, prints as \c "entity(invalid)" so the two are never
 * confused. The registry, a storage and a query builder print a one-line
 * summary of their counts, such as \c "registry(alive=2)". A \c basic_view is an
 * input range, so the standard range formatter already prints its matches when
 * the components are formattable, and it gets no summary formatter here.
 *
 * This support is opt-in: \c registry.hpp stays free of \c \<format\>, and only
 * code that includes this header pays for \c \<format\> / \c \<ostream\>.
 */

#include <format>
#include <ostream>
#include <string>
#include <string_view>

#include <nexenne/ecs/registry.hpp>
#include <nexenne/ecs/view.hpp>

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

/**
 * @brief Debug string for a \c registry: its live entity count.
 *
 * Example: \c "registry(alive=2)". The registry is also an input range of
 * \c entity_id, but its formatter prints this summary rather than every handle.
 *
 * @param reg Registry to describe.
 *
 * @return A string of the form \c "registry(alive=N)".
 *
 * @pre None.
 * @post \p reg is unchanged.
 */
[[nodiscard]] inline auto to_string(registry const& reg) -> std::string {
  return std::format("registry(alive={})", reg.alive());
}

/**
 * @brief Streams a \c registry via its debug string.
 *
 * @param os Output stream.
 * @param reg Registry to print.
 *
 * @return Reference to \p os, so calls chain.
 *
 * @pre \p os is in a good state.
 * @post The registry summary has been appended to \p os.
 */
inline auto operator<<(std::ostream& os, registry const& reg) -> std::ostream& {
  return os << to_string(reg);
}

/**
 * @brief Debug string for a \c component_storage: its live and slot counts.
 *
 * Example: \c "component_storage(size=2, slots=3)", where \c size counts the
 * live components and \c slots counts live slots plus tombstones.
 *
 * @tparam T Component type stored.
 * @param s Storage to describe.
 *
 * @return A string of the form \c "component_storage(size=N, slots=M)".
 *
 * @pre None.
 * @post \p s is unchanged.
 */
template <typename T>
[[nodiscard]] auto to_string(component_storage<T> const& s) -> std::string {
  return std::format("component_storage(size={}, slots={})", s.size(), s.slot_count());
}

/**
 * @brief Streams a \c component_storage via its debug string.
 *
 * @tparam T Component type stored.
 * @param os Output stream.
 * @param s Storage to print.
 *
 * @return Reference to \p os, so calls chain.
 *
 * @pre \p os is in a good state.
 * @post The storage summary has been appended to \p os.
 */
template <typename T>
auto operator<<(std::ostream& os, component_storage<T> const& s) -> std::ostream& {
  return os << to_string(s);
}

/**
 * @brief Debug string for a \c typed_query_builder: its accumulated list sizes.
 *
 * Example: \c "query(with=2, without=1)" after two \c with and one
 * \c without calls. The lists live in the builder's type, so only their sizes
 * are printed.
 *
 * @tparam Includes Component types the query requires.
 * @tparam Excludes Component types the query forbids.
 * @param q Query builder to describe.
 *
 * @return A string of the form \c "query(with=N, without=M)".
 *
 * @pre None.
 * @post \p q is unchanged.
 */
template <typename... Includes, typename... Excludes>
[[nodiscard]] auto to_string([[maybe_unused]] typed_query_builder<
                             detail::type_list<Includes...>,
                             detail::type_list<Excludes...>> const& q) -> std::string {
  return std::format("query(with={}, without={})", sizeof...(Includes), sizeof...(Excludes));
}

/**
 * @brief Streams a \c typed_query_builder via its debug string.
 *
 * @tparam Includes Component types the query requires.
 * @tparam Excludes Component types the query forbids.
 * @param os Output stream.
 * @param q Query builder to print.
 *
 * @return Reference to \p os, so calls chain.
 *
 * @pre \p os is in a good state.
 * @post The query summary has been appended to \p os.
 */
template <typename... Includes, typename... Excludes>
auto operator<<(
  std::ostream& os,
  typed_query_builder<detail::type_list<Includes...>, detail::type_list<Excludes...>> const& q
) -> std::ostream& {
  return os << to_string(q);
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

/**
 * @brief \c std::format support for \c registry: prints its \c to_string summary.
 *
 * A full specialization, so it takes precedence over the standard range
 * formatter the registry would otherwise match as an input range of handles.
 * Inherits the string formatter so a width or alignment spec applies to the
 * whole rendering.
 *
 * @pre None.
 * @post None.
 */
template <>
struct std::formatter<nexenne::ecs::registry> : std::formatter<std::string_view> {
  /**
   * @brief Formats the registry's \c to_string form through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param reg Registry to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The registry summary has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::ecs::registry const& reg, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::ecs::to_string(reg), ctx);
  }
};

/**
 * @brief \c std::format support for \c component_storage: prints its \c to_string summary.
 *
 * Inherits the string formatter so a width or alignment spec applies to the
 * whole rendering.
 *
 * @tparam T Component type stored.
 *
 * @pre None.
 * @post None.
 */
template <typename T>
struct std::formatter<nexenne::ecs::component_storage<T>> : std::formatter<std::string_view> {
  /**
   * @brief Formats the storage's \c to_string form through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param s Storage to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The storage summary has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::ecs::component_storage<T> const& s, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::ecs::to_string(s), ctx);
  }
};

/**
 * @brief \c std::format support for \c typed_query_builder: prints its \c to_string summary.
 *
 * Inherits the string formatter so a width or alignment spec applies to the
 * whole rendering.
 *
 * @tparam Includes Component types the query requires.
 * @tparam Excludes Component types the query forbids.
 *
 * @pre None.
 * @post None.
 */
template <typename... Includes, typename... Excludes>
struct std::formatter<nexenne::ecs::typed_query_builder<
  nexenne::ecs::detail::type_list<Includes...>,
  nexenne::ecs::detail::type_list<Excludes...>>> : std::formatter<std::string_view> {
  /**
   * @brief Formats the builder's \c to_string form through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param q Query builder to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The query summary has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(
    nexenne::ecs::typed_query_builder<
      nexenne::ecs::detail::type_list<Includes...>,
      nexenne::ecs::detail::type_list<Excludes...>> const& q,
    FormatContext& ctx
  ) const {
    return std::formatter<std::string_view>::format(nexenne::ecs::to_string(q), ctx);
  }
};
