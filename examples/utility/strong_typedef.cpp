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

using meters = quantity<struct meters_tag, double>;
using seconds = quantity<struct seconds_tag, double>;

using bytes = quantity<struct bytes_tag, std::uint32_t>;
using pages = quantity<struct pages_tag, std::uint32_t>;

using device_id = identifier<struct device_id_tag, std::uint16_t>;

}  // namespace

auto main() -> int {
  auto const distance{meters{150.0} + meters{50.0}};
  auto const doubled{distance * 2.0};
  auto const elapsed{seconds{12.0}};

  std::println("distance: {} m", distance.get());
  std::println("doubled:  {} m", doubled.get());
  std::println("elapsed:  {} s", elapsed.get());

  auto const image{bytes{4096}};
  auto const page{bytes{512}};
  auto const span{pages{image / page}};
  std::println("image spans {} pages", span.get());

  auto const a{device_id{0x10}};
  auto const b{device_id{0x20}};
  std::println("device {} < device {}: {}", a.get(), b.get(), a < b);

  return 0;
}
