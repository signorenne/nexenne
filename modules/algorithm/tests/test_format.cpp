#include <doctest/doctest.h>

#include <cstdint>
#include <format>
#include <sstream>
#include <string>

#include <nexenne/algorithm/encoding/codec_error.hpp>
#include <nexenne/algorithm/format.hpp>
#include <nexenne/algorithm/numerical/numerical_error.hpp>

namespace algorithm = nexenne::algorithm;

TEST_CASE("std::format on codec_error matches to_string") {
  CHECK(
    std::format("{}", algorithm::codec_error::invalid_input)
    == algorithm::to_string(algorithm::codec_error::invalid_input)
  );
  CHECK(
    std::format("{}", algorithm::codec_error::buffer_too_small)
    == algorithm::to_string(algorithm::codec_error::buffer_too_small)
  );
}

TEST_CASE("std::format on numerical_error matches to_string") {
  CHECK(
    std::format("{}", algorithm::numerical_error::not_bracketed)
    == algorithm::to_string(algorithm::numerical_error::not_bracketed)
  );
  CHECK(
    std::format("{}", algorithm::numerical_error::no_convergence)
    == algorithm::to_string(algorithm::numerical_error::no_convergence)
  );
}

TEST_CASE("std::format on codec_error covers incomplete_input") {
  CHECK(
    std::format("{}", algorithm::codec_error::incomplete_input)
    == algorithm::to_string(algorithm::codec_error::incomplete_input)
  );
}

TEST_CASE("std::format honours width and alignment specs on the error name") {
  CHECK(std::format("{:>16}", algorithm::codec_error::invalid_input) == "   invalid_input");
}

TEST_CASE("format of a_star_result agrees across the three layers") {
  auto const r{algorithm::a_star_result<std::uint32_t, int>{.path = {0, 1, 2}, .cost = 5}};
  CHECK(algorithm::to_string(r) == "a_star_result(path=[0, 1, 2], cost=5)");
  CHECK(std::format("{}", r) == algorithm::to_string(r));
  auto os{std::ostringstream{}};
  os << r;
  CHECK(os.str() == algorithm::to_string(r));
}

TEST_CASE("format of scc_result agrees across the three layers") {
  auto const r{algorithm::scc_result<std::uint32_t>{.labels = {0, 0, 1}, .num_components = 2}};
  CHECK(algorithm::to_string(r) == "scc_result(labels=[0, 0, 1], num_components=2)");
  CHECK(std::format("{}", r) == algorithm::to_string(r));
  auto os{std::ostringstream{}};
  os << r;
  CHECK(os.str() == algorithm::to_string(r));
}

TEST_CASE("format of components_result agrees across the three layers") {
  auto const r{
    algorithm::components_result<void, std::uint32_t>{.labels = {0, 1, 1}, .num_components = 2}
  };
  CHECK(algorithm::to_string(r) == "components_result(labels=[0, 1, 1], num_components=2)");
  CHECK(std::format("{}", r) == algorithm::to_string(r));
  auto os{std::ostringstream{}};
  os << r;
  CHECK(os.str() == algorithm::to_string(r));
}

TEST_CASE("format of floyd_warshall_result agrees across the three layers") {
  auto const r{
    algorithm::floyd_warshall_result<std::uint32_t, int>{.distances = {0, 1, 1, 0}, .n = 2}
  };
  CHECK(algorithm::to_string(r) == "floyd_warshall_result(n=2, distances=[0, 1, 1, 0])");
  CHECK(std::format("{}", r) == algorithm::to_string(r));
  auto os{std::ostringstream{}};
  os << r;
  CHECK(os.str() == algorithm::to_string(r));
}

TEST_CASE("format of mst_edge agrees across the three layers") {
  auto const e{algorithm::mst_edge<int, std::uint32_t>{.from = 0, .to = 1, .weight = 7}};
  CHECK(algorithm::to_string(e) == "mst_edge(from=0, to=1, weight=7)");
  CHECK(std::format("{}", e) == algorithm::to_string(e));
  auto os{std::ostringstream{}};
  os << e;
  CHECK(os.str() == algorithm::to_string(e));
}

TEST_CASE("std::format honours width and alignment specs on an mst_edge") {
  auto const e{algorithm::mst_edge<int, std::uint32_t>{.from = 3, .to = 4, .weight = 9}};
  CHECK(std::format("{}", e) == "mst_edge(from=3, to=4, weight=9)");
}
