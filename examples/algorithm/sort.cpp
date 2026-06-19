/**
 * @file
 * @brief Example: the nexenne::algorithm integer sorts.
 *
 * Shows counting_sort (small known value range) and radix_sort (full-width
 * unsigned values), including the heap-free caller-scratch radix overload.
 */

#include <array>
#include <cstdint>
#include <cstdio>
#include <span>
#include <vector>

#include <nexenne/algorithm/sort/counting_sort.hpp>
#include <nexenne/algorithm/sort/radix_sort.hpp>

namespace alg = nexenne::algorithm;

auto print(char const* const label, std::span<std::uint32_t const> const v) -> void {
  std::printf("%s", label);
  for (auto const x : v) {
    std::printf(" %u", x);
  }
  std::printf("\n");
}

auto main() -> int {
  // counting_sort: values in a known small range [0, 9].
  auto digits{std::vector<std::uint32_t>{4, 1, 9, 1, 0, 4, 7, 2}};
  alg::counting_sort(std::span<std::uint32_t>{digits}, 9u);
  print("counting_sort:", digits);

  // radix_sort: arbitrary 32-bit values, allocating overload.
  auto keys{std::vector<std::uint32_t>{0xFF00u, 3u, 0xDEADBEEFu, 42u, 0x10000u, 1u}};
  alg::radix_sort(std::span<std::uint32_t>{keys});
  print("radix_sort:   ", keys);

  // radix_sort with caller-provided scratch: no allocation.
  auto more{std::vector<std::uint32_t>{50, 40, 30, 20, 10}};
  auto scratch{std::vector<std::uint32_t>(more.size())};
  alg::radix_sort(std::span<std::uint32_t>{more}, std::span<std::uint32_t>{scratch});
  print("radix (scratch):", more);
  return 0;
}
