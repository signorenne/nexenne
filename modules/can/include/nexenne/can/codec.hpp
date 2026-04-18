#pragma once

/**
 * @file
 * @brief Encode and decode signal values into and out of a frame's payload.
 *
 * This is the layer that joins a \c signal.hpp description, its compiled
 * \c packing_plan.hpp, and a \c frame.hpp. The \c read_bits and
 * \c write_bits functions move the raw integer bit field; \c decode and \c encode
 * add the two's-complement sign handling and the linear scaling that turns a raw
 * integer into a physical value and back. Every function bounds-checks the field
 * against the frame's data length, so a signal that runs past a short frame is
 * reported, not read out of bounds.
 *
 * Splitting the work this way keeps each type single-purpose: the plan knows the
 * bits, the signal knows the scaling, and these functions apply them to a frame.
 */

#include <bit>
#include <cassert>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <optional>
#include <type_traits>

#include <nexenne/can/error.hpp>
#include <nexenne/can/frame.hpp>
#include <nexenne/can/packing_plan.hpp>
#include <nexenne/can/signal.hpp>

namespace nexenne::can {

namespace detail {

/**
 * @brief Builds a mask whose low \p bits bits are one and the rest zero.
 *
 * Handles the full-width and zero cases explicitly, so it is safe to ask for
 * every bit or none without the undefined shift a naive \c (1 << bits) - 1 would
 * invoke at the type width.
 *
 * @tparam T Unsigned integer type to build the mask in.
 * @param bits Number of low bits to set, from 0 to the bit width of \p T.
 *
 * @return A value whose low \p bits bits are one and the rest zero.
 *
 * @pre \p bits is at most the bit width of \p T.
 * @post \c std::popcount(result) equals \p bits.
 */
template <std::unsigned_integral T>
[[nodiscard]] constexpr auto low_mask(std::size_t const bits) noexcept -> T {
  assert(bits <= sizeof(T) * 8 && "low_mask: bits exceed type width");
  if (bits == 0) {
    return T{0};
  }
  if (bits >= sizeof(T) * 8) {
    return static_cast<T>(~T{0});
  }
  return static_cast<T>((T{1} << bits) - 1);
}

/**
 * @brief Sign-extends the low \p bits bits of \p value to the full signed width.
 *
 * Interprets the low \p bits bits of \p value as a two's-complement number and
 * extends its sign through the high bits, using the branchless trick of flipping
 * the sign bit then subtracting it: if the sign bit (bit \c bits-1) is set the
 * subtraction carries it up, otherwise the value is unchanged.
 *
 * @tparam T Unsigned integer type of the input.
 * @param value Raw bits, already masked to the field width.
 * @param bits Field width, from 1 to the bit width of \p T.
 *
 * @return \p value read as a \p bits-wide two's-complement number, as the signed
 *         counterpart of \p T.
 *
 * @pre \p bits is between 1 and the bit width of \p T; a larger or zero width
 *      asserts in debug.
 * @post None.
 */
template <std::unsigned_integral T>
[[nodiscard]] constexpr auto
sign_extend(T const value, std::size_t const bits) noexcept -> std::make_signed_t<T> {
  assert(bits >= 1 && bits <= sizeof(T) * 8 && "sign_extend: bits out of range");
  using signed_type = std::make_signed_t<T>;
  if (bits >= sizeof(T) * 8) {
    return static_cast<signed_type>(value);
  }
  auto const sign_bit{static_cast<T>(T{1} << (bits - 1))};
  return static_cast<signed_type>(static_cast<T>(value ^ sign_bit) - sign_bit);
}

/**
 * @brief Reinterprets a field's raw bits as an IEEE-754 floating-point value.
 *
 * A \c bits of 64 reads the raw bits as a \c double; any other width (32 in
 * practice) reads the low bits as a \c float and widens to \c double.
 *
 * @param raw Raw field bits, right-aligned.
 * @param bits Field width; 32 or 64 for a float signal.
 *
 * @return The value the raw bits encode, as a \c double.
 *
 * @pre \p bits is 32 or 64.
 * @post None.
 */
[[nodiscard]] constexpr auto float_from_raw(std::uint64_t const raw, std::uint8_t const bits
) noexcept -> double {
  if (bits == 64U) {
    return std::bit_cast<double>(raw);
  }
  return static_cast<double>(std::bit_cast<float>(static_cast<std::uint32_t>(raw)));
}

/**
 * @brief Encodes a floating-point value into a field's raw IEEE-754 bits.
 *
 * @param value Value to encode.
 * @param bits Field width; 32 or 64 for a float signal.
 *
 * @return The raw bits, right-aligned, ready for \c packing_plan::insert.
 *
 * @pre \p bits is 32 or 64.
 * @post Bits above \p bits of the result are zero.
 */
[[nodiscard]] constexpr auto raw_from_float(double const value, std::uint8_t const bits
) noexcept -> std::uint64_t {
  if (bits == 64U) {
    return std::bit_cast<std::uint64_t>(value);
  }
  return std::bit_cast<std::uint32_t>(static_cast<float>(value));
}

/**
 * @brief Reports whether a raw value is the "not available" pattern for a policy.
 *
 * @param policy Which extreme raw value counts as not-available.
 * @param raw Raw field value, masked to the field width.
 * @param bits Field width, 1 through 64.
 *
 * @return \c true when \p raw is the all-ones value, the all-zeros value, or
 *         either, as the policy selects.
 *
 * @pre \p bits is between 1 and 64.
 * @post None.
 */
[[nodiscard]] constexpr auto is_dont_care(
  invalid_value const policy, std::uint64_t const raw, std::uint8_t const bits
) noexcept -> bool {
  auto const ones{detail::low_mask<std::uint64_t>(bits)};
  switch (policy) {
    case invalid_value::none:
      return false;
    case invalid_value::all_ones:
      return raw == ones;
    case invalid_value::all_zeros:
      return raw == 0U;
    case invalid_value::all_ones_or_zeros:
      return raw == ones || raw == 0U;
  }
  return false;
}

}  // namespace detail

/**
 * @brief Reads the raw bit field of a plan from a frame.
 *
 * @param plan Compiled bit layout of the field.
 * @param f Frame to read from.
 *
 * @return The raw value on success, or \c can_error::signal_out_of_range when
 *         the field runs past the frame's data length.
 *
 * @pre None.
 * @post On success the result has bits above the field width zero.
 */
[[nodiscard]] constexpr auto
read_bits(packing_plan const& plan, frame const& f) noexcept -> result<std::uint64_t> {
  if (plan.required_length() > f.length()) {
    return std::unexpected{can_error::signal_out_of_range};
  }
  return plan.extract(f.data());
}

/**
 * @brief Writes a raw bit field into a frame.
 *
 * @param plan Compiled bit layout of the field.
 * @param f Frame to write into.
 * @param raw Raw value whose low \c plan.bit_length() bits are stored.
 *
 * @return Empty on success, or \c can_error::signal_out_of_range when the field
 *         runs past the frame's data length.
 *
 * @pre None.
 * @post On success the field bits of \p f hold the low bits of \p raw and the
 *       rest of the payload is unchanged.
 */
[[nodiscard]] constexpr auto
write_bits(packing_plan const& plan, frame& f, std::uint64_t const raw) noexcept -> result<void> {
  if (plan.required_length() > f.length()) {
    return std::unexpected{can_error::signal_out_of_range};
  }
  plan.insert(f.data(), raw & detail::low_mask<std::uint64_t>(plan.bit_length()));
  return {};
}

/**
 * @brief Decodes a signal's physical value from a frame.
 *
 * Reads the raw field, applies two's-complement sign extension when the signal
 * is signed, then the linear scaling \c physical = \c raw * \c scale + \c offset.
 *
 * @param sig Signal supplying the scaling.
 * @param plan Compiled bit layout of \p sig.
 * @param f Frame to read from.
 *
 * @return The physical value on success, or \c can_error::signal_out_of_range
 *         when the field runs past the frame's data length.
 *
 * @pre \p plan was compiled from \p sig.
 * @post None.
 */
[[nodiscard]] constexpr auto
decode(signal const& sig, packing_plan const& plan, frame const& f) noexcept -> result<double> {
  auto const raw{read_bits(plan, f)};
  if (!raw) {
    return std::unexpected{raw.error()};
  }
  double numeric{0.0};
  if (sig.is_float()) {
    numeric = detail::float_from_raw(*raw, plan.bit_length());
  } else if (plan.is_signed()) {
    numeric = static_cast<double>(detail::sign_extend(*raw, plan.bit_length()));
  } else {
    numeric = static_cast<double>(*raw);
  }
  return numeric * sig.scale() + sig.offset();
}

/**
 * @brief Decodes a signal, returning no value when its raw field is not-available.
 *
 * Like \c decode, but honours the signal's \c invalid_value policy: when the raw
 * field is the all-ones or all-zeros "not available" pattern the signal selects,
 * the result holds \c std::nullopt instead of a meaningless physical value.
 *
 * @param sig Signal supplying the scaling and invalid-value policy.
 * @param plan Compiled bit layout of \p sig.
 * @param f Frame to read from.
 *
 * @return The physical value, \c std::nullopt when the field is not-available, or
 *         \c can_error::signal_out_of_range when the field runs past the frame.
 *
 * @pre \p plan was compiled from \p sig.
 * @post None.
 */
[[nodiscard]] constexpr auto decode_value(
  signal const& sig, packing_plan const& plan, frame const& f
) noexcept -> result<std::optional<double>> {
  auto const raw{read_bits(plan, f)};
  if (!raw) {
    return std::unexpected{raw.error()};
  }
  if (detail::is_dont_care(sig.invalid(), *raw, plan.bit_length())) {
    return std::optional<double>{};
  }
  double numeric{0.0};
  if (sig.is_float()) {
    numeric = detail::float_from_raw(*raw, plan.bit_length());
  } else if (plan.is_signed()) {
    numeric = static_cast<double>(detail::sign_extend(*raw, plan.bit_length()));
  } else {
    numeric = static_cast<double>(*raw);
  }
  return std::optional<double>{numeric * sig.scale() + sig.offset()};
}

/**
 * @brief Encodes a physical value into a frame through a signal.
 *
 * Clamps the value to the signal's range, inverts the scaling to a raw integer,
 * checks the integer fits the field width and signedness, and writes it.
 *
 * @param sig Signal supplying the scaling and clamp range.
 * @param plan Compiled bit layout of \p sig.
 * @param f Frame to write into.
 * @param value Physical value to encode.
 *
 * @return Empty on success, \c can_error::signal_out_of_range when the field
 *         runs past the frame's data length, or \c can_error::value_out_of_range
 *         when the rounded raw value does not fit the field.
 *
 * @pre \p plan was compiled from \p sig. A zero \c sig.scale() is not a
 *      precondition: it makes the scaled value non-finite, which is reported as
 *      \c can_error::value_out_of_range rather than asserted.
 * @post On success the field bits of \p f hold the encoded value.
 */
[[nodiscard]] inline auto encode(
  signal const& sig, packing_plan const& plan, frame& f, double const value
) noexcept -> result<void> {
  double clamped{value};
  if (clamped < sig.minimum()) {
    clamped = sig.minimum();
  }
  if (clamped > sig.maximum()) {
    clamped = sig.maximum();
  }
  auto const scaled{(clamped - sig.offset()) / sig.scale()};
  if (!std::isfinite(scaled)) {
    return std::unexpected{can_error::value_out_of_range};
  }
  // A float signal stores the IEEE-754 bit pattern directly, with no rounding or
  // field-range check (every finite value fits its 32 or 64 bits).
  if (sig.is_float()) {
    return write_bits(plan, f, detail::raw_from_float(scaled, plan.bit_length()));
  }
  // Round in the double domain and bound the result before any integer cast:
  // std::round never overflows (it returns a double), unlike std::llround, whose
  // result is unspecified once the value leaves [LLONG_MIN, LLONG_MAX]. The
  // exclusive upper bounds are exact powers of two, so an in-range rounded value
  // always casts without undefined behaviour.
  auto const rounded{std::round(scaled)};
  auto const bits{plan.bit_length()};
  if (plan.is_signed()) {
    double const min_value{
      bits >= 64U ? -9223372036854775808.0
                  : static_cast<double>(-(std::int64_t{1} << (bits - 1U)))
    };
    double const max_exclusive{
      bits >= 64U ? 9223372036854775808.0
                  : static_cast<double>(std::int64_t{1} << (bits - 1U))
    };
    if (rounded < min_value || rounded >= max_exclusive) {
      return std::unexpected{can_error::value_out_of_range};
    }
    return write_bits(plan, f, static_cast<std::uint64_t>(static_cast<std::int64_t>(rounded)));
  }
  double const max_exclusive{
    bits >= 64U ? 18446744073709551616.0
                : static_cast<double>(detail::low_mask<std::uint64_t>(bits)) + 1.0
  };
  if (rounded < 0.0 || rounded >= max_exclusive) {
    return std::unexpected{can_error::value_out_of_range};
  }
  return write_bits(plan, f, static_cast<std::uint64_t>(rounded));
}

/**
 * @brief Encodes a physical value, rejecting one outside the signal's range.
 *
 * Like \c encode, but instead of clamping a value below \c sig.minimum() or above
 * \c sig.maximum() it returns \c can_error::value_out_of_range. This matches
 * cantools' default strict behaviour, where \c encode clamps only when asked. A
 * signal with the default unbounded range never rejects on range.
 *
 * @param sig Signal supplying the scaling and range.
 * @param plan Compiled bit layout of \p sig.
 * @param f Frame to write into.
 * @param value Physical value to encode.
 *
 * @return Empty on success, \c can_error::value_out_of_range when \p value is
 *         outside the signal's range or does not fit the field, or
 *         \c can_error::signal_out_of_range when the field runs past the frame.
 *
 * @pre \p plan was compiled from \p sig.
 * @post On success the field bits of \p f hold the encoded value.
 */
[[nodiscard]] inline auto encode_strict(
  signal const& sig, packing_plan const& plan, frame& f, double const value
) noexcept -> result<void> {
  if (value < sig.minimum() || value > sig.maximum()) {
    return std::unexpected{can_error::value_out_of_range};
  }
  return encode(sig, plan, f, value);
}

/**
 * @brief The raw "not available" value for a field: all of its bits set.
 *
 * J1939 and many DBC databases reserve the all-ones raw value of a signal to
 * mean "not available" (parameter not supported or not yet measured).
 *
 * @param plan Compiled bit layout of the field.
 *
 * @return The raw value with the field's low \c plan.bit_length() bits all one.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] constexpr auto not_available_value(packing_plan const& plan) noexcept -> std::uint64_t {
  return detail::low_mask<std::uint64_t>(plan.bit_length());
}

/**
 * @brief Writes the "not available" sentinel (all-ones) into a field.
 *
 * @param plan Compiled bit layout of the field.
 * @param f Frame to write into.
 *
 * @return Empty on success, or \c can_error::signal_out_of_range when the field
 *         runs past the frame's data length.
 *
 * @pre None.
 * @post On success the field holds its all-ones "not available" value.
 */
[[nodiscard]] constexpr auto
write_not_available(packing_plan const& plan, frame& f) noexcept -> result<void> {
  return write_bits(plan, f, not_available_value(plan));
}

/**
 * @brief Reports whether a field holds the "not available" sentinel.
 *
 * @param plan Compiled bit layout of the field.
 * @param f Frame to read from.
 *
 * @return \c true when the field is all-ones, \c false otherwise, or
 *         \c can_error::signal_out_of_range when the field runs past the frame's
 *         data length.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] constexpr auto
is_not_available(packing_plan const& plan, frame const& f) noexcept -> result<bool> {
  auto const raw{read_bits(plan, f)};
  if (!raw) {
    return std::unexpected{raw.error()};
  }
  return *raw == not_available_value(plan);
}

}  // namespace nexenne::can
