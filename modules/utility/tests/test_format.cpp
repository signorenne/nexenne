/**
 * @file
 * @brief Tests for the nexenne::utility formatters.
 */

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include <nexenne/utility/format.hpp>
#include <nexenne/utility/ignore.hpp>

namespace {

namespace util = nexenne::utility;

enum class bit : std::uint8_t {
  a = 1U << 0U,
  b = 1U << 1U,
};

enum class led : std::uint8_t {
  red = 1U << 0U,
  green = 1U << 2U,
};

[[nodiscard]] constexpr auto to_string(led const l) noexcept -> std::string_view {
  return l == led::red ? "red" : "green";
}

using count = util::strong_typedef<struct count_tag, int>;

TEST_CASE("nexenne::utility::format.hpp alone formats every utility type") {
  CHECK(std::format("{}", util::cobs::error::truncated_input) == "truncated_input");
  CHECK(std::format("{:#x}", util::flags{bit::a} | bit::b) == "0x3");
  CHECK(std::format("{:>4}", util::static_string{"ab"}) == "  ab");
  CHECK(std::format("{:03}", count{7}) == "007");
  CHECK(std::format("{}", util::ability::scale) == "scale");
}

TEST_CASE("nexenne::utility::format.hpp gives every utility type to_string and operator<<") {
  CHECK(util::to_string(util::flags{bit::a} | bit::b) == "3");
  CHECK(util::to_string(util::static_string{"ab"}) == std::string{"ab"});
  CHECK(util::to_string(count{7}) == "7");

  auto os{std::ostringstream{}};
  os << util::cobs::error::truncated_input << ' ' << (util::flags{bit::a} | bit::b) << ' '
     << util::static_string{"ab"} << ' ' << count{7} << ' ' << util::ability::scale;
  CHECK(os.str() == "truncated_input 3 ab 7 scale");
}

TEST_CASE("nexenne::utility::flags prints a named enum's set by its bit names") {
  auto const both{util::flags{led::red} | led::green};
  CHECK(std::format("{}", both) == "red green");
  CHECK(std::format("{:>10}", both) == " red green");
  CHECK(util::to_string(both) == "red green");
  CHECK(util::to_string(util::flags<led>{}).empty());
  auto os{std::ostringstream{}};
  os << both;
  CHECK(os.str() == "red green");
  CHECK(std::format("{:#x}", both.raw()) == "0x5");
  CHECK(std::format("{}", util::flags{bit::a} | bit::b) == "3");
}

/**
 * @brief Whether \c to_string and \c operator<< print exactly the formatter's text.
 *
 * @tparam T Formattable type under test.
 * @param value Value to print three ways.
 *
 * @return \c true when all three layers agree.
 *
 * @pre None.
 * @post None.
 */
template <typename T>
auto three_layers_agree(T const& value) -> bool {
  auto os{std::ostringstream{}};
  os << value;
  auto const formatted{std::format("{}", value)};
  return util::to_string(value) == formatted && os.str() == formatted;
}

struct opaque {};

TEST_CASE("nexenne::utility::buffer_cursor prints its position and size") {
  auto buf{std::array<std::byte, 8>{}};
  auto cursor{util::buffer_cursor{std::span{buf}}};
  cursor.advance(2);
  CHECK(std::format("{}", cursor) == "buffer_cursor(position=2, size=8)");
  CHECK(std::format("{:>35}", cursor) == "  buffer_cursor(position=2, size=8)");
  CHECK(three_layers_agree(cursor));
}

TEST_CASE("nexenne::utility::lazy prints its cached value without running the factory") {
  auto runs{0};
  auto const value{util::lazy{[&runs] {
    ++runs;
    return 42;
  }}};
  CHECK(std::format("{}", value) == "lazy(pending)");
  CHECK(runs == 0);
  CHECK(*value == 42);
  CHECK(std::format("{}", value) == "lazy(42)");
  CHECK(runs == 1);
  CHECK(three_layers_agree(value));

  auto const hidden{util::lazy{[] { return opaque{}; }}};
  util::ignore(*hidden);
  CHECK(std::format("{}", hidden) == "lazy(ready)");
}

TEST_CASE("nexenne::utility::non_null prints the address it holds") {
  auto target{7};
  auto const raw{util::non_null{&target}};
  CHECK(std::format("{}", raw) == std::format("non_null({})", static_cast<void const*>(&target)));
  CHECK(three_layers_agree(raw));

  auto owned{util::non_null{std::make_unique<int>(3)}};
  auto const* const address{owned.get().get()};
  CHECK(std::format("{}", owned) == std::format("non_null({})", static_cast<void const*>(address)));
  auto const moved{std::move(owned)};
  CHECK(std::format("{}", owned) == "non_null(null)");
  CHECK(std::format("{}", moved) == std::format("non_null({})", static_cast<void const*>(address)));
}

TEST_CASE("nexenne::utility::unique_resource prints its ownership and handle") {
  auto closes{0};
  auto const close{[&closes](int) noexcept { ++closes; }};
  auto file{util::unique_resource{3, close}};
  CHECK(std::format("{}", file) == "unique_resource(owns=true, handle=3)");
  CHECK(three_layers_agree(file));
  file.reset();
  CHECK(std::format("{}", file) == "unique_resource(owns=false, handle=3)");
  CHECK(closes == 1);

  auto const failed{util::make_unique_resource_checked(-1, -1, close)};
  CHECK(std::format("{}", failed) == "unique_resource(owns=false, handle=-1)");

  auto target{0};
  auto const pointer{util::unique_resource{&target, [](int*) noexcept {}}};
  CHECK(
    std::format("{}", pointer)
    == std::format("unique_resource(owns=true, handle={})", static_cast<void const*>(&target))
  );

  auto const unnamed{util::unique_resource{opaque{}, [](opaque) noexcept {}}};
  CHECK(std::format("{}", unnamed) == "unique_resource(owns=true)");
}

TEST_CASE("nexenne::utility::scope_guard and defer print whether the cleanup is armed") {
  auto guard{util::scope_guard{[] {}}};
  CHECK(std::format("{}", guard) == "scope_guard(armed)");
  guard.dismiss();
  CHECK(std::format("{}", guard) == "scope_guard(dismissed)");
  CHECK(three_layers_agree(guard));

  auto const always{util::defer{[] {}}};
  CHECK(std::format("{}", always) == "defer(armed)");
  CHECK(three_layers_agree(always));
}

TEST_CASE(
  "nexenne::utility::function_ref and in_place_function print whether a callable is bound"
) {
  auto const twice{[](int const x) { return 2 * x; }};
  auto const empty_ref{util::function_ref<int(int)>{}};
  auto const bound_ref{util::function_ref<int(int)>{twice}};
  CHECK(std::format("{}", empty_ref) == "function_ref(empty)");
  CHECK(std::format("{}", bound_ref) == "function_ref(bound)");
  CHECK(three_layers_agree(bound_ref));

  auto fn{util::in_place_function<int(int), 32>{twice}};
  CHECK(std::format("{}", fn) == "in_place_function(bound, capacity=32)");
  CHECK(three_layers_agree(fn));
  fn.reset();
  CHECK(std::format("{}", fn) == "in_place_function(empty, capacity=32)");
}

}  // namespace
