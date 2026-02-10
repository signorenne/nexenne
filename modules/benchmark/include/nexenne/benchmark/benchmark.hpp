#pragma once

/**
 * @file
 * @brief Micro-benchmark runner: auto-tuning, statistics, comparison.
 *
 * \c nexenne::benchmark is a small focused toolkit for timing short routines
 * and comparing variants. The philosophy: do one thing well, stay header-only,
 * no surprises.
 *
 * \code
 *   auto r1{nexenne::benchmark::run("vector_3 cross", [] noexcept {
 *       auto a{nexenne::math::vector_3_f{1, 2, 3}};
 *       auto b{nexenne::math::vector_3_f{4, 5, 6}};
 *       auto c{nexenne::math::cross(a, b)};
 *       nexenne::benchmark::do_not_optimize(c);
 *   })};
 *   r1.print();
 *
 *   auto r2{nexenne::benchmark::run("vector_3 dot", [] noexcept {
 *       // ...
 *   })};
 *
 *   nexenne::benchmark::compare(r1, r2).print();
 * \endcode
 *
 * What it does:
 *   - Auto-tunes the iteration count. A calibration pass measures the
 *     single-call cost, then picks how many iterations fit in a target
 *     measurement window (default 100 ms).
 *   - Multi-sample statistics. Runs N independent batches (default 10) and
 *     reports mean, median, stddev, min, max, and coefficient of variation
 *     (cv = stddev/mean, a noise gauge).
 *   - Anti-DCE primitives. \c do_not_optimize / \c clobber_memory (in
 *     do_not_optimize.hpp) prevent the compiler from folding a benchmark to a
 *     no-op. Without them the numbers are fiction. (See Chandler Carruth,
 *     CppCon 2015.)
 *   - Compares two benchmarks: ratio plus speedup factor, no statistical test.
 *     Use the cv and the spread to judge significance.
 *
 * What it deliberately does NOT do:
 *   - No fixture / setup-per-iteration mechanism, use lambda captures.
 *   - No memory profiling, use external tools (perf, valgrind).
 *   - No threading models, runs single-threaded.
 *   - No registration macros, explicit \c run("name", fn) calls.
 *   - No statistical p-values, the cv tells you if it is noisy.
 */

#include <algorithm>
#include <chrono>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <format>
#include <iostream>
#include <limits>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nexenne/benchmark/do_not_optimize.hpp>
#include <nexenne/chrono/duration_parts.hpp>
#include <nexenne/chrono/stopwatch.hpp>

namespace nexenne::benchmark {

namespace detail {

/**
 * @brief Escapes a string for embedding inside a JSON string literal.
 *
 * Escapes the characters JSON forbids unescaped (the quote, the backslash, and
 * the control characters below 0x20), so a benchmark name containing a quote,
 * backslash, or newline still yields valid JSON.
 *
 * @param s Text to escape.
 *
 * @return \p s with JSON-significant characters escaped.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] inline auto json_escape(std::string_view const s) -> std::string {
  auto out{std::string{}};
  out.reserve(s.size() + 2);
  for (auto const c : s) {
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\b':
        out += "\\b";
        break;
      case '\f':
        out += "\\f";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          out += std::format("\\u{:04x}", static_cast<unsigned>(static_cast<unsigned char>(c)));
        } else {
          out += c;
        }
    }
  }
  return out;
}

/**
 * @brief Converts a (budget / per-call) ratio to a usable iteration count.
 *
 * Floors the ratio to an integer count, returning 0 when it is non-finite or
 * non-positive (an absurd, negative, or unmeasurable budget), so a caller's
 * \c min_iterations floor wins instead, and capping it well below \c SIZE_MAX so
 * the timing loop stays bounded. The cap also avoids the undefined behaviour of
 * casting a negative, NaN, or out-of-range \c double to \c std::size_t.
 *
 * @param ratio Budget nanoseconds divided by per-call nanoseconds.
 *
 * @return A bounded iteration count, or 0 when \p ratio is unusable.
 *
 * @pre None.
 * @post The result is at most a fixed cap of about 2^40.
 */
[[nodiscard]] inline auto iters_from_ratio(double const ratio) noexcept -> std::size_t {
  if (!std::isfinite(ratio) || ratio <= 0.0) {
    return 0;
  }
  // Bound the loop and keep the double->size_t cast in range (out-of-range is
  // UB). The 2^40 figure is width-independent; on a 32-bit size_t (an MCU
  // target) it exceeds SIZE_MAX, so clamp to SIZE_MAX there as well.
  constexpr double cap{1'099'511'627'776.0};  // 2^40, ample headroom
  auto const bounded{ratio < cap ? ratio : cap};
  constexpr auto size_max{static_cast<double>(std::numeric_limits<std::size_t>::max())};
  if (bounded >= size_max) {
    return std::numeric_limits<std::size_t>::max();
  }
  return static_cast<std::size_t>(bounded);
}

}  // namespace detail

/**
 * @brief Configuration knobs for a benchmark run.
 *
 * Defaults are calibrated for typical micro-benchmarks (nanosecond to
 * millisecond range). Tweak when measurements are noisy or you want faster
 * turnaround.
 */
struct config {
  /// Wall-clock budget per sample batch; the runner fills it with repeated calls.
  std::chrono::nanoseconds target_duration{std::chrono::milliseconds{100}};
  /// Number of independent sample batches; statistics are computed across their means.
  std::size_t sample_count{10};
  /// Minimum iterations per batch regardless of timing, a baseline statistical floor.
  std::size_t min_iterations{1};
  /// When true, one extra batch runs and is discarded so caches and CPU frequency settle.
  bool warmup{true};
};

/**
 * @brief Outcome of a benchmark run: per-sample means and statistics over them.
 *
 * The samples themselves are exposed so callers can do their own analysis (for
 * example dump a CDF or fit a model). The default \c print shows the
 * common-case summary.
 */
class result {
private:
  std::string m_name{};
  std::vector<double> m_sample_means_ns{};
  std::size_t m_total_iterations{0};

public:
  /**
   * @brief Constructs an empty result with no samples.
   *
   * Every statistical accessor reports zero on a default-constructed result,
   * since there is no measurement to summarise.
   *
   * @pre None.
   * @post \c name() is empty, \c samples() is empty, and \c total_iterations()
   *       is zero.
   */
  result() = default;

  /**
   * @brief Constructs a result from a finished benchmark run.
   *
   * Takes ownership of the per-sample mean timings and records the label and
   * the total number of timed iterations across all sample batches.
   *
   * @param name Label shown in \c print and \c to_json output.
   * @param sample_means_ns Per-sample mean timings, in nanoseconds per
   *        iteration, one entry per sample batch.
   * @param total_iterations Total iterations summed across every batch.
   *
   * @pre None.
   * @post \c name() equals \p name, \c samples() views the moved-in data, and
   *       \c total_iterations() equals \p total_iterations.
   */
  result(
    std::string name, std::vector<double> sample_means_ns, std::size_t const total_iterations
  ) noexcept
      : m_name{std::move(name)}
      , m_sample_means_ns{std::move(sample_means_ns)}
      , m_total_iterations{total_iterations} {}

  /**
   * @brief Label identifying this benchmark.
   *
   * @return A view of the name passed at construction.
   *
   * @pre None.
   * @post The result is unchanged; the view is valid while the result lives and
   *       is not reassigned.
   */
  [[nodiscard]] auto name() const noexcept -> std::string_view {
    return m_name;
  }

  /**
   * @brief Per-sample mean timings, one entry per sample batch.
   *
   * Exposed so callers can run their own analysis, for example dumping a CDF or
   * fitting a model, rather than relying on the built-in summaries.
   *
   * @return A span of per-sample means, in nanoseconds per iteration.
   *
   * @pre None.
   * @post The result is unchanged; the span is valid while the result lives and
   *       is not reassigned.
   */
  [[nodiscard]] auto samples() const noexcept -> std::span<double const> {
    return m_sample_means_ns;
  }

  /**
   * @brief Total number of timed iterations across all sample batches.
   *
   * @return The summed iteration count.
   *
   * @pre None.
   * @post The result is unchanged.
   */
  [[nodiscard]] auto total_iterations() const noexcept -> std::size_t {
    return m_total_iterations;
  }

  /**
   * @brief Arithmetic mean of the per-sample means, in ns per iteration.
   *
   * Averages the one mean-timing data point produced by each sample batch.
   *
   * @return The mean nanoseconds per iteration, or 0 when there are no samples.
   *
   * @pre None.
   * @post The result is unchanged; the returned value is non-negative.
   *
   * @complexity \c O(n) in the number of samples.
   */
  [[nodiscard]] auto mean() const noexcept -> double {
    if (m_sample_means_ns.empty()) {
      return 0.0;
    }
    auto sum{0.0};
    for (auto const v : m_sample_means_ns) {
      sum += v;
    }
    return sum / static_cast<double>(m_sample_means_ns.size());
  }

  /**
   * @brief Median of the per-sample means, in ns per iteration.
   *
   * Robust to outliers and often the most honest single-number summary of a
   * benchmark. Sorts a copy of the samples, so the stored data is untouched.
   *
   * @return The median nanoseconds per iteration, or 0 when there are no
   *         samples. For an even sample count the mean of the two central
   *         values is returned.
   *
   * @pre None.
   * @post The result is unchanged; the returned value is non-negative.
   *
   * @complexity \c O(n log n) in the number of samples.
   */
  [[nodiscard]] auto median() const noexcept -> double {
    if (m_sample_means_ns.empty()) {
      return 0.0;
    }
    auto sorted{m_sample_means_ns};
    std::ranges::sort(sorted);
    auto const n{sorted.size()};
    if (n % 2 == 1) {
      return sorted[n / 2];
    }
    return (sorted[n / 2 - 1] + sorted[n / 2]) * 0.5;
  }

  /**
   * @brief Sample standard deviation of the per-sample means.
   *
   * Uses the \c n-1 (Bessel-corrected) normalisation, the unbiased estimator
   * for samples drawn from a larger population of runs.
   *
   * @return The standard deviation in nanoseconds per iteration, or 0 when
   *         fewer than two samples are present.
   *
   * @pre None.
   * @post The result is unchanged; the returned value is non-negative.
   *
   * @complexity \c O(n) in the number of samples.
   */
  [[nodiscard]] auto stddev() const noexcept -> double {
    if (m_sample_means_ns.size() < 2) {
      return 0.0;
    }
    auto const mu{mean()};
    auto sq{0.0};
    for (auto const v : m_sample_means_ns) {
      auto const d{v - mu};
      sq += d * d;
    }
    return std::sqrt(sq / static_cast<double>(m_sample_means_ns.size() - 1));
  }

  /**
   * @brief Fastest per-sample mean observed.
   *
   * The smallest of the per-sample means is the batch least disturbed by
   * background noise, and is often the closest estimate of the true cost.
   *
   * @return The minimum nanoseconds per iteration, or 0 when there are no
   *         samples.
   *
   * @pre None.
   * @post The result is unchanged; the returned value is non-negative.
   *
   * @complexity \c O(n) in the number of samples.
   */
  [[nodiscard]] auto min() const noexcept -> double {
    if (m_sample_means_ns.empty()) {
      return 0.0;
    }
    return *std::ranges::min_element(m_sample_means_ns);
  }

  /**
   * @brief Slowest per-sample mean observed.
   *
   * The largest of the per-sample means, useful for gauging the worst-case
   * spread caused by scheduling or cache effects.
   *
   * @return The maximum nanoseconds per iteration, or 0 when there are no
   *         samples.
   *
   * @pre None.
   * @post The result is unchanged; the returned value is non-negative.
   *
   * @complexity \c O(n) in the number of samples.
   */
  [[nodiscard]] auto max() const noexcept -> double {
    if (m_sample_means_ns.empty()) {
      return 0.0;
    }
    return *std::ranges::max_element(m_sample_means_ns);
  }

  /**
   * @brief Coefficient of variation: \c stddev() divided by \c mean().
   *
   * A unit-free gauge of measurement noise. Below roughly 5% the measurement is
   * generally reliable; above roughly 10% background noise likely dominates and
   * the result should be distrusted.
   *
   * @return The coefficient of variation, or 0 when \c mean() is zero.
   *
   * @pre None.
   * @post The result is unchanged; the returned value is non-negative.
   *
   * @complexity \c O(n) in the number of samples.
   */
  [[nodiscard]] auto cv() const noexcept -> double {
    auto const m{mean()};
    return m == 0.0 ? 0.0 : stddev() / m;
  }

  /**
   * @brief Throughput in items per second, derived from \c mean().
   *
   * Converts the mean nanoseconds per iteration into items per second given
   * that each iteration handles \p items_per_iteration items. Useful for
   * reporting "X entries inserted per second" instead of raw ns/iter.
   *
   * @param items_per_iteration Number of items processed per iteration.
   *
   * @return Items processed per second, or 0 when \c mean() is zero.
   *
   * @pre None.
   * @post The result is unchanged; the returned value is non-negative.
   */
  [[nodiscard]] auto items_per_second(std::size_t const items_per_iteration
  ) const noexcept -> double {
    auto const m{mean()};
    if (m == 0.0) {
      return 0.0;
    }
    return static_cast<double>(items_per_iteration) * 1e9 / m;
  }

  /**
   * @brief Throughput in bytes per second, derived from \c mean().
   *
   * Converts the mean nanoseconds per iteration into bytes per second given
   * that each iteration touches \p bytes_per_iteration bytes. Useful for
   * memory-bandwidth-shaped benchmarks such as copy, hash, or serialize.
   *
   * @param bytes_per_iteration Number of bytes touched per iteration.
   *
   * @return Bytes processed per second, or 0 when \c mean() is zero.
   *
   * @pre None.
   * @post The result is unchanged; the returned value is non-negative.
   */
  [[nodiscard]] auto bytes_per_second(std::size_t const bytes_per_iteration
  ) const noexcept -> double {
    auto const m{mean()};
    if (m == 0.0) {
      return 0.0;
    }
    return static_cast<double>(bytes_per_iteration) * 1e9 / m;
  }

  /**
   * @brief Writes the human-readable single-line summary to a stream.
   *
   * Emits the same line as \c to_string followed by a newline, defaulting to
   * standard output.
   *
   * @param os Destination stream; defaults to \c std::cout.
   *
   * @pre None.
   * @post The result is unchanged and one summary line has been written to
   *       \p os.
   *
   * @throws std::ios_base::failure if \p os is configured to throw on a write
   *         failure.
   */
  auto print(std::ostream& os = std::cout) const -> void {
    os << *this;
  }

  /**
   * @brief Single-line human-readable summary as a string.
   *
   * For callers without a stream handy, for example logging, asserting on
   * output, or capturing into a buffer for later display. Shows the label,
   * median, mean plus or minus stddev, coefficient of variation, and the
   * min-to-max range, each timing auto-scaled to ns, us, ms, or s.
   *
   * @return The formatted summary line, without a trailing newline.
   *
   * @pre None.
   * @post The result is unchanged.
   *
   * @throws std::bad_alloc if allocating the result string fails.
   */
  [[nodiscard]] auto to_string() const -> std::string {
    return std::format(
      "{:<32} median: {:>10} mean: {:>10} +/- {:>8} ({:>5.1f}% cv)  range: [{} .. {}]",
      m_name,
      format_time(median()),
      format_time(mean()),
      format_time(stddev()),
      cv() * 100.0,
      format_time(min()),
      format_time(max())
    );
  }

  /**
   * @brief Streams the single-line summary plus a trailing newline.
   *
   * Lets a result drop straight into a stream chain, for example
   * \c std::cout << run(...). Emits exactly what \c to_string produces followed
   * by a newline.
   *
   * @param os Destination stream.
   * @param r Result to write.
   *
   * @return The stream \p os, to allow chaining.
   *
   * @pre None.
   * @post One summary line and a newline have been written to \p os; \p r is
   *       unchanged.
   *
   * @throws std::bad_alloc if formatting the summary string fails.
   */
  friend auto operator<<(std::ostream& os, result const& r) -> std::ostream& {
    return os << r.to_string() << '\n';
  }

  /**
   * @brief Serialises the result as a JSON object for CI integration.
   *
   * Writes a single JSON object carrying the name, the mean, median, stddev,
   * min, and max in nanoseconds, the coefficient of variation, and the full
   * array of per-sample means. No trailing newline is added.
   *
   * @note This hand-rolls a small, fixed JSON shape. Once the \c serialization
   *       module is ported, switch this to emit through it for correct,
   *       reusable JSON encoding rather than a bespoke format string.
   *
   * @param os Destination stream.
   *
   * @pre None.
   * @post The result is unchanged and one JSON object has been written to
   *       \p os.
   *
   * @throws std::ios_base::failure if \p os is configured to throw on a write
   *         failure.
   */
  auto to_json(std::ostream& os) const -> void {
    os << std::format(
      R"({{"name":"{}","mean_ns":{:.3f},"median_ns":{:.3f},"stddev_ns":{:.3f},"min_ns":{:.3f},"max_ns":{:.3f},"cv":{:.4f},"samples":[)",
      detail::json_escape(m_name),
      mean(),
      median(),
      stddev(),
      min(),
      max(),
      cv()
    );
    for (auto i{std::size_t{0}}; i < m_sample_means_ns.size(); ++i) {
      if (i != 0) {
        os << ',';
      }
      os << std::format("{:.3f}", m_sample_means_ns[i]);
    }
    os << "]}";
  }

private:
  [[nodiscard]] static auto format_time(double const ns) -> std::string {
    // Reuse chrono's auto-scaling SI formatter (ns / us / ms / s), which keeps
    // the sub-millisecond resolution micro-timing needs.
    return chrono::format_scaled(std::chrono::duration<double, std::nano>{ns});
  }
};

/**
 * @brief Side-by-side comparison of two benchmark results.
 */
class comparison {
private:
  result const* m_baseline{nullptr};
  result const* m_candidate{nullptr};

public:
  /**
   * @brief Constructs a comparison referencing a baseline and a candidate.
   *
   * Stores pointers to the two results; neither is copied. The comparison reads
   * their means lazily through the accessors below.
   *
   * @param baseline Reference result, the point of comparison.
   * @param candidate Result being judged against \p baseline.
   *
   * @pre \p baseline and \p candidate outlive this comparison.
   * @post This comparison refers to \p baseline and \p candidate.
   */
  constexpr comparison(result const& baseline, result const& candidate) noexcept
      : m_baseline{&baseline}, m_candidate{&candidate} {}

  /**
   * @brief Ratio of candidate mean to baseline mean.
   *
   * A value above 1 means the candidate is slower than the baseline; below 1
   * means it is faster. There is no statistical test; judge significance from
   * the coefficient of variation and the spread.
   *
   * @return The candidate mean divided by the baseline mean, or 0 when the
   *         baseline mean is zero.
   *
   * @pre None.
   * @post The comparison and both referenced results are unchanged; the
   *       returned value is non-negative.
   */
  [[nodiscard]] auto ratio() const noexcept -> double {
    auto const b{m_baseline->mean()};
    return b == 0.0 ? 0.0 : m_candidate->mean() / b;
  }

  /**
   * @brief Speedup factor of the candidate over the baseline.
   *
   * The reciprocal of \c ratio. A value above 1 means the candidate is faster
   * than the baseline; below 1 means it is slower.
   *
   * @return The baseline mean divided by the candidate mean, or 0 when the
   *         candidate mean is zero.
   *
   * @pre None.
   * @post The comparison and both referenced results are unchanged; the
   *       returned value is non-negative.
   */
  [[nodiscard]] auto speedup() const noexcept -> double {
    auto const c{m_candidate->mean()};
    return c == 0.0 ? 0.0 : m_baseline->mean() / c;
  }

  /**
   * @brief Writes the human-readable comparison to a stream.
   *
   * Emits the same text as \c to_string followed by a newline, defaulting to
   * standard output.
   *
   * @param os Destination stream; defaults to \c std::cout.
   *
   * @pre None.
   * @post The comparison and both referenced results are unchanged and the text
   *       has been written to \p os.
   *
   * @throws std::ios_base::failure if \p os is configured to throw on a write
   *         failure.
   */
  auto print(std::ostream& os = std::cout) const -> void {
    os << *this;
  }

  /**
   * @brief Multi-line human-readable comparison as a string.
   *
   * Renders the baseline summary, the candidate summary, and a final line
   * stating how many times faster or slower the candidate is. The factor is
   * normalised so it always reads at least 1, with the direction named
   * explicitly.
   *
   * @return The formatted comparison text, without a trailing newline.
   *
   * @pre None.
   * @post The comparison and both referenced results are unchanged.
   *
   * @throws std::bad_alloc if allocating the result string fails.
   */
  [[nodiscard]] auto to_string() const -> std::string {
    auto const s{speedup()};
    // A zero mean on either side makes the speedup zero or non-finite, so the
    // reciprocal would be inf/NaN; report the factor as unavailable instead.
    if (!std::isfinite(s) || s <= 0.0) {
      return std::format(
        "{}\n{}\n  -> speedup unavailable (a result has a zero mean)",
        m_baseline->to_string(),
        m_candidate->to_string()
      );
    }
    return std::format(
      "{}\n{}\n  -> candidate is {:.2f}x {} than baseline",
      m_baseline->to_string(),
      m_candidate->to_string(),
      (s >= 1.0 ? s : 1.0 / s),
      (s >= 1.0 ? "faster" : "slower")
    );
  }

  /**
   * @brief Streams the comparison text plus a trailing newline.
   *
   * Lets a comparison drop straight into a stream chain. Emits exactly what
   * \c to_string produces followed by a newline.
   *
   * @param os Destination stream.
   * @param c Comparison to write.
   *
   * @return The stream \p os, to allow chaining.
   *
   * @pre None.
   * @post The comparison text and a newline have been written to \p os; \p c is
   *       unchanged.
   *
   * @throws std::bad_alloc if formatting the comparison string fails.
   */
  friend auto operator<<(std::ostream& os, comparison const& c) -> std::ostream& {
    return os << c.to_string() << '\n';
  }
};

/**
 * @brief Builds a comparison of \p candidate against \p baseline.
 *
 * Convenience factory that constructs a \c comparison referring to the two
 * results. Neither result is copied, so both must outlive the comparison.
 *
 * @param baseline Reference result, the point of comparison.
 * @param candidate Result being judged against \p baseline.
 *
 * @return A comparison referring to \p baseline and \p candidate.
 *
 * @pre \p baseline and \p candidate outlive the returned comparison.
 * @post Both results are unchanged.
 */
[[nodiscard]] inline auto
compare(result const& baseline, result const& candidate) noexcept -> comparison {
  return comparison{baseline, candidate};
}

/**
 * @brief Runs \p fn enough times to fill the time budget per sample, repeats
 *        for \c sample_count samples, and returns the statistics.
 *
 * \p fn is invoked with no arguments. Use lambda captures for any state you
 * need, and call \c do_not_optimize on at least one output so the compiler
 * cannot fold the call away.
 *
 * A calibration pass measures the single-call cost and derives the iteration
 * count; an optional warmup batch is then discarded before the timed batches.
 *
 * @tparam Fn Any \c std::invocable<>.
 * @param name Label for the result, shown in \c print output.
 * @param fn Routine to benchmark.
 * @param cfg Optional config overrides.
 *
 * @return A \c result holding the per-sample means and their statistics.
 *
 * @pre \p fn is safe to invoke repeatedly and at least once for calibration.
 * @post The returned result carries one per-sample mean for each of
 *       \c cfg.sample_count batches; \c name() equals \p name.
 *
 * @throws Whatever \p fn throws, and \c std::bad_alloc if collecting the
 *         samples fails.
 *
 * @complexity Runs \p fn roughly \c cfg.sample_count times the per-batch
 *             iteration count, plus calibration and optional warmup.
 */
template <std::invocable Fn>
[[nodiscard]] auto run(std::string_view const name, Fn&& fn, config const cfg = {}) -> result {
  using ns_d = std::chrono::duration<double, std::nano>;
  auto timer{chrono::stopwatch{}};

  // Calibration: measure the single-call cost, derive the iteration count.
  timer.restart();
  fn();
  auto const single_ns{timer.elapsed<ns_d>().count()};

  auto const target_ns{static_cast<double>(cfg.target_duration.count())};
  auto iters_per_sample{cfg.min_iterations};
  if (single_ns > 0.0) {
    auto const estimated{detail::iters_from_ratio(target_ns / single_ns)};
    iters_per_sample = std::max(iters_per_sample, estimated);
  } else {
    // Single call too fast to measure; use a healthy default.
    iters_per_sample = std::max(iters_per_sample, std::size_t{1024});
  }
  // Never let the per-batch divisor reach zero (e.g. min_iterations set to 0
  // with a target shorter than a single call), which would yield NaN timings.
  iters_per_sample = std::max(iters_per_sample, std::size_t{1});

  // Optional warmup batch (discarded).
  if (cfg.warmup) {
    for (auto i{std::size_t{0}}; i < iters_per_sample; ++i) {
      fn();
    }
  }

  auto sample_means_ns{std::vector<double>{}};
  sample_means_ns.reserve(cfg.sample_count);

  auto total_iters{std::size_t{0}};
  for (auto s{std::size_t{0}}; s < cfg.sample_count; ++s) {
    timer.restart();
    for (auto i{std::size_t{0}}; i < iters_per_sample; ++i) {
      fn();
    }
    auto const elapsed_ns{timer.elapsed<ns_d>().count()};
    sample_means_ns.push_back(elapsed_ns / static_cast<double>(iters_per_sample));
    total_iters += iters_per_sample;
  }

  return result{std::string{name}, std::move(sample_means_ns), total_iters};
}

/**
 * @brief Runs \p fn enough times to fill the budget, calling \p setup before
 *        each iteration. Only \p fn is timed.
 *
 * Use for benchmarks needing fresh state per iteration, e.g. "insert into an
 * empty container". Plain \c run with lambda captures would let the container
 * grow across iterations, skewing later samples.
 *
 * Trade-off: each iteration is timed individually (one clock pair), adding tens
 * of ns of overhead per iteration. Fine for anything taking microseconds or
 * longer; for sub-100 ns benchmarks prefer plain \c run with a different state
 * strategy.
 *
 * Calibration measures \p fn alone for the reported timing but caps the
 * iteration count by the combined setup-plus-fn wall cost, so an expensive
 * \p setup cannot make a run unbounded.
 *
 * @tparam Setup Any \c std::invocable<> run before each timed call.
 * @tparam Fn Any \c std::invocable<> whose cost is measured.
 * @param name Label for the result, shown in \c print output.
 * @param setup Routine run before every iteration; not timed.
 * @param fn Routine to benchmark; only this is timed.
 * @param cfg Optional config overrides.
 *
 * @return A \c result holding the per-sample means and their statistics.
 *
 * @pre \p setup and \p fn are safe to invoke repeatedly and at least once for
 *      calibration.
 * @post The returned result carries one per-sample mean for each of
 *       \c cfg.sample_count batches; \c name() equals \p name.
 *
 * @throws Whatever \p setup or \p fn throws, and \c std::bad_alloc if
 *         collecting the samples fails.
 *
 * @complexity Runs \p setup and \p fn together roughly \c cfg.sample_count
 *             times the per-batch iteration count, plus calibration and warmup.
 */
template <std::invocable Setup, std::invocable Fn>
[[nodiscard]] auto run_with_setup(
  std::string_view const name, Setup&& setup, Fn&& fn, config const cfg = {}
) -> result {
  using ns_d = std::chrono::duration<double, std::nano>;
  auto fn_timer{chrono::stopwatch{}};
  auto iter_timer{chrono::stopwatch{}};

  // Calibration: time fn alone (fn_timer) for the reported cost, and the whole
  // setup-plus-fn iteration (iter_timer) for the cap, so an expensive setup
  // cannot make a run unbounded.
  iter_timer.restart();
  setup();
  fn_timer.restart();
  fn();
  auto const single_ns{fn_timer.elapsed<ns_d>().count()};
  auto const iteration_ns{iter_timer.elapsed<ns_d>().count()};

  auto const target_ns{static_cast<double>(cfg.target_duration.count())};
  auto iters_per_sample{cfg.min_iterations};
  if (single_ns > 0.0) {
    auto const estimated{detail::iters_from_ratio(target_ns / single_ns)};
    iters_per_sample = std::max(iters_per_sample, estimated);
  } else {
    iters_per_sample = std::max(iters_per_sample, std::size_t{1024});
  }
  if (iteration_ns > 0.0) {
    auto const wall_limited{
      std::max(std::size_t{1}, detail::iters_from_ratio(target_ns / iteration_ns))
    };
    iters_per_sample = std::min(iters_per_sample, wall_limited);
    iters_per_sample = std::max(iters_per_sample, cfg.min_iterations);
  }
  // Guard against a zero divisor in the per-batch mean below.
  iters_per_sample = std::max(iters_per_sample, std::size_t{1});

  if (cfg.warmup) {
    for (auto i{std::size_t{0}}; i < iters_per_sample; ++i) {
      setup();
      fn();
    }
  }

  auto sample_means_ns{std::vector<double>{}};
  sample_means_ns.reserve(cfg.sample_count);
  auto total_iters{std::size_t{0}};

  for (auto s{std::size_t{0}}; s < cfg.sample_count; ++s) {
    auto accumulated_ns{0.0};
    for (auto i{std::size_t{0}}; i < iters_per_sample; ++i) {
      setup();  // not timed
      fn_timer.restart();
      fn();
      accumulated_ns += fn_timer.elapsed<ns_d>().count();
    }
    sample_means_ns.push_back(accumulated_ns / static_cast<double>(iters_per_sample));
    total_iters += iters_per_sample;
  }

  return result{std::string{name}, std::move(sample_means_ns), total_iters};
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
