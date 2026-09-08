/**
 * @file
 * @brief Brand quantities and identifiers with nexenne::utility::strong_typedef.
 *
 * A tour of the opt-in model: a tag makes a type distinct, and the ability
 * bitmask grants exactly the operators that make sense for it. The example:
 *
 *   1. adds two lengths and scales one by a scalar; the results stay
 *      \c quantity types, and adding a length to a duration does not compile
 *      because the two tags are unrelated;
 *   2. divides an image size by a page size, a ratio that yields a bare
 *      unsigned integer, and wraps it back into a distinct page-count unit;
 *   3. compares two device ids, an \c identifier that compares and hashes but
 *      has no arithmetic, so adding them does not compile;
 *   4. formats every wrapper straight through its underlying value.
 */

#include <cstdint>
#include <print>

#include <nexenne/utility/format.hpp>
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
  auto const doubled{distance * 2.0};                 // ok: scalar scale
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
