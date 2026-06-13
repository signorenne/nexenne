/**
 * @file
 * @brief Using a nexenne container as a std::unordered_map key via hash.hpp.
 *
 * Including hash.hpp registers std::hash specializations, so value containers
 * with a canonical order (here a static_vector) drop straight into the standard
 * hashed containers as keys.
 */

#include <print>
#include <string>
#include <unordered_map>

#include <nexenne/container/hash.hpp>

namespace {

namespace cn = nexenne::container;

}  // namespace

auto main() -> int {
  // Memoise a result keyed by a small fixed-capacity vector of inputs.
  std::unordered_map<cn::static_vector<int, 4>, std::string> memo;

  cn::static_vector<int, 4> key;
  key.push_back(1);
  key.push_back(2);
  key.push_back(3);
  memo[key] = "computed";

  cn::static_vector<int, 4> probe;
  probe.push_back(1);
  probe.push_back(2);
  probe.push_back(3);

  std::println("entries: {}", memo.size());
  std::println("probe hit: {}", memo.contains(probe));
  std::println("value: {}", memo[probe]);
  // entries: 1
  // probe hit: true
  // value: computed
  return 0;
}
