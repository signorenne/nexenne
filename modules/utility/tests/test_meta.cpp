/**
 * @file
 * @brief Tests for the nexenne::utility meta helpers.
 */

#include <doctest/doctest.h>

#include <concepts>
#include <string_view>
#include <type_traits>
#include <utility>

#include <nexenne/utility/meta.hpp>
#include <nexenne/utility/type_list.hpp>

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
static_assert(util::function_arity_v<functor&> == 1);

using nullary = void();
static_assert(util::function_arity_v<nullary> == 0);
static_assert(std::same_as<util::function_return_t<nullary>, void>);

using free_fn = int(char);
static_assert(util::function_arity_v<free_fn> == 1);
static_assert(!util::function_is_noexcept_v<free_fn>);
static_assert(std::same_as<util::function_arg_t<free_fn, 0>, char>);

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

static_assert(!util::always_false_v<>);
static_assert(!util::always_false_v<void>);
static_assert(!util::always_false_v<int, double>);
static_assert(!util::always_false_v<int, int, int>);
static_assert(!util::always_false_v<obj, fn, char const*>);
static_assert(std::same_as<decltype(util::always_false_v<int>), bool const>);

static_assert(std::same_as<util::function_args_t<fn>, util::type_list<int, double>>);
static_assert(std::same_as<util::function_args_t<nullary>, util::type_list<>>);
static_assert(std::same_as<util::function_args_t<free_fn>, util::type_list<char>>);
static_assert(std::same_as<util::function_args_t<functor>, util::type_list<int>>);

using fn_ptr = int (*)(char, long);
static_assert(util::function_arity_v<fn_ptr> == 2);
static_assert(std::same_as<util::function_return_t<fn_ptr>, int>);
static_assert(std::same_as<util::function_arg_t<fn_ptr, 0>, char>);
static_assert(std::same_as<util::function_arg_t<fn_ptr, 1>, long>);
static_assert(!util::function_is_noexcept_v<fn_ptr>);

using fn_ptr_ne = void (*)() noexcept;
static_assert(util::function_arity_v<fn_ptr_ne> == 0);
static_assert(util::function_is_noexcept_v<fn_ptr_ne>);
static_assert(std::same_as<util::function_args_t<fn_ptr_ne>, util::type_list<>>);

using const_fn_ptr = int (*const)(char, long);
static_assert(util::function_arity_v<const_fn_ptr> == 2);
static_assert(std::same_as<util::function_return_t<const_fn_ptr>, int>);
static_assert(std::same_as<util::function_arg_t<const_fn_ptr, 1>, long>);
static_assert(
  std::same_as<util::function_args_t<const_fn_ptr const&>, util::type_list<char, long>>
);
static_assert(util::function_is_noexcept_v<void (*const volatile)() noexcept>);
static_assert(util::function_arity_v<functor const> == 1);
static_assert(util::function_arity_v<functor const&> == 1);

using fn_type_ne = bool(int) noexcept;
static_assert(util::function_is_noexcept_v<fn_type_ne>);
static_assert(std::same_as<util::function_return_t<fn_type_ne>, bool>);
static_assert(std::same_as<util::function_arg_t<fn_type_ne, 0>, int>);

static_assert(util::function_arity_v<int (obj::*)(char) volatile> == 1);
static_assert(util::function_is_noexcept_v<int (obj::*)(char) volatile noexcept>);
static_assert(!util::function_is_noexcept_v<int (obj::*)(char) const volatile>);
static_assert(util::function_is_noexcept_v<int (obj::*)(char) const volatile noexcept>);
static_assert(std::same_as<util::function_return_t<int (obj::*)() const volatile&&>, int>);

static_assert(!util::function_is_noexcept_v<void (obj::*)() &>);
static_assert(!util::function_is_noexcept_v<void (obj::*)() &&>);
static_assert(!util::function_is_noexcept_v<void (obj::*)() const&>);
static_assert(!util::function_is_noexcept_v<void (obj::*)() const&&>);
static_assert(!util::function_is_noexcept_v<void (obj::*)() volatile&>);
static_assert(!util::function_is_noexcept_v<void (obj::*)() const volatile&&>);

static_assert(
  std::same_as<util::function_args_t<long (obj::*)(int, char) const>, util::type_list<int, char>>
);
static_assert(util::function_arity_v<void (obj::*)() const> == 0);

struct mutable_functor {
  auto operator()(double, double) -> int {
    return 0;
  }
};

static_assert(util::function_arity_v<mutable_functor> == 2);
static_assert(std::same_as<util::function_return_t<mutable_functor>, int>);
static_assert(std::same_as<util::function_arg_t<mutable_functor, 1>, double>);
static_assert(!util::function_is_noexcept_v<mutable_functor>);

struct noexcept_functor {
  auto operator()(int) const noexcept -> char {
    return 'a';
  }
};

static_assert(util::function_is_noexcept_v<noexcept_functor>);
static_assert(std::same_as<util::function_return_t<noexcept_functor>, char>);

static_assert(util::function_arity_v<functor const&> == 1);
static_assert(util::function_arity_v<functor&&> == 1);
static_assert(std::same_as<util::function_return_t<functor&&>, bool>);
static_assert(util::function_is_noexcept_v<noexcept_functor&>);

constexpr auto sample_lambda{[](int, char const*) noexcept -> long { return 0; }};
using lambda_t = decltype(sample_lambda);
static_assert(util::function_arity_v<lambda_t> == 2);
static_assert(util::function_is_noexcept_v<lambda_t>);
static_assert(std::same_as<util::function_return_t<lambda_t>, long>);
static_assert(std::same_as<util::function_arg_t<lambda_t, 0>, int>);
static_assert(std::same_as<util::function_arg_t<lambda_t, 1>, char const*>);
static_assert(std::same_as<util::function_args_t<lambda_t>, util::type_list<int, char const*>>);

TEST_CASE("nexenne::utility::function_traits inspects a capturing lambda") {
  auto const captured{42};
  auto const lam{[](int x) -> int { return x + captured; }};
  using lam_t = decltype(lam);
  CHECK(util::function_arity_v<lam_t> == 1);
  CHECK(std::same_as<util::function_return_t<lam_t>, int>);
  CHECK(std::same_as<util::function_arg_t<lam_t, 0>, int>);
  CHECK_FALSE(util::function_is_noexcept_v<lam_t>);
  CHECK(lam(8) == 50);
}

TEST_CASE("nexenne::utility::type_name spells the type") {
  CHECK(util::type_name<int>().find("int") != std::string_view::npos);
  CHECK(util::type_name<std::pair<int, double>>().find("pair") != std::string_view::npos);
}

TEST_CASE("nexenne::utility::type_name handles qualified, templated, and array types") {
  // Compiler-dependent spelling, so assert on stable substrings only.
  CHECK(util::type_name<double>().find("double") != std::string_view::npos);
  CHECK(util::type_name<char const*>().find("char") != std::string_view::npos);
  CHECK(util::type_name<unsigned long>().find("long") != std::string_view::npos);

  auto const pair_name{util::type_name<std::pair<int, char>>()};
  CHECK(pair_name.find("int") != std::string_view::npos);
  CHECK(pair_name.find("char") != std::string_view::npos);

  using nested = util::type_list<int, util::type_list<char>>;
  auto const nested_name{util::type_name<nested>()};
  CHECK(nested_name.find("type_list") != std::string_view::npos);

  CHECK(util::type_name<int[5]>().find("int") != std::string_view::npos);

  CHECK(util::type_name<obj>().find("obj") != std::string_view::npos);

  CHECK_FALSE(util::type_name<int>().empty());
}

TEST_CASE("nexenne::utility::type_name is deterministic across calls") {
  CHECK(util::type_name<long>() == util::type_name<long>());
  CHECK(util::type_name<int>() != util::type_name<long>());
}

}  // namespace
