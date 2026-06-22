#include <doctest/doctest.h>

#include <array>
#include <format>
#include <string>
#include <string_view>

#include <nexenne/serialization/format.hpp>

namespace {

using namespace nexenne::serialization;

TEST_CASE("nexenne::serialization::error - std::format matches to_string") {
  constexpr std::array errors{
    error::invalid_input,
    error::unexpected_character,
    error::unexpected_end,
    error::invalid_number,
    error::invalid_string,
    error::invalid_escape,
    error::duplicate_key,
    error::depth_limit_exceeded,
    error::type_mismatch,
    error::path_not_found,
    error::buffer_full,
    error::buffer_underrun,
    error::string_too_long,
  };
  for (auto const e : errors) {
    CHECK(std::format("{}", e) == std::string{to_string(e)});
  }
}

TEST_CASE("nexenne::serialization::error - format spec applies to the name") {
  // The inherited string formatter honours width and alignment.
  CHECK(std::format("{:>16}", error::buffer_full) == "     buffer_full");
  CHECK(std::format("[{:<8}]", error::type_mismatch) == "[type_mismatch]");
}

TEST_CASE("nexenne::serialization::json::value - std::format matches serialize") {
  auto const v{json::value{json::object{
    {"name", "alice"},
    {"age", 30},
    {"admin", true},
    {"scores", json::array{42, 73, 99}},
  }}};
  CHECK(std::format("{}", v) == json::serialize(v));
}

TEST_CASE("nexenne::serialization::json::value - scalars format like serialize") {
  CHECK(std::format("{}", json::value{}) == "null");
  CHECK(std::format("{}", json::value{true}) == "true");
  CHECK(std::format("{}", json::value{42}) == "42");
  CHECK(std::format("{}", json::value{"hi"}) == "\"hi\"");
}

}  // namespace
