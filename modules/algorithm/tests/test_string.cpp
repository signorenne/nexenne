/**
 * @file
 * @brief Tests for nexenne::algorithm string algorithms.
 *
 * The single-pattern searches (kmp, boyer_moore) are validated differentially
 * against std::string_view::find, and the all-match searches (z, kmp_find_all,
 * aho_corasick) against a brute-force overlapping reference, over random
 * small-alphabet inputs that force frequent matches and periodic structure.
 * z_function, levenshtein, and the suffix array are each checked against an
 * independent reference, with the suffix array also verified to be a sorted
 * permutation and its LCP recomputed brute force.
 */

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nexenne/algorithm/string/aho_corasick.hpp>
#include <nexenne/algorithm/string/boyer_moore.hpp>
#include <nexenne/algorithm/string/kmp.hpp>
#include <nexenne/algorithm/string/levenshtein.hpp>
#include <nexenne/algorithm/string/suffix_array.hpp>
#include <nexenne/algorithm/string/z_function.hpp>

namespace {

namespace alg = nexenne::algorithm;
using std::size_t;

struct lcg {
  std::uint64_t state{0x243F6A8885A308D3ull};

  auto next() -> std::uint64_t {
    state = state * 6364136223846793005ull + 1442695040888963407ull;
    return state >> 11;
  }
};

[[nodiscard]] auto random_string(lcg& gen, std::size_t const n, int const alphabet) -> std::string {
  auto s{std::string{}};
  s.reserve(n);
  for (auto i{std::size_t{0}}; i < n; ++i) {
    s.push_back(
      static_cast<char>('a' + static_cast<int>(gen.next() % static_cast<std::uint64_t>(alphabet)))
    );
  }
  return s;
}

// Independent references.

[[nodiscard]] auto find_all_brute(std::string_view const hay, std::string_view const need)
  -> std::vector<std::size_t> {
  auto out{std::vector<std::size_t>{}};
  if (need.empty() || need.size() > hay.size()) {
    return out;
  }
  for (auto p{hay.find(need)}; p != std::string_view::npos; p = hay.find(need, p + 1)) {
    out.push_back(p);
  }
  return out;
}

[[nodiscard]] auto z_brute(std::string_view const s) -> std::vector<std::size_t> {
  auto const n{s.size()};
  auto z{std::vector<std::size_t>(n, 0)};
  if (n == 0) {
    return z;
  }
  z[0] = n;
  for (auto i{std::size_t{1}}; i < n; ++i) {
    auto k{std::size_t{0}};
    while (i + k < n && s[k] == s[i + k]) {
      ++k;
    }
    z[i] = k;
  }
  return z;
}

[[nodiscard]] auto
levenshtein_full(std::string_view const a, std::string_view const b) -> std::size_t {
  auto d{std::vector<std::vector<std::size_t>>(a.size() + 1, std::vector<std::size_t>(b.size() + 1))
  };
  for (auto i{std::size_t{0}}; i <= a.size(); ++i) {
    d[i][0] = i;
  }
  for (auto j{std::size_t{0}}; j <= b.size(); ++j) {
    d[0][j] = j;
  }
  for (auto i{std::size_t{1}}; i <= a.size(); ++i) {
    for (auto j{std::size_t{1}}; j <= b.size(); ++j) {
      auto const cost{a[i - 1] == b[j - 1] ? std::size_t{0} : std::size_t{1}};
      d[i][j] = std::min({d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + cost});
    }
  }
  return d[a.size()][b.size()];
}

// kmp_find / boyer_moore_find

TEST_CASE("nexenne::algorithm single-pattern search known answers") {
  CHECK(alg::kmp_find("hello world", "world") == 6);
  CHECK(alg::boyer_moore_find("hello world", "world") == 6);
  CHECK(alg::kmp_find("abc", "abc") == 0);
  CHECK(alg::kmp_find("abc", "") == 0);  // empty needle
  CHECK(alg::boyer_moore_find("abc", "") == 0);
  CHECK(alg::kmp_find("abc", "abcd") == std::string_view::npos);  // needle longer
  CHECK(alg::kmp_find("abcabc", "x") == std::string_view::npos);  // absent
  CHECK(alg::kmp_find("aaaaaa", "aaa") == 0);                     // periodic
  CHECK(alg::boyer_moore_find("aaaaaa", "aaab") == std::string_view::npos);
}

TEST_CASE("nexenne::algorithm kmp_find and boyer_moore_find match std::find") {
  auto gen{lcg{}};
  for (auto trial{0}; trial < 4000; ++trial) {
    auto const hay{random_string(gen, gen.next() % 40, 3)};
    // Half the time draw the needle as a real substring to force hits.
    auto need{std::string{}};
    if (!hay.empty() && (gen.next() & 1u)) {
      auto const start{gen.next() % hay.size()};
      auto const len{gen.next() % (hay.size() - start + 1)};
      need = hay.substr(start, len);
    } else {
      need = random_string(gen, gen.next() % 6, 3);
    }
    auto const expected{std::string_view{hay}.find(need)};
    CHECK(alg::kmp_find(hay, need) == expected);
    CHECK(alg::boyer_moore_find(hay, need) == expected);
  }
}

// z_function / find_all

TEST_CASE("nexenne::algorithm::z_function matches the brute-force reference") {
  auto gen{lcg{}};
  for (auto trial{0}; trial < 2000; ++trial) {
    auto const s{random_string(gen, gen.next() % 50, 3)};
    CHECK(alg::z_function(s) == z_brute(s));
  }
  CHECK(alg::z_function("").empty());
}

TEST_CASE("nexenne::algorithm all-match searches find every overlapping occurrence") {
  auto gen{lcg{}};
  for (auto trial{0}; trial < 3000; ++trial) {
    auto const hay{random_string(gen, gen.next() % 40, 2)};  // binary alphabet: dense overlaps
    auto need{std::string{}};
    if (!hay.empty() && (gen.next() & 1u)) {
      auto const start{gen.next() % hay.size()};
      need = hay.substr(start, 1 + gen.next() % 3);
    } else {
      need = random_string(gen, 1 + gen.next() % 3, 2);
    }
    auto const expected{find_all_brute(hay, need)};

    CHECK(alg::z_find_all(hay, need) == expected);

    auto kmp_hits{std::vector<std::size_t>{}};
    alg::kmp_find_all(hay, need, [&](std::size_t const p) { kmp_hits.push_back(p); });
    CHECK(kmp_hits == expected);
  }
}

TEST_CASE("nexenne::algorithm::kmp_find_all overlapping and early termination") {
  CHECK(alg::z_find_all("aaaa", "aa") == std::vector<std::size_t>{0, 1, 2});
  auto hits{std::vector<std::size_t>{}};
  alg::kmp_find_all("aaaa", "aa", [&](std::size_t const p) {
    hits.push_back(p);
    return hits.size() < 2;  // stop after two matches
  });
  CHECK(hits == std::vector<std::size_t>{0, 1});
}

// levenshtein

TEST_CASE("nexenne::algorithm::levenshtein known answers") {
  CHECK(alg::levenshtein("kitten", "sitting") == 3);
  CHECK(alg::levenshtein("flaw", "lawn") == 2);
  CHECK(alg::levenshtein("", "abc") == 3);
  CHECK(alg::levenshtein("abc", "abc") == 0);
  CHECK(alg::levenshtein("abc", "xyz") == 3);
}

TEST_CASE("nexenne::algorithm::levenshtein matches the full-matrix reference and is symmetric") {
  auto gen{lcg{}};
  for (auto trial{0}; trial < 3000; ++trial) {
    auto const a{random_string(gen, gen.next() % 18, 3)};
    auto const b{random_string(gen, gen.next() % 18, 3)};
    auto const d{alg::levenshtein(a, b)};
    CHECK(d == levenshtein_full(a, b));
    CHECK(d == alg::levenshtein(b, a));  // symmetric
  }
}

// suffix_array / lcp

TEST_CASE("nexenne::algorithm::build_suffix_array known answer (banana)") {
  auto const sa{alg::build_suffix_array("banana")};
  CHECK(sa == std::vector<std::int32_t>{5, 3, 1, 0, 4, 2});
}

TEST_CASE("nexenne::algorithm suffix array is a sorted permutation with a correct LCP") {
  auto gen{lcg{}};
  for (auto trial{0}; trial < 1500; ++trial) {
    auto const text{random_string(gen, gen.next() % 40, 3)};
    auto const sa{alg::build_suffix_array(text)};
    auto const n{text.size()};
    REQUIRE(sa.size() == n);

    // Permutation of [0, n) and adjacent suffixes are in lexicographic order.
    auto seen{std::vector<char>(n, 0)};
    for (auto const idx : sa) {
      REQUIRE(idx >= 0);
      REQUIRE(static_cast<std::size_t>(idx) < n);
      seen[static_cast<std::size_t>(idx)] = 1;
    }
    CHECK(std::ranges::count(seen, 1) == static_cast<std::ptrdiff_t>(n));
    for (auto i{std::size_t{1}}; i < n; ++i) {
      auto const prev{std::string_view{text}.substr(static_cast<std::size_t>(sa[i - 1]))};
      auto const curr{std::string_view{text}.substr(static_cast<std::size_t>(sa[i]))};
      CHECK(prev <= curr);
    }

    // LCP matches a brute-force common-prefix length of adjacent suffixes.
    auto const lcp{alg::build_lcp(text, std::span<std::int32_t const>{sa})};
    REQUIRE(lcp.size() == n);
    if (n > 0) {
      CHECK(lcp[0] == 0);
    }
    for (auto i{std::size_t{1}}; i < n; ++i) {
      auto const a{std::string_view{text}.substr(static_cast<std::size_t>(sa[i - 1]))};
      auto const b{std::string_view{text}.substr(static_cast<std::size_t>(sa[i]))};
      auto k{std::size_t{0}};
      while (k < a.size() && k < b.size() && a[k] == b[k]) {
        ++k;
      }
      CHECK(static_cast<std::size_t>(lcp[i]) == k);
    }
  }
}

// aho_corasick

TEST_CASE("nexenne::algorithm::aho_corasick classic dictionary (he/she/his/hers)") {
  auto m{alg::aho_corasick{}};
  auto const he{m.add_pattern("he")};
  auto const she{m.add_pattern("she")};
  auto const his{m.add_pattern("his")};
  auto const hers{m.add_pattern("hers")};
  m.build();
  CHECK(m.pattern_count() == 4);
  CHECK(m.pattern_length(hers) == 4);
  CHECK(m.pattern_length(his) == 3);  // present in the dictionary, absent from "ushers"

  auto got{std::vector<std::pair<std::size_t, std::size_t>>{}};
  m.scan("ushers", [&](std::size_t const id, std::size_t const end) { got.push_back({id, end}); });
  std::ranges::sort(got);
  // "ushers": she@1..4, he@2..4, hers@2..6.
  auto want{std::vector<std::pair<std::size_t, std::size_t>>{{she, 4}, {he, 4}, {hers, 6}}};
  std::ranges::sort(want);
  CHECK(got == want);
}

TEST_CASE("nexenne::algorithm::aho_corasick matches brute force for many patterns") {
  auto gen{lcg{}};
  for (auto trial{0}; trial < 600; ++trial) {
    auto const text{random_string(gen, gen.next() % 60, 3)};
    auto patterns{std::vector<std::string>{}};
    auto const count{1 + gen.next() % 5};
    auto m{alg::aho_corasick{}};
    for (auto p{std::uint64_t{0}}; p < count; ++p) {
      auto pat{random_string(gen, 1 + gen.next() % 4, 3)};  // non-empty
      patterns.push_back(pat);
      m.add_pattern(pat);
    }
    m.build();

    auto got{std::vector<std::pair<std::size_t, std::size_t>>{}};
    m.scan(text, [&](std::size_t const id, std::size_t const end) { got.push_back({id, end}); });
    std::ranges::sort(got);

    auto want{std::vector<std::pair<std::size_t, std::size_t>>{}};
    for (auto id{std::size_t{0}}; id < patterns.size(); ++id) {
      for (auto const start : find_all_brute(text, patterns[id])) {
        want.push_back({id, start + patterns[id].size()});
      }
    }
    std::ranges::sort(want);
    CHECK(got == want);
  }
}

TEST_CASE("nexenne::algorithm::aho_corasick early termination and unbuilt matcher") {
  auto m{alg::aho_corasick{}};
  m.add_pattern("a");
  m.build();
  auto count{0};
  m.scan("aaaa", [&](std::size_t, std::size_t) {
    ++count;
    return count < 2;  // stop after two
  });
  CHECK(count == 2);

  auto unbuilt{alg::aho_corasick{}};
  unbuilt.add_pattern("a");
  auto reported{false};
  unbuilt.scan("aaa", [&](std::size_t, std::size_t) { reported = true; });
  CHECK(!reported);  // never built, reports nothing
}

TEST_CASE("nexenne::algorithm::levenshtein degenerate and known distances") {
  CHECK(alg::levenshtein("", "") == 0);
  CHECK(alg::levenshtein("abc", "") == 3);            // all deletions
  CHECK(alg::levenshtein("", "abc") == 3);            // all insertions
  CHECK(alg::levenshtein("abc", "abc") == 0);         // identical
  CHECK(alg::levenshtein("a", "b") == 1);             // single substitution
  CHECK(alg::levenshtein("kitten", "sitting") == 3);  // textbook value
  CHECK(alg::levenshtein("flaw", "lawn") == 2);
  CHECK(alg::levenshtein("aaaaa", "aaa") == 2);  // repeated characters
}

TEST_CASE("nexenne::algorithm all-match search on degenerate strings") {
  using vec = std::vector<std::size_t>;
  CHECK(alg::z_find_all("ab", "abc") == vec{});            // needle longer than haystack
  CHECK(alg::z_find_all("aaaa", "a") == vec{0, 1, 2, 3});  // every position matches
  CHECK(alg::z_find_all("aaaa", "aaaa") == vec{0});        // whole-string match
  CHECK(alg::z_find_all("abcabc", "xyz") == vec{});        // no match
  auto kmp_hits{vec{}};
  alg::kmp_find_all("aaaa", "aa", [&](std::size_t const p) { kmp_hits.push_back(p); });
  CHECK(kmp_hits == vec{0, 1, 2});  // kmp agrees with z on the periodic case
}

}  // namespace
