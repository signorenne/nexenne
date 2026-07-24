#pragma once

/**
 * @file
 * @brief Drop detection over the sequence numbers of an edge event stream.
 *
 * A kernel event buffer or a transport ring that overflows drops events
 * silently; the only trace is a gap in the sequence numbers of the events
 * that survive. \c sequence_tracker watches those numbers and reports each
 * gap as it is observed, so a production system can count lost edges,
 * raise an alarm, or trigger a resynchronising read instead of trusting a
 * stream that skipped.
 */

#include <cstdint>

#include <nexenne/gpio/line_types.hpp>

namespace nexenne::gpio {

/**
 * @brief Counts events lost between consecutive observed sequence numbers.
 *
 * Feed every event's sequence number in arrival order. Events carrying the
 * unset sequence (zero) pass through untracked, so streams from backends
 * that do not number their events simply never report a gap.
 */
class sequence_tracker {
public:
  using value_type = event_sequence;

private:
  event_sequence m_last{0};
  std::uint64_t m_dropped{0};

public:
  /**
   * @brief Constructs a tracker that has observed nothing.
   *
   * @pre None.
   * @post \c last() is the unset sequence and \c dropped() is zero.
   */
  constexpr sequence_tracker() noexcept = default;

  /**
   * @brief The highest sequence number observed so far.
   *
   * @return The stored sequence; zero when nothing was tracked yet.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto last() const noexcept -> event_sequence {
    return m_last;
  }

  /**
   * @brief Total events lost across every gap observed so far.
   *
   * @return The accumulated drop count.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto dropped() const noexcept -> std::uint64_t {
    return m_dropped;
  }

  /**
   * @brief Forgets everything, as after a reopen.
   *
   * @pre None.
   * @post \c last() is the unset sequence and \c dropped() is zero.
   */
  constexpr auto reset() noexcept -> void {
    m_last = event_sequence{0};
    m_dropped = 0;
  }

  /**
   * @brief Feeds one observed sequence number.
   *
   * The first tracked number establishes the baseline and reports no gap.
   * After that, a number more than one above the last reports the missing
   * count; a number at or below the last (a duplicate or reordered event)
   * reports zero and leaves the baseline where it was. The unset sequence
   * (zero) is ignored entirely.
   *
   * @param sequence Sequence number of the event just observed.
   *
   * @return How many events were skipped immediately before this one.
   *
   * @pre None.
   * @post \c dropped() grew by the returned amount and \c last() is the
   *       highest number fed so far.
   *
   * @complexity \c O(1).
   */
  constexpr auto feed(event_sequence const sequence) noexcept -> std::uint64_t {
    if (sequence == event_sequence{0}) {
      return 0;
    }
    if (m_last == event_sequence{0} || sequence.get() <= m_last.get()) {
      if (sequence.get() > m_last.get()) {
        m_last = sequence;
      }
      return 0;
    }
    auto const gap{sequence.get() - m_last.get() - 1};
    m_last = sequence;
    m_dropped += gap;
    return gap;
  }
};

}  // namespace nexenne::gpio
