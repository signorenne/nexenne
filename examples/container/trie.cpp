/**
 * @file
 * @brief trie as an autocomplete dictionary: prefix queries over shared paths.
 *
 * Words sharing a prefix share trie nodes, so membership and prefix tests cost
 * O(key length) and the structure answers "does any word start with this?"
 * directly.
 */

#include <print>
#include <span>
#include <string>

#include <nexenne/container/trie.hpp>

namespace {

namespace cn = nexenne::container;

}  // namespace

auto main() -> int {
  using namespace std::string_literals;
  cn::trie<char, int> dict;  // word -> frequency
  dict.insert("car"s, 3);
  dict.insert("card"s, 1);
  dict.insert("care"s, 5);
  dict.insert("dog"s, 2);

  std::println("words: {}", dict.size());
  std::println("has 'care': {}", dict.contains("care"s));
  std::println("any word starts with 'car': {}", dict.starts_with("car"s));
  std::println("any word starts with 'cat': {}", dict.starts_with("cat"s));

  std::print("words under prefix walk:");
  dict.for_each([](std::span<char const> key, int freq) {
    std::print(" {}({})", std::string{key.begin(), key.end()}, freq);
  });
  std::println("");
  // words: 4
  // has 'care': true
  // any word starts with 'car': true
  // any word starts with 'cat': false
  // words under prefix walk: car(3) card(1) care(5) dog(2)
  return 0;
}
