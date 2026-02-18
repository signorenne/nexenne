/**
 * @file
 * @brief Tests for the nexenne::container error codes and result alias.
 */

#include <doctest/doctest.h>

#include <expected>
#include <string_view>
#include <type_traits>

#include <nexenne/container/bitset_dynamic.hpp>
#include <nexenne/container/error.hpp>
#include <nexenne/container/indexed_priority_queue.hpp>
#include <nexenne/container/ring_buffer.hpp>

namespace {

namespace cn = nexenne::container;

static_assert(std::is_same_v<cn::result<int>, std::expected<int, cn::container_error>>);

static_assert(cn::to_string(cn::container_error::full) == "full");
static_assert(cn::to_string(cn::container_error::empty) == "empty");
static_assert(cn::to_string(cn::container_error::out_of_range) == "out_of_range");
static_assert(cn::to_string(cn::container_error::not_found) == "not_found");

// to_string is constexpr and returns a stable, program-lifetime view.
static_assert(std::is_same_v<decltype(cn::to_string(cn::container_error::full)), std::string_view>);
static_assert(noexcept(cn::to_string(cn::container_error::full)));

TEST_CASE("nexenne::container::result carries either a value or an error") {
  cn::result<int> const ok{42};
  cn::result<int> const bad{std::unexpect, cn::container_error::empty};

  CHECK(ok.has_value());
  CHECK(ok.value() == 42);
  CHECK_FALSE(bad.has_value());
  CHECK(bad.error() == cn::container_error::empty);
  CHECK(cn::to_string(bad.error()) == "empty");
}

TEST_CASE("nexenne::container::to_string names every enumerator distinctly") {
  CHECK(cn::to_string(cn::container_error::full) == "full");
  CHECK(cn::to_string(cn::container_error::empty) == "empty");
  CHECK(cn::to_string(cn::container_error::out_of_range) == "out_of_range");
  CHECK(cn::to_string(cn::container_error::not_found) == "not_found");
  // All four names are distinct.
  CHECK(cn::to_string(cn::container_error::full) != cn::to_string(cn::container_error::empty));
  CHECK(
    cn::to_string(cn::container_error::out_of_range)
    != cn::to_string(cn::container_error::not_found)
  );
}

TEST_CASE("nexenne::container real operations surface container_error::full") {
  cn::ring_buffer<int, 2> r;  // capacity 2
  REQUIRE(r.push(1).has_value());
  REQUIRE(r.push(2).has_value());
  auto const overflow{r.push(3)};
  REQUIRE_FALSE(overflow.has_value());
  CHECK(overflow.error() == cn::container_error::full);
}

TEST_CASE("nexenne::container real operations surface container_error::empty") {
  cn::ring_buffer<int, 2> r;
  auto const drained{r.pop()};
  REQUIRE_FALSE(drained.has_value());
  CHECK(drained.error() == cn::container_error::empty);
}

TEST_CASE("nexenne::container real operations surface container_error::out_of_range") {
  cn::bitset_dynamic const b(4);
  auto const past_end{b.test(4)};
  REQUIRE_FALSE(past_end.has_value());
  CHECK(past_end.error() == cn::container_error::out_of_range);
}

TEST_CASE("nexenne::container real operations surface container_error::not_found") {
  cn::indexed_priority_queue<int> q;
  auto const h{q.push(10)};
  REQUIRE(q.erase(h).has_value());
  // The handle is now stale, so a second erase reports not_found.
  auto const again{q.erase(h)};
  REQUIRE_FALSE(again.has_value());
  CHECK(again.error() == cn::container_error::not_found);
}

}  // namespace
