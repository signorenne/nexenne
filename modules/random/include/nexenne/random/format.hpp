#pragma once

/**
 * @file
 * @brief Debug printing and formatting for the random engines and distributions.
 *
 * Three interlocking layers for every covered type:
 *   - \c to_string(x) producing a readable representation;
 *   - \c operator<<(std::ostream&, x) for stream output (delegates to to_string);
 *   - a \c std::formatter specialization so \c std::format("{}", x) works.
 *
 * Covered types: the two engines \c pcg32 and \c xoshiro256ss (name plus internal
 * state, so a reproducibility report can log an engine's exact position) and the
 * five distributions \c normal_distribution, \c exponential_distribution,
 * \c gamma_distribution, \c poisson_distribution, and \c discrete_distribution
 * (name plus parameters). The distribution formatters forward the format spec to
 * each numeric parameter, so \c std::format("{:.2f}", dist) applies to its values.
 *
 * The standard \c format header is heavy, so this header is opt-in: include it only where formatting
 * is needed, not from the hot leaf headers.
 */

#include <cstddef>
#include <format>
#include <ostream>
#include <string>
#include <string_view>

#include <nexenne/random/discrete.hpp>
#include <nexenne/random/exponential.hpp>
#include <nexenne/random/gamma.hpp>
#include <nexenne/random/normal.hpp>
#include <nexenne/random/pcg.hpp>
#include <nexenne/random/poisson.hpp>
#include <nexenne/random/xoshiro.hpp>

namespace nexenne::random {

/**
 * @brief Debug string of the form "pcg32(state=0x...)".
 *
 * The 64-bit state is printed in zero-padded hexadecimal; together with the
 * fixed stream it locates the engine's position for a replay log.
 *
 * @param engine Engine to print.
 *
 * @return The debug string.
 *
 * @pre None.
 * @post Names the engine and its state word.
 */
[[nodiscard]] inline auto to_string(pcg32 const& engine) -> std::string {
  return std::format("pcg32(state={:#018x})", engine.state());
}

/**
 * @brief Debug string of the form "xoshiro256ss(state=[0x.., 0x.., 0x.., 0x..])".
 *
 * The four 64-bit lanes are printed in zero-padded hexadecimal and fully
 * determine all future output.
 *
 * @param engine Engine to print.
 *
 * @return The debug string.
 *
 * @pre None.
 * @post Names the engine and lists its four state lanes.
 */
[[nodiscard]] inline auto to_string(xoshiro256ss const& engine) -> std::string {
  auto const s{engine.state()};
  return std::format(
    "xoshiro256ss(state=[{:#018x}, {:#018x}, {:#018x}, {:#018x}])", s[0], s[1], s[2], s[3]
  );
}

/**
 * @brief Debug string of the form "normal_distribution(mean=.., stddev=..)".
 *
 * @tparam T Sample type.
 * @param dist Distribution to print.
 *
 * @return The debug string.
 *
 * @pre None.
 * @post Names the distribution and lists its mean and standard deviation.
 */
template <std::floating_point T>
[[nodiscard]] auto to_string(normal_distribution<T> const& dist) -> std::string {
  return std::format("normal_distribution(mean={}, stddev={})", dist.mean(), dist.stddev());
}

/**
 * @brief Debug string of the form "exponential_distribution(rate=..)".
 *
 * @tparam T Sample type.
 * @param dist Distribution to print.
 *
 * @return The debug string.
 *
 * @pre None.
 * @post Names the distribution and lists its rate parameter.
 */
template <std::floating_point T>
[[nodiscard]] auto to_string(exponential_distribution<T> const& dist) -> std::string {
  return std::format("exponential_distribution(rate={})", dist.rate());
}

/**
 * @brief Debug string of the form "gamma_distribution(shape=.., scale=..)".
 *
 * @tparam T Sample type.
 * @param dist Distribution to print.
 *
 * @return The debug string.
 *
 * @pre None.
 * @post Names the distribution and lists its shape and scale parameters.
 */
template <std::floating_point T>
[[nodiscard]] auto to_string(gamma_distribution<T> const& dist) -> std::string {
  return std::format("gamma_distribution(shape={}, scale={})", dist.shape(), dist.scale());
}

/**
 * @brief Debug string of the form "poisson_distribution(lambda=..)".
 *
 * @tparam T Result integer type.
 * @param dist Distribution to print.
 *
 * @return The debug string.
 *
 * @pre None.
 * @post Names the distribution and lists its mean parameter.
 */
template <std::integral T>
[[nodiscard]] auto to_string(poisson_distribution<T> const& dist) -> std::string {
  return std::format("poisson_distribution(lambda={})", dist.mean());
}

/**
 * @brief Debug string of the form "discrete_distribution([p0, p1, ...])".
 *
 * Lists the normalized probability of every outcome, in outcome order.
 *
 * @tparam T Weight type.
 * @param dist Distribution to print.
 *
 * @return The debug string.
 *
 * @pre None.
 * @post Lists the per-outcome probabilities.
 */
template <std::floating_point T>
[[nodiscard]] auto to_string(discrete_distribution<T> const& dist) -> std::string {
  using size_type = typename discrete_distribution<T>::size_type;
  auto result{std::string{"discrete_distribution(["}};
  for (size_type i{0}; i < dist.size(); ++i) {
    if (i != 0) {
      result += ", ";
    }
    result += std::format("{}", dist.probability(i));
  }
  result += "])";
  return result;
}

/**
 * @brief Streams a \c pcg32 via its debug string.
 *
 * @param os Output stream.
 * @param engine Engine to print.
 *
 * @return Reference to \p os, so calls chain.
 *
 * @pre \p os is in a good state.
 * @post The textual engine has been appended to \p os.
 */
inline auto operator<<(std::ostream& os, pcg32 const& engine) -> std::ostream& {
  return os << to_string(engine);
}

/**
 * @brief Streams a \c xoshiro256ss via its debug string.
 *
 * @param os Output stream.
 * @param engine Engine to print.
 *
 * @return Reference to \p os, so calls chain.
 *
 * @pre \p os is in a good state.
 * @post The textual engine has been appended to \p os.
 */
inline auto operator<<(std::ostream& os, xoshiro256ss const& engine) -> std::ostream& {
  return os << to_string(engine);
}

/**
 * @brief Streams a \c normal_distribution via its debug string.
 *
 * @tparam T Sample type.
 * @param os Output stream.
 * @param dist Distribution to print.
 *
 * @return Reference to \p os, so calls chain.
 *
 * @pre \p os is in a good state.
 * @post The textual distribution has been appended to \p os.
 */
template <std::floating_point T>
auto operator<<(std::ostream& os, normal_distribution<T> const& dist) -> std::ostream& {
  return os << to_string(dist);
}

/**
 * @brief Streams an \c exponential_distribution via its debug string.
 *
 * @tparam T Sample type.
 * @param os Output stream.
 * @param dist Distribution to print.
 *
 * @return Reference to \p os, so calls chain.
 *
 * @pre \p os is in a good state.
 * @post The textual distribution has been appended to \p os.
 */
template <std::floating_point T>
auto operator<<(std::ostream& os, exponential_distribution<T> const& dist) -> std::ostream& {
  return os << to_string(dist);
}

/**
 * @brief Streams a \c gamma_distribution via its debug string.
 *
 * @tparam T Sample type.
 * @param os Output stream.
 * @param dist Distribution to print.
 *
 * @return Reference to \p os, so calls chain.
 *
 * @pre \p os is in a good state.
 * @post The textual distribution has been appended to \p os.
 */
template <std::floating_point T>
auto operator<<(std::ostream& os, gamma_distribution<T> const& dist) -> std::ostream& {
  return os << to_string(dist);
}

/**
 * @brief Streams a \c poisson_distribution via its debug string.
 *
 * @tparam T Result integer type.
 * @param os Output stream.
 * @param dist Distribution to print.
 *
 * @return Reference to \p os, so calls chain.
 *
 * @pre \p os is in a good state.
 * @post The textual distribution has been appended to \p os.
 */
template <std::integral T>
auto operator<<(std::ostream& os, poisson_distribution<T> const& dist) -> std::ostream& {
  return os << to_string(dist);
}

/**
 * @brief Streams a \c discrete_distribution via its debug string.
 *
 * @tparam T Weight type.
 * @param os Output stream.
 * @param dist Distribution to print.
 *
 * @return Reference to \p os, so calls chain.
 *
 * @pre \p os is in a good state.
 * @post The textual distribution has been appended to \p os.
 */
template <std::floating_point T>
auto operator<<(std::ostream& os, discrete_distribution<T> const& dist) -> std::ostream& {
  return os << to_string(dist);
}

}  // namespace nexenne::random

/**
 * @brief \c std::format support for \c pcg32: prints its \c to_string state.
 *
 * Inherits the string formatter so a spec (width, alignment) applies to the
 * whole rendering.
 */
template <>
struct std::formatter<nexenne::random::pcg32> : std::formatter<std::string_view> {
  /**
   * @brief Formats the engine's \c to_string rendering through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param engine Engine to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The textual engine has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::random::pcg32 const& engine, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::random::to_string(engine), ctx);
  }
};

/**
 * @brief \c std::format support for \c xoshiro256ss: prints its \c to_string state.
 *
 * Inherits the string formatter so a spec (width, alignment) applies to the
 * whole rendering.
 */
template <>
struct std::formatter<nexenne::random::xoshiro256ss> : std::formatter<std::string_view> {
  /**
   * @brief Formats the engine's \c to_string rendering through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param engine Engine to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The textual engine has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::random::xoshiro256ss const& engine, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::random::to_string(engine), ctx);
  }
};

/**
 * @brief \c std::format support for \c normal_distribution: the spec applies to each parameter.
 *
 * @tparam T Sample type.
 */
template <std::floating_point T>
struct std::formatter<nexenne::random::normal_distribution<T>> {
  std::formatter<T> component;  ///< Parses and applies the per-parameter spec.

  /**
   * @brief Forwards the spec to the parameter formatter.
   *
   * @param ctx Format parse context.
   *
   * @return Iterator past the parsed spec.
   *
   * @pre None.
   * @post \c component holds the parsed spec.
   */
  constexpr auto parse(std::format_parse_context& ctx) {
    return component.parse(ctx);
  }

  /**
   * @brief Writes \c "normal_distribution(mean=.., stddev=..)" with the parsed spec.
   *
   * @tparam FormatContext Deduced output context type.
   * @param dist Distribution to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted distribution has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::random::normal_distribution<T> const& dist, FormatContext& ctx) const {
    auto out{std::format_to(ctx.out(), "normal_distribution(mean=")};
    ctx.advance_to(out);
    out = component.format(dist.mean(), ctx);
    out = std::format_to(out, ", stddev=");
    ctx.advance_to(out);
    out = component.format(dist.stddev(), ctx);
    *out++ = ')';
    return out;
  }
};

/**
 * @brief \c std::format support for \c exponential_distribution: the spec applies to the rate.
 *
 * @tparam T Sample type.
 */
template <std::floating_point T>
struct std::formatter<nexenne::random::exponential_distribution<T>> {
  std::formatter<T> component;  ///< Parses and applies the parameter spec.

  /**
   * @brief Forwards the spec to the parameter formatter.
   *
   * @param ctx Format parse context.
   *
   * @return Iterator past the parsed spec.
   *
   * @pre None.
   * @post \c component holds the parsed spec.
   */
  constexpr auto parse(std::format_parse_context& ctx) {
    return component.parse(ctx);
  }

  /**
   * @brief Writes \c "exponential_distribution(rate=..)" with the parsed spec.
   *
   * @tparam FormatContext Deduced output context type.
   * @param dist Distribution to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted distribution has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::random::exponential_distribution<T> const& dist, FormatContext& ctx) const {
    auto out{std::format_to(ctx.out(), "exponential_distribution(rate=")};
    ctx.advance_to(out);
    out = component.format(dist.rate(), ctx);
    *out++ = ')';
    return out;
  }
};

/**
 * @brief \c std::format support for \c gamma_distribution: the spec applies to each parameter.
 *
 * @tparam T Sample type.
 */
template <std::floating_point T>
struct std::formatter<nexenne::random::gamma_distribution<T>> {
  std::formatter<T> component;  ///< Parses and applies the per-parameter spec.

  /**
   * @brief Forwards the spec to the parameter formatter.
   *
   * @param ctx Format parse context.
   *
   * @return Iterator past the parsed spec.
   *
   * @pre None.
   * @post \c component holds the parsed spec.
   */
  constexpr auto parse(std::format_parse_context& ctx) {
    return component.parse(ctx);
  }

  /**
   * @brief Writes \c "gamma_distribution(shape=.., scale=..)" with the parsed spec.
   *
   * @tparam FormatContext Deduced output context type.
   * @param dist Distribution to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted distribution has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::random::gamma_distribution<T> const& dist, FormatContext& ctx) const {
    auto out{std::format_to(ctx.out(), "gamma_distribution(shape=")};
    ctx.advance_to(out);
    out = component.format(dist.shape(), ctx);
    out = std::format_to(out, ", scale=");
    ctx.advance_to(out);
    out = component.format(dist.scale(), ctx);
    *out++ = ')';
    return out;
  }
};

/**
 * @brief \c std::format support for \c poisson_distribution: the spec applies to lambda.
 *
 * The mean is a \c double regardless of the result integer type, so the spec is
 * forwarded to a \c double formatter.
 *
 * @tparam T Result integer type.
 */
template <std::integral T>
struct std::formatter<nexenne::random::poisson_distribution<T>> {
  std::formatter<double> component;  ///< Parses and applies the parameter spec.

  /**
   * @brief Forwards the spec to the parameter formatter.
   *
   * @param ctx Format parse context.
   *
   * @return Iterator past the parsed spec.
   *
   * @pre None.
   * @post \c component holds the parsed spec.
   */
  constexpr auto parse(std::format_parse_context& ctx) {
    return component.parse(ctx);
  }

  /**
   * @brief Writes \c "poisson_distribution(lambda=..)" with the parsed spec.
   *
   * @tparam FormatContext Deduced output context type.
   * @param dist Distribution to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted distribution has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::random::poisson_distribution<T> const& dist, FormatContext& ctx) const {
    auto out{std::format_to(ctx.out(), "poisson_distribution(lambda=")};
    ctx.advance_to(out);
    out = component.format(dist.mean(), ctx);
    *out++ = ')';
    return out;
  }
};

/**
 * @brief \c std::format support for \c discrete_distribution: the spec applies to each probability.
 *
 * Writes \c "discrete_distribution([p0, p1, ...])" with every probability under
 * the parsed spec.
 *
 * @tparam T Weight type.
 */
template <std::floating_point T>
struct std::formatter<nexenne::random::discrete_distribution<T>> {
  std::formatter<T> component;  ///< Parses and applies the per-probability spec.

  /**
   * @brief Forwards the spec to the probability formatter.
   *
   * @param ctx Format parse context.
   *
   * @return Iterator past the parsed spec.
   *
   * @pre None.
   * @post \c component holds the parsed spec.
   */
  constexpr auto parse(std::format_parse_context& ctx) {
    return component.parse(ctx);
  }

  /**
   * @brief Writes the per-outcome probabilities, each under the parsed spec.
   *
   * @tparam FormatContext Deduced output context type.
   * @param dist Distribution to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted distribution has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::random::discrete_distribution<T> const& dist, FormatContext& ctx) const {
    using size_type = typename nexenne::random::discrete_distribution<T>::size_type;
    auto out{std::format_to(ctx.out(), "discrete_distribution([")};
    for (size_type i{0}; i < dist.size(); ++i) {
      if (i != 0) {
        out = std::format_to(out, ", ");
      }
      ctx.advance_to(out);
      out = component.format(dist.probability(i), ctx);
    }
    out = std::format_to(out, "])");
    return out;
  }
};
