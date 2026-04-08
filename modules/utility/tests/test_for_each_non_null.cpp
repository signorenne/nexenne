/**
 * @file
 * @brief Tests for nexenne::utility::for_each_non_null.
 */

#include <doctest/doctest.h>

#include <array>
#include <memory>
#include <vector>

#include <nexenne/utility/for_each_non_null.hpp>

namespace {

using nexenne::utility::for_each_non_null;

}  // namespace

TEST_CASE("nexenne::utility::for_each_non_null skips null raw pointers") {
  auto a{1};
  auto b{2};
  auto const ptrs{std::array<int*, 4>{&a, nullptr, &b, nullptr}};

  auto sum{0};
  for_each_non_null(ptrs, [&sum](int& value) { sum += value; });
  CHECK(sum == 3);  // only a and b were visited
}

TEST_CASE("nexenne::utility::for_each_non_null preserves order and passes the pointee") {
  auto a{10};
  auto b{20};
  auto c{30};
  auto const ptrs{std::array<int*, 3>{&a, &b, &c}};

  auto seen{std::vector<int>{}};
  for_each_non_null(ptrs, [&seen](int const& value) { seen.push_back(value); });
  CHECK(seen == std::vector<int>{10, 20, 30});
}

TEST_CASE("nexenne::utility::for_each_non_null works over smart pointers") {
  auto owners{std::vector<std::shared_ptr<int>>{}};
  owners.push_back(std::make_shared<int>(5));
  owners.push_back(nullptr);
  owners.push_back(std::make_shared<int>(7));

  auto sum{0};
  for_each_non_null(owners, [&sum](int& value) { sum += value; });
  CHECK(sum == 12);
}

TEST_CASE("nexenne::utility::for_each_non_null on an all-null range calls nothing") {
  auto const ptrs{std::array<int*, 3>{nullptr, nullptr, nullptr}};
  auto calls{0};
  for_each_non_null(ptrs, [&calls](int&) { ++calls; });
  CHECK(calls == 0);
}
