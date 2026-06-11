/**
 * @file
 * @brief stable_vector keeps raw pointers valid across growth.
 *
 * A pointer taken from a std::vector dangles the moment the vector reallocates.
 * stable_vector never moves an existing element, so a pointer stays good for the
 * element's whole lifetime, no matter how much the container grows afterwards.
 */

#include <print>

#include <nexenne/container/stable_vector.hpp>

namespace {

namespace cn = nexenne::container;

}  // namespace

auto main() -> int {
  cn::stable_vector<int, 4> pool;
  int* const first{pool.push_back(100)};  // keep a raw pointer to the first element

  for (int i{0}; i < 1000; ++i) {
    pool.push_back(i);  // grows across many chunks; nothing is relocated
  }

  std::println("first element via the saved pointer: {}", *first);
  std::println(
    "size {}, chunks {}, pointer still valid: {}",
    pool.size(),
    pool.chunk_count(),
    first == &pool[0]
  );
  // first element via the saved pointer: 100
  // size 1001, chunks 251, pointer still valid: true
  return 0;
}
