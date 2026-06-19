/**
 * @file
 * @brief Tests for nexenne::utility::overloaded.
 */

#include <doctest/doctest.h>

#include <string>
#include <type_traits>
#include <variant>

#include <nexenne/utility/overloaded.hpp>

namespace {

namespace util = nexenne::utility;

// CTAD deduces overloaded<Ts...> from the constructor arguments.
static_assert(std::is_aggregate_v<util::overloaded<>>);

TEST_CASE("nexenne::utility::overloaded dispatches a variant by alternative") {
  using nexenne::utility::overloaded;
  using payload = std::variant<int, std::string>;

  auto const classify{[](payload const& value) -> std::string {
    return std::visit(
      overloaded{
        [](int) { return std::string{"int"}; },
        [](std::string const&) { return std::string{"string"}; },
      },
      value
    );
  }};

  CHECK(classify(payload{42}) == "int");
  CHECK(classify(payload{std::string{"hi"}}) == "string");
}

TEST_CASE("nexenne::utility::overloaded invokes directly and falls back to a generic handler") {
  auto const dispatch{nexenne::utility::overloaded{
    [](int) { return 1; },
    [](double) { return 2; },
    [](auto const&) { return 0; },  // generic catch-all
  }};

  CHECK(dispatch(5) == 1);
  CHECK(dispatch(3.0) == 2);
  CHECK(dispatch("text") == 0);  // neither int nor double: generic handler
}

TEST_CASE("nexenne::utility::overloaded merges two lambdas") {
  auto const dispatch{util::overloaded{
    [](int) { return 'i'; },
    [](char const*) { return 's'; },
  }};

  CHECK(dispatch(0) == 'i');
  CHECK(dispatch("x") == 's');
}

TEST_CASE("nexenne::utility::overloaded merges three lambdas") {
  auto const dispatch{util::overloaded{
    [](int) { return 1; },
    [](double) { return 2; },
    [](char) { return 3; },
  }};

  CHECK(dispatch(0) == 1);
  CHECK(dispatch(0.0) == 2);
  CHECK(dispatch('a') == 3);
}

TEST_CASE("nexenne::utility::overloaded merges N lambdas") {
  auto const dispatch{util::overloaded{
    [](int) { return 1; },
    [](double) { return 2; },
    [](char) { return 3; },
    [](bool) { return 4; },
    [](long) { return 5; },
  }};

  CHECK(dispatch(true) == 4);  // bool resolves before int/long
  CHECK(dispatch(5L) == 5);
  CHECK(dispatch(0.0) == 2);
}

TEST_CASE("nexenne::utility::overloaded supports a mutable stateful lambda") {
  auto counter{0};
  auto dispatch{util::overloaded{
    [count = 0](int) mutable { return ++count; },
    // Also mutable: mixing a non-const (mutable) and a const call operator whose
    // parameter types interconvert (int/double) makes the call ambiguous, because
    // the object-constness and argument conversions rank in opposite directions.
    [&counter](double) mutable { return ++counter; },
  }};

  CHECK(dispatch(0) == 1);  // mutable capture advances
  CHECK(dispatch(0) == 2);
  CHECK(dispatch(0) == 3);

  CHECK(dispatch(0.0) == 1);  // reference capture mutates the outer variable
  CHECK(dispatch(0.0) == 2);
  CHECK(counter == 2);
}

TEST_CASE("nexenne::utility::overloaded reference captures share outer state") {
  auto total{0};
  auto last{std::string{}};
  auto const sink{util::overloaded{
    [&total](int v) { total += v; },
    [&last](std::string const& s) { last = s; },
  }};

  sink(10);
  sink(5);
  sink(std::string{"done"});

  CHECK(total == 15);
  CHECK(last == "done");
}

TEST_CASE("nexenne::utility::overloaded deduces a common visit return type") {
  using payload = std::variant<int, double, char>;

  auto const to_double{[](payload const& v) -> double {
    return std::visit(
      util::overloaded{
        [](int x) { return static_cast<double>(x); },
        [](double x) { return x; },
        [](char x) { return static_cast<double>(x); },
      },
      v
    );
  }};

  CHECK(to_double(payload{3}) == doctest::Approx(3.0));
  CHECK(to_double(payload{2.5}) == doctest::Approx(2.5));
  CHECK(to_double(payload{char{4}}) == doctest::Approx(4.0));
}

TEST_CASE("nexenne::utility::overloaded is callable when const") {
  util::overloaded const dispatch{
    [](int) { return 1; },
    [](double) { return 2; },
  };

  CHECK(dispatch(0) == 1);
  CHECK(dispatch(0.0) == 2);
}

TEST_CASE("nexenne::utility::overloaded visits every alternative of a variant") {
  using payload = std::variant<int, double, std::string, char>;

  auto const name{[](payload const& v) -> std::string {
    return std::visit(
      util::overloaded{
        [](int) { return std::string{"int"}; },
        [](double) { return std::string{"double"}; },
        [](std::string const&) { return std::string{"string"}; },
        [](char) { return std::string{"char"}; },
      },
      v
    );
  }};

  CHECK(name(payload{1}) == "int");
  CHECK(name(payload{1.0}) == "double");
  CHECK(name(payload{std::string{"hi"}}) == "string");
  CHECK(name(payload{'z'}) == "char");
}

TEST_CASE("nexenne::utility::overloaded visits a single-alternative variant") {
  using payload = std::variant<int>;
  auto const v{payload{7}};
  auto const out{std::visit(util::overloaded{[](int x) { return x * 2; }}, v)};
  CHECK(out == 14);
}

}  // namespace
