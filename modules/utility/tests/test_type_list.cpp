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

static_assert(util::tl_size_v<util::type_list<int>> == 1);
static_assert(util::tl_size_v<util::type_list<int, char, long, short, double, float>> == 6);
static_assert(util::tl_size_v<util::type_list<void>> == 1);
static_assert(std::same_as<decltype(util::tl_size_v<list>), std::size_t const>);
static_assert(util::tl_size<list>::value == 3);  // the trait, not just the alias

static_assert(std::same_as<util::tl_at_t<list, 1>, float>);
static_assert(std::same_as<util::tl_at_t<util::type_list<char, int, char>, 2>, char>);
static_assert(std::same_as<typename util::tl_at<list, 0>::type, int>);  // the trait directly
// Reference and cv-qualified element types are carried verbatim.
static_assert(std::same_as<util::tl_at_t<util::type_list<int const, int&>, 0>, int const>);
static_assert(std::same_as<util::tl_at_t<util::type_list<int const, int&>, 1>, int&>);

static_assert(util::tl_contains_v<list, int>);
static_assert(util::tl_contains_v<list, double>);  // last element
static_assert(!util::tl_contains_v<list, void>);
static_assert(util::tl_contains_v<util::type_list<int, int>, int>);  // duplicate still found
// const int is a distinct type from int.
static_assert(util::tl_contains_v<util::type_list<int const>, int const>);
static_assert(!util::tl_contains_v<util::type_list<int const>, int>);
static_assert(util::tl_contains<list, float>::value);  // the trait directly

static_assert(util::tl_index_of_v<list, int> == 0);
static_assert(util::tl_index_of_v<list, float> == 1);
static_assert(util::tl_index_of_v<util::type_list<char, char, char>, char> == 0);
static_assert(util::tl_index_of_v<util::type_list<void, int, void>, int> == 1);
static_assert(util::tl_index_of<list, double>::value == 2);  // the trait directly

static_assert(std::same_as<util::tl_push_front_t<util::type_list<>, int>, util::type_list<int>>);
static_assert(std::same_as<
              util::tl_push_back_t<util::type_list<int>, char>,
              util::type_list<int, char>>);
static_assert(std::same_as<
              util::tl_push_front_t<util::type_list<int>, char>,
              util::type_list<char, int>>);
// Pushing the same type that already exists does NOT deduplicate.
static_assert(std::same_as<
              util::tl_push_back_t<util::type_list<int>, int>,
              util::type_list<int, int>>);
// push_front then push_back wraps the list on both ends.
static_assert(std::same_as<
              util::tl_push_back_t<util::tl_push_front_t<list, char>, char>,
              util::type_list<char, int, float, double, char>>);

static_assert(std::same_as<
              util::tl_concat_t<util::type_list<>, util::type_list<>>,
              util::type_list<>>);
static_assert(std::same_as<util::tl_concat_t<list, util::type_list<>>, list>);
static_assert(std::same_as<
              util::tl_concat_t<list, list>,
              util::type_list<int, float, double, int, float, double>>);
static_assert(std::same_as<
              util::tl_concat_t<util::type_list<int>, util::type_list<>>,
              util::type_list<int>>);

static_assert(std::same_as<util::tl_transform_t<list, std::type_identity>, list>);  // identity
static_assert(std::same_as<
              util::tl_transform_t<util::type_list<int*, char*>, std::remove_pointer>,
              util::type_list<int, char>>);
// add_pointer then remove_pointer round-trips to the original list.
static_assert(std::same_as<
              util::
                tl_transform_t<util::tl_transform_t<list, std::add_pointer>, std::remove_pointer>,
              list>);
static_assert(std::same_as<
              util::tl_transform_t<util::type_list<int>, std::add_const>,
              util::type_list<int const>>);
static_assert(std::same_as<
              util::tl_transform_t<util::type_list<int, char>, std::add_lvalue_reference>,
              util::type_list<int&, char&>>);

static_assert(std::same_as<
              util::tl_filter_t<util::type_list<int, char, long>, std::is_integral>,
              util::type_list<int, char, long>>);  // all kept, order preserved
static_assert(std::
                same_as<util::tl_filter_t<util::type_list<>, std::is_integral>, util::type_list<>>);
static_assert(std::same_as<
              util::tl_filter_t<util::type_list<float>, std::is_integral>,
              util::type_list<>>);  // single, dropped
static_assert(std::same_as<
              util::tl_filter_t<util::type_list<int>, std::is_integral>,
              util::type_list<int>>);  // single, kept
// Interleaved keep/drop preserves relative order of survivors.
static_assert(std::same_as<
              util::tl_filter_t<util::type_list<float, int, double, char, long>, std::is_integral>,
              util::type_list<int, char, long>>);
static_assert(std::same_as<
              util::tl_filter_t<util::type_list<int*, int, char*>, std::is_pointer>,
              util::type_list<int*, char*>>);

static_assert(std::same_as<util::tl_unique_t<util::type_list<int>>, util::type_list<int>>);
static_assert(std::same_as<
              util::tl_unique_t<util::type_list<int, char, long>>,
              util::type_list<int, char, long>>);  // all distinct: identity
static_assert(std::same_as<
              util::tl_unique_t<util::type_list<int, int, char, char>>,
              util::type_list<int, char>>);  // adjacent runs collapse
static_assert(std::same_as<
              util::tl_unique_t<util::type_list<char, int, char, int, char>>,
              util::type_list<char, int>>);  // scattered duplicates, first kept
// Idempotence: applying tl_unique twice equals applying it once.
static_assert(std::same_as<
              util::tl_unique_t<util::tl_unique_t<util::type_list<int, int, float, int>>>,
              util::tl_unique_t<util::type_list<int, int, float, int>>>);
// const-sensitivity: distinct cv-qualified types are not duplicates.
static_assert(std::same_as<
              util::tl_unique_t<util::type_list<int, int const, int>>,
              util::type_list<int, int const>>);

static_assert(util::tl_size_v<util::tl_concat_t<list, list>> == 6);
static_assert(util::tl_size_v<util::tl_unique_t<util::tl_concat_t<list, list>>> == 3);
static_assert(util::tl_contains_v<util::tl_push_back_t<util::type_list<>, void>, void>);

TEST_CASE("nexenne::utility::type_list metafunctions evaluate at compile time") {
  CHECK(util::tl_size_v<list> == 3);
}

TEST_CASE("nexenne::utility::type_list size/index/contains observable at runtime") {
  // These are constants but exercised under the runtime test harness so they
  // run under sanitizers alongside the static_asserts.
  CHECK(util::tl_size_v<util::type_list<>> == 0);
  CHECK(util::tl_size_v<util::type_list<int, char, long>> == 3);
  CHECK(util::tl_index_of_v<list, double> == 2);
  CHECK(util::tl_contains_v<list, float>);
  CHECK_FALSE(util::tl_contains_v<list, char>);
}

}  // namespace
