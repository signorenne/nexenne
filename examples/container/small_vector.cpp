/**
 * @file
 * @brief small_vector: inline storage for the common small case, heap when it
 *        grows past the inline capacity.
 *
 * Collect the positions of a character in a string. Most inputs have few hits,
 * so the result stays in the inline buffer with no allocation; a pathological
 * input spills to the heap, transparently.
 */

#include <cstddef>
#include <print>
#include <string_view>

#include <nexenne/container/small_vector.hpp>

namespace {

namespace cn = nexenne::container;

auto find_all(std::string_view const text, char const target) -> cn::small_vector<std::size_t, 8> {
  cn::small_vector<std::size_t, 8> hits;
  for (std::size_t i{0}; i < text.size(); ++i) {
    if (text[i] == target) {
      hits.push_back(i);
    }
  }
  return hits;
}

}  // namespace

auto main() -> int {
  auto const few{find_all("hello world", 'o')};  // 2 hits, fits inline
  std::println("'o' appears {} times, inline storage: {}", few.size(), few.is_inline());

  auto const many{find_all("aaaaaaaaaaaa", 'a')};  // 12 hits, spills to heap
  std::println("'a' appears {} times, inline storage: {}", many.size(), many.is_inline());

  std::print("'o' positions:");
  for (std::size_t const pos : few) {
    std::print(" {}", pos);
  }
  std::println("");
  // 'o' appears 2 times, inline storage: true
  // 'a' appears 12 times, inline storage: false
  // 'o' positions: 4 7
  return 0;
}
