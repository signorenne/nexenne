#pragma once

/**
 * @file
 * @brief Stopwatch with a fixed-capacity lap buffer (no heap).
 *
 * Functionally a sibling of \c stopwatch: start / pause / resume
 * / lap / peek_lap / reset / restart, plus min / max / average
 * across stored laps. The difference is storage: laps live in a
 * \c std::array<duration, N> instead of a \c std::vector, so the
 * type never allocates, so it is safe for ESP32, Cortex-M, AVR, and any
 * heap-averse target.
 *
 * Overflow policy: once the buffer is full, additional \c lap()
 * calls still close out the current segment (updating
 * \c lap_sum / \c lap_baseline / \c laps_dropped) but do not
 * store the duration. The returned \c optional carries the
 * segment regardless, so the caller can take action on it
 * (write it elsewhere, log it, etc.).
 *
 * Aggregates:
 *   - \c lap_count            : total laps observed (including dropped).
 *   - \c stored_lap_count     : laps actually retained in the buffer.
 *   - \c lap_sum / \c lap_average : computed over all laps,
 *     including dropped ones (so \c lap_average is a faithful
 *     running mean even after the buffer fills).
 *   - \c lap_min / \c lap_max : computed only over the stored
 *     buffer (we don't have the dropped values anymore).
 *
 * @tparam N Capacity of the lap buffer.
 * @tparam Clock Steady clock to measure with.
 */

#include <algorithm>
#include <array>
#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include <nexenne/chrono/concepts.hpp>

namespace nexenne::chrono {

/**
 * @brief Stopwatch with a fixed-capacity, allocation-free lap buffer.
 *
 * Mirrors \c stopwatch but stores up to \p N laps in a \c std::array, so the
 * type never allocates. Once the buffer is full, further laps are still
 * counted and folded into \c lap_sum and \c lap_average but not retained, and
 * the overflow is reported through \c laps_dropped.
 *
 * @tparam N Capacity of the lap buffer; must be positive.
 * @tparam Clock Steady clock to measure with.
 *
 * @pre \p N is greater than zero.
 * @post A default-constructed fixed_stopwatch is idle with zero elapsed time
 *       and no laps.
 */
template <std::size_t N, steady_clock_like Clock = std::chrono::steady_clock>
  requires(N > 0)
class fixed_stopwatch {
public:
  using clock_type = Clock;
  using duration = typename Clock::duration;
  using time_point = typename Clock::time_point;

  /**
   * @brief Lifecycle state of a \c fixed_stopwatch.
   *
   * @pre None.
   * @post None.
   */
  enum class state : std::uint8_t {
    idle,
    running,
    paused,
  };

  static constexpr std::size_t capacity{N};  ///< Lap-buffer capacity, equal to \p N.

private:
  std::array<duration, N> m_laps{};
  time_point m_start{};
  duration m_accumulated{duration::zero()};
  duration m_lap_baseline{duration::zero()};
  duration m_lap_sum{duration::zero()};
  std::size_t m_stored{0};
  std::size_t m_total_laps{0};
  std::size_t m_laps_dropped{0};
  state m_state{state::idle};

  [[nodiscard]] auto stored_span() const noexcept -> std::span<duration const> {
    return std::span<duration const>{m_laps.data(), m_stored};
  }

public:
  /**
   * @brief Construct an idle fixed-capacity stopwatch.
   *
   * @pre None.
   * @post \c is_idle() is \c true with zero elapsed time and no laps.
   */
  constexpr fixed_stopwatch() noexcept = default;

  /**
   * @brief Current lifecycle state.
   *
   * @return The stored \c state.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto current_state() const noexcept -> state {
    return m_state;
  }

  /**
   * @brief Whether the stopwatch is idle.
   *
   * @return \c true if the state is \c state::idle.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto is_idle() const noexcept -> bool {
    return m_state == state::idle;
  }

  /**
   * @brief Whether the stopwatch is running.
   *
   * @return \c true if the state is \c state::running.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto is_running() const noexcept -> bool {
    return m_state == state::running;
  }

  /**
   * @brief Whether the stopwatch is paused.
   *
   * @return \c true if the state is \c state::paused.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto is_paused() const noexcept -> bool {
    return m_state == state::paused;
  }

  /**
   * @brief Start timing from idle.
   *
   * No-op if already running or paused. Clears accumulated time and laps.
   *
   * @pre None.
   * @post If the prior state was \c idle, the stopwatch is \c running with
   *       zero accumulated time and no laps; otherwise the state is
   *       unchanged.
   */
  auto start() noexcept -> void {
    if (m_state != state::idle) {
      return;
    }
    m_start = Clock::now();
    m_accumulated = duration::zero();
    m_lap_baseline = duration::zero();
    m_lap_sum = duration::zero();
    m_stored = 0;
    m_total_laps = 0;
    m_laps_dropped = 0;
    m_state = state::running;
  }

  /**
   * @brief Freeze the accumulator at the current elapsed time.
   *
   * No-op unless running.
   *
   * @pre None.
   * @post If the prior state was \c running, the stopwatch is \c paused and
   *       elapsed time is frozen; otherwise the state is unchanged.
   */
  auto pause() noexcept -> void {
    if (m_state != state::running) {
      return;
    }
    m_accumulated += (Clock::now() - m_start);
    m_state = state::paused;
  }

  /**
   * @brief Resume timing from where \c pause left off.
   *
   * No-op unless paused.
   *
   * @pre None.
   * @post If the prior state was \c paused, the stopwatch is \c running;
   *       otherwise the state is unchanged.
   */
  auto resume() noexcept -> void {
    if (m_state != state::paused) {
      return;
    }
    m_start = Clock::now();
    m_state = state::running;
  }

  /**
   * @brief Stop, clear all state, and return to idle.
   *
   * @pre None.
   * @post \c is_idle() is \c true with zero elapsed time and no laps.
   */
  auto reset() noexcept -> void {
    m_state = state::idle;
    m_start = time_point{};
    m_accumulated = duration::zero();
    m_lap_baseline = duration::zero();
    m_lap_sum = duration::zero();
    m_stored = 0;
    m_total_laps = 0;
    m_laps_dropped = 0;
  }

  /**
   * @brief Reset, then start.
   *
   * @pre None.
   * @post The stopwatch is \c running with zero accumulated time and no
   *       laps, timing from this call.
   */
  auto restart() noexcept -> void {
    reset();
    start();
  }

  /**
   * @brief Total accumulated elapsed time.
   *
   * Reads \c Clock::now(). Zero while idle; frozen while paused.
   *
   * @return The elapsed time.
   *
   * @pre None.
   * @post The result is greater than or equal to \c duration::zero().
   */
  [[nodiscard]] auto elapsed() const noexcept -> duration {
    return elapsed_at(Clock::now());
  }

  /**
   * @brief Total accumulated elapsed time in the units \p D.
   *
   * @tparam D Duration type the result is cast to.
   *
   * @return The elapsed time, in units \p D.
   *
   * @pre None.
   * @post The result is greater than or equal to \c D::zero().
   */
  template <chrono_duration D>
  [[nodiscard]] auto elapsed() const noexcept -> D {
    return std::chrono::duration_cast<D>(elapsed());
  }

  /**
   * @brief Elapsed time against a caller-supplied \c now() snapshot.
   *
   * @param now The time snapshot to measure against.
   *
   * @return The elapsed time as of \p now.
   *
   * @pre None.
   * @post The result is greater than or equal to \c duration::zero() when
   *       \p now is not before the start.
   */
  [[nodiscard]] auto elapsed_at(time_point const now) const noexcept -> duration {
    switch (m_state) {
      case state::idle:
        return duration::zero();
      case state::running:
        return m_accumulated + (now - m_start);
      case state::paused:
        return m_accumulated;
    }
    return duration::zero();
  }

  /**
   * @brief Close the current lap and return its duration.
   *
   * The segment is always counted and folded into \c lap_sum and
   * \c lap_average; it is stored only while the buffer has room, otherwise
   * \c laps_dropped grows. The returned value carries the segment in either
   * case so the caller can act on it.
   *
   * @return The closed segment duration, or \c std::nullopt when idle.
   *
   * @pre None.
   * @post When not idle, \c lap_count() has grown by one; \c stored_lap_count()
   *       grew by one if there was room, else \c laps_dropped() grew by one.
   */
  [[nodiscard]] auto lap() noexcept -> std::optional<duration> {
    if (m_state == state::idle) {
      return std::nullopt;
    }
    auto const total{elapsed_at(Clock::now())};
    auto const seg{total - m_lap_baseline};
    m_lap_sum += seg;
    m_lap_baseline = total;
    ++m_total_laps;

    if (m_stored < N) {
      m_laps[m_stored++] = seg;
    } else {
      ++m_laps_dropped;
    }
    return seg;
  }

  /**
   * @brief Duration of the in-progress lap without closing it.
   *
   * @return The current open segment, or \c std::nullopt when idle.
   *
   * @pre None.
   * @post The lap state is unchanged.
   */
  [[nodiscard]] auto peek_lap() const noexcept -> std::optional<duration> {
    if (m_state == state::idle) {
      return std::nullopt;
    }
    auto const total{elapsed_at(Clock::now())};
    return total - m_lap_baseline;
  }

  /**
   * @brief Discard all recorded laps and rebase the current segment.
   *
   * Resets the stored, total, and dropped lap counts; the next lap measures
   * from now.
   *
   * @pre None.
   * @post \c lap_count(), \c stored_lap_count(), and \c laps_dropped() are
   *       all zero and \c lap_sum() is \c duration::zero().
   */
  auto clear_laps() noexcept -> void {
    m_stored = 0;
    m_lap_sum = duration::zero();
    m_lap_baseline = elapsed_at(Clock::now());
    m_total_laps = 0;
    m_laps_dropped = 0;
  }

  /**
   * @brief Number of laps actually retained in the buffer.
   *
   * @return The stored lap count, at most \p N.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto stored_lap_count() const noexcept -> std::size_t {
    return m_stored;
  }

  /**
   * @brief Total laps observed, including any that were dropped.
   *
   * @return The cumulative lap count.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto lap_count() const noexcept -> std::size_t {
    return m_total_laps;
  }

  /**
   * @brief Number of laps observed but not stored because the buffer filled.
   *
   * @return The dropped lap count.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto laps_dropped() const noexcept -> std::size_t {
    return m_laps_dropped;
  }

  /**
   * @brief Sum of all observed lap durations, including dropped ones.
   *
   * @return The total across every observed lap.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto lap_sum() const noexcept -> duration {
    return m_lap_sum;
  }

  /**
   * @brief Smallest stored lap duration.
   *
   * Computed only over retained laps, since dropped values are not kept.
   *
   * @return The minimum stored lap, or \c std::nullopt when none are stored.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto lap_min() const noexcept -> std::optional<duration> {
    if (m_stored == 0) {
      return std::nullopt;
    }
    return *std::ranges::min_element(stored_span());
  }

  /**
   * @brief Largest stored lap duration.
   *
   * Computed only over retained laps, since dropped values are not kept.
   *
   * @return The maximum stored lap, or \c std::nullopt when none are stored.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto lap_max() const noexcept -> std::optional<duration> {
    if (m_stored == 0) {
      return std::nullopt;
    }
    return *std::ranges::max_element(stored_span());
  }

  /**
   * @brief Mean lap duration over all observed laps.
   *
   * Uses the total observed lap count, so the average stays faithful even
   * after the buffer fills and laps begin to be dropped.
   *
   * @return The average lap, or \c std::nullopt when no laps were observed.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto lap_average() const noexcept -> std::optional<duration> {
    if (m_total_laps == 0) {
      return std::nullopt;
    }
    return m_lap_sum / static_cast<typename duration::rep>(m_total_laps);
  }

  /**
   * @brief Read-only view of the stored laps.
   *
   * @return A span over the retained laps, valid until the next mutating
   *         lap operation.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto laps() const noexcept -> std::span<duration const> {
    return stored_span();
  }

  /**
   * @brief Three-way comparison by elapsed time.
   *
   * Reads \c Clock::now() once and compares both sides against that single
   * snapshot, so scheduling jitter between two \c now() reads cannot flip
   * the result.
   *
   * @return The ordering of the two elapsed times.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] friend auto
  operator<=>(fixed_stopwatch const& a, fixed_stopwatch const& b) noexcept {
    auto const now{Clock::now()};
    return a.elapsed_at(now) <=> b.elapsed_at(now);
  }

  /**
   * @brief Equality by elapsed time against a single \c now() snapshot.
   *
   * @return \c true if both stopwatches have equal elapsed time.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] friend auto
  operator==(fixed_stopwatch const& a, fixed_stopwatch const& b) noexcept -> bool {
    auto const now{Clock::now()};
    return a.elapsed_at(now) == b.elapsed_at(now);
  }
};

}  // namespace nexenne::chrono
