/**
 * @file
 * @brief A fixed-capacity sample buffer with nexenne::container::static_vector.
 *
 * Collect sensor readings into inline storage with no heap; an overflow is an
 * explicit error rather than a silent reallocation.
 */

#include <print>

#include <nexenne/container/static_vector.hpp>

namespace {

namespace cn = nexenne::container;

}  // namespace

auto main() -> int {
  cn::static_vector<int, 4> readings;

  for (int const sample : {12, 7, 19, 4, 25}) {  // five samples, capacity four
    if (auto const pushed{readings.push_back(sample)}; !pushed.has_value()) {
      std::println("dropped sample {}: buffer is {}", sample, cn::to_string(pushed.error()));
    }
  }

  std::println("stored {} of {} samples", readings.size(), readings.capacity());

  int sum{0};
  for (int const value : readings) {  // range-based iteration over the live span
    sum += value;
  }
  std::println("average of stored: {}", sum / static_cast<int>(readings.size()));
  // dropped sample 25: buffer is full
  // stored 4 of 4 samples
  // average of stored: 10
  return 0;
}
