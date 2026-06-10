/**
 * @file
 * @brief Tests for the nexenne::utility meta helpers.
 */

#include <doctest/doctest.h>

#include <concepts>
#include <string_view>
#include <utility>

#include <nexenne/utility/meta.hpp>

namespace {

namespace util = nexenne::utility;

using fn = void(int, double) noexcept;

static_assert(util::function_arity_v<fn> == 2);
static_assert(util::function_is_noexcept_v<fn>);
static_assert(std::same_as<util::function_return_t<fn>, void>);
static_assert(std::same_as<util::function_arg_t<fn, 0>, int>);
static_assert(std::same_as<util::function_arg_t<fn, 1>, double>);

static_assert(!util::always_false_v<int>);

struct functor {
  auto operator()(int) const -> bool {
    return true;
  }
};

static_assert(util::function_arity_v<functor> == 1);
static_assert(std::same_as<util::function_return_t<functor>, bool>);
static_assert(std::same_as<util::function_arg_t<functor, 0>, int>);
static_assert(util::function_arity_v<functor&> == 1);  // reference to a callable

// Free function types: nullary and a non-noexcept multi-arg.
using nullary = void();
static_assert(util::function_arity_v<nullary> == 0);
static_assert(std::same_as<util::function_return_t<nullary>, void>);

using free_fn = int(char);
static_assert(util::function_arity_v<free_fn> == 1);
static_assert(!util::function_is_noexcept_v<free_fn>);
static_assert(std::same_as<util::function_arg_t<free_fn, 0>, char>);

// Member function pointers across the cv / ref / noexcept matrix.
struct obj {};

static_assert(util::function_arity_v<int (obj::*)(char)> == 1);
static_assert(std::same_as<util::function_return_t<int (obj::*)(char)>, int>);
static_assert(std::same_as<util::function_arg_t<int (obj::*)(char), 0>, char>);
static_assert(!util::function_is_noexcept_v<int (obj::*)(char)>);
static_assert(util::function_is_noexcept_v<int (obj::*)(char) noexcept>);
static_assert(!util::function_is_noexcept_v<int (obj::*)(char) const>);
static_assert(util::function_arity_v<long (obj::*)(int, int) const noexcept> == 2);
static_assert(util::function_is_noexcept_v<long (obj::*)(int, int) const noexcept>);
static_assert(util::function_is_noexcept_v<void (obj::*)() & noexcept>);
static_assert(std::same_as<util::function_return_t<double (obj::*)() &&>, double>);

TEST_CASE("nexenne::utility::type_name spells the type") {
  CHECK(util::type_name<int>().find("int") != std::string_view::npos);
  CHECK(util::type_name<std::pair<int, double>>().find("pair") != std::string_view::npos);
}

}  // namespace
