/**
 * @file
 * @brief Tag a strong-typed unit at compile time with static_string.
 *
 * \c static_string is structural, so it can be a non-type template parameter:
 * here it tags a unit, the symbol lives in the type, and the symbol is recovered
 * at runtime through \c view or the formatter. The example:
 *
 *   1. concatenates two symbols at compile time into a derived unit;
 *   2. shows that the template argument is a capacity bound, not the length:
 *      \c size scans to the first NUL, so a literal holding "ab", a NUL, and
 *      "cd" (capacity 6) has size 2, and a default-constructed instance is
 *      empty whatever its capacity;
 *   3. formats quantities with their symbols through the formatter;
 *   4. uses \c static_string as an unordered_map key through its \c std::hash
 *      specialisation ("water" has capacity 6).
 */

#include <print>
#include <unordered_map>

#include <nexenne/utility/format.hpp>
#include <nexenne/utility/static_string.hpp>

namespace {

template <nexenne::utility::static_string Symbol>
struct quantity {
  double value{0.0};

  [[nodiscard]] static constexpr auto symbol() noexcept {
    return Symbol.view();
  }
};

}  // namespace

auto main() -> int {
  constexpr auto metre{nexenne::utility::static_string{"m"}};
  constexpr auto per_s{nexenne::utility::static_string{"/s"}};
  constexpr auto speed_sym{metre + per_s};
  static_assert(speed_sym.view() == "m/s");
  static_assert(speed_sym.size() == 3);

  constexpr auto padded{nexenne::utility::static_string{"ab\0cd"}};
  static_assert(padded.size() == 2);
  static_assert(padded.view() == "ab");
  constexpr auto blank{nexenne::utility::static_string<16>{}};
  static_assert(blank.empty() && blank.size() == 0);

  auto const distance{quantity<"m">{42.0}};
  auto const mass{quantity<"kg">{7.5}};

  std::println("{:>6.1f} {}", distance.value, decltype(distance)::symbol());
  std::println("{:>6.1f} {}", mass.value, decltype(mass)::symbol());
  std::println("derived unit: {}", speed_sym);

  auto density{std::unordered_map<nexenne::utility::static_string<6>, double>{}};
  density.emplace(nexenne::utility::static_string{"water"}, 1000.0);
  density.emplace(nexenne::utility::static_string{"steel"}, 7850.0);
  std::println("density[water] = {}", density.at(nexenne::utility::static_string{"water"}));

  return 0;
}
