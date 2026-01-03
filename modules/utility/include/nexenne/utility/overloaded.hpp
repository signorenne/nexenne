#pragma once

/**
 * @file
 * @brief Merge several callables into one overload set for std::visit.
 */

namespace nexenne::utility {

/**
 * @brief Merges several callables into a single overload set for \c std::visit.
 *
 * Inherits from every callable in \p Ts and pulls each \c operator() into the
 * merged type, so one \c overloaded object dispatches a \c std::variant by
 * alternative without a handwritten visitor struct. It is a stateless
 * aggregate, so there is no runtime overhead. This is the P0051-style helper,
 * standardised in spirit but never shipped in the standard library.
 *
 * @tparam Ts Pack of callable types to merge.
 *
 * @pre The merged \c operator() overload set is unambiguous for every
 *      intended argument type.
 * @post None.
 *
 * @par Example
 * \code
 * using payload = std::variant<int, std::string>;
 * std::visit(nexenne::utility::overloaded{
 *     [](int value)               { handle_int(value); },
 *     [](std::string const& text) { handle_text(text); },
 * }, payload{42});
 * \endcode
 */
template <typename... Ts>
struct overloaded : Ts... {
  using Ts::operator()...;
};

/**
 * @brief Deduces \c overloaded's \p Ts from its constructor arguments.
 *
 * @tparam Ts Callable types of the constructor arguments.
 *
 * @pre None.
 * @post \c overloaded{a, b} deduces \c overloaded<decltype(a), decltype(b)>.
 */
template <typename... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

}  // namespace nexenne::utility
