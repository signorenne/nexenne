#pragma once

/**
 * @file
 * @brief Online (single-pass) statistical estimators over sample streams.
 *
 * Three small types for statistics over a long stream without storing it:
 * \c running_stats (Welford's algorithm: mean, variance, stddev, min, max,
 * count; numerically stable, O(1) per sample, mergeable), \c histogram (a
 * fixed-bucket histogram with dedicated underflow/overflow bins and quantile
 * estimation, zero allocation), and \c ema_stats (exponentially-weighted moving
 * mean and variance, tracking recent behaviour when the distribution drifts).
 * Useful for telemetry, monitoring, tail-latency profiling, and anomaly
 * detection.
 */

#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace nexenne::algorithm {

/**
 * @brief Online mean, variance, stddev, min, max, and count.
 *
 * Uses Welford's algorithm, numerically stable even when the mean is large
 * relative to the standard deviation, where naive sum and sum-of-squares
 * approaches lose precision.
 *
 * @tparam T Floating-point sample type.
 */
template <std::floating_point T = double>
class running_stats {
public:
  using value_type = T;           ///< Floating-point sample type.
  using size_type = std::size_t;  ///< Type for the sample count.

private:
  size_type m_count{0};
  T m_mean{T{0}};
  T m_m2{T{0}};  ///< Sum of squared deviations from the mean.
  T m_min{std::numeric_limits<T>::infinity()};
  T m_max{-std::numeric_limits<T>::infinity()};

public:
  /**
   * @brief Constructs an empty accumulator.
   *
   * @pre None.
   * @post \c count() is zero.
   */
  constexpr running_stats() noexcept = default;

  /**
   * @brief Incorporates one sample into the running statistics.
   *
   * Updates the mean, the sum of squared deviations, and the min and max via
   * Welford's numerically stable recurrence.
   *
   * @param x Sample value.
   *
   * @pre None.
   * @post \c count() grows by one and every statistic reflects \p x.
   *
   * @complexity \c O(1).
   */
  constexpr auto push(T const x) noexcept -> void {
    ++m_count;
    auto const delta{x - m_mean};
    m_mean += delta / static_cast<T>(m_count);
    auto const delta2{x - m_mean};
    m_m2 += delta * delta2;
    if (x < m_min) {
      m_min = x;
    }
    if (x > m_max) {
      m_max = x;
    }
  }

  /**
   * @brief Number of samples accumulated so far.
   *
   * @return The sample count.
   *
   * @pre None.
   * @post The accumulator is unchanged.
   */
  [[nodiscard]] constexpr auto count() const noexcept -> size_type {
    return m_count;
  }

  /**
   * @brief Arithmetic mean of the samples.
   *
   * @return The running mean, or 0 when no sample has been pushed.
   *
   * @pre None.
   * @post The accumulator is unchanged.
   */
  [[nodiscard]] constexpr auto mean() const noexcept -> T {
    return m_count == 0 ? T{0} : m_mean;
  }

  /**
   * @brief Population variance, dividing by \c N.
   *
   * @return The population variance, or 0 with fewer than two samples.
   *
   * @pre None.
   * @post The accumulator is unchanged; the result is non-negative.
   */
  [[nodiscard]] constexpr auto variance() const noexcept -> T {
    return m_count < 2 ? T{0} : m_m2 / static_cast<T>(m_count);
  }

  /**
   * @brief Sample variance, dividing by \c N-1 (the unbiased estimator).
   *
   * @return The sample variance, or 0 with fewer than two samples.
   *
   * @pre None.
   * @post The accumulator is unchanged; the result is non-negative.
   */
  [[nodiscard]] constexpr auto sample_variance() const noexcept -> T {
    return m_count < 2 ? T{0} : m_m2 / static_cast<T>(m_count - 1);
  }

  /**
   * @brief Population standard deviation.
   *
   * @return The square root of \c variance().
   *
   * @pre None.
   * @post The accumulator is unchanged; the result is non-negative.
   */
  [[nodiscard]] auto stddev() const noexcept -> T {
    return std::sqrt(variance());
  }

  /**
   * @brief Sample standard deviation.
   *
   * @return The square root of \c sample_variance().
   *
   * @pre None.
   * @post The accumulator is unchanged; the result is non-negative.
   */
  [[nodiscard]] auto sample_stddev() const noexcept -> T {
    return std::sqrt(sample_variance());
  }

  /**
   * @brief Smallest sample seen so far.
   *
   * @return The minimum sample, or 0 when no sample has been pushed.
   *
   * @pre None.
   * @post The accumulator is unchanged.
   */
  [[nodiscard]] constexpr auto min() const noexcept -> T {
    return m_count == 0 ? T{0} : m_min;
  }

  /**
   * @brief Largest sample seen so far.
   *
   * @return The maximum sample, or 0 when no sample has been pushed.
   *
   * @pre None.
   * @post The accumulator is unchanged.
   */
  [[nodiscard]] constexpr auto max() const noexcept -> T {
    return m_count == 0 ? T{0} : m_max;
  }

  /**
   * @brief Spread between the largest and smallest samples.
   *
   * @return \c max() - min(), or 0 when no sample has been pushed.
   *
   * @pre None.
   * @post The accumulator is unchanged; the result is non-negative.
   */
  [[nodiscard]] constexpr auto range() const noexcept -> T {
    return m_count == 0 ? T{0} : (m_max - m_min);
  }

  /**
   * @brief Merges the statistics of \p other into this accumulator.
   *
   * Combines two independently accumulated streams in closed form without
   * rescanning either, enabling parallel reduction.
   *
   * @param other Accumulator to fold in.
   *
   * @pre None.
   * @post This reflects the union of both sample sets and \c count() equals the
   *       sum of the two prior counts; \p other is unchanged.
   *
   * @complexity \c O(1).
   */
  auto merge(running_stats const& other) noexcept -> void {
    if (other.m_count == 0) {
      return;
    }
    if (m_count == 0) {
      *this = other;
      return;
    }
    auto const n1{static_cast<T>(m_count)};
    auto const n2{static_cast<T>(other.m_count)};
    auto const n{n1 + n2};
    auto const delta{other.m_mean - m_mean};
    m_mean = (n1 * m_mean + n2 * other.m_mean) / n;
    m_m2 += other.m_m2 + delta * delta * n1 * n2 / n;
    m_count = m_count + other.m_count;
    if (other.m_min < m_min) {
      m_min = other.m_min;
    }
    if (other.m_max > m_max) {
      m_max = other.m_max;
    }
  }

  /**
   * @brief Clears all accumulated statistics.
   *
   * @pre None.
   * @post \c count() is zero and the accumulator behaves as freshly constructed.
   */
  constexpr auto reset() noexcept -> void {
    m_count = 0;
    m_mean = T{0};
    m_m2 = T{0};
    m_min = std::numeric_limits<T>::infinity();
    m_max = -std::numeric_limits<T>::infinity();
  }
};

/**
 * @brief Fixed-bucket histogram over a configured range.
 *
 * Splits \c [min, max] into \p N equal-width buckets. Samples below \c min go
 * into the underflow bin and samples at or above \c max into the overflow bin,
 * so no data is lost. Storage is a \c std::array (zero allocation).
 *
 * @tparam T Floating-point sample type.
 * @tparam N Number of in-range buckets (excluding under and over).
 */
template <std::floating_point T = double, std::size_t N = 32>
  requires(N > 0)
class histogram {
public:
  using value_type = T;                        ///< Floating-point sample type.
  using size_type = std::size_t;               ///< Type for indices and counts.
  static constexpr size_type bucket_count{N};  ///< Number of in-range buckets.

private:
  T m_min{T{0}};
  T m_max{T{1}};
  T m_width{T{1} / static_cast<T>(N)};
  std::array<std::uint64_t, N> m_buckets{};
  std::uint64_t m_underflow{0};
  std::uint64_t m_overflow{0};
  std::uint64_t m_total{0};

public:
  /**
   * @brief Constructs a histogram spanning \c [min, max] in \p N buckets.
   *
   * @param min Lower edge of the in-range span.
   * @param max Upper edge of the in-range span.
   *
   * @pre \p max is greater than \p min.
   * @post The histogram is empty and spans \c [min, max].
   */
  constexpr histogram(T const min, T const max) noexcept
      : m_min{min}, m_max{max}, m_width{(max - min) / static_cast<T>(N)} {}

  /**
   * @brief Records one sample into the appropriate bucket.
   *
   * Samples below \c min increment the underflow bin and samples at or above
   * \c max the overflow bin, so no data is lost.
   *
   * @param x Sample value.
   *
   * @pre None.
   * @post \c total() grows by one and exactly one bin reflects \p x.
   *
   * @complexity \c O(1).
   */
  constexpr auto push(T const x) noexcept -> void {
    ++m_total;
    if (x < m_min) {
      ++m_underflow;
      return;
    }
    if (x >= m_max) {
      ++m_overflow;
      return;
    }
    auto const idx{static_cast<size_type>((x - m_min) / m_width)};
    ++m_buckets[idx < N ? idx : N - 1];
  }

  /**
   * @brief Total samples recorded, including under and overflow.
   *
   * @return The sample count.
   *
   * @pre None.
   * @post The histogram is unchanged.
   */
  [[nodiscard]] constexpr auto total() const noexcept -> std::uint64_t {
    return m_total;
  }

  /**
   * @brief Number of samples that fell below the configured minimum.
   *
   * @return The underflow count.
   *
   * @pre None.
   * @post The histogram is unchanged.
   */
  [[nodiscard]] constexpr auto underflow() const noexcept -> std::uint64_t {
    return m_underflow;
  }

  /**
   * @brief Number of samples that fell at or above the configured maximum.
   *
   * @return The overflow count.
   *
   * @pre None.
   * @post The histogram is unchanged.
   */
  [[nodiscard]] constexpr auto overflow() const noexcept -> std::uint64_t {
    return m_overflow;
  }

  /**
   * @brief Count in the in-range bucket at index \p i.
   *
   * @param i Bucket index.
   *
   * @return The number of samples in bucket \p i.
   *
   * @pre \p i is less than \c bucket_count; a larger index is undefined
   *      behaviour.
   * @post The histogram is unchanged.
   */
  [[nodiscard]] constexpr auto bucket(size_type const i) const noexcept -> std::uint64_t {
    return m_buckets[i];
  }

  /**
   * @brief View over all in-range bucket counts, lowest bucket first.
   *
   * @return A span of \c bucket_count counts.
   *
   * @pre None.
   * @post The span is valid while the histogram lives and is not reset.
   */
  [[nodiscard]] constexpr auto buckets() const noexcept -> std::span<std::uint64_t const> {
    return std::span<std::uint64_t const>{m_buckets};
  }

  /**
   * @brief Approximate \p p quantile of the recorded samples.
   *
   * Walks the cumulative bucket counts until the target fraction is reached and
   * returns that bucket's midpoint. Resolution is one bucket width.
   *
   * @param p Quantile fraction in \c [0, 1], for example 0.95.
   *
   * @return The estimated quantile value, or 0 when no sample is recorded.
   *
   * @pre \p p is in \c [0, 1].
   * @post The histogram is unchanged; the result lies within \c [min, max].
   *
   * @complexity \c O(N) in the bucket count.
   */
  [[nodiscard]] auto quantile(double const p) const noexcept -> T {
    if (m_total == 0) {
      return T{0};
    }
    auto const target{static_cast<std::uint64_t>(static_cast<double>(m_total) * p)};
    auto cumulative{m_underflow};
    if (cumulative > target) {
      return m_min;
    }
    for (auto i{size_type{0}}; i < N; ++i) {
      cumulative += m_buckets[i];
      if (cumulative >= target) {
        return m_min + (static_cast<T>(i) + T{0.5}) * m_width;
      }
    }
    return m_max;
  }

  /**
   * @brief Clears all bucket counts while keeping the configured range.
   *
   * @pre None.
   * @post \c total() is zero and every count is zero; the range is unchanged.
   */
  constexpr auto reset() noexcept -> void {
    m_buckets = {};
    m_underflow = 0;
    m_overflow = 0;
    m_total = 0;
  }
};

/**
 * @brief Exponentially-weighted moving mean and variance.
 *
 * Tracks recent behaviour rather than the full history; older samples decay by
 * a factor of \c (1 - alpha) per step, computed inline as
 * \c m[n] = alpha*x[n] + (1 - alpha)*m[n-1] and
 * \c v[n] = alpha*(x[n] - m[n])^2 + (1 - alpha)*v[n-1]. Use when the underlying
 * distribution drifts (sensor calibration, load patterns, network conditions).
 *
 * @tparam T Floating-point sample type.
 */
template <std::floating_point T = double>
class ema_stats {
public:
  using value_type = T;  ///< Floating-point sample type.

private:
  T m_alpha{T{0}};
  T m_mean{T{0}};
  T m_var{T{0}};
  bool m_primed{false};

public:
  /**
   * @brief Constructs an estimator with smoothing factor \p alpha.
   *
   * @param alpha Smoothing factor in \c (0, 1]; larger tracks recent samples
   *        faster, smaller smooths more.
   *
   * @pre \p alpha is in \c (0, 1].
   * @post The estimator is unprimed and reports zero until the first sample.
   */
  constexpr explicit ema_stats(T const alpha) noexcept : m_alpha{alpha} {}

  /**
   * @brief Incorporates one sample into the moving mean and variance.
   *
   * The first sample primes the estimator to that value with zero variance;
   * later samples decay older history by \c (1 - alpha) per step.
   *
   * @param x Sample value.
   *
   * @pre None.
   * @post \c mean() and \c variance() reflect \p x weighted by the smoothing
   *       factor.
   *
   * @complexity \c O(1).
   */
  constexpr auto push(T const x) noexcept -> void {
    if (!m_primed) {
      m_mean = x;
      m_var = T{0};
      m_primed = true;
      return;
    }
    auto const old_mean{m_mean};
    m_mean = m_alpha * x + (T{1} - m_alpha) * m_mean;
    auto const dx{x - m_mean};
    auto const dm{m_mean - old_mean};
    m_var = m_alpha * dx * dx + (T{1} - m_alpha) * (m_var + dm * dm);
  }

  /**
   * @brief Current exponentially-weighted moving mean.
   *
   * @return The moving mean, or 0 before the first sample.
   *
   * @pre None.
   * @post The estimator is unchanged.
   */
  [[nodiscard]] constexpr auto mean() const noexcept -> T {
    return m_mean;
  }

  /**
   * @brief Current exponentially-weighted moving variance.
   *
   * @return The moving variance, or 0 before the first sample.
   *
   * @pre None.
   * @post The estimator is unchanged; the result is non-negative.
   */
  [[nodiscard]] constexpr auto variance() const noexcept -> T {
    return m_var;
  }

  /**
   * @brief Current exponentially-weighted moving standard deviation.
   *
   * @return The square root of \c variance().
   *
   * @pre None.
   * @post The estimator is unchanged; the result is non-negative.
   */
  [[nodiscard]] auto stddev() const noexcept -> T {
    return std::sqrt(m_var);
  }

  /**
   * @brief Clears the moving statistics and unprimes the estimator.
   *
   * @pre None.
   * @post \c mean() and \c variance() are zero and the next sample primes the
   *       estimator afresh; the smoothing factor is unchanged.
   */
  constexpr auto reset() noexcept -> void {
    m_mean = T{0};
    m_var = T{0};
    m_primed = false;
  }
};

}  // namespace nexenne::algorithm
