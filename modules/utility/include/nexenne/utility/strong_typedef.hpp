#pragma once

/**
 * @file
 * @brief Distinct, opt-in strong type wrapping an underlying value.
 *
 * \c strong_typedef<Tag, T, Ops> wraps a value of type \p T and brands it with
 * a unique \p Tag, so logically distinct quantities that share a primitive
 * representation cannot be mixed accidentally. Unlike a bare typedef every
 * capability is opt-in: a fresh wrapper supports only construction, value
 * access, hashing and formatting. Arithmetic, comparison and bitwise operators
 * are enabled individually (or in groups) through the \c ability bitmask, so
 * a chip identifier never silently gains the ability to be multiplied.
 *
 * The design targets embedded and hot-path use: the header pulls in no stream
 * headers; the wrapper is trivially copyable whenever \p T is, so it passes in
 * registers; operators are \c constexpr and \c noexcept; and capabilities are a
 * scoped-enum bitmask (a single integer), not a type list, keeping template
 * instantiation cheap.
 *
 * \code
 * using namespace nexenne::utility;
 * using meters  = strong_typedef<struct meters_tag, double,
 *                                ability::arithmetic | ability::comparable>;
 * using chip_id = strong_typedef<struct chip_id_tag, std::uint16_t,
 *                                ability::comparable>;
 * auto total{meters{10.0} + meters{5.0}};   // ok: arithmetic enabled
 * auto half {meters{10.0} / 2.0};           // ok: scalar divide
 * auto ratio{meters{10.0} / meters{2.0}};   // ok: dimensionless double
 * // chip_id{1} * 2;                        // ERROR: scale not enabled
 * \endcode
 */

#include <bit>
#include <cassert>
#include <cmath>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <limits>
#include <string_view>
#include <type_traits>
#include <utility>

namespace nexenne::utility {

/**
 * @brief Opt-in capability flags for \c strong_typedef, combined with \c |.
 *
 * Each flag enables one operator family; the groups (\c arithmetic,
 * \c comparable, \c bitwise) are unions provided for convenience. The flag set
 * of a wrapper is a compile-time constant, so unsupported operators are removed
 * from overload resolution rather than failing at the call site.
 *
 * @note \c shift and \c bitops require an unsigned integral underlying:
 *       requesting either on a wrapper whose primary underlying is not an
 *       unsigned integral type is a compile error (a \c static_assert). They
 *       are cleared silently only for a rebound result type, when a mixed
 *       operation promotes to a signed or floating common underlying, through
 *       \c sanitized.
 */
enum class ability : std::uint32_t {
  none = 0U,

  add = 1U << 0U,          ///< \c a+b and \c a+=b (same tag).
  subtract = 1U << 1U,     ///< \c a-b and \c a-=b (same tag).
  unary_minus = 1U << 2U,  ///< \c -a.
  scale = 1U << 3U,        ///< \c a*scalar, \c scalar*a, \c a/scalar and \c *=, \c /=.
  ratio = 1U << 4U,        ///< \c a/b (same tag) yielding a bare scalar.
  modulo = 1U << 5U,       ///< \c a%b and \c a%=b (same tag, integral).
  increment = 1U << 6U,    ///< \c ++a and \c a++.
  decrement = 1U << 7U,    ///< \c --a and \c a--.

  equality = 1U << 8U,  ///< \c == and \c != (same tag).
  ordered = 1U << 9U,   ///< \c <=> and the relational operators (same tag).

  bit_and = 1U << 10U,  ///< \c a&b and \c a&=b (same tag).
  bit_or = 1U << 11U,   ///< \c a|b and \c a|=b (same tag).
  bit_xor = 1U << 12U,  ///< \c a^b and \c a^=b (same tag).
  bit_not = 1U << 13U,  ///< \c ~a.
  shift = 1U << 14U,    ///< shifts and shift-assign (unsigned T only).
  bitops = 1U << 15U,   ///< popcount, rotl, rotr, count*, bit_width (unsigned T only).

  unary_plus = 1U << 16U,  ///< \c +a.
  saturating = 1U << 17U,  ///< \c sat_add and \c sat_sub free functions.
  boolean = 1U << 18U,     ///< explicit \c operator \c bool.
  absolute = 1U << 19U,    ///< \c abs free function.

  /// Convenience group: everything one expects of a numeric quantity.
  arithmetic =
    add | subtract | unary_minus | unary_plus | scale | ratio | increment | decrement | absolute,
  /// Convenience group: equality plus ordering.
  comparable = equality | ordered,
  /// Convenience group: the four integral bitwise operators.
  bitwise = bit_and | bit_or | bit_xor | bit_not,
};

/**
 * @brief Union of two capability sets.
 *
 * @param a Left set.
 * @param b Right set.
 *
 * @return The set holding every flag present in \p a or \p b.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] constexpr auto operator|(ability a, ability b) noexcept -> ability {
  return static_cast<ability>(std::to_underlying(a) | std::to_underlying(b));
}

/**
 * @brief Intersection of two capability sets.
 *
 * @param a Left set.
 * @param b Right set.
 *
 * @return The set holding only flags present in both \p a and \p b.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] constexpr auto operator&(ability a, ability b) noexcept -> ability {
  return static_cast<ability>(std::to_underlying(a) & std::to_underlying(b));
}

/**
 * @brief Symmetric difference of two capability sets.
 *
 * @param a Left set.
 * @param b Right set.
 *
 * @return The set holding flags present in exactly one of \p a and \p b.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] constexpr auto operator^(ability a, ability b) noexcept -> ability {
  return static_cast<ability>(std::to_underlying(a) ^ std::to_underlying(b));
}

/**
 * @brief Complement of a capability set.
 *
 * @param a Set to complement.
 *
 * @return The set with every bit of \p a inverted.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] constexpr auto operator~(ability a) noexcept -> ability {
  return static_cast<ability>(~std::to_underlying(a));
}

/**
 * @brief Tests whether \p set contains every flag in \p flag.
 *
 * @param set Capability set to inspect.
 * @param flag Flag (or group) to look for.
 *
 * @return \c true when all bits of \p flag are present in \p set.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] constexpr auto has(ability set, ability flag) noexcept -> bool {
  return (std::to_underlying(set) & std::to_underlying(flag)) == std::to_underlying(flag);
}

/**
 * @brief Human-readable name of a single \c ability flag or named group.
 *
 * Recognises every individual flag, the three convenience groups
 * (\c arithmetic, \c comparable, \c bitwise) and \c none. Any other value
 * (an ad-hoc union of flags with no dedicated name) reports \c "unknown".
 *
 * @param flag Flag, named group or \c none to describe.
 *
 * @return A static string view naming \p flag.
 *
 * @pre None.
 * @post The returned view refers to a string with program lifetime.
 */
[[nodiscard]] constexpr auto to_string(ability flag) noexcept -> std::string_view {
  switch (flag) {
    case ability::none:
      return "none";
    case ability::add:
      return "add";
    case ability::subtract:
      return "subtract";
    case ability::unary_minus:
      return "unary_minus";
    case ability::scale:
      return "scale";
    case ability::ratio:
      return "ratio";
    case ability::modulo:
      return "modulo";
    case ability::increment:
      return "increment";
    case ability::decrement:
      return "decrement";
    case ability::equality:
      return "equality";
    case ability::ordered:
      return "ordered";
    case ability::bit_and:
      return "bit_and";
    case ability::bit_or:
      return "bit_or";
    case ability::bit_xor:
      return "bit_xor";
    case ability::bit_not:
      return "bit_not";
    case ability::shift:
      return "shift";
    case ability::bitops:
      return "bitops";
    case ability::unary_plus:
      return "unary_plus";
    case ability::saturating:
      return "saturating";
    case ability::boolean:
      return "boolean";
    case ability::absolute:
      return "absolute";
    case ability::arithmetic:
      return "arithmetic";
    case ability::comparable:
      return "comparable";
    case ability::bitwise:
      return "bitwise";
  }
  return "unknown";
}

/**
 * @brief Drops capabilities that the underlying type \p T cannot support.
 *
 * \c shift and \c bitops require an unsigned integral underlying; when a binary
 * operation rebinds to a different (signed or floating) common type, those
 * flags are removed so the result type stays well-formed.
 *
 * @tparam T Prospective underlying type.
 * @param ops Capability set to sanitise.
 *
 * @return \p ops with the unsigned-only flags cleared when \p T is not an
 *         unsigned integral type.
 *
 * @pre None.
 * @post None.
 */
template <typename T>
[[nodiscard]] constexpr auto sanitized(ability ops) noexcept -> ability {
  if constexpr (std::unsigned_integral<T>) {
    return ops;
  } else {
    return ops & ~(ability::shift | ability::bitops);
  }
}

template <typename Tag, typename T, ability Ops = ability::none>
class strong_typedef;

/// @cond INTERNAL
namespace detail {

template <typename X>
using value_t = typename std::remove_cvref_t<X>::value_type;

template <typename X>
using tag_t = typename std::remove_cvref_t<X>::tag_type;

}  // namespace detail

/// @endcond

/**
 * @brief Trait whose \c value member is \c true when \p X is a \c strong_typedef.
 *
 * The primary template inherits \c std::false_type; a partial specialisation
 * over \c strong_typedef inherits \c std::true_type. Use the \c strong_typedef_like
 * concept for cv-qualified or reference candidates.
 *
 * @tparam X Candidate type (expected without cv or reference qualifiers).
 */
template <typename X>
struct is_strong_typedef : std::false_type {};

/// @cond INTERNAL
template <typename Tag, typename T, ability Ops>
struct is_strong_typedef<strong_typedef<Tag, T, Ops>> : std::true_type {};

/// @endcond

/**
 * @brief Satisfied by \c strong_typedef and its cv/ref variants.
 *
 * @tparam X Candidate type.
 */
template <typename X>
concept strong_typedef_like = is_strong_typedef<std::remove_cvref_t<X>>::value;

/**
 * @brief Satisfied when two strong types share the same \c Tag.
 *
 * @tparam A First strong type.
 * @tparam B Second strong type.
 */
template <typename A, typename B>
concept same_tag_as = strong_typedef_like<A> && strong_typedef_like<B>
                      && std::same_as<detail::tag_t<A>, detail::tag_t<B>>;

/// @cond INTERNAL
namespace detail {

// The underlying type a same-tag binary operation rebinds to.
template <typename A, typename B>
using common_value_t = std::common_type_t<value_t<A>, value_t<B>>;

template <typename X>
inline constexpr ability ops_of{std::remove_cvref_t<X>::abilities};

template <typename X, ability Flag>
inline constexpr bool has_op{has(ops_of<X>, Flag)};

/**
 * @brief Casts \p v to \p To, eliding the cast when the types already match.
 *
 * Centralising fold-backs here keeps the operators free of narrowing in braced
 * initialisation while staying clean under \c -Wconversion and
 * \c -Wuseless-cast.
 *
 * @tparam To Target type.
 * @tparam From Source type (deduced from \p v).
 * @param v Value to convert.
 *
 * @return \p v expressed as type \p To.
 *
 * @pre None.
 * @post None.
 */
template <typename To, typename From>
[[nodiscard]] constexpr auto convert(From v) noexcept -> To {
  if constexpr (std::same_as<To, From>) {
    return v;
  } else {
    return static_cast<To>(v);
  }
}

/**
 * @brief Reduces a shift or rotate count into the range [0, bit width of \p U).
 *
 * Branchless and free of undefined behaviour: negative counts wrap through the
 * unsigned domain, and the modulo by a power-of-two width compiles to a mask.
 *
 * @tparam U Unsigned integral underlying type whose width bounds the count.
 * @tparam S Integral type of the incoming count.
 * @param s Raw shift or rotate count (may be negative).
 *
 * @return \p s reduced into the range [0, number of value bits in \p U).
 *
 * @pre None.
 * @post None.
 */
template <std::unsigned_integral U, std::integral S>
[[nodiscard]] constexpr auto normalize_shift(S s) noexcept -> unsigned {
  constexpr auto width{static_cast<unsigned>(std::numeric_limits<U>::digits)};
  using us = std::make_unsigned_t<S>;
  return static_cast<unsigned>(static_cast<us>(s) % static_cast<us>(width));
}

/**
 * @brief Saturating add for unsigned integers, clamping to the max on overflow.
 *
 * @tparam U Unsigned integral type.
 * @param a Left addend.
 * @param b Right addend.
 *
 * @return \p a plus \p b, or the maximum value of \p U when the sum would
 *         overflow.
 *
 * @pre None.
 * @post None.
 */
template <std::unsigned_integral U>
[[nodiscard]] constexpr auto sat_add_impl(U a, U b) noexcept -> U {
  U const c{static_cast<U>(a + b)};
  return c < a ? std::numeric_limits<U>::max() : c;
}

/**
 * @brief Saturating subtract for unsigned integers, clamping to zero on underflow.
 *
 * @tparam U Unsigned integral type.
 * @param a Minuend.
 * @param b Subtrahend.
 *
 * @return \p a minus \p b, or zero when the difference would underflow.
 *
 * @pre None.
 * @post None.
 */
template <std::unsigned_integral U>
[[nodiscard]] constexpr auto sat_sub_impl(U a, U b) noexcept -> U {
  return a < b ? U{0} : static_cast<U>(a - b);
}

/**
 * @brief Saturating add for signed integers, clamping to the representable range.
 *
 * @tparam S Signed integral type.
 * @param a Left addend.
 * @param b Right addend.
 *
 * @return \p a plus \p b, clamped to the range of \p S on overflow or underflow.
 *
 * @pre None.
 * @post None.
 */
template <std::signed_integral S>
[[nodiscard]] constexpr auto sat_add_impl(S a, S b) noexcept -> S {
  using lim = std::numeric_limits<S>;
  if (b > 0 && a > static_cast<S>(lim::max() - b)) {
    return lim::max();
  }
  if (b < 0 && a < static_cast<S>(lim::min() - b)) {
    return lim::min();
  }
  return static_cast<S>(a + b);
}

/**
 * @brief Saturating subtract for signed integers, clamping to the representable range.
 *
 * @tparam S Signed integral type.
 * @param a Minuend.
 * @param b Subtrahend.
 *
 * @return \p a minus \p b, clamped to the range of \p S on overflow or underflow.
 *
 * @pre None.
 * @post None.
 */
template <std::signed_integral S>
[[nodiscard]] constexpr auto sat_sub_impl(S a, S b) noexcept -> S {
  using lim = std::numeric_limits<S>;
  if (b < 0 && a > static_cast<S>(lim::max() + b)) {
    return lim::max();
  }
  if (b > 0 && a < static_cast<S>(lim::min() + b)) {
    return lim::min();
  }
  return static_cast<S>(a - b);
}

}  // namespace detail

/// @endcond

/**
 * @brief Distinct value type wrapping \p T, branded by \p Tag, with \p Ops
 *        capabilities.
 *
 * Two specialisations that share \p T but differ in \p Tag are unrelated types,
 * so the compiler rejects mixing them. Operations beyond construction, value
 * access, hashing and formatting are enabled only when the matching
 * \c ability flag is set in \p Ops.
 *
 * @tparam Tag Unique tag type distinguishing wrappers that share \p T.
 * @tparam T Underlying value type.
 * @tparam Ops Capability bitmask (see \c ability); defaults to
 *             \c ability::none.
 *
 * @invariant \c shift and \c bitops operators are available only when \p T is
 *            an unsigned integral type, regardless of \p Ops.
 */
template <typename Tag, typename T, ability Ops>
class strong_typedef {
public:
  using value_type = T;
  using tag_type = Tag;

  /// @brief The capability set this wrapper was instantiated with.
  static constexpr ability abilities{Ops};

  static_assert(
    !has(Ops, ability::shift) || std::unsigned_integral<T>,
    "ability::shift requires an unsigned integral underlying type"
  );
  static_assert(
    !has(Ops, ability::bitops) || std::unsigned_integral<T>,
    "ability::bitops requires an unsigned integral underlying type"
  );

private:
  /// @brief The wrapped underlying value.
  [[no_unique_address]] T m_value{};

public:
  /**
   * @brief Constructs a wrapper holding a value-initialised \p T.
   *
   * @pre None.
   * @post \c get() equals a value-initialised \p T.
   */
  constexpr strong_typedef() noexcept(std::is_nothrow_default_constructible_v<T>) = default;

  /**
   * @brief Constructs a wrapper holding \p value (always explicit).
   *
   * Explicit construction prevents a bare \p T from silently becoming a tagged
   * value in an argument list.
   *
   * @param value Underlying value to wrap; moved in.
   *
   * @pre None.
   * @post \c get() equals the moved-in \p value.
   */
  constexpr explicit strong_typedef(T value) noexcept(std::is_nothrow_move_constructible_v<T>)
      : m_value{std::move(value)} {}

  /**
   * @brief Explicitly converts from another same-tag wrapper.
   *
   * Enables intentional conversions between wrappers that share \p Tag but
   * differ in their underlying type (a 16-bit and a 32-bit representation of
   * the same quantity) or differ only in their ability set (granting or
   * dropping capabilities on the same underlying). The only excluded source is
   * the exact same specialisation, which is handled by the copy constructor.
   *
   * @tparam U Other underlying type (may equal \p T).
   * @tparam Ops2 Other capability set (ignored).
   * @param other Source wrapper carrying the same \p Tag.
   *
   * @pre None.
   * @post \c get() equals \c static_cast<T>(other.get()).
   */
  template <typename U, ability Ops2>
    requires(!std::same_as<strong_typedef<Tag, U, Ops2>, strong_typedef>)
            && std::constructible_from<T, U const&>
  constexpr explicit strong_typedef(
    strong_typedef<Tag, U, Ops2> const& other
  ) noexcept(std::is_nothrow_constructible_v<T, U const&>)
      : m_value{static_cast<T>(other.get())} {}

  /**
   * @brief The underlying value of a const lvalue (read-only).
   *
   * @return Const reference to the wrapped value.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto get() const& noexcept -> T const& {
    return m_value;
  }

  /**
   * @brief The underlying value of a mutable lvalue.
   *
   * @return Mutable reference to the wrapped value.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto get() & noexcept -> T& {
    return m_value;
  }

  /**
   * @brief The underlying value of an rvalue, ready to be moved from.
   *
   * @return Rvalue reference to the wrapped value.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto get() && noexcept -> T&& {
    return std::move(m_value);
  }

  /**
   * @brief The underlying value of a const rvalue.
   *
   * @return Const rvalue reference to the wrapped value.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto get() const&& noexcept -> T const&& {
    return std::move(m_value);
  }

  /**
   * @brief Explicit conversion to the underlying value.
   *
   * Explicit so the wrapper cannot silently collapse back to \p T inside an
   * arithmetic chain.
   *
   * @return Const reference to the wrapped value.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr explicit operator T const&() const noexcept {
    return m_value;
  }

  /**
   * @brief Explicit conversion to \c bool (requires \c ability::boolean).
   *
   * @return \c true when the underlying value is truthy.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr explicit operator bool() const noexcept
    requires(has(Ops, ability::boolean)) && (!std::same_as<std::remove_cv_t<T>, bool>)
            && requires(T const& v) { static_cast<bool>(v); }
  {
    return static_cast<bool>(m_value);
  }

  /**
   * @brief Adds a same-tag value in place (requires \c ability::add).
   *
   * @param o Value to add.
   *
   * @return Reference to \c *this.
   *
   * @pre None.
   * @post \c get() holds the previous value plus \p o.
   *
   * @note Unlike the free \c operator+, compound assignment accepts only the
   *       exact same specialisation: the destination underlying cannot widen,
   *       so a mixed-underlying right-hand side is rejected by design.
   */
  constexpr auto operator+=(strong_typedef const& o) noexcept(noexcept(m_value + o.m_value))
    -> strong_typedef&
    requires(has(Ops, ability::add)) && requires(T& a, T const& b) { a + b; }
  {
    m_value = detail::convert<T>(m_value + o.m_value);
    return *this;
  }

  /**
   * @brief Subtracts a same-tag value in place (requires \c ability::subtract).
   *
   * @param o Value to subtract.
   *
   * @return Reference to \c *this.
   *
   * @pre None.
   * @post \c get() holds the previous value minus \p o.
   *
   * @note Unlike the free \c operator-, compound assignment accepts only the
   *       exact same specialisation: the destination underlying cannot widen,
   *       so a mixed-underlying right-hand side is rejected by design.
   */
  constexpr auto operator-=(strong_typedef const& o) noexcept(noexcept(m_value - o.m_value))
    -> strong_typedef&
    requires(has(Ops, ability::subtract)) && requires(T& a, T const& b) { a - b; }
  {
    m_value = detail::convert<T>(m_value - o.m_value);
    return *this;
  }

  /**
   * @brief Scales by a scalar in place (requires \c ability::scale).
   *
   * @tparam S Scalar type (not a strong type).
   * @param s Scalar factor.
   *
   * @return Reference to \c *this.
   *
   * @pre None.
   * @post \c get() holds the previous value scaled by \p s.
   */
  template <typename S>
    requires(has(Ops, ability::scale)) && (!strong_typedef_like<S>)
            && requires(T& a, S const& b) { a * b; }
  constexpr auto operator*=(S const& s) noexcept(noexcept(m_value * s)) -> strong_typedef& {
    m_value = detail::convert<T>(m_value * s);
    return *this;
  }

  /**
   * @brief Divides by a scalar in place (requires \c ability::scale).
   *
   * @tparam S Scalar type (not a strong type).
   * @param s Scalar divisor.
   *
   * @return Reference to \c *this.
   *
   * @pre \p s is nonzero when \p T is an integral type (integer division by
   *      zero is undefined behaviour).
   * @post \c get() holds the previous value divided by \p s.
   */
  template <typename S>
    requires(has(Ops, ability::scale)) && (!strong_typedef_like<S>)
            && requires(T& a, S const& b) { a / b; }
  constexpr auto operator/=(S const& s) noexcept(noexcept(m_value / s)) -> strong_typedef& {
    m_value = detail::convert<T>(m_value / s);
    return *this;
  }

  /**
   * @brief Modulo by a same-tag value in place (requires \c ability::modulo).
   *
   * @param o Value to take the modulo by.
   *
   * @return Reference to \c *this.
   *
   * @pre \p o is nonzero (integer modulo by zero is undefined behaviour).
   * @post \c get() holds the previous value modulo \p o.
   *
   * @note Unlike the free \c operator%, compound assignment accepts only the
   *       exact same specialisation: the destination underlying cannot widen,
   *       so a mixed-underlying right-hand side is rejected by design.
   */
  constexpr auto operator%=(strong_typedef const& o) noexcept(noexcept(m_value % o.m_value))
    -> strong_typedef&
    requires(has(Ops, ability::modulo)) && requires(T& a, T const& b) { a % b; }
  {
    m_value = detail::convert<T>(m_value % o.m_value);
    return *this;
  }

  /**
   * @brief Bitwise-AND with a same-tag value in place (requires \c ability::bit_and).
   *
   * @param o Value to AND in.
   *
   * @return Reference to \c *this.
   *
   * @pre None.
   * @post \c get() holds the bitwise AND of its previous value and \p o.
   *
   * @note Unlike the free \c operator&, compound assignment accepts only the
   *       exact same specialisation: the destination underlying cannot widen,
   *       so a mixed-underlying right-hand side is rejected by design.
   */
  constexpr auto operator&=(strong_typedef const& o) noexcept(noexcept(m_value & o.m_value))
    -> strong_typedef&
    requires(has(Ops, ability::bit_and)) && requires(T& a, T const& b) { a & b; }
  {
    m_value = detail::convert<T>(m_value & o.m_value);
    return *this;
  }

  /**
   * @brief Bitwise-OR with a same-tag value in place (requires \c ability::bit_or).
   *
   * @param o Value to OR in.
   *
   * @return Reference to \c *this.
   *
   * @pre None.
   * @post \c get() holds the bitwise OR of its previous value and \p o.
   *
   * @note Unlike the free \c operator|, compound assignment accepts only the
   *       exact same specialisation: the destination underlying cannot widen,
   *       so a mixed-underlying right-hand side is rejected by design.
   */
  constexpr auto operator|=(strong_typedef const& o) noexcept(noexcept(m_value | o.m_value))
    -> strong_typedef&
    requires(has(Ops, ability::bit_or)) && requires(T& a, T const& b) { a | b; }
  {
    m_value = detail::convert<T>(m_value | o.m_value);
    return *this;
  }

  /**
   * @brief Bitwise-XOR with a same-tag value in place (requires \c ability::bit_xor).
   *
   * @param o Value to XOR in.
   *
   * @return Reference to \c *this.
   *
   * @pre None.
   * @post \c get() holds the bitwise XOR of its previous value and \p o.
   *
   * @note Unlike the free \c operator^, compound assignment accepts only the
   *       exact same specialisation: the destination underlying cannot widen,
   *       so a mixed-underlying right-hand side is rejected by design.
   */
  constexpr auto operator^=(strong_typedef const& o) noexcept(noexcept(m_value ^ o.m_value))
    -> strong_typedef&
    requires(has(Ops, ability::bit_xor)) && requires(T& a, T const& b) { a ^ b; }
  {
    m_value = detail::convert<T>(m_value ^ o.m_value);
    return *this;
  }

  /**
   * @brief Left-shifts in place (requires \c ability::shift, unsigned \p T).
   *
   * @tparam S Integral shift-count type.
   * @param s Shift amount in the range [0, bit width of \p T).
   *
   * @return Reference to \c *this.
   *
   * @pre \p s is non-negative and less than the bit width of \p T; a negative
   *      or out-of-range count is undefined behaviour (checked by \c assert in
   *      debug builds).
   * @post \c get() holds the previous value shifted left by \p s.
   */
  template <typename S>
    requires(has(Ops, ability::shift)) && std::unsigned_integral<T>
            && std::integral<std::remove_cvref_t<S>> && (!strong_typedef_like<S>)
  constexpr auto operator<<=(S s) noexcept(noexcept(m_value << s)) -> strong_typedef& {
    assert(
      std::cmp_greater_equal(s, 0) && std::cmp_less(s, std::numeric_limits<T>::digits)
      && "shift count out of range"
    );
    m_value = detail::convert<T>(m_value << s);
    return *this;
  }

  /**
   * @brief Right-shifts in place (requires \c ability::shift, unsigned \p T).
   *
   * @tparam S Integral shift-count type.
   * @param s Shift amount in the range [0, bit width of \p T).
   *
   * @return Reference to \c *this.
   *
   * @pre \p s is non-negative and less than the bit width of \p T; a negative
   *      or out-of-range count is undefined behaviour (checked by \c assert in
   *      debug builds).
   * @post \c get() holds the previous value shifted right by \p s.
   */
  template <typename S>
    requires(has(Ops, ability::shift)) && std::unsigned_integral<T>
            && std::integral<std::remove_cvref_t<S>> && (!strong_typedef_like<S>)
  constexpr auto operator>>=(S s) noexcept(noexcept(m_value >> s)) -> strong_typedef& {
    assert(
      std::cmp_greater_equal(s, 0) && std::cmp_less(s, std::numeric_limits<T>::digits)
      && "shift count out of range"
    );
    m_value = detail::convert<T>(m_value >> s);
    return *this;
  }

  /**
   * @brief Pre-increment (requires \c ability::increment).
   *
   * @return Reference to \c *this after incrementing.
   *
   * @pre None.
   * @post \c get() is one greater than before.
   */
  constexpr auto operator++() noexcept(noexcept(++m_value)) -> strong_typedef&
    requires(has(Ops, ability::increment)) && requires(T& v) { ++v; }
  {
    ++m_value;
    return *this;
  }

  /**
   * @brief Pre-decrement (requires \c ability::decrement).
   *
   * @return Reference to \c *this after decrementing.
   *
   * @pre None.
   * @post \c get() is one less than before.
   */
  constexpr auto operator--() noexcept(noexcept(--m_value)) -> strong_typedef&
    requires(has(Ops, ability::decrement)) && requires(T& v) { --v; }
  {
    --m_value;
    return *this;
  }

  /**
   * @brief Post-increment (requires \c ability::increment).
   *
   * @return A copy of the value before incrementing.
   *
   * @pre None.
   * @post \c get() is one greater than the returned value.
   */
  constexpr auto
  operator++(int) noexcept(std::is_nothrow_copy_constructible_v<T> && noexcept(++m_value))
    -> strong_typedef
    requires(has(Ops, ability::increment)) && requires(T& v) { v++; }
  {
    auto tmp{*this};
    ++m_value;
    return tmp;
  }

  /**
   * @brief Post-decrement (requires \c ability::decrement).
   *
   * @return A copy of the value before decrementing.
   *
   * @pre None.
   * @post \c get() is one less than the returned value.
   */
  constexpr auto
  operator--(int) noexcept(std::is_nothrow_copy_constructible_v<T> && noexcept(--m_value))
    -> strong_typedef
    requires(has(Ops, ability::decrement)) && requires(T& v) { v--; }
  {
    auto tmp{*this};
    --m_value;
    return tmp;
  }

  /**
   * @brief ADL swap exchanging the underlying values.
   *
   * @param a First wrapper.
   * @param b Second wrapper.
   *
   * @pre None.
   * @post \p a and \p b have exchanged values.
   */
  friend constexpr auto
  swap(strong_typedef& a, strong_typedef& b) noexcept(std::is_nothrow_swappable_v<T>) -> void {
    using std::swap;
    swap(a.m_value, b.m_value);
  }
};

/**
 * @name Ready-made profiles
 * @brief Aliases for the common capability sets, so the frequent cases stay short.
 * @{
 */

/**
 * @brief Numeric quantity: arithmetic plus comparison (e.g. a length).
 *
 * @tparam Tag Unique tag type.
 * @tparam T Underlying value type.
 */
template <typename Tag, typename T>
using quantity = strong_typedef<Tag, T, ability::arithmetic | ability::comparable>;

/**
 * @brief Opaque identifier: comparison and hashing only, no arithmetic.
 *
 * @tparam Tag Unique tag type.
 * @tparam T Underlying value type.
 */
template <typename Tag, typename T>
using identifier = strong_typedef<Tag, T, ability::comparable>;

/**
 * @brief Monotonic counter: comparison plus increment and decrement.
 *
 * @tparam Tag Unique tag type.
 * @tparam T Underlying value type.
 */
template <typename Tag, typename T>
using counter =
  strong_typedef<Tag, T, ability::comparable | ability::increment | ability::decrement>;

/**
 * @brief Register-style value: bitwise, shifts and bit-counting ops (unsigned \p T).
 *
 * @tparam Tag Unique tag type.
 * @tparam T Underlying value type (unsigned integral).
 */
template <typename Tag, typename T>
using bitfield =
  strong_typedef<Tag, T, ability::bitwise | ability::shift | ability::bitops | ability::comparable>;

/** @} */

/// @cond INTERNAL
namespace detail {

// Result type of a same-tag binary operation: common underlying, unioned
// abilities sanitised for that underlying.
template <typename A, typename B>
using common_strong_t = strong_typedef<
  tag_t<A>,
  common_value_t<A, B>,
  sanitized<common_value_t<A, B>>(ops_of<A> | ops_of<B>)>;

}  // namespace detail

/// @endcond

/**
 * @brief Sum of two same-tag values (requires \c ability::add on both).
 *
 * @tparam A Left strong type.
 * @tparam B Right strong type.
 * @param a Left addend.
 * @param b Right addend.
 *
 * @return Their sum as the common strong type.
 *
 * @pre None.
 * @post None.
 */
template <strong_typedef_like A, strong_typedef_like B>
  requires same_tag_as<A, B> && detail::has_op<A, ability::add> && detail::has_op<B, ability::add>
           && requires(detail::value_t<A> u, detail::value_t<B> v) { u + v; }
[[nodiscard]] constexpr auto operator+(A const& a, B const& b) noexcept(noexcept(a.get() + b.get()))
  -> detail::common_strong_t<A, B> {
  using ret = detail::common_strong_t<A, B>;
  using r = typename ret::value_type;
  return ret{detail::convert<r>(detail::convert<r>(a.get()) + detail::convert<r>(b.get()))};
}

/**
 * @brief Difference of two same-tag values (requires \c ability::subtract on both).
 *
 * @tparam A Left strong type.
 * @tparam B Right strong type.
 * @param a Minuend.
 * @param b Subtrahend.
 *
 * @return Their difference as the common strong type.
 *
 * @pre None.
 * @post None.
 */
template <strong_typedef_like A, strong_typedef_like B>
  requires same_tag_as<A, B> && detail::has_op<A, ability::subtract>
           && detail::has_op<B, ability::subtract>
           && requires(detail::value_t<A> u, detail::value_t<B> v) { u - v; }
[[nodiscard]] constexpr auto operator-(A const& a, B const& b) noexcept(noexcept(a.get() - b.get()))
  -> detail::common_strong_t<A, B> {
  using ret = detail::common_strong_t<A, B>;
  using r = typename ret::value_type;
  return ret{detail::convert<r>(detail::convert<r>(a.get()) - detail::convert<r>(b.get()))};
}

/**
 * @brief Negation (requires \c ability::unary_minus).
 *
 * @tparam X Strong type.
 * @param x Operand.
 *
 * @return A wrapper holding the negated value.
 *
 * @pre None.
 * @post None.
 */
template <strong_typedef_like X>
  requires detail::has_op<X, ability::unary_minus> && requires(detail::value_t<X> v) { -v; }
[[nodiscard]] constexpr auto operator-(X const& x) noexcept(noexcept(-x.get()))
  -> std::remove_cvref_t<X> {
  using s = std::remove_cvref_t<X>;
  return s{detail::convert<typename s::value_type>(-x.get())};
}

/**
 * @brief Unary plus (requires \c ability::unary_plus); preserves the strong type.
 *
 * @tparam X Strong type.
 * @param x Operand.
 *
 * @return A wrapper holding the promoted value.
 *
 * @pre None.
 * @post None.
 */
template <strong_typedef_like X>
  requires detail::has_op<X, ability::unary_plus> && requires(detail::value_t<X> v) { +v; }
[[nodiscard]] constexpr auto operator+(X const& x) noexcept(noexcept(+x.get()))
  -> std::remove_cvref_t<X> {
  using s = std::remove_cvref_t<X>;
  return s{detail::convert<typename s::value_type>(+x.get())};
}

/**
 * @brief Absolute value (requires \c ability::absolute).
 *
 * For an unsigned underlying this is the identity. For a floating-point
 * underlying it forwards to \c std::abs (constexpr in C++23), which handles the
 * sign of negative zero and of NaN correctly. For a signed integral underlying
 * it negates the value when it is negative.
 *
 * @tparam X Strong type.
 * @param x Operand.
 *
 * @return A wrapper holding \c |x|.
 *
 * @pre For a signed integral \p T, \p x is not the most negative value:
 *      negating \c INT_MIN and its analogues is undefined behaviour.
 * @post None.
 */
template <strong_typedef_like X>
  requires detail::has_op<X, ability::absolute> && requires(detail::value_t<X> v) { -v; }
[[nodiscard]] constexpr auto abs(X const& x) noexcept(noexcept(-x.get()))
  -> std::remove_cvref_t<X> {
  using s = std::remove_cvref_t<X>;
  using t = typename s::value_type;
  if constexpr (std::floating_point<t>) {
    return s{std::abs(x.get())};
  } else if constexpr (std::is_signed_v<t>) {
    return s{x.get() < t{0} ? detail::convert<t>(-x.get()) : x.get()};
  } else {
    return s{x.get()};
  }
}

/**
 * @brief Modulo of two same-tag values (requires \c ability::modulo on both).
 *
 * @tparam A Left strong type.
 * @tparam B Right strong type.
 * @param a Dividend.
 * @param b Divisor.
 *
 * @return The remainder as the common strong type.
 *
 * @pre \p b is nonzero (integer modulo by zero is undefined behaviour).
 * @post None.
 */
template <strong_typedef_like A, strong_typedef_like B>
  requires same_tag_as<A, B> && detail::has_op<A, ability::modulo>
           && detail::has_op<B, ability::modulo>
           && requires(detail::value_t<A> u, detail::value_t<B> v) { u % v; }
[[nodiscard]] constexpr auto operator%(A const& a, B const& b) noexcept(noexcept(a.get() % b.get()))
  -> detail::common_strong_t<A, B> {
  using ret = detail::common_strong_t<A, B>;
  using r = typename ret::value_type;
  return ret{detail::convert<r>(detail::convert<r>(a.get()) % detail::convert<r>(b.get()))};
}

/**
 * @brief Product of a wrapped value and a scalar (requires \c ability::scale).
 *
 * The result keeps the same strong type as \p x: the product is folded back
 * into the underlying type, mirroring \c operator*=. Convert explicitly first
 * if a wider result type is wanted.
 *
 * @tparam X Strong type.
 * @tparam S Scalar type (not a strong type).
 * @param x Wrapped value.
 * @param s Scalar factor.
 *
 * @return A wrapper holding \c x*s.
 *
 * @pre None.
 * @post None.
 */
template <strong_typedef_like X, typename S>
  requires(!strong_typedef_like<S>) && detail::has_op<X, ability::scale>
          && requires(detail::value_t<X> u, S const& v) { u * v; }
[[nodiscard]] constexpr auto operator*(X const& x, S const& s) noexcept(noexcept(x.get() * s))
  -> std::remove_cvref_t<X> {
  using st = std::remove_cvref_t<X>;
  return st{detail::convert<typename st::value_type>(x.get() * s)};
}

/**
 * @brief Product of a scalar and a wrapped value (requires \c ability::scale).
 *
 * @tparam S Scalar type (not a strong type).
 * @tparam X Strong type.
 * @param s Scalar factor.
 * @param x Wrapped value.
 *
 * @return A wrapper holding \c s*x.
 *
 * @pre None.
 * @post None.
 */
template <typename S, strong_typedef_like X>
  requires(!strong_typedef_like<S>) && detail::has_op<X, ability::scale>
          && requires(S const& v, detail::value_t<X> u) { v * u; }
[[nodiscard]] constexpr auto operator*(S const& s, X const& x) noexcept(noexcept(s * x.get()))
  -> std::remove_cvref_t<X> {
  using st = std::remove_cvref_t<X>;
  return st{detail::convert<typename st::value_type>(s * x.get())};
}

/**
 * @brief Quotient of a wrapped value and a scalar (requires \c ability::scale).
 *
 * Like \c operator*, the result keeps the same strong type as \p x.
 *
 * @tparam X Strong type.
 * @tparam S Scalar type (not a strong type).
 * @param x Wrapped value.
 * @param s Scalar divisor.
 *
 * @return A wrapper holding \c x/s.
 *
 * @pre \p s is nonzero when \p T is an integral type (integer division by zero
 *      is undefined behaviour).
 * @post None.
 */
template <strong_typedef_like X, typename S>
  requires(!strong_typedef_like<S>) && detail::has_op<X, ability::scale>
          && requires(detail::value_t<X> u, S const& v) { u / v; }
[[nodiscard]] constexpr auto operator/(X const& x, S const& s) noexcept(noexcept(x.get() / s))
  -> std::remove_cvref_t<X> {
  using st = std::remove_cvref_t<X>;
  return st{detail::convert<typename st::value_type>(x.get() / s)};
}

/**
 * @brief Same-tag ratio: divides two values of the same unit (requires \c ability::ratio).
 *
 * Dividing two same-tag values yields a dimensionless scalar of their common
 * underlying type.
 *
 * @tparam A Left strong type.
 * @tparam B Right strong type.
 * @param a Dividend.
 * @param b Divisor.
 *
 * @return \c a.get()/b.get() as a bare value.
 *
 * @pre \p b is nonzero when the common underlying is an integral type (integer
 *      division by zero is undefined behaviour).
 * @post None.
 */
template <strong_typedef_like A, strong_typedef_like B>
  requires same_tag_as<A, B> && detail::has_op<A, ability::ratio>
           && detail::has_op<B, ability::ratio>
           && requires(detail::value_t<A> u, detail::value_t<B> v) { u / v; }
[[nodiscard]] constexpr auto operator/(A const& a, B const& b) noexcept(noexcept(a.get() / b.get()))
  -> detail::common_value_t<A, B> {
  using r = detail::common_value_t<A, B>;
  return detail::convert<r>(a.get()) / detail::convert<r>(b.get());
}

/**
 * @brief Bitwise-AND of two same-tag values (requires \c ability::bit_and on both).
 *
 * @tparam A Left strong type.
 * @tparam B Right strong type.
 * @param a Left operand.
 * @param b Right operand.
 *
 * @return Their bitwise AND as the common strong type.
 *
 * @pre None.
 * @post None.
 */
template <strong_typedef_like A, strong_typedef_like B>
  requires same_tag_as<A, B> && detail::has_op<A, ability::bit_and>
           && detail::has_op<B, ability::bit_and>
           && requires(detail::value_t<A> u, detail::value_t<B> v) { u & v; }
[[nodiscard]] constexpr auto operator&(A const& a, B const& b) noexcept(noexcept(a.get() & b.get()))
  -> detail::common_strong_t<A, B> {
  using ret = detail::common_strong_t<A, B>;
  using r = typename ret::value_type;
  return ret{detail::convert<r>(detail::convert<r>(a.get()) & detail::convert<r>(b.get()))};
}

/**
 * @brief Bitwise-OR of two same-tag values (requires \c ability::bit_or on both).
 *
 * @tparam A Left strong type.
 * @tparam B Right strong type.
 * @param a Left operand.
 * @param b Right operand.
 *
 * @return Their bitwise OR as the common strong type.
 *
 * @pre None.
 * @post None.
 */
template <strong_typedef_like A, strong_typedef_like B>
  requires same_tag_as<A, B> && detail::has_op<A, ability::bit_or>
           && detail::has_op<B, ability::bit_or>
           && requires(detail::value_t<A> u, detail::value_t<B> v) { u | v; }
[[nodiscard]] constexpr auto operator|(A const& a, B const& b) noexcept(noexcept(a.get() | b.get()))
  -> detail::common_strong_t<A, B> {
  using ret = detail::common_strong_t<A, B>;
  using r = typename ret::value_type;
  return ret{detail::convert<r>(detail::convert<r>(a.get()) | detail::convert<r>(b.get()))};
}

/**
 * @brief Bitwise-XOR of two same-tag values (requires \c ability::bit_xor on both).
 *
 * @tparam A Left strong type.
 * @tparam B Right strong type.
 * @param a Left operand.
 * @param b Right operand.
 *
 * @return Their bitwise XOR as the common strong type.
 *
 * @pre None.
 * @post None.
 */
template <strong_typedef_like A, strong_typedef_like B>
  requires same_tag_as<A, B> && detail::has_op<A, ability::bit_xor>
           && detail::has_op<B, ability::bit_xor>
           && requires(detail::value_t<A> u, detail::value_t<B> v) { u ^ v; }
[[nodiscard]] constexpr auto operator^(A const& a, B const& b) noexcept(noexcept(a.get() ^ b.get()))
  -> detail::common_strong_t<A, B> {
  using ret = detail::common_strong_t<A, B>;
  using r = typename ret::value_type;
  return ret{detail::convert<r>(detail::convert<r>(a.get()) ^ detail::convert<r>(b.get()))};
}

/**
 * @brief Bitwise-NOT (requires \c ability::bit_not).
 *
 * @tparam X Strong type.
 * @param x Operand.
 *
 * @return A wrapper holding the complement of \p x.
 *
 * @pre None.
 * @post None.
 */
template <strong_typedef_like X>
  requires detail::has_op<X, ability::bit_not> && requires(detail::value_t<X> v) { ~v; }
[[nodiscard]] constexpr auto operator~(X const& x) noexcept(noexcept(~x.get()))
  -> std::remove_cvref_t<X> {
  using s = std::remove_cvref_t<X>;
  return s{detail::convert<typename s::value_type>(~x.get())};
}

/**
 * @brief Left shift by a scalar (requires \c ability::shift, unsigned underlying).
 *
 * @tparam X Strong type.
 * @tparam S Integral shift-count type.
 * @param x Operand.
 * @param s Shift amount in the range [0, bit width of the underlying).
 *
 * @return A wrapper holding \p x shifted left by \p s.
 *
 * @pre \p s is non-negative and less than the bit width of the underlying; a
 *      negative or out-of-range count is undefined behaviour (checked by
 *      \c assert in debug builds).
 * @post None.
 */
template <strong_typedef_like X, typename S>
  requires(!strong_typedef_like<S>) && detail::has_op<X, ability::shift>
          && std::integral<std::remove_cvref_t<S>> && std::unsigned_integral<detail::value_t<X>>
[[nodiscard]] constexpr auto operator<<(X const& x, S s) noexcept(noexcept(x.get() << s))
  -> std::remove_cvref_t<X> {
  using st = std::remove_cvref_t<X>;
  using u = typename st::value_type;
  assert(
    std::cmp_greater_equal(s, 0) && std::cmp_less(s, std::numeric_limits<u>::digits)
    && "shift count out of range"
  );
  return st{detail::convert<u>(x.get() << s)};
}

/**
 * @brief Right shift by a scalar (requires \c ability::shift, unsigned underlying).
 *
 * @tparam X Strong type.
 * @tparam S Integral shift-count type.
 * @param x Operand.
 * @param s Shift amount in the range [0, bit width of the underlying).
 *
 * @return A wrapper holding \p x shifted right by \p s.
 *
 * @pre \p s is non-negative and less than the bit width of the underlying; a
 *      negative or out-of-range count is undefined behaviour (checked by
 *      \c assert in debug builds).
 * @post None.
 */
template <strong_typedef_like X, typename S>
  requires(!strong_typedef_like<S>) && detail::has_op<X, ability::shift>
          && std::integral<std::remove_cvref_t<S>> && std::unsigned_integral<detail::value_t<X>>
[[nodiscard]] constexpr auto operator>>(X const& x, S s) noexcept(noexcept(x.get() >> s))
  -> std::remove_cvref_t<X> {
  using st = std::remove_cvref_t<X>;
  using u = typename st::value_type;
  assert(
    std::cmp_greater_equal(s, 0) && std::cmp_less(s, std::numeric_limits<u>::digits)
    && "shift count out of range"
  );
  return st{detail::convert<u>(x.get() >> s)};
}

/**
 * @brief Equality of two same-tag values (requires \c ability::equality on both).
 *
 * @tparam A Left strong type.
 * @tparam B Right strong type.
 * @param a Left operand.
 * @param b Right operand.
 *
 * @return \c true when the underlying values compare equal.
 *
 * @pre None.
 * @post None.
 */
template <strong_typedef_like A, strong_typedef_like B>
  requires same_tag_as<A, B> && detail::has_op<A, ability::equality>
           && detail::has_op<B, ability::equality>
           && requires(detail::value_t<A> u, detail::value_t<B> v) { u == v; }
[[nodiscard]] constexpr auto
operator==(A const& a, B const& b) noexcept(noexcept(a.get() == b.get())) -> bool {
  using r = detail::common_value_t<A, B>;
  return detail::convert<r>(a.get()) == detail::convert<r>(b.get());
}

/**
 * @brief Three-way comparison of two same-tag values (requires \c ability::ordered on both).
 *
 * @tparam A Left strong type.
 * @tparam B Right strong type.
 * @param a Left operand.
 * @param b Right operand.
 *
 * @return The ordering of the underlying values.
 *
 * @pre None.
 * @post None.
 */
template <strong_typedef_like A, strong_typedef_like B>
  requires same_tag_as<A, B> && detail::has_op<A, ability::ordered>
           && detail::has_op<B, ability::ordered>
           && requires(detail::value_t<A> u, detail::value_t<B> v) { u <=> v; }
[[nodiscard]] constexpr auto
operator<=>(A const& a, B const& b) noexcept(noexcept(a.get() <=> b.get()))
  -> std::compare_three_way_result_t<detail::common_value_t<A, B>, detail::common_value_t<A, B>> {
  using r = detail::common_value_t<A, B>;
  return detail::convert<r>(a.get()) <=> detail::convert<r>(b.get());
}

/**
 * @brief Population count (requires \c ability::bitops, unsigned underlying).
 *
 * @tparam X Strong type with an unsigned underlying.
 * @param x Operand.
 *
 * @return The number of set bits in \p x.
 *
 * @pre None.
 * @post None.
 */
template <strong_typedef_like X>
  requires detail::has_op<X, ability::bitops> && std::unsigned_integral<detail::value_t<X>>
[[nodiscard]] constexpr auto popcount(X const& x) noexcept -> int {
  return std::popcount(x.get());
}

/**
 * @brief Rotate left (requires \c ability::bitops, unsigned underlying).
 *
 * @tparam X Strong type with an unsigned underlying.
 * @tparam S Integral rotate-count type.
 * @param x Operand.
 * @param s Rotate amount, normalised into the value width.
 *
 * @return A wrapper holding \p x rotated left by \p s.
 *
 * @pre None.
 * @post None.
 */
template <strong_typedef_like X, std::integral S>
  requires detail::has_op<X, ability::bitops> && std::unsigned_integral<detail::value_t<X>>
[[nodiscard]] constexpr auto rotl(X const& x, S s) noexcept -> std::remove_cvref_t<X> {
  using st = std::remove_cvref_t<X>;
  using u = typename st::value_type;
  return st{std::rotl(x.get(), static_cast<int>(detail::normalize_shift<u>(s)))};
}

/**
 * @brief Rotate right (requires \c ability::bitops, unsigned underlying).
 *
 * @tparam X Strong type with an unsigned underlying.
 * @tparam S Integral rotate-count type.
 * @param x Operand.
 * @param s Rotate amount, normalised into the value width.
 *
 * @return A wrapper holding \p x rotated right by \p s.
 *
 * @pre None.
 * @post None.
 */
template <strong_typedef_like X, std::integral S>
  requires detail::has_op<X, ability::bitops> && std::unsigned_integral<detail::value_t<X>>
[[nodiscard]] constexpr auto rotr(X const& x, S s) noexcept -> std::remove_cvref_t<X> {
  using st = std::remove_cvref_t<X>;
  using u = typename st::value_type;
  return st{std::rotr(x.get(), static_cast<int>(detail::normalize_shift<u>(s)))};
}

/**
 * @brief Count leading zero bits (requires \c ability::bitops, unsigned underlying).
 *
 * @tparam X Strong type with an unsigned underlying.
 * @param x Operand.
 *
 * @return The number of leading zero bits in \p x.
 *
 * @pre None.
 * @post None.
 */
template <strong_typedef_like X>
  requires detail::has_op<X, ability::bitops> && std::unsigned_integral<detail::value_t<X>>
[[nodiscard]] constexpr auto countl_zero(X const& x) noexcept -> int {
  return std::countl_zero(x.get());
}

/**
 * @brief Count leading one bits (requires \c ability::bitops, unsigned underlying).
 *
 * @tparam X Strong type with an unsigned underlying.
 * @param x Operand.
 *
 * @return The number of leading one bits in \p x.
 *
 * @pre None.
 * @post None.
 */
template <strong_typedef_like X>
  requires detail::has_op<X, ability::bitops> && std::unsigned_integral<detail::value_t<X>>
[[nodiscard]] constexpr auto countl_one(X const& x) noexcept -> int {
  return std::countl_one(x.get());
}

/**
 * @brief Count trailing zero bits (requires \c ability::bitops, unsigned underlying).
 *
 * @tparam X Strong type with an unsigned underlying.
 * @param x Operand.
 *
 * @return The number of trailing zero bits in \p x.
 *
 * @pre None.
 * @post None.
 */
template <strong_typedef_like X>
  requires detail::has_op<X, ability::bitops> && std::unsigned_integral<detail::value_t<X>>
[[nodiscard]] constexpr auto countr_zero(X const& x) noexcept -> int {
  return std::countr_zero(x.get());
}

/**
 * @brief Count trailing one bits (requires \c ability::bitops, unsigned underlying).
 *
 * @tparam X Strong type with an unsigned underlying.
 * @param x Operand.
 *
 * @return The number of trailing one bits in \p x.
 *
 * @pre None.
 * @post None.
 */
template <strong_typedef_like X>
  requires detail::has_op<X, ability::bitops> && std::unsigned_integral<detail::value_t<X>>
[[nodiscard]] constexpr auto countr_one(X const& x) noexcept -> int {
  return std::countr_one(x.get());
}

/**
 * @brief Smallest number of bits needed to represent the value (requires \c ability::bitops).
 *
 * @tparam X Strong type with an unsigned underlying.
 * @param x Operand.
 *
 * @return The bit width of \p x.
 *
 * @pre None.
 * @post None.
 */
template <strong_typedef_like X>
  requires detail::has_op<X, ability::bitops> && std::unsigned_integral<detail::value_t<X>>
[[nodiscard]] constexpr auto bit_width(X const& x) noexcept -> int {
  return std::bit_width(x.get());
}

/**
 * @brief Saturating add of two same-tag integral values (requires \c ability::saturating).
 *
 * @tparam A Left strong type.
 * @tparam B Right strong type.
 * @param a Left addend.
 * @param b Right addend.
 *
 * @return Their sum, clamped to the representable range, as the common strong type.
 *
 * @pre None.
 * @post None.
 */
template <strong_typedef_like A, strong_typedef_like B>
  requires same_tag_as<A, B> && detail::has_op<A, ability::saturating>
           && detail::has_op<B, ability::saturating> && std::integral<detail::common_value_t<A, B>>
[[nodiscard]] constexpr auto sat_add(A const& a, B const& b) noexcept(noexcept(a.get() + b.get()))
  -> detail::common_strong_t<A, B> {
  using ret = detail::common_strong_t<A, B>;
  using r = typename ret::value_type;
  return ret{detail::sat_add_impl<r>(detail::convert<r>(a.get()), detail::convert<r>(b.get()))};
}

/**
 * @brief Saturating subtract of two same-tag integral values (requires \c ability::saturating).
 *
 * @tparam A Left strong type.
 * @tparam B Right strong type.
 * @param a Minuend.
 * @param b Subtrahend.
 *
 * @return Their difference, clamped to the representable range, as the common
 *         strong type.
 *
 * @pre None.
 * @post None.
 */
template <strong_typedef_like A, strong_typedef_like B>
  requires same_tag_as<A, B> && detail::has_op<A, ability::saturating>
           && detail::has_op<B, ability::saturating> && std::integral<detail::common_value_t<A, B>>
[[nodiscard]] constexpr auto sat_sub(A const& a, B const& b) noexcept(noexcept(a.get() - b.get()))
  -> detail::common_strong_t<A, B> {
  using ret = detail::common_strong_t<A, B>;
  using r = typename ret::value_type;
  return ret{detail::sat_sub_impl<r>(detail::convert<r>(a.get()), detail::convert<r>(b.get()))};
}

/**
 * @brief A mutable reference to the underlying value.
 *
 * @tparam X Strong type.
 * @param x Wrapper to unwrap.
 *
 * @return Mutable reference to the wrapped value.
 *
 * @pre None.
 * @post None.
 */
template <strong_typedef_like X>
[[nodiscard]] constexpr auto to_underlying(X& x) noexcept ->
  typename std::remove_cvref_t<X>::value_type& {
  return x.get();
}

/**
 * @brief A const reference to the underlying value.
 *
 * @tparam X Strong type.
 * @param x Wrapper to unwrap.
 *
 * @return Const reference to the wrapped value.
 *
 * @pre None.
 * @post None.
 */
template <strong_typedef_like X>
[[nodiscard]] constexpr auto to_underlying(X const& x) noexcept ->
  typename std::remove_cvref_t<X>::value_type const& {
  return x.get();
}

}  // namespace nexenne::utility

/**
 * @brief \c std::hash specialisation forwarding to the underlying value's hash.
 *
 * Lets a \c strong_typedef key an unordered container whenever \p T is
 * hashable; only instantiated on use.
 *
 * @tparam Tag Tag type of the wrapper.
 * @tparam T Underlying value type.
 * @tparam Ops Capability set of the wrapper.
 */
template <typename Tag, typename T, nexenne::utility::ability Ops>
  requires requires(T const& t) { std::hash<T>{}(t); }
struct std::hash<nexenne::utility::strong_typedef<Tag, T, Ops>> {
  /**
   * @brief Hashes \p value by hashing its underlying value.
   *
   * @param value Wrapper to hash.
   *
   * @return The hash of \c value.get().
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto operator()(
    nexenne::utility::strong_typedef<Tag, T, Ops> const& value
  ) const noexcept(noexcept(std::hash<T>{}(value.get()))) -> std::size_t {
    return std::hash<T>{}(value.get());
  }
};

/**
 * @brief \c std::common_type specialisation for two same-tag wrappers.
 *
 * The result wraps the common underlying type and the union of both capability
 * sets.
 *
 * @tparam Tag Shared tag type.
 * @tparam T First underlying type.
 * @tparam U Second underlying type.
 * @tparam O1 First capability set.
 * @tparam O2 Second capability set.
 */
template <
  typename Tag,
  typename T,
  typename U,
  nexenne::utility::ability O1,
  nexenne::utility::ability O2>
struct std::common_type<
  nexenne::utility::strong_typedef<Tag, T, O1>,
  nexenne::utility::strong_typedef<Tag, U, O2>> {
  using type = nexenne::utility::strong_typedef<
    Tag,
    std::common_type_t<T, U>,
    nexenne::utility::sanitized<std::common_type_t<T, U>>(O1 | O2)>;
};

/**
 * @brief \c std::formatter specialisation inheriting the underlying type's formatter.
 *
 * Format specs such as "{:>8}" or "{:.2f}" pass straight through to \p T.
 *
 * @tparam Tag Tag type of the wrapper.
 * @tparam T Underlying value type.
 * @tparam Ops Capability set of the wrapper.
 * @tparam CharT Character type of the format context.
 */
template <typename Tag, typename T, nexenne::utility::ability Ops, typename CharT>
struct std::formatter<nexenne::utility::strong_typedef<Tag, T, Ops>, CharT>
    : std::formatter<T, CharT> {
  /**
   * @brief Formats \p value by formatting its underlying value.
   *
   * @tparam Context Formatting context type.
   * @param value Wrapper to format.
   * @param ctx Format context to write into.
   *
   * @return The output iterator past the formatted text.
   *
   * @pre None.
   * @post None.
   */
  template <typename Context>
  auto format(nexenne::utility::strong_typedef<Tag, T, Ops> const& value, Context& ctx) const {
    return std::formatter<T, CharT>::format(value.get(), ctx);
  }
};

/**
 * @brief \c std::formatter specialisation printing an \c ability by its name.
 *
 * Forwards to \c to_string, so \c std::format("{}", ability::scale) yields
 * \c "scale" and a string spec such as "{:>10}" pads the name.
 */
template <>
struct std::formatter<nexenne::utility::ability> : std::formatter<std::string_view> {
  /**
   * @brief Formats \p flag by writing its name.
   *
   * @tparam Context Formatting context type.
   * @param flag Flag, named group or \c none to format.
   * @param ctx Format context to write into.
   *
   * @return The output iterator past the formatted text.
   *
   * @pre None.
   * @post None.
   */
  template <typename Context>
  auto format(nexenne::utility::ability flag, Context& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::utility::to_string(flag), ctx);
  }
};
