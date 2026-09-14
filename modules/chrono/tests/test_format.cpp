/**
 * @file
 * @brief Tests for the nexenne::chrono formatters.
 */

#include <doctest/doctest.h>

#include <chrono>
#include <format>
#include <string>

#include <nexenne/chrono/format.hpp>

namespace {

namespace ch = nexenne::chrono;
using namespace std::chrono_literals;

TEST_CASE("nexenne::chrono::format.hpp alone formats every printable chrono type") {
  auto const parts{ch::extract_parts(std::chrono::milliseconds{61'500})};
  CHECK(std::format("{}", parts) == ch::format(std::chrono::milliseconds{61'500}));

  ch::stopwatch<> const sw;
  CHECK(std::format("{}", sw) == ch::format(std::chrono::milliseconds{0}));
  ch::static_stopwatch<2> const ssw;
  CHECK(std::format("{}", ssw) == ch::format(std::chrono::milliseconds{0}));

  ch::countdown<> const cd{5s};
  CHECK(!std::format("{}", cd).empty());
  auto const dl{ch::deadline<>::after(5s)};
  CHECK(!std::format("{:!}", dl).empty());
  ch::interval<> const iv{1s};
  CHECK(!std::format("{}", iv).empty());
}

}  // namespace
