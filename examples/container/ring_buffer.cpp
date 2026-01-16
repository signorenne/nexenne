/**
 * @file
 * @brief ring_buffer as a rolling window of the most recent N samples.
 *
 * push_overwrite never fails: once the buffer is full it drops the oldest
 * element to make room, so the buffer always holds the latest N readings.
 */

#include <print>

#include <nexenne/container/ring_buffer.hpp>

namespace {

namespace cn = nexenne::container;

}  // namespace

auto main() -> int {
  cn::ring_buffer<int, 3> recent;  // keep only the last 3 readings

  for (int const reading : {20, 21, 23, 22, 25}) {
    recent.push_overwrite(reading);  // evicts the oldest once full
  }

  std::print("last {} readings (oldest first):", recent.size());
  for (int const reading : recent) {  // FIFO iteration
    std::print(" {}", reading);
  }
  std::println("");

  int sum{0};
  for (int const reading : recent) {
    sum += reading;
  }
  std::println("rolling average: {}", sum / static_cast<int>(recent.size()));
  // last 3 readings (oldest first): 23 22 25
  // rolling average: 23
  return 0;
}
