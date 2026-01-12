/**
 * @file
 * @brief Tests for the nexenne::utility std::expected helpers.
 */

#include <doctest/doctest.h>

#include <expected>
#include <memory>
#include <optional>
#include <string>

#include <nexenne/utility/expected_utils.hpp>

namespace {

namespace util = nexenne::utility;

using exp = std::expected<int, std::string>;
using evoid = std::expected<void, std::string>;

TEST_CASE("nexenne::utility::into_optional drops the error channel") {
  CHECK(util::into_optional(exp{42}) == std::optional{42});
  CHECK(util::into_optional(exp{std::unexpect, "bad"}) == std::nullopt);
  CHECK(util::into_optional(evoid{}));
  CHECK_FALSE(util::into_optional(evoid{std::unexpect, "x"}));
}

TEST_CASE("nexenne::utility::flatten collapses a nested expected") {
  using nested = std::expected<exp, std::string>;
  CHECK(util::flatten(nested{exp{7}}).value() == 7);
  CHECK(util::flatten(nested{std::unexpect, "outer"}).error() == "outer");
  CHECK(util::flatten(nested{exp{std::unexpect, "inner"}}).error() == "inner");
}

TEST_CASE("nexenne::utility::first_error returns the first failure") {
  CHECK(util::first_error(evoid{}, evoid{}, evoid{}).has_value());

  auto const r{
    util::first_error(evoid{}, evoid{std::unexpect, "second"}, evoid{std::unexpect, "third"})
  };
  REQUIRE_FALSE(r.has_value());
  CHECK(r.error() == "second");
}

TEST_CASE("nexenne::utility::try_or falls back through the error") {
  CHECK(util::try_or(exp{5}, [](std::string const&) { return -1; }) == 5);
  CHECK(util::try_or(exp{std::unexpect, "e"}, [](std::string const&) { return -1; }) == -1);
}

TEST_CASE("nexenne::utility rvalue overloads move the contained value out") {
  using exp_ptr = std::expected<std::unique_ptr<int>, std::string>;
  auto opt{util::into_optional(exp_ptr{std::make_unique<int>(5)})};
  REQUIRE(opt.has_value());
  CHECK(**opt == 5);

  using nested_ptr = std::expected<exp_ptr, std::string>;
  auto flat{util::flatten(nested_ptr{exp_ptr{std::make_unique<int>(7)}})};
  REQUIRE(flat.has_value());
  CHECK(*flat.value() == 7);

  auto const n{util::try_or(exp{std::unexpect, "err"}, [](std::string&& s) {
    return static_cast<int>(s.size());
  })};
  CHECK(n == 3);
}

TEST_CASE("nexenne::utility::first_error handles position-zero and single-argument cases") {
  auto const r0{util::first_error(evoid{std::unexpect, "first"}, evoid{})};
  REQUIRE_FALSE(r0.has_value());
  CHECK(r0.error() == "first");

  CHECK(util::first_error(evoid{}).has_value());
  CHECK_FALSE(util::first_error(evoid{std::unexpect, "x"}).has_value());
}

}  // namespace
