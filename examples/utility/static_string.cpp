/**
 * @file
 * @brief Tag a strong-typed unit at compile time with static_string.
 */

#include <print>
#include <unordered_map>

#include <nexenne/utility/static_string.hpp>

// static_string is structural, so it can be a non-type template parameter.
// Here it tags a strong-typed unit at compile time: the unit symbol lives in
// the type, and the symbol is recovered at runtime through view()/formatter.
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
  // Compile-time concatenation builds a derived unit symbol.
  constexpr auto metre{nexenne::utility::static_string{"m"}};
  constexpr auto per_s{nexenne::utility::static_string{"/s"}};
  constexpr auto speed_sym{metre + per_s};
  static_assert(speed_sym.view() == "m/s");
  static_assert(speed_sym.size() == 3);

  auto const distance{quantity<"m">{42.0}};
  auto const mass{quantity<"kg">{7.5}};

  // The formatter specialisation lets a static_string drop straight into format.
  std::println("{:>6.1f} {}", distance.value, decltype(distance)::symbol());
  std::println("{:>6.1f} {}", mass.value, decltype(mass)::symbol());
  std::println("derived unit: {}", speed_sym);

  // std::hash specialisation makes it a usable key ("water" is static_string<6>).
  auto density{std::unordered_map<nexenne::utility::static_string<6>, double>{}};
  density.emplace(nexenne::utility::static_string{"water"}, 1000.0);
  density.emplace(nexenne::utility::static_string{"steel"}, 7850.0);
  std::println("density[water] = {}", density.at(nexenne::utility::static_string{"water"}));

  return 0;
}
