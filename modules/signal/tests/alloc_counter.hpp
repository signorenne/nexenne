#pragma once

/**
 * @file alloc_counter.hpp
 * @brief Global allocation counter shared by the signal test suites.
 *
 * The replacement \c operator new and \c operator delete that feed it are
 * defined once, in alloc_counter.cpp, since a program may replace them only
 * once. The counter lives in a named namespace so every suite sees the same
 * statics.
 */

#include <cstddef>

namespace signal_tests {

/**
 * @brief Counts the program's global heap allocations and the bytes they ask for.
 *
 * Tests read a value before and after the code under test and compare.
 */
struct alloc_counter {
  static inline std::size_t allocations{0};
  static inline std::size_t deallocations{0};
  static inline std::size_t bytes{0};

  /**
   * @brief Returns the number of allocations made so far.
   *
   * @return The running allocation count.
   *
   * @pre None.
   * @post None.
   */
  static auto snapshot() noexcept -> std::size_t {
    return allocations;
  }
};

}  // namespace signal_tests
