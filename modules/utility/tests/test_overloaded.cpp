/**
 * @file
 * @brief Tests for nexenne::utility::overloaded.
 */

#include <doctest/doctest.h>

#include <string>
#include <variant>

#include <nexenne/utility/overloaded.hpp>

namespace {

TEST_CASE("nexenne::utility::overloaded dispatches a variant by alternative") {
  using nexenne::utility::overloaded;
  using payload = std::variant<int, std::string>;

  auto const classify{[](payload const& value) -> std::string {
    return std::visit(
      overloaded{
        [](int) { return std::string{"int"}; },
        [](std::string const&) { return std::string{"string"}; },
      },
      value
    );
  }};

  CHECK(classify(payload{42}) == "int");
  CHECK(classify(payload{std::string{"hi"}}) == "string");
}

TEST_CASE("nexenne::utility::overloaded invokes directly and falls back to a generic handler") {
  auto const dispatch{nexenne::utility::overloaded{
    [](int) { return 1; },
    [](double) { return 2; },
    [](auto const&) { return 0; },  // generic catch-all
  }};

  CHECK(dispatch(5) == 1);
  CHECK(dispatch(3.0) == 2);
  CHECK(dispatch("text") == 0);  // neither int nor double: generic handler
}

}  // namespace
