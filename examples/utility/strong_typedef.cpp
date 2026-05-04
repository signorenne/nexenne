/**
 * @file
 * @brief Brand quantities and identifiers with nexenne::utility::strong_typedef.
 *
 * A tour of the opt-in model: a tag makes a type distinct, and the ability
 * bitmask grants exactly the operators that make sense for it. Here a length
 * and a duration are unit-safe quantities, an image size divided by a page size
 * yields a dimensionless page count (a different unit), device ids only compare,
 * and every wrapper formats straight through its underlying value.
 */

#include <cstdint>
#include <print>

#include <nexenne/utility/strong_typedef.hpp>

namespace {

using namespace nexenne::utility;

// Two numeric quantities. Each carries its own tag, so meters and seconds never
// mix even though both wrap a double. The quantity profile grants arithmetic
// and comparison.
using meters = quantity<struct meters_tag, double>;
using seconds = quantity<struct seconds_tag, double>;

// A byte budget and a dimensionless page count over unsigned integers.
using bytes = quantity<struct bytes_tag, std::uint32_t>;
using pages = quantity<struct pages_tag, std::uint32_t>;

// An opaque identifier: comparable and hashable, but no arithmetic at all.
using device_id = identifier<struct device_id_tag, std::uint16_t>;

}  // namespace

auto main() -> int {
  // Unit-safe arithmetic: same tag adds, a scalar scales, and the result keeps
  // its strong type so it can never be confused with a plain double.
  auto const distance{meters{150.0} + meters{50.0}};  // ok: same tag
  auto const doubled{distance * 2.0};                  // ok: scalar scale
  auto const elapsed{seconds{12.0}};

  std::println("distance: {} m", distance.get());
  std::println("doubled:  {} m", doubled.get());
  std::println("elapsed:  {} s", elapsed.get());

  // distance + elapsed;  // ERROR: meters and seconds are unrelated types

  // A ratio divides two same-tag values into a bare, dimensionless scalar. Wrap
  // that scalar back into a different unit to keep it branded: an image size
  // divided by a page size is a page count.
  auto const image{bytes{4096}};
  auto const page{bytes{512}};
  auto const span{pages{image / page}};  // image / page is a bare std::uint32_t
  std::println("image spans {} pages", span.get());

  // Comparison is opt-in too: identifiers compare and hash but never do math.
  auto const a{device_id{0x10}};
  auto const b{device_id{0x20}};
  std::println("device {} < device {}: {}", a.get(), b.get(), a < b);
  // a + b;  // ERROR: identifiers have no arithmetic

  return 0;
}
