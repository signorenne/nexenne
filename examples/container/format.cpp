/**
 * @file
 * @brief Debug-printing nexenne containers with to_string, << and std::format.
 *
 * Including format.hpp gives every printable container a to_string, an ostream
 * inserter, and a std::formatter, so containers drop into diagnostics the same
 * way the standard ones do. The output is for humans, not serialisation.
 */

#include <format>
#include <iostream>
#include <print>
#include <string>

#include <nexenne/container/format.hpp>
#include <nexenne/utility/discard.hpp>

namespace {

namespace cn = nexenne::container;

}  // namespace

auto main() -> int {
  cn::static_vector<int, 8> v;
  nexenne::utility::discard(v.push_back(1));
  nexenne::utility::discard(v.push_back(2));
  nexenne::utility::discard(v.push_back(3));

  cn::flat_hash_map<std::string, int> m;
  m.insert("a", 1);

  std::println("{}", cn::to_string(v));  // to_string
  std::cout << v << '\n';                // operator<<
  std::println("{}", v);                 // std::formatter
  std::println("{}", m);
  // static_vector[1, 2, 3]
  // static_vector[1, 2, 3]
  // static_vector[1, 2, 3]
  // flat_hash_map{a: 1}
  return 0;
}
