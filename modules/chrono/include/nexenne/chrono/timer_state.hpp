#pragma once

/**
 * @file
 * @brief Lifecycle states of the stopwatches and the countdown.
 *
 * The states live at namespace scope, not inside the class templates, so one
 * \c to_string and one \c std::formatter serve every clock: a type nested in a
 * class template cannot be named by a formatter specialisation. Each class
 * keeps its \c state alias, so \c stopwatch<>::state::running still reads as
 * before. Their names, through \c to_string, \c operator<< and the formatter,
 * live in \c format.hpp.
 */

#include <cstdint>

namespace nexenne::chrono {

/**
 * @brief Lifecycle state of a \c stopwatch or a \c static_stopwatch.
 *
 * @pre None.
 * @post None.
 */
enum class stopwatch_state : std::uint8_t {
  idle,
  running,
  paused,
};

/**
 * @brief Lifecycle state of a \c countdown.
 *
 * @pre None.
 * @post None.
 */
enum class countdown_state : std::uint8_t {
  idle,
  running,
  paused,
  expired,
};

}  // namespace nexenne::chrono
