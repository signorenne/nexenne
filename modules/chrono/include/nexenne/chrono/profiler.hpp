#pragma once

/**
 * @file
 * @brief Per-name aggregator of timed scope durations.
 *
 * Designed to pair with \c scope_timer: ask the profiler for a
 * \c sink(name), hand that to \c scope_timer, and every
 * scope-bounded measurement under that name lands in this
 * profiler's stats bucket.
 *
 * \code
 * auto prof{profiler<>{}};
 *
 * for (auto&& msg : queue) {
 *     auto t{scope_timer{prof.sink("decode")}};
 *     decode(msg);
 * }
 *
 * auto const s{prof["decode"]};
 * std::print("decoded {} messages in {} us (avg {} us)\n",
 *            s.count,
 *            std::chrono::duration_cast<std::chrono::microseconds>(s.total).count(),
 *            std::chrono::duration_cast<std::chrono::microseconds>(s.mean()).count());
 * \endcode
 *
 * Per-bucket stats:
 *   - \c count : number of recorded samples.
 *   - \c total : sum of all samples.
 *   - \c min   : smallest sample (\c duration::max() before any
 *                sample is seen).
 *   - \c max   : largest sample.
 *   - \c mean  : \c total / count (zero when \c count is zero).
 *
 * \c sink(name) returns a callable that captures a pointer to
 * the bucket. \c std::map iterators are stable across insertions,
 * so the pointer stays valid as new buckets are added. \c reset()
 * zeros stats in-place rather than removing buckets, so prior
 * sinks remain valid; use \c remove() if you really need to
 * drop a bucket (and accept that any sink targeting it becomes
 * dangling).
 *
 * \warning Allocation: \c record() and \c sink() insert into the
 * map on first use of a name, which can throw \c std::bad_alloc.
 * Every other method is \c noexcept.
 *
 * @tparam Clock Steady clock the recorded durations are anchored
 *                in (taken from any \c scope_timer driven by the
 *                same clock).
 */

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <utility>

#include <nexenne/chrono/concepts.hpp>

namespace nexenne::chrono {

/**
 * @brief Per-name aggregator of timed-scope durations.
 *
 * Pairs with \c scope_timer: hand a \c sink(name) to a scope timer and every
 * measurement under that name lands in this profiler's stats bucket. Buckets
 * live in a \c std::map keyed by name; iterators are stable across
 * insertions, so a \c sink callable that caches a bucket pointer stays valid
 * as new names are added.
 *
 * @tparam Clock Steady clock the recorded durations are anchored in.
 *
 * @pre None.
 * @post A default-constructed profiler holds no buckets.
 *
 * @warning \c record and \c sink insert into the map on first use of a name
 *          and so may throw \c std::bad_alloc; every other method is
 *          \c noexcept.
 */
template <steady_clock_like Clock = std::chrono::steady_clock>
class profiler {
public:
  using clock_type = Clock;
  using duration = typename Clock::duration;

  /**
   * @brief Accumulated statistics for one named bucket.
   *
   * @pre None.
   * @post None.
   */
  struct stats {
    std::uint64_t count{0};            ///< Number of recorded samples.
    duration total{duration::zero()};  ///< Sum of all samples.
    duration min{duration::max()};     ///< Smallest sample seen.
    duration max{duration::min()};     ///< Largest sample seen (min() sentinel so a
                                       ///< sample set of only negatives is not lost).

    /**
     * @brief Mean sample duration.
     *
     * @return \c total divided by \c count, or \c duration::zero() when no
     *         samples have been recorded.
     *
     * @pre None.
     * @post None.
     */
    [[nodiscard]] constexpr auto mean() const noexcept -> duration {
      return count == 0 ? duration::zero() : total / static_cast<typename duration::rep>(count);
    }
  };

private:
  std::map<std::string, stats, std::less<>> m_stats{};

  static auto update(stats& s, duration const d) noexcept -> void {
    ++s.count;
    s.total += d;
    if (d < s.min) {
      s.min = d;
    }
    if (d > s.max) {
      s.max = d;
    }
  }

public:
  /**
   * @brief Record a single sample under \p name.
   *
   * Updates the named bucket's count, total, min, and max. Inserts a new
   * bucket on first use of \p name.
   *
   * @param name Bucket name; looked up heterogeneously without allocating.
   * @param d Sample duration to fold in.
   *
   * @pre None.
   * @post The bucket for \p name has \c count increased by one and its
   *       aggregates updated to include \p d.
   * @throws std::bad_alloc if a new bucket must be inserted and allocation
   *         fails.
   */
  auto record(std::string_view const name, duration const d) -> void {
    // Heterogeneous lookup (\c std::less<> on the map) lets us
    // probe with a \c string_view; only insert allocates a new
    // \c std::string.
    auto it{m_stats.find(name)};
    if (it == m_stats.end()) {
      it = m_stats.emplace(std::string{name}, stats{}).first;
    }
    update(it->second, d);
  }

  /**
   * @brief Make a \c scope_timer callback that records into \p name.
   *
   * The returned callable caches a pointer to the bucket, so each
   * invocation is a single in-place update with no fresh map lookup.
   * Inserts a new bucket on first use of \p name.
   *
   * @param name Bucket name to record into.
   *
   * @return A callable taking a \c duration that folds it into the bucket.
   *
   * @pre None.
   * @post A bucket for \p name exists.
   * @throws std::bad_alloc if a new bucket must be inserted and allocation
   *         fails.
   *
   * @warning The returned callable holds a pointer into this profiler and
   *          must not outlive it; a bucket removed via \c remove leaves any
   *          sink targeting it dangling.
   */
  [[nodiscard]] auto sink(std::string_view const name) {
    auto it{m_stats.find(name)};
    if (it == m_stats.end()) {
      it = m_stats.emplace(std::string{name}, stats{}).first;
    }
    auto* const bucket{&it->second};
    return [bucket](duration const d) noexcept { update(*bucket, d); };
  }

  /**
   * @brief Look up a bucket's statistics by name.
   *
   * @param name Bucket name to read.
   *
   * @return A copy of the bucket's \c stats, or a default-constructed
   *         \c stats with \c count zero when \p name is unknown.
   *
   * @pre None.
   * @post None; no bucket is inserted for an unknown name.
   */
  [[nodiscard]] auto operator[](std::string_view const name) const -> stats {
    auto const it{m_stats.find(name)};
    if (it == m_stats.end()) {
      return {};
    }
    return it->second;
  }

  /**
   * @brief Whether a bucket exists for \p name.
   *
   * @param name Bucket name to test.
   *
   * @return \c true if a bucket for \p name is present.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto contains(std::string_view const name) const noexcept -> bool {
    return m_stats.find(name) != m_stats.end();
  }

  /**
   * @brief Number of buckets.
   *
   * @return The count of distinct recorded names.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto size() const noexcept -> std::size_t {
    return m_stats.size();
  }

  /**
   * @brief Whether the profiler holds no buckets.
   *
   * @return \c true if no names have been recorded.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto empty() const noexcept -> bool {
    return m_stats.empty();
  }

  /**
   * @brief Read-only view of every bucket.
   *
   * @return A reference to the underlying name-to-stats map.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto buckets() const noexcept -> std::map<std::string, stats, std::less<>> const& {
    return m_stats;
  }

  /**
   * @brief Zero every bucket's statistics in place.
   *
   * Buckets are retained, so previously-returned \c sink callables stay
   * valid. To drop a bucket entirely use \c remove instead.
   *
   * @pre None.
   * @post Every bucket has \c count zero and reset aggregates; the set of
   *       bucket names is unchanged.
   */
  auto reset() noexcept -> void {
    for (auto& [_, s] : m_stats) {
      s = stats{};
    }
  }

  /**
   * @brief Zero a single bucket's statistics in place.
   *
   * The bucket itself stays in the map, so any sink targeting it keeps
   * recording into the freshly-zeroed stats. No-op for an unknown name.
   *
   * @param name Bucket name to clear.
   *
   * @pre None.
   * @post If \p name exists, its bucket has \c count zero and reset
   *       aggregates.
   */
  auto reset(std::string_view const name) noexcept -> void {
    auto const it{m_stats.find(name)};
    if (it != m_stats.end()) {
      it->second = stats{};
    }
  }

  /**
   * @brief Remove a bucket entirely.
   *
   * No-op for an unknown name.
   *
   * @param name Bucket name to drop.
   *
   * @pre None.
   * @post No bucket for \p name remains in the map.
   *
   * @warning Invalidates any sink previously returned for \p name; calling
   *          that sink afterward is undefined behaviour.
   */
  auto remove(std::string_view const name) -> void {
    auto const it{m_stats.find(name)};
    if (it != m_stats.end()) {
      m_stats.erase(it);
    }
  }
};

}  // namespace nexenne::chrono
