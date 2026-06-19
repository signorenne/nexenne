/**
 * @file
 * @brief Tests for nexenne::container::trie.
 */

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <random>
#include <set>
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

TEST_CASE("nexenne::container::trie the empty trie") {
  trie_t t;
  CHECK(t.empty());
  CHECK(t.size() == 0);
  CHECK(t.find("x"s) == nullptr);
  CHECK_FALSE(t.contains("x"s));
  CHECK_FALSE(t.erase("x"s));
  CHECK(t.starts_with(""s));  // empty prefix always matches the root
  CHECK_FALSE(t.starts_with("a"s));
  CHECK_FALSE(t.contains(""s));  // no value stored at the empty key
  CHECK(trie_t::max_size() > 0);
  int visited{0};
  t.for_each([&](std::span<char const>, int) { ++visited; });
  CHECK(visited == 0);
}

TEST_CASE("nexenne::container::trie the empty-string key holds its own value") {
  trie_t t;
  CHECK(t.insert(""s, 7));  // empty key is a real, distinct entry
  CHECK(t.size() == 1);
  CHECK(t.contains(""s));
  REQUIRE(t.find(""s) != nullptr);
  CHECK(*t.find(""s) == 7);
  CHECK(t.insert("a"s, 1));  // coexists with non-empty keys
  CHECK(t.size() == 2);
  CHECK_FALSE(t.insert(""s, 8));  // replace
  CHECK(*t.find(""s) == 8);
  CHECK(t.erase(""s));
  CHECK_FALSE(t.contains(""s));
  CHECK(t.contains("a"s));  // erasing the empty key keeps others
  CHECK(t.size() == 1);
}

TEST_CASE("nexenne::container::trie a key that is a strict prefix of another") {
  trie_t t;
  t.insert("car"s, 1);
  t.insert("card"s, 2);  // "car" is a prefix of "card"
  CHECK(t.contains("car"s));
  CHECK(t.contains("card"s));
  // erasing the shorter key must not break the longer one that lives past it
  CHECK(t.erase("car"s));
  CHECK_FALSE(t.contains("car"s));
  CHECK(t.contains("card"s));
  CHECK(*t.find("card"s) == 2);
  // and erasing the longer key after that prunes cleanly
  CHECK(t.erase("card"s));
  CHECK(t.empty());
  CHECK_FALSE(t.starts_with("c"s));
}

TEST_CASE("nexenne::container::trie erasing a deep key keeps unrelated siblings intact") {
  trie_t t;
  for (auto const& [k, v] : std::vector<std::pair<std::string, int>>{
         {"apple", 1}, {"apply", 2}, {"apt", 3}, {"banana", 4}
       }) {
    t.insert(k, v);
  }
  CHECK(t.erase("apple"s));
  CHECK_FALSE(t.contains("apple"s));
  CHECK(t.contains("apply"s));   // shares "appl"
  CHECK(t.contains("apt"s));     // shares "ap"
  CHECK(t.contains("banana"s));  // unrelated branch
  CHECK(t.size() == 3);
  CHECK(t.starts_with("appl"s));  // "apply" still keeps the shared prefix alive
}

TEST_CASE("nexenne::container::trie const find overload") {
  trie_t t;
  t.insert("hi"s, 5);
  trie_t const& ct{t};
  REQUIRE(ct.find("hi"s) != nullptr);
  CHECK(*ct.find("hi"s) == 5);
  CHECK(ct.find("ho"s) == nullptr);
}

TEST_CASE("nexenne::container::trie const for_each visits entries") {
  trie_t t;
  t.insert("a"s, 1);
  t.insert("bb"s, 2);
  trie_t const& ct{t};
  int sum{0};
  ct.for_each([&](std::span<char const>, int const& v) { sum += v; });
  CHECK(sum == 3);
}

TEST_CASE("nexenne::container::trie clear and swap") {
  trie_t a;
  a.insert("a"s, 1);
  a.insert("ab"s, 2);
  a.clear();
  CHECK(a.empty());
  CHECK_FALSE(a.contains("a"s));
  a.insert("z"s, 9);  // usable after clear

  trie_t b;
  b.insert("x"s, 7);
  swap(a, b);  // friend swap
  CHECK(a.contains("x"s));
  CHECK(b.contains("z"s));
  a.swap(b);  // member swap
  CHECK(a.contains("z"s));
  CHECK(b.contains("x"s));
}

TEST_CASE("nexenne::container::trie copy assignment and self-assignment") {
  trie_t a;
  a.insert("cat"s, 1);
  a.insert("car"s, 2);
  trie_t b;
  b.insert("dog"s, 9);
  b = a;  // replaces b's content with a deep clone
  CHECK(a == b);
  b.insert("cart"s, 3);
  CHECK_FALSE(a.contains("cart"s));  // independent
  CHECK_FALSE(a.contains("dog"s));

  auto const& alias{a};
  a = alias;  // NOLINT: self copy-assignment is a no-op
  CHECK(a.contains("cat"s));
  CHECK(a.size() == 2);
}

TEST_CASE("nexenne::container::trie move assignment") {
  trie_t a;
  a.insert("hi"s, 1);
  trie_t b;
  b.insert("bye"s, 2);
  b = std::move(a);
  CHECK(b.contains("hi"s));
  CHECK_FALSE(b.contains("bye"s));
  CHECK(a.empty());    // NOLINT: moved-from reset to usable empty trie
  a.insert("ok"s, 3);  // still usable
  CHECK(a.contains("ok"s));
}

TEST_CASE("nexenne::container::trie keyed by raw bytes (uint8_t tokens)") {
  cn::trie<std::uint8_t, int> t;
  std::vector<std::uint8_t> const a{0x00, 0xFF, 0x80};  // includes the zero byte
  std::vector<std::uint8_t> const b{0x00, 0xFF};        // a prefix of a
  CHECK(t.insert(a, 1));
  CHECK(t.insert(b, 2));
  CHECK(*t.find(a) == 1);
  CHECK(*t.find(b) == 2);
  CHECK(t.starts_with(std::vector<std::uint8_t>{0x00}));
  CHECK(t.size() == 2);
  CHECK(t.erase(b));
  CHECK(t.contains(a));  // erasing the prefix keeps the longer byte key
}

TEST_CASE("nexenne::container::trie of non-trivial std::string values") {
  cn::trie<char, std::string> t;
  CHECK(t.insert("greet"s, std::string("hello")));
  CHECK(t.insert("greeting"s, std::string("howdy")));  // shares prefix "greet"
  CHECK(*t.find("greet"s) == "hello");
  CHECK(*t.find("greeting"s) == "howdy");
  cn::trie<char, std::string> clone{t};  // copy exercised under LSan
  CHECK(clone == t);
  CHECK(t.erase("greet"s));
  CHECK(t.contains("greeting"s));
  CHECK(clone.contains("greet"s));  // clone independent
}

TEST_CASE("nexenne::container::trie differential against std::set<std::string> under random ops") {
  std::mt19937 rng{777};
  std::uniform_int_distribution<int> len{0, 4};
  std::uniform_int_distribution<int> ch{'a', 'd'};  // small alphabet -> shared prefixes
  std::uniform_int_distribution<int> op{0, 2};
  auto const make_key{[&]() -> std::string {
    std::string s;
    int const n{len(rng)};
    for (int i{0}; i < n; ++i) {
      s.push_back(static_cast<char>(ch(rng)));
    }
    return s;
  }};
  cn::trie<char, int> t;
  std::set<std::string> ref;
  for (int step{0}; step < 5000; ++step) {
    auto const key{make_key()};
    switch (op(rng)) {
      case 0: {
        bool const fresh{t.insert(key, step)};
        bool const refFresh{ref.insert(key).second};
        CHECK(fresh == refFresh);
        break;
      }
      case 1: {
        bool const removed{t.erase(key)};
        bool const refRemoved{ref.erase(key) == 1};
        CHECK(removed == refRemoved);
        break;
      }
      default:
        CHECK(t.contains(key) == (ref.count(key) == 1));
        break;
    }
    CHECK(t.size() == ref.size());
  }
  // the set of stored keys must match the reference exactly
  std::set<std::string> harvested;
  t.for_each([&](std::span<char const> k, int) { harvested.emplace(k.begin(), k.end()); });
  CHECK(harvested == ref);
}

TEST_CASE("nexenne::container::trie erase prunes only the dead path") {
  trie_t t;
  t.insert("ab"s, 1);  // holds a value
  t.insert("abc"s, 2);
  t.insert("abd"s, 3);
  t.insert("xyz"s, 4);  // unrelated branch

  // Drop only node 'c': 'b' survives (it holds a value and still has child 'd'),
  // and the unrelated "xyz" must be untouched.
  CHECK(t.erase("abc"s));
  CHECK_FALSE(t.contains("abc"s));
  CHECK(t.contains("ab"s));
  CHECK(t.contains("abd"s));
  CHECK(t.contains("xyz"s));
  CHECK(t.size() == 3);

  // Clearing "ab"'s value keeps the node alive because of the 'd' child.
  CHECK(t.erase("ab"s));
  CHECK_FALSE(t.contains("ab"s));
  CHECK(t.contains("abd"s));
  CHECK(t.size() == 2);

  // Removing the last key under the "ab" prefix collapses the whole chain.
  CHECK(t.erase("abd"s));
  CHECK_FALSE(t.contains("abd"s));
  CHECK(t.contains("xyz"s));
  CHECK(t.size() == 1);

  // A long, sibling-free key is fully reclaimed and can be re-inserted fresh.
  t.insert("abcdefgh"s, 9);
  CHECK(t.erase("abcdefgh"s));
  CHECK(t.insert("abcdefgh"s, 10));  // fresh insertion => the chain was clean
  CHECK(t.size() == 2);
}

TEST_CASE("nexenne::container::trie deep keys do not overflow the stack") {
  // A single very long key makes the trie 100k nodes deep. Copy, traversal,
  // equality, erase, and teardown must all be iterative; recursion to this depth
  // would overflow the stack.
  auto const deep{std::string(100000, 'a')};
  {
    trie_t t;
    CHECK(t.insert(deep, 1));
    CHECK(t.insert(deep + "b", 2));  // branches one level deeper
    CHECK(t.size() == 2);

    trie_t copy{t};  // clone_subtree
    CHECK(copy.size() == 2);
    CHECK(copy == t);  // nodes_equal

    auto count{0};
    copy.for_each([&](std::span<char const>, int) { count += 1; });  // for_each_impl
    CHECK(count == 2);

    CHECK(copy.contains(deep));
    CHECK(copy.erase(deep));  // path-local erase down the long chain
    CHECK(copy.size() == 1);
    CHECK(copy != t);
  }  // iterative destructors run here
}

}  // namespace
