/**
 * @file
 * @brief Foundation tests for nexenne::geometry: the error space and result alias.
 */

#include <doctest/doctest.h>

#include <expected>
#include <string_view>
#include <type_traits>

#include <nexenne/geometry/error.hpp>

namespace {

namespace geo = nexenne::geometry;

TEST_CASE("nexenne::geometry::to_string names every error") {
  CHECK(geo::to_string(geo::geometry_error::degenerate_primitive) == "degenerate_primitive");
  CHECK(geo::to_string(geo::geometry_error::invalid_input) == "invalid_input");
  CHECK(geo::to_string(geo::geometry_error::parallel) == "parallel");
}

TEST_CASE("nexenne::geometry::result is std::expected over geometry_error") {
  static_assert(
    std::is_same_v<geo::result<int>, std::expected<int, geo::geometry_error>>,
    "result<T> must alias std::expected<T, geometry_error>"
  );

  geo::result<int> const ok{42};
  REQUIRE(ok.has_value());
  CHECK(*ok == 42);

  geo::result<int> const bad{std::unexpected{geo::geometry_error::degenerate_primitive}};
  REQUIRE_FALSE(bad.has_value());
  CHECK(bad.error() == geo::geometry_error::degenerate_primitive);
}

}  // namespace
