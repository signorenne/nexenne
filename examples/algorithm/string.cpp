/**
 * @file
 * @brief Example: the nexenne::algorithm string algorithms.
 *
 * Shows single-pattern search (kmp, boyer_moore), all-match search (z),
 * edit distance, multi-pattern Aho-Corasick, and the suffix array.
 */

#include <cstddef>
#include <cstdio>
#include <span>
#include <string_view>

#include <nexenne/algorithm/string/aho_corasick.hpp>
#include <nexenne/algorithm/string/boyer_moore.hpp>
#include <nexenne/algorithm/string/kmp.hpp>
#include <nexenne/algorithm/string/levenshtein.hpp>
#include <nexenne/algorithm/string/suffix_array.hpp>
#include <nexenne/algorithm/string/z_function.hpp>

namespace alg = nexenne::algorithm;

auto main() -> int {
  constexpr std::string_view text{"the cat sat on the mat"};

  std::printf("kmp_find(\"the\")         = %zu\n", alg::kmp_find(text, "the"));
  std::printf("boyer_moore_find(\"mat\") = %zu\n", alg::boyer_moore_find(text, "mat"));

  std::printf("z_find_all(\"at\")        =");
  for (auto const pos : alg::z_find_all(text, "at")) {
    std::printf(" %zu", pos);
  }
  std::printf("\n");

  std::printf("levenshtein(kitten, sitting) = %zu\n", alg::levenshtein("kitten", "sitting"));

  // Aho-Corasick: many patterns in one scan.
  auto m{alg::aho_corasick{}};
  m.add_pattern("cat");
  m.add_pattern("at");
  m.add_pattern("the");
  m.build();
  std::printf("aho_corasick matches (id, end):");
  m.scan(text, [](std::size_t const id, std::size_t const end) {
    std::printf(" (%zu,%zu)", id, end);
  });
  std::printf("\n");

  // Suffix array of a small string.
  auto const sa{alg::build_suffix_array("banana")};
  std::printf("suffix_array(banana)    =");
  for (auto const i : sa) {
    std::printf(" %d", i);
  }
  std::printf("\n");
  return 0;
}
