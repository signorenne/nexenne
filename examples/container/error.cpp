/**
 * @file
 * @brief The container error policy and result alias in action.
 *
 * A fallible operation returns result<T> (std::expected<T, container_error>)
 * rather than throwing; the caller inspects the value or names the error.
 */

#include <array>
#include <cstddef>
#include <print>
#include <span>

#include <nexenne/container/error.hpp>

namespace {

namespace cn = nexenne::container;

// Bounds-checked element access: a hit returns the value, a miss returns an
// out_of_range error instead of throwing or asserting.
auto nth(std::span<int const> data, std::size_t const index) -> cn::result<int> {
  if (index >= data.size()) {
    return std::unexpected{cn::container_error::out_of_range};
  }
  return data[index];
}

}  // namespace

auto main() -> int {
  std::array<int, 3> const data{10, 20, 30};

  for (auto const index : {std::size_t{1}, std::size_t{5}}) {
    if (auto const value{nth(data, index)}; value.has_value()) {
      std::println("data[{}] = {}", index, *value);
    } else {
      std::println("data[{}] -> error: {}", index, cn::to_string(value.error()));
    }
  }
  // data[1] = 20
  // data[5] -> error: out_of_range
  return 0;
}
