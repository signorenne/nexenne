/**
 * @file
 * @brief small_vector: inline storage for the common small case, heap when it
 *        grows past the inline capacity.
 *
 * small_vector<T, N> keeps its first N elements in an inline buffer - zero heap
 * traffic while the size stays at or below N - and migrates to heap storage,
 * std::vector-style, only when it grows past N. This tour collects character
 * positions to show the inline/heap boundary, then walks the rest of the
 * vector-like surface: capacity growth, span/ranges interop, checked pop_back,
 * and shrink_to_fit migrating back inline.
 */

#include <cstddef>
#include <numeric>
#include <print>
#include <span>
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

// span() exposes the contiguous live range, so a small_vector drops straight
// into any std::span-taking algorithm with no copy.
auto total(std::span<std::size_t const> const xs) -> std::size_t {
  return std::accumulate(xs.begin(), xs.end(), std::size_t{0});
}

}  // namespace

auto main() -> int {
  auto few{find_all("hello world", 'o')};  // 2 hits, fits inline
  std::println("'o' appears {} times, inline storage: {}", few.size(), few.is_inline());

  auto many{find_all("aaaaaaaaaaaa", 'a')};  // 12 hits, spills to heap
  std::println("'a' appears {} times, inline storage: {}", many.size(), many.is_inline());

  // Indexing and iteration are the same as std::vector's.
  std::print("'o' positions:");
  for (std::size_t const pos : few) {
    std::print(" {}", pos);
  }
  std::println("");
  std::println("sum of 'a' positions (via std::span): {}", total(many.span()));

  // Watch the inline -> heap transition slot by slot. Capacity holds at the
  // inline N until the push that overflows it, then grows geometrically on the
  // heap; once on the heap it stays there until shrink_to_fit is called.
  cn::small_vector<int, 4> v;
  for (int i{0}; i < 6; ++i) {
    v.push_back(i);
    std::println(
      "  push {}: size {}, capacity {}, inline {}", i, v.size(), v.capacity(), v.is_inline()
    );
  }

  // pop_back is checked: popping an empty vector reports an error rather than
  // being undefined, the module's policy for fallible state changes.
  while (v.pop_back()) {
    // drop every element
  }
  std::println("after draining: empty {}, capacity retained {}", v.empty(), v.capacity());

  // The vector is empty but still owns its heap block; shrink_to_fit releases it
  // and migrates back into the inline buffer.
  v.shrink_to_fit();
  std::println("after shrink_to_fit: inline {}, capacity {}", v.is_inline(), v.capacity());
  // 'o' appears 2 times, inline storage: true
  // 'a' appears 12 times, inline storage: false
  // 'o' positions: 4 7
  // sum of 'a' positions (via std::span): 66
  //   push 0: size 1, capacity 4, inline true
  //   push 1: size 2, capacity 4, inline true
  //   push 2: size 3, capacity 4, inline true
  //   push 3: size 4, capacity 4, inline true
  //   push 4: size 5, capacity 8, inline false
  //   push 5: size 6, capacity 8, inline false
  // after draining: empty true, capacity retained 8
  // after shrink_to_fit: inline true, capacity 4
  return 0;
}
