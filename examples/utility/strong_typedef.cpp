/**
 * @file
 * @brief Brand quantities and identifiers with nexenne::utility::strong_typedef.
 */

#include <cstdint>
#include <print>

#include <nexenne/utility/strong_typedef.hpp>

namespace {

using namespace nexenne::utility;

// Distinct numeric quantities: each has its own tag, so meters and seconds
// never mix even though both wrap a double.
using meters = quantity<struct meters_tag, double>;
using seconds = quantity<struct seconds_tag, double>;

// An opaque identifier: comparable and hashable, but no arithmetic.
using device_id = identifier<struct device_id_tag, std::uint16_t>;

}  // namespace

auto main() -> int {
  auto const distance{meters{150.0}};
  auto const elapsed{seconds{12.0}};

  auto const total{distance + meters{50.0}};  // ok: same tag, arithmetic
  auto const doubled{distance * 2.0};         // ok: scalar scale
  auto const ratio{distance / meters{2.0}};   // dimensionless double

  std::println("total distance: {} m", total.get());
  std::println("doubled: {} m", doubled.get());
  std::println("half-count ratio: {}", ratio);
  std::println("elapsed: {} s", elapsed.get());

  // distance + elapsed;  // ERROR: meters and seconds are unrelated types

  auto const a{device_id{0x10}};
  auto const b{device_id{0x20}};
  std::println("device {} == device {}: {}", a.get(), b.get(), a == b);
  std::println("device {} < device {}: {}", a.get(), b.get(), a < b);
  // a + b;  // ERROR: identifiers have no arithmetic

  return 0;
}
