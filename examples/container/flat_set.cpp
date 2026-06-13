/**
 * @file
 * @brief flat_set as a sorted lookup set: O(log N) contains, ordered iteration.
 *
 * Built once from a word list, then queried; the sorted contiguous storage makes
 * membership a cache-friendly binary search and iteration a flat, in-order walk.
 */

#include <print>
#include <string>

#include <nexenne/container/flat_set.hpp>

namespace {

namespace cn = nexenne::container;

}  // namespace

auto main() -> int {
  cn::flat_set<std::string> stopwords;
  for (auto const& word : {"the", "a", "of", "and", "a"}) {
    stopwords.insert(word);  // the second "a" is dropped as a duplicate
  }
  std::println("{} stopwords, contains 'the': {}", stopwords.size(), stopwords.contains("the"));

  std::print("sorted:");
  for (auto const& word : stopwords) {
    std::print(" {}", word);
  }
  std::println("");
  // 4 stopwords, contains 'the': true
  // sorted: a and of the
  return 0;
}
