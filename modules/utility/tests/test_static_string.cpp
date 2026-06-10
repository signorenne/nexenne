/**
 * @file
 * @brief Tests for nexenne::utility::static_string.
 */

#include <doctest/doctest.h>

#include <format>
#include <functional>
#include <string>
#include <string_view>

#include <nexenne/utility/static_string.hpp>

namespace {

namespace util = nexenne::utility;

template <util::static_string Name>
struct named {
  static constexpr auto name() noexcept -> std::string_view {
    return Name.view();
  }
};

static_assert(util::static_string{"hello"}.size() == 5);
static_assert(util::static_string{""}.empty());
static_assert(util::static_string{"abc"}.view() == "abc");
static_assert(util::static_string{"abc"}[0] == 'a');
static_assert(named<"hello">::name() == "hello");
static_assert(!util::static_string{"x"}.empty());
static_assert((util::static_string{"ab"} + util::static_string{"cd"}).view() == "abcd");
static_assert((util::static_string{""} + util::static_string{"cd"}).view() == "cd");  // empty lhs
static_assert((util::static_string{"ab"} + util::static_string{""}).view() == "ab");  // empty rhs
static_assert(
  ((util::static_string{"ab"} + util::static_string{"cd"}) + util::static_string{"ef"}).view()
  == "abcdef"
);  // chained concatenation
static_assert(util::static_string{"abc"} == util::static_string{"abc"});
static_assert(util::static_string{"abc"} != util::static_string{"abd"});
static_assert(util::static_string{"abc"} < util::static_string{"abd"});  // lexicographic <=>

TEST_CASE("nexenne::utility::static_string formats and hashes via its body") {
  CHECK(std::format("{:>5}", util::static_string{"ab"}) == "   ab");
  CHECK(std::format("{}", util::static_string{"abc"}) == "abc");

  auto const hashed{std::hash<util::static_string<4>>{}(util::static_string{"abc"})};
  CHECK(hashed == std::hash<std::string_view>{}(std::string_view{"abc"}));
}

TEST_CASE("nexenne::utility::static_string is null-terminated and iterates its body") {
  constexpr auto s{util::static_string{"abc"}};
  CHECK(std::string_view{s.c_str()} == "abc");  // c_str() is null-terminated

  std::string collected;
  for (auto const ch : s) {
    collected.push_back(ch);
  }
  CHECK(collected == "abc");  // begin()/end() span the body, not the terminator
}

}  // namespace
