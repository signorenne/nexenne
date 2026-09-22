/**
 * @file
 * @brief Printing the nexenne::algorithm errors and graph results via format.hpp.
 *
 * The algorithm headers stay free of <format>; format.hpp adds a
 * std::formatter, a to_string and an operator<< for each type a caller gets
 * back and may want to show, all printing the same text. This prints a failed
 * decode and a failed root search by name, then a hand-built spanning-tree edge
 * and shortest path.
 */

#include <cstdint>
#include <format>
#include <iostream>
#include <print>

#include <nexenne/algorithm/encoding/base_n.hpp>
#include <nexenne/algorithm/format.hpp>
#include <nexenne/algorithm/graph/a_star.hpp>
#include <nexenne/algorithm/graph/kruskal_mst.hpp>
#include <nexenne/algorithm/numerical/bisection.hpp>

namespace {

namespace alg = nexenne::algorithm;

}  // namespace

auto main() -> int {
  if (auto const bytes{alg::base64_decode("not base64!")}; !bytes) {
    std::println("decode: [{:>18}]", bytes.error());
  }
  auto const no_root{[](double const x) -> double { return x * x + 1.0; }};
  if (auto const root{alg::bisection<double>(no_root, 0.0, 2.0)}; !root) {
    std::cout << "root:   " << root.error() << '\n';
  }

  auto const edge{alg::mst_edge<double, std::uint32_t>{.from = 0, .to = 2, .weight = 1.5}};
  std::println("edge:   {}", edge);

  auto const path{alg::a_star_result<std::uint32_t, int>{.path = {0, 3, 4}, .cost = 7}};
  std::cout << "path:   " << path << '\n';
  std::println("same:   {}", alg::to_string(path) == std::format("{}", path));
  return 0;
}
