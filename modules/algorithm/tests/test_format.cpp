#include <doctest/doctest.h>

#include <format>
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

TEST_CASE("std::format honours width and alignment specs on the error name") {
  CHECK(std::format("{:>16}", algorithm::codec_error::invalid_input) == "   invalid_input");
}
