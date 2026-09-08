/**
 * @file
 * @brief Tests for the nexenne::utility std::expected helpers.
 */

#include <doctest/doctest.h>

#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <nexenne/utility/expected_utils.hpp>

namespace {

namespace util = nexenne::utility;

using exp = std::expected<int, std::string>;
using evoid = std::expected<void, std::string>;

TEST_CASE("nexenne::utility::into_optional drops the error channel") {
  CHECK(util::into_optional(exp{42}) == std::optional{42});
  CHECK(util::into_optional(exp{std::unexpect, "bad"}) == std::nullopt);
  CHECK(util::into_optional(evoid{}));
  CHECK_FALSE(util::into_optional(evoid{std::unexpect, "x"}));
}

TEST_CASE("nexenne::utility::flatten collapses a nested expected") {
  using nested = std::expected<exp, std::string>;
  CHECK(util::flatten(nested{exp{7}}).value() == 7);
  CHECK(util::flatten(nested{std::unexpect, "outer"}).error() == "outer");
  CHECK(util::flatten(nested{exp{std::unexpect, "inner"}}).error() == "inner");
}

TEST_CASE("nexenne::utility::first_error returns the first failure") {
  CHECK(util::first_error(evoid{}, evoid{}, evoid{}).has_value());

  auto const r{
    util::first_error(evoid{}, evoid{std::unexpect, "second"}, evoid{std::unexpect, "third"})
  };
  REQUIRE_FALSE(r.has_value());
  CHECK(r.error() == "second");
}

TEST_CASE("nexenne::utility::try_or falls back through the error") {
  CHECK(util::try_or(exp{5}, [](std::string const&) { return -1; }) == 5);
  CHECK(util::try_or(exp{std::unexpect, "e"}, [](std::string const&) { return -1; }) == -1);
}

struct sensor_fault {
  int code{};

  [[nodiscard]] auto fallback() const -> int {
    return -code;
  }
};

TEST_CASE("nexenne::utility::try_or accepts a pointer to member function as the fallback") {
  using reading = std::expected<int, sensor_fault>;
  reading const failed{std::unexpect, sensor_fault{7}};
  CHECK(util::try_or(failed, &sensor_fault::fallback) == -7);
  CHECK(util::try_or(reading{std::unexpect, sensor_fault{3}}, &sensor_fault::fallback) == -3);
  CHECK(util::try_or(reading{11}, &sensor_fault::fallback) == 11);
}

TEST_CASE("nexenne::utility rvalue overloads move the contained value out") {
  using exp_ptr = std::expected<std::unique_ptr<int>, std::string>;
  auto opt{util::into_optional(exp_ptr{std::make_unique<int>(5)})};
  REQUIRE(opt.has_value());
  CHECK(**opt == 5);

  using nested_ptr = std::expected<exp_ptr, std::string>;
  auto flat{util::flatten(nested_ptr{exp_ptr{std::make_unique<int>(7)}})};
  REQUIRE(flat.has_value());
  CHECK(*flat.value() == 7);

  auto const n{util::try_or(exp{std::unexpect, "err"}, [](std::string&& s) {
    return static_cast<int>(s.size());
  })};
  CHECK(n == 3);
}

TEST_CASE("nexenne::utility::first_error handles position-zero and single-argument cases") {
  auto const r0{util::first_error(evoid{std::unexpect, "first"}, evoid{})};
  REQUIRE_FALSE(r0.has_value());
  CHECK(r0.error() == "first");

  CHECK(util::first_error(evoid{}).has_value());
  CHECK_FALSE(util::first_error(evoid{std::unexpect, "x"}).has_value());
}

TEST_CASE("nexenne::utility::into_optional const lvalue copies, source untouched") {
  exp const src{99};
  auto const opt{util::into_optional(src)};
  REQUIRE(opt.has_value());
  CHECK(*opt == 99);
  CHECK(src.has_value());
  CHECK(src.value() == 99);

  exp const bad{std::unexpect, "boom"};
  CHECK(util::into_optional(bad) == std::nullopt);
  CHECK_FALSE(bad.has_value());
  CHECK(bad.error() == "boom");
}

TEST_CASE("nexenne::utility::into_optional non-trivial value type round-trips") {
  using exp_str = std::expected<std::string, int>;
  CHECK(util::into_optional(exp_str{"hello"}) == std::optional<std::string>{"hello"});
  CHECK(util::into_optional(exp_str{std::unexpect, 7}) == std::nullopt);
}

TEST_CASE(
  "nexenne::utility::into_optional rvalue moves a move-only value, leaves nullopt on error"
) {
  using exp_ptr = std::expected<std::unique_ptr<int>, std::string>;
  auto miss{util::into_optional(exp_ptr{std::unexpect, "no"})};
  CHECK_FALSE(miss.has_value());

  exp_ptr src{std::make_unique<int>(64)};
  auto got{util::into_optional(std::move(src))};
  REQUIRE(got.has_value());
  CHECK(**got == 64);
  REQUIRE(src.has_value());
  CHECK(src.value() == nullptr);
}

TEST_CASE("nexenne::utility::into_optional void overload reports success as bool and is noexcept") {
  evoid const ok{};
  evoid const bad{std::unexpect, "e"};
  static_assert(noexcept(util::into_optional(ok)));
  CHECK(util::into_optional(ok) == true);
  CHECK(util::into_optional(bad) == false);
}

TEST_CASE("nexenne::utility::flatten const lvalue with non-trivial value and error types") {
  using exp_str = std::expected<std::string, std::string>;
  using nested = std::expected<exp_str, std::string>;

  nested const ok{exp_str{"deep"}};
  CHECK(util::flatten(ok).value() == "deep");

  nested const outer_err{std::unexpect, "outer"};
  CHECK(util::flatten(outer_err).error() == "outer");

  nested const inner_err{exp_str{std::unexpect, "inner"}};
  CHECK(util::flatten(inner_err).error() == "inner");

  CHECK(ok.has_value());
  CHECK(ok.value().value() == "deep");
}

TEST_CASE("nexenne::utility::flatten rvalue moves the inner error out") {
  using nested = std::expected<exp, std::string>;
  auto r{util::flatten(nested{exp{std::unexpect, "moved-inner"}})};
  REQUIRE_FALSE(r.has_value());
  CHECK(r.error() == "moved-inner");

  auto r2{util::flatten(nested{std::unexpect, "moved-outer"})};
  REQUIRE_FALSE(r2.has_value());
  CHECK(r2.error() == "moved-outer");
}

TEST_CASE("nexenne::utility::flatten rvalue moves a move-only inner value out") {
  using exp_ptr = std::expected<std::unique_ptr<int>, std::string>;
  using nested = std::expected<exp_ptr, std::string>;
  auto r{util::flatten(nested{exp_ptr{std::make_unique<int>(11)}})};
  REQUIRE(r.has_value());
  REQUIRE(r.value() != nullptr);
  CHECK(*r.value() == 11);
}

TEST_CASE("nexenne::utility::first_error returns the earliest of several errors") {
  auto const r{util::first_error(
    evoid{}, evoid{}, evoid{std::unexpect, "third"}, evoid{std::unexpect, "fourth"}, evoid{}
  )};
  REQUIRE_FALSE(r.has_value());
  CHECK(r.error() == "third");
}

TEST_CASE("nexenne::utility::first_error with a single success and a single failure") {
  CHECK(util::first_error(evoid{}).has_value());
  CHECK(util::first_error(evoid{std::unexpect, "only"}).error() == "only");
}

TEST_CASE("nexenne::utility::first_error error at the very last position is found") {
  auto const r{util::first_error(evoid{}, evoid{}, evoid{}, evoid{std::unexpect, "last"})};
  REQUIRE_FALSE(r.has_value());
  CHECK(r.error() == "last");
}

TEST_CASE("nexenne::utility::first_error does not short-circuit argument evaluation") {
  int built{0};
  auto make_ok{[&]() -> evoid {
    ++built;
    return evoid{};
  }};
  auto make_err{[&]() -> evoid {
    ++built;
    return evoid{std::unexpect, "fail"};
  }};

  auto const r{util::first_error(make_err(), make_ok(), make_ok())};
  REQUIRE_FALSE(r.has_value());
  CHECK(r.error() == "fail");
  CHECK(built == 3);
}

TEST_CASE("nexenne::utility::try_or does not invoke the fallback on the value path") {
  int calls{0};
  auto const v{util::try_or(exp{17}, [&](std::string const&) {
    ++calls;
    return -1;
  })};
  CHECK(v == 17);
  CHECK(calls == 0);
}

TEST_CASE("nexenne::utility::try_or passes the error to the fallback") {
  std::string seen;
  auto const v{util::try_or(exp{std::unexpect, "details"}, [&](std::string const& e) {
    seen = e;
    return static_cast<int>(e.size());
  })};
  CHECK(seen == "details");
  CHECK(v == 7);
}

TEST_CASE("nexenne::utility::try_or with non-trivial value type returns a copy of the value") {
  using exp_str = std::expected<std::string, std::string>;
  exp_str const ok{"kept"};
  auto const v{util::try_or(ok, [](std::string const&) { return std::string{"fallback"}; })};
  CHECK(v == "kept");

  exp_str const bad{std::unexpect, "err"};
  auto const w{util::try_or(bad, [](std::string const& e) { return "from-" + e; })};
  CHECK(w == "from-err");
}

TEST_CASE("nexenne::utility::try_or rvalue overload moves a move-only value out on success") {
  using exp_ptr = std::expected<std::unique_ptr<int>, std::string>;
  auto v{util::try_or(exp_ptr{std::make_unique<int>(88)}, [](std::string&&) {
    return std::make_unique<int>(-1);
  })};
  REQUIRE(v != nullptr);
  CHECK(*v == 88);
}

TEST_CASE("nexenne::utility::try_or rvalue overload moves the error into the fallback") {
  using exp_ptr = std::expected<std::unique_ptr<int>, std::string>;
  auto v{util::try_or(exp_ptr{std::unexpect, "rvalue-error"}, [](std::string&& e) {
    auto out{std::make_unique<int>(static_cast<int>(e.size()))};
    return out;
  })};
  REQUIRE(v != nullptr);
  CHECK(*v == 12);
}

TEST_CASE("nexenne::utility chaining: transform/and_then succeed then flatten") {
  auto produce{[](int x) -> std::expected<exp, std::string> {
    if (x < 0) {
      return std::unexpected{std::string{"negative"}};
    }
    return exp{x * 10};
  }};

  auto const good{util::flatten(produce(4))};
  REQUIRE(good.has_value());
  CHECK(good.value() == 40);

  auto const outer{util::flatten(produce(-1))};
  REQUIRE_FALSE(outer.has_value());
  CHECK(outer.error() == "negative");
}

TEST_CASE("nexenne::utility chaining short-circuits: later step not called after an error") {
  int second_calls{0};
  auto const result{exp{std::unexpect, "early"}.and_then(
                                                 [](int x) -> exp { return exp{x + 1}; }
  ).transform([&](int x) {
    ++second_calls;
    return x * 2;
  })};
  auto const value{util::try_or(result, [](std::string const& e) {
    return e == "early" ? -100 : -1;
  })};
  CHECK(value == -100);
  CHECK(second_calls == 0);
}

TEST_CASE("nexenne::utility chaining: error injected in the middle of a chain") {
  int tail_calls{0};
  auto const result{exp{5}.and_then(
                            [](int x) -> exp { return exp{x + 5}; }
  ).and_then([](int) -> exp {
     return exp{std::unexpect, "mid"};
   }).transform([&](int x) {
    ++tail_calls;
    return x;
  })};
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error() == "mid");
  CHECK(tail_calls == 0);
  CHECK(util::into_optional(result) == std::nullopt);
}

TEST_CASE("nexenne::utility chaining with void-returning steps via first_error") {
  int step_runs{0};
  auto step{[&](bool ok, char const* msg) -> evoid {
    ++step_runs;
    if (ok) {
      return evoid{};
    }
    return evoid{std::unexpect, std::string{msg}};
  }};

  auto const r{util::first_error(step(true, "a"), step(false, "b"), step(false, "c"))};
  REQUIRE_FALSE(r.has_value());
  CHECK(r.error() == "b");
  CHECK(step_runs == 3);

  step_runs = 0;
  auto const ok{util::first_error(step(true, "a"), step(true, "b"))};
  CHECK(ok.has_value());
  CHECK(step_runs == 2);
}

TEST_CASE("nexenne::utility chaining: void expected combined with into_optional bool") {
  auto const ok{evoid{}.and_then([]() -> evoid { return evoid{}; }).and_then([]() -> evoid {
    return evoid{};
  })};
  CHECK(util::into_optional(ok) == true);

  auto const bad{evoid{}.and_then(
                          []() -> evoid { return evoid{std::unexpect, "stop"}; }
  ).and_then([]() -> evoid { return evoid{}; })};
  CHECK(util::into_optional(bad) == false);
}

TEST_CASE("nexenne::utility chaining: deeply nested expected flattened then into_optional") {
  using inner = std::expected<int, std::string>;
  using middle = std::expected<inner, std::string>;

  middle const ok{inner{7}};
  auto const opt{util::into_optional(util::flatten(ok))};
  REQUIRE(opt.has_value());
  CHECK(*opt == 7);

  middle const inner_bad{inner{std::unexpect, "deep-error"}};
  CHECK(util::into_optional(util::flatten(inner_bad)) == std::nullopt);
}

TEST_CASE("nexenne::utility chaining: try_or supplies a default that flows into more work") {
  auto const recovered{util::try_or(exp{std::unexpect, "lost"}, [](std::string const&) {
    return 3;
  })};
  auto const final{exp{recovered}.transform([](int x) { return x * x; })};
  REQUIRE(final.has_value());
  CHECK(final.value() == 9);
}

}  // namespace
