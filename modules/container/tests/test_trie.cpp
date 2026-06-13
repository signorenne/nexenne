/**
 * @file
 * @brief Tests for nexenne::container::trie.
 */

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <nexenne/container/trie.hpp>

namespace {

namespace cn = nexenne::container;
using trie_t = cn::trie<char, int>;
using namespace std::string_literals;

TEST_CASE("nexenne::container::trie insert fresh vs replace, find, contains") {
  trie_t t;
  CHECK(t.insert("cat"s, 1));         // fresh
  CHECK(t.insert("car"s, 2));         // shares prefix "ca"
  CHECK_FALSE(t.insert("cat"s, 99));  // replace, returns false
  CHECK(t.size() == 2);
  REQUIRE(t.find("cat"s) != nullptr);
  CHECK(*t.find("cat"s) == 99);  // replaced value
  CHECK(*t.find("car"s) == 2);
  CHECK(t.find("ca"s) == nullptr);  // prefix, but no value stored there
  CHECK(t.contains("car"s));
  CHECK_FALSE(t.contains("dog"s));
}

TEST_CASE("nexenne::container::trie starts_with matches stored prefixes") {
  trie_t t;
  t.insert("cat"s, 1);
  t.insert("cart"s, 2);
  CHECK(t.starts_with("ca"s));
  CHECK(t.starts_with("car"s));
  CHECK(t.starts_with("cat"s));
  CHECK(t.starts_with(""s));  // empty prefix always matches a non-empty trie root
  CHECK_FALSE(t.starts_with("dog"s));
  CHECK_FALSE(t.starts_with("cats"s));  // longer than any stored key
}

TEST_CASE("nexenne::container::trie erase removes value and prunes dead nodes") {
  trie_t t;
  t.insert("cat"s, 1);
  t.insert("car"s, 2);
  CHECK(t.erase("cat"s));
  CHECK(t.size() == 1);
  CHECK_FALSE(t.contains("cat"s));
  CHECK(t.contains("car"s));     // sibling survives
  CHECK(t.starts_with("ca"s));   // shared prefix kept (car still there)
  CHECK_FALSE(t.erase("cat"s));  // already gone
  CHECK(t.erase("car"s));
  CHECK(t.empty());
  CHECK_FALSE(t.starts_with("c"s));  // whole branch pruned once empty
}

TEST_CASE("nexenne::container::trie for_each visits every entry once") {
  trie_t t;
  t.insert("a"s, 1);
  t.insert("ab"s, 2);
  t.insert("abc"s, 3);
  t.insert("b"s, 4);
  std::vector<std::pair<std::string, int>> seen;
  t.for_each([&](std::span<char const> key, int v) {
    seen.emplace_back(std::string{key.begin(), key.end()}, v);
  });
  std::ranges::sort(seen);
  std::vector<std::pair<std::string, int>> const expected{
    {"a", 1}, {"ab", 2}, {"abc", 3}, {"b", 4}
  };
  CHECK(seen == expected);
}

TEST_CASE("nexenne::container::trie for_each can mutate values in place") {
  trie_t t;
  t.insert("x"s, 10);
  t.insert("y"s, 20);
  t.for_each([](std::span<char const>, int& v) { v += 1; });
  CHECK(*t.find("x"s) == 11);
  CHECK(*t.find("y"s) == 21);
}

TEST_CASE("nexenne::container::trie deep copy is independent") {
  trie_t a;
  a.insert("cat"s, 1);
  a.insert("car"s, 2);
  trie_t b{a};
  CHECK(a == b);
  b.insert("cart"s, 3);
  b.erase("cat"s);
  CHECK(a.contains("cat"s));  // a unchanged
  CHECK(a.size() == 2);
  CHECK_FALSE(b.contains("cat"s));
  CHECK(b.contains("cart"s));
}

TEST_CASE("nexenne::container::trie move steals the graph") {
  trie_t a;
  a.insert("hi"s, 1);
  trie_t b{std::move(a)};
  CHECK(b.contains("hi"s));
  CHECK(a.empty());    // NOLINT: moved-from is reset to a usable empty trie
  a.insert("ok"s, 2);  // still usable
  CHECK(a.contains("ok"s));
}

TEST_CASE("nexenne::container::trie equality compares keys and values") {
  trie_t a;
  a.insert("a"s, 1);
  a.insert("b"s, 2);
  trie_t b;
  b.insert("b"s, 2);  // different insert order
  b.insert("a"s, 1);
  trie_t c;
  c.insert("a"s, 1);
  c.insert("b"s, 99);  // different value
  CHECK(a == b);
  CHECK(a != c);
}

TEST_CASE("nexenne::container::trie handles wide character keys without a sparse blowup") {
  cn::trie<char32_t, int> t;
  std::u32string const emoji{U"\U0001F600"};  // a single high code point
  std::u32string const word{U"\U0001F600\U0001F4A9"};
  CHECK(t.insert(emoji, 1));
  CHECK(t.insert(word, 2));
  CHECK(*t.find(emoji) == 1);
  CHECK(*t.find(word) == 2);
  CHECK(t.starts_with(emoji));
  CHECK(t.size() == 2);
}

TEST_CASE("nexenne::container::trie holds a move-only value") {
  cn::trie<char, std::unique_ptr<int>> t;
  t.insert("a"s, std::make_unique<int>(1));
  t.insert("ab"s, std::make_unique<int>(2));
  REQUIRE(t.find("a"s) != nullptr);
  CHECK(**t.find("a"s) == 1);
  CHECK(**t.find("ab"s) == 2);
  CHECK(t.erase("a"s));
  CHECK(t.contains("ab"s));  // erasing "a" keeps "ab" reachable
}

}  // namespace
