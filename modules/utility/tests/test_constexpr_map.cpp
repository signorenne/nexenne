/**
 * @file
 * @brief Tests for nexenne::utility::constexpr_map.
 */

#include <doctest/doctest.h>

#include <array>
#include <string_view>
#include <utility>
#include <vector>

#include <nexenne/utility/constexpr_map.hpp>

namespace {

namespace util = nexenne::utility;

constexpr auto names{util::constexpr_map{std::array{
  std::pair{1, std::string_view{"one"}},
  std::pair{3, std::string_view{"three"}},
  std::pair{2, std::string_view{"two"}},
}}};

static_assert(names.size() == 3);
static_assert(names.contains(2));
static_assert(!names.contains(99));
static_assert(*names.find(3) == "three");
static_assert(*names.find(2) == "two");
static_assert(names.find(99) == nullptr);
static_assert(names.find(0) == nullptr);  // below every key
static_assert(names.find(4) == nullptr);  // above every key
static_assert(names[1] == "one");
static_assert(names.data().front().first == 1);  // data() is sorted
static_assert(names.data().back().first == 3);

// Single entry with a non-string (only the key need be ordered).
constexpr auto solo{util::constexpr_map{std::array{std::pair{5, 100}}}};
static_assert(solo.size() == 1);
static_assert(*solo.find(5) == 100);
static_assert(solo.find(4) == nullptr);
static_assert(solo.find(6) == nullptr);

// Duplicate keys are retained; find returns a value for the key.
constexpr auto dups{util::constexpr_map{std::array{std::pair{1, 10}, std::pair{1, 20}}}};
static_assert(dups.size() == 2);
static_assert(dups.contains(1));

TEST_CASE("nexenne::utility::constexpr_map iterates in sorted key order") {
  std::vector<int> keys;
  for (auto const& entry : names) {
    keys.push_back(entry.first);
  }
  CHECK(keys == std::vector{1, 2, 3});
}

}  // namespace
