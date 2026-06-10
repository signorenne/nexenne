/**
 * @file
 * @brief Tests for nexenne::utility::type_list.
 */

#include <doctest/doctest.h>

#include <concepts>
#include <type_traits>

#include <nexenne/utility/type_list.hpp>

namespace {

namespace util = nexenne::utility;
using list = util::type_list<int, float, double>;

static_assert(util::tl_size_v<list> == 3);
static_assert(util::tl_size_v<util::type_list<>> == 0);

static_assert(std::same_as<util::tl_at_t<list, 0>, int>);
static_assert(std::same_as<util::tl_at_t<list, 2>, double>);

static_assert(util::tl_contains_v<list, float>);
static_assert(!util::tl_contains_v<list, char>);

static_assert(util::tl_index_of_v<list, double> == 2);

static_assert(std::same_as<
              util::tl_push_back_t<list, char>,
              util::type_list<int, float, double, char>>);
static_assert(std::same_as<
              util::tl_push_front_t<list, char>,
              util::type_list<char, int, float, double>>);

static_assert(std::same_as<
              util::tl_concat_t<util::type_list<int>, util::type_list<float, char>>,
              util::type_list<int, float, char>>);

static_assert(std::same_as<
              util::tl_transform_t<list, std::add_pointer>,
              util::type_list<int*, float*, double*>>);

static_assert(std::same_as<
              util::tl_filter_t<util::type_list<int, float, char, double>, std::is_integral>,
              util::type_list<int, char>>);

static_assert(std::same_as<
              util::tl_unique_t<util::type_list<int, float, int, double, float>>,
              util::type_list<int, float, double>>);

// Degenerate and boundary cases.
static_assert(std::same_as<util::tl_unique_t<list>, list>);  // no duplicates: identity
static_assert(std::same_as<util::tl_unique_t<util::type_list<>>, util::type_list<>>);
static_assert(std::
                same_as<util::tl_unique_t<util::type_list<int, int, int>>, util::type_list<int>>);
static_assert(util::tl_index_of_v<util::type_list<int, float, int>, int> == 0);  // first match
static_assert(!util::tl_contains_v<util::type_list<>, int>);
static_assert(util::tl_size_v<util::type_list<int, int>> == 2);  // counts duplicates
static_assert(std::same_as<util::tl_at_t<util::type_list<int>, 0>, int>);
static_assert(std::same_as<util::tl_push_back_t<util::type_list<>, int>, util::type_list<int>>);
static_assert(std::same_as<util::tl_concat_t<util::type_list<>, list>, list>);
static_assert(std::same_as<
              util::tl_transform_t<util::type_list<>, std::add_pointer>,
              util::type_list<>>);
static_assert(std::same_as<
              util::tl_filter_t<util::type_list<float, double>, std::is_integral>,
              util::type_list<>>);

TEST_CASE("nexenne::utility::type_list metafunctions evaluate at compile time") {
  CHECK(util::tl_size_v<list> == 3);
}

}  // namespace
