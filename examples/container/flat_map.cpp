/**
 * @file
 * @brief flat_map as a word-count map: O(log N) lookup over key-sorted storage.
 *
 * operator[] inserts a zero on first sight of a word and returns a mutable
 * reference, so the classic counting idiom works, and iteration yields the
 * entries already sorted by key.
 *
 * Expected output:
 *
 * \code
 * counts (sorted by word): cat=1 mat=1 on=1 sat=1 the=3
 * 'the' appears 3 times
 * \endcode
 */

#include <print>
#include <string>

#include <nexenne/container/flat_map.hpp>

namespace {

namespace cn = nexenne::container;

}  // namespace

auto main() -> int {
  cn::flat_map<std::string, int> counts;
  for (auto const& word : {"the", "cat", "sat", "on", "the", "mat", "the"}) {
    counts[word] += 1;
  }

  std::print("counts (sorted by word):");
  for (auto const& [word, n] : counts) {
    std::print(" {}={}", word, n);
  }
  std::println("");

  if (auto const* const the{counts.at("the")}) {
    std::println("'the' appears {} times", *the);
  }
  return 0;
}
