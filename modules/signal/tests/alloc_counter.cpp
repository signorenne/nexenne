#include "alloc_counter.hpp"

#include <cstddef>
#include <cstdlib>
#include <new>

auto operator new(std::size_t const size) -> void* {
  ++signal_tests::alloc_counter::allocations;
  signal_tests::alloc_counter::bytes += size;
  // [new.delete]: even a zero-size request must return a non-null, distinct pointer.
  auto* const p{__builtin_malloc(size == 0 ? std::size_t{1} : size)};
  if (p == nullptr) {
    throw std::bad_alloc{};
  }
  return p;
}

auto operator delete(void* const ptr) noexcept -> void {
  if (ptr != nullptr) {
    ++signal_tests::alloc_counter::deallocations;
    __builtin_free(ptr);
  }
}

auto operator delete(void* const ptr, std::size_t) noexcept -> void {
  ::operator delete(ptr);
}

// The aligned forms too: small_vector allocates through them, bypassing the plain one above.
auto operator new(std::size_t const size, std::align_val_t const align) -> void* {
  ++signal_tests::alloc_counter::allocations;
  signal_tests::alloc_counter::bytes += size;
  // aligned_alloc wants a size that is a whole multiple of the alignment.
  auto const alignment{static_cast<std::size_t>(align)};
  auto const rounded{(size + alignment - 1) / alignment * alignment};
  auto* const p{std::aligned_alloc(alignment, rounded == 0 ? alignment : rounded)};
  if (p == nullptr) {
    throw std::bad_alloc{};
  }
  return p;
}

auto operator delete(void* const ptr, std::align_val_t) noexcept -> void {
  ::operator delete(ptr);
}

auto operator delete(void* const ptr, std::size_t, std::align_val_t) noexcept -> void {
  ::operator delete(ptr);
}
