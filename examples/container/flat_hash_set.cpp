/**
 * @file
 * @brief flat_hash_set for membership and deduplication: "have I seen this?"
 *
 * A set is a map that forgot its values: the same contiguous, linear-probing
 * storage as flat_hash_map, holding only keys. Here it deduplicates a stream of
 * tokens and answers membership in amortised constant time.
 */

#include <print>
#include <string>

#include <nexenne/container/flat_hash_set.hpp>

namespace {

namespace cn = nexenne::container;

}  // namespace

auto main() -> int {
  cn::flat_hash_set<std::string> seen;
  std::string const stream[]{"alpha", "beta", "alpha", "gamma", "beta"};

  int unique{0};
  for (auto const& token : stream) {
    if (seen.insert(token)) {  // true only the first time a token appears
      ++unique;
    }
  }

  std::println("{} tokens, {} unique", std::size(stream), unique);
  std::println("seen 'alpha': {}", seen.contains("alpha"));
  std::println("seen 'delta': {}", seen.contains("delta"));
  // 5 tokens, 3 unique
  // seen 'alpha': true
  // seen 'delta': false
  return 0;
}
