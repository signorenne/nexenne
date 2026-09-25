#pragma once

/**
 * @file
 * @brief The formatting layers for the benchmark runner's types.
 *
 * \c std::formatter specializations for \c result, \c comparison and
 * \c config, plus the free \c to_string and \c operator<< for \c config.
 * \c result and \c comparison carry their \c to_string as a member and
 * stream through a friend \c operator<<, which ends the text with a newline
 * so a result prints as a line. Every formatter accepts an empty spec only.
 * Include this header where the runner's types meet \c std::format; the
 * runner header does not pull it in.
 */

#include <format>
#include <ostream>
#include <string>

#include <nexenne/benchmark/benchmark.hpp>
#include <nexenne/chrono/duration_parts.hpp>

namespace nexenne::benchmark {

/**
 * @brief Renders a \c config's four knobs on one line.
 *
 * Output looks like \c "{target: 100.00 ms, sample_count: 10, min_iterations: 1,
 * warmup: on}"; the target duration is auto-scaled through
 * \c chrono::format_scaled.
 *
 * @param cfg Config to render.
 *
 * @return A freshly allocated string.
 *
 * @pre None.
 * @post None.
 *
 * @throws std::bad_alloc if the string cannot be allocated.
 */
[[nodiscard]] inline auto to_string(config const& cfg) -> std::string {
  return std::format(
    "{{target: {}, sample_count: {}, min_iterations: {}, warmup: {}}}",
    chrono::format_scaled(cfg.target_duration),
    cfg.sample_count,
    cfg.min_iterations,
    cfg.warmup ? "on" : "off"
  );
}

/**
 * @brief Streams a \c config via its \c to_string.
 *
 * @param os Output stream.
 * @param cfg Config to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The knobs of \p cfg have been written to \p os.
 */
inline auto operator<<(std::ostream& os, config const& cfg) -> std::ostream& {
  return os << to_string(cfg);
}

}  // namespace nexenne::benchmark

/**
 * @brief \c std::format support for \c nexenne::benchmark::result.
 *
 * Makes \c std::format("{}", r) emit the single-line summary produced by
 * \c result::to_string. Accepts an empty format spec only.
 */
template <>
struct std::formatter<nexenne::benchmark::result> {
  /**
   * @brief Parses the format spec, which must be empty.
   *
   * @param ctx Format parse context positioned at the spec.
   *
   * @return Iterator to the closing brace of the spec.
   *
   * @pre The format spec for this type is empty.
   * @post The parse context is unchanged.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    auto const it{ctx.begin()};
    if (it != ctx.end() && *it != '}') {
      throw std::format_error{"nexenne::benchmark formatter accepts no format spec"};
    }
    return it;
  }

  /**
   * @brief Writes \p r as its single-line summary into the output.
   *
   * @param r Result to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the written summary.
   *
   * @pre None.
   * @post The summary of \p r has been written through \p ctx; \p r is
   *       unchanged.
   *
   * @throws std::bad_alloc if building the summary string fails.
   */
  static auto format(nexenne::benchmark::result const& r, auto& ctx) {
    return std::format_to(ctx.out(), "{}", r.to_string());
  }
};

/**
 * @brief \c std::format support for \c nexenne::benchmark::comparison.
 *
 * Makes \c std::format("{}", c) emit the multi-line text produced by
 * \c comparison::to_string. Accepts an empty format spec only.
 */
template <>
struct std::formatter<nexenne::benchmark::comparison> {
  /**
   * @brief Parses the format spec, which must be empty.
   *
   * @param ctx Format parse context positioned at the spec.
   *
   * @return Iterator to the closing brace of the spec.
   *
   * @pre The format spec for this type is empty.
   * @post The parse context is unchanged.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    auto const it{ctx.begin()};
    if (it != ctx.end() && *it != '}') {
      throw std::format_error{"nexenne::benchmark formatter accepts no format spec"};
    }
    return it;
  }

  /**
   * @brief Writes \p c as its multi-line comparison text into the output.
   *
   * @param c Comparison to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the written text.
   *
   * @pre None.
   * @post The text of \p c has been written through \p ctx; \p c is unchanged.
   *
   * @throws std::bad_alloc if building the comparison string fails.
   */
  static auto format(nexenne::benchmark::comparison const& c, auto& ctx) {
    return std::format_to(ctx.out(), "{}", c.to_string());
  }
};

/**
 * @brief \c std::format support for \c nexenne::benchmark::config.
 *
 * Makes \c std::format("{}", cfg) render the four knobs a result was produced
 * with, so a harness can log its settings. The target duration is auto-scaled
 * through \c chrono::format_scaled. Accepts an empty format spec only.
 */
template <>
struct std::formatter<nexenne::benchmark::config> {
  /**
   * @brief Parses the format spec, which must be empty.
   *
   * @param ctx Format parse context positioned at the spec.
   *
   * @return Iterator to the closing brace of the spec.
   *
   * @pre The format spec for this type is empty.
   * @post The parse context is unchanged.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    auto const it{ctx.begin()};
    if (it != ctx.end() && *it != '}') {
      throw std::format_error{"nexenne::benchmark formatter accepts no format spec"};
    }
    return it;
  }

  /**
   * @brief Writes \p cfg as its four knobs into the output.
   *
   * @param cfg Config to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the written text.
   *
   * @pre None.
   * @post The knobs of \p cfg have been written through \p ctx; \p cfg is
   *       unchanged.
   *
   * @throws std::bad_alloc if building the output fails.
   */
  static auto format(nexenne::benchmark::config const& cfg, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::benchmark::to_string(cfg));
  }
};
