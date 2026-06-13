#pragma once

/**
 * @file
 * @brief Umbrella header for the nexenne::chrono module.
 *
 * Portable timing primitives, every type templated on a clock so the same code
 * runs under \c std::chrono::steady_clock on a PC, \c tick_clock<Backend> on an
 * MCU, and \c manual_clock in tests. Foundations: the \c clock_like /
 * \c steady_clock_like / \c chrono_duration / \c tick_backend concepts, the
 * \c tick_clock backend adapter, and \c manual_clock. Helpers: \c duration_parts
 * with \c extract_parts and \c format, the saturating \c to_count_sat, and the
 * \c frequency Hz/period conversions. Measurement and control: \c stopwatch,
 * \c fixed_stopwatch, \c scope_timer, \c profiler, \c frame_timer, \c countdown,
 * \c deadline, \c interval, \c alarm, and \c rate_limiter.
 */

#include <nexenne/chrono/alarm.hpp>
#include <nexenne/chrono/concepts.hpp>
#include <nexenne/chrono/conversion.hpp>
#include <nexenne/chrono/countdown.hpp>
#include <nexenne/chrono/deadline.hpp>
#include <nexenne/chrono/duration_parts.hpp>
#include <nexenne/chrono/fixed_stopwatch.hpp>
#include <nexenne/chrono/frame_timer.hpp>
#include <nexenne/chrono/frequency.hpp>
#include <nexenne/chrono/interval.hpp>
#include <nexenne/chrono/manual_clock.hpp>
#include <nexenne/chrono/profiler.hpp>
#include <nexenne/chrono/rate_limiter.hpp>
#include <nexenne/chrono/scope_timer.hpp>
#include <nexenne/chrono/stopwatch.hpp>
#include <nexenne/chrono/tick_clock.hpp>

namespace nexenne::chrono {}
