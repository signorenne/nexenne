#pragma once

/**
 * @file
 * @brief Stopwatch: start / pause / resume / lap, accumulating
 *        across intervals like the physical device.
 *
 * Lifecycle:
 *
 *   - \c idle    : freshly constructed or just \c reset()
 *   - \c running : accumulating time
 *   - \c paused  : accumulator frozen at the moment of \c pause()
 *
 * Laps capture the duration of each segment between successive
 * \c lap() calls (or between \c start() and the first \c lap()).
 * Each \c lap() returns the segment it just closed; \c peek_lap()
 * returns the in-progress segment without closing it.
 *
 * Allocation: \c std::vector \<duration\> grows on \c lap(); call
 * \c reserve_laps(n) up front if you know the lap count.
 * \c lap() and \c reserve_laps() may throw \c std::bad_alloc on
 * OOM; every other method is \c noexcept. For an OOM-free variant
 * use \c static_stopwatch.
 *
 * @tparam Clock Steady clock to read \c now() from.
 */

#include <algorithm>
#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <ranges>
#include <string>
#include <vector>

#include <nexenne/chrono/concepts.hpp>
#include <nexenne/chrono/duration_parts.hpp>

namespace nexenne::chrono {

/**
 * @brief Start / pause / resume / lap stopwatch over a steady clock.
 *
 * Accumulates elapsed time across running intervals like a physical
 * stopwatch and records per-segment lap durations in a growable buffer.
 * Lap storage may throw \c std::bad_alloc on growth; every other method is
 * \c noexcept. For an allocation-free variant use \c static_stopwatch.
 *
 * @tparam Clock Steady clock to read \c now() from.
 *
 * @pre None.
 * @post A default-constructed stopwatch is idle with zero elapsed time and no
 *       laps.
 */
template <steady_clock_like Clock = std::chrono::steady_clock>
class stopwatch {
public:
  using clock_type = Clock;
  using duration = typename Clock::duration;
  using time_point = typename Clock::time_point;

  /**
   * @brief Lifecycle state of a \c stopwatch.
   *
   * @pre None.
   * @post None.
   */
  enum class state : std::uint8_t {
    idle,
    running,
    paused,
  };

private:
  time_point m_start{};
  duration m_accumulated{duration::zero()};
  duration m_lap_baseline{duration::zero()};
  duration m_lap_sum{duration::zero()};
  std::vector<duration> m_laps{};
  state m_state{state::idle};

public:
  /**
   * @brief Construct an idle stopwatch.
   *
   * @pre None.
   * @post \c is_idle() is \c true with zero elapsed time and no laps.
   */
  constexpr stopwatch() noexcept = default;

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
    m_laps.clear();
    m_lap_sum = duration::zero();
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
    m_laps.clear();
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
   * @brief Close the current lap and return its duration.
   *
   * Records the segment since the previous lap boundary, or since \c start
   * for the first lap, and advances the lap baseline.
   *
   * @return The closed segment duration, or \c std::nullopt when idle.
   *
   * @pre None.
   * @post On success, \c lap_count() has grown by one and \c lap_sum() has
   *       increased by the returned segment.
   * @throws std::bad_alloc if the lap buffer must grow and allocation fails.
   */
  [[nodiscard]] auto lap() -> std::optional<duration> {
    if (m_state == state::idle) {
      return std::nullopt;
    }
    auto const total{elapsed_at(Clock::now())};
    auto const seg{total - m_lap_baseline};
    m_laps.push_back(seg);
    m_lap_sum += seg;
    m_lap_baseline = total;
    return seg;
  }

  /**
   * @brief Close the current lap and return its duration in units \p D.
   *
   * @tparam D Duration type the result is cast to.
   *
   * @return The closed segment in units \p D, or \c std::nullopt when idle.
   *
   * @pre None.
   * @post On success, \c lap_count() has grown by one and \c lap_sum() has
   *       increased by the closed segment.
   * @throws std::bad_alloc if the lap buffer must grow and allocation fails.
   */
  template <chrono_duration D>
  [[nodiscard]] auto lap() -> std::optional<D> {
    auto const r{lap()};
    if (!r) {
      return std::nullopt;
    }
    return std::chrono::duration_cast<D>(*r);
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
   * @brief Number of closed laps.
   *
   * @return The count of recorded laps.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto lap_count() const noexcept -> std::size_t {
    return m_laps.size();
  }

  /**
   * @brief Discard all recorded laps and rebase the current segment.
   *
   * Keeps the running or paused state; the next lap measures from now.
   *
   * @pre None.
   * @post \c lap_count() is zero and \c lap_sum() is \c duration::zero().
   */
  auto clear_laps() noexcept -> void {
    m_laps.clear();
    m_lap_sum = duration::zero();
    m_lap_baseline = elapsed_at(Clock::now());
  }

  /**
   * @brief Reserve capacity for \p n laps up front.
   *
   * Pre-allocating avoids reallocation, and thus the throwing path, during
   * subsequent \c lap() calls.
   *
   * @param n Number of laps to reserve storage for.
   *
   * @pre None.
   * @post The lap buffer can hold at least \p n laps without reallocating.
   * @throws std::bad_alloc if the reservation cannot be satisfied.
   */
  auto reserve_laps(std::size_t const n) -> void {
    m_laps.reserve(n);
  }

  /**
   * @brief Sum of all recorded lap durations.
   *
   * @return The total of every closed lap.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto lap_sum() const noexcept -> duration {
    return m_lap_sum;
  }

  /**
   * @brief Smallest recorded lap duration.
   *
   * @return The minimum lap, or \c std::nullopt when no laps exist.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto lap_min() const noexcept -> std::optional<duration> {
    if (m_laps.empty()) {
      return std::nullopt;
    }
    return *std::ranges::min_element(m_laps);
  }

  /**
   * @brief Largest recorded lap duration.
   *
   * @return The maximum lap, or \c std::nullopt when no laps exist.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto lap_max() const noexcept -> std::optional<duration> {
    if (m_laps.empty()) {
      return std::nullopt;
    }
    return *std::ranges::max_element(m_laps);
  }

  /**
   * @brief Mean recorded lap duration.
   *
   * @return The average lap, or \c std::nullopt when no laps exist.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto lap_average() const noexcept -> std::optional<duration> {
    if (m_laps.empty()) {
      return std::nullopt;
    }
    return m_lap_sum / static_cast<typename duration::rep>(m_laps.size());
  }

  /**
   * @brief Read-only view of the recorded laps.
   *
   * @return A reference to the lap buffer, valid until the next mutating
   *         lap operation.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto laps() const noexcept -> std::vector<duration> const& {
    return m_laps;
  }

  /**
   * @brief Elapsed time against a caller-supplied \c now() snapshot.
   *
   * Useful when comparing two stopwatches against the same instant, which
   * avoids two separate \c Clock::now() reads and the skew between them.
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
  [[nodiscard]] friend auto operator<=>(stopwatch const& a, stopwatch const& b) noexcept {
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
  [[nodiscard]] friend auto operator==(stopwatch const& a, stopwatch const& b) noexcept -> bool {
    auto const now{Clock::now()};
    return a.elapsed_at(now) == b.elapsed_at(now);
  }
};

}  // namespace nexenne::chrono

/**
 * @brief \c std::format support for \c stopwatch.
 *
 * Formats the current elapsed time using the same token layout as
 * \c nexenne::chrono::format. A leading \c '!' disables suppress-zero.
 *
 * @tparam Clock Steady clock of the formatted stopwatch.
 *
 * @pre None.
 * @post None.
 */
template <nexenne::chrono::steady_clock_like Clock>
struct std::formatter<nexenne::chrono::stopwatch<Clock>, char> {
private:
  bool suppress_zero{true};

public:
  /**
   * @brief Parse the format spec flags.
   *
   * @param ctx The format parse context.
   *
   * @return Iterator past the consumed spec.
   *
   * @pre None.
   * @post The \c '!' flag, if present, has been consumed.
   */
  constexpr auto parse(std::format_parse_context& ctx) {
    auto it{ctx.begin()};
    auto const end{ctx.end()};
    if (it != end && *it == '!') {
      suppress_zero = false;
      ++it;
    }
    return it;
  }

  /**
   * @brief Write the formatted stopwatch to the output.
   *
   * @tparam Out Output iterator type of the format context.
   * @param sw The stopwatch to format.
   * @param ctx The format context to write into.
   *
   * @return Iterator past the written output.
   *
   * @pre None.
   * @post None.
   */
  template <class Out>
  auto format(
    nexenne::chrono::stopwatch<Clock> const& sw, std::basic_format_context<Out, char>& ctx
  ) const {
    auto const ms{sw.template elapsed<std::chrono::milliseconds>()};
    auto const s{nexenne::chrono::format(ms, "{s-}{d}d:{h}h:{m}m:{s}s.{ms}", suppress_zero)};
    return std::ranges::copy(s, ctx.out()).out;
  }
};
