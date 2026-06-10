/**
 * @file
 * @brief Tests for nexenne::utility::unique_resource.
 */

#include <doctest/doctest.h>

#include <functional>
#include <utility>
#include <vector>

#include <nexenne/utility/unique_resource.hpp>

namespace {

namespace util = nexenne::utility;

TEST_CASE("nexenne::utility::unique_resource runs the deleter once at destruction") {
  int closed{-1};
  {
    auto r{util::unique_resource{7, [&](int fd) { closed = fd; }}};
    CHECK(r.owns());
    CHECK(r.get() == 7);
    CHECK(closed == -1);
  }
  CHECK(closed == 7);
}

TEST_CASE("nexenne::utility::unique_resource release suppresses the deleter") {
  int closed{-1};
  {
    auto r{util::unique_resource{7, [&](int fd) { closed = fd; }}};
    auto const fd{r.release()};
    CHECK(fd == 7);
    CHECK_FALSE(r.owns());
  }
  CHECK(closed == -1);
}

TEST_CASE("nexenne::utility::unique_resource move transfers ownership; deleter runs once") {
  int closes{0};
  {
    auto a{util::unique_resource{7, [&](int) { ++closes; }}};
    auto b{std::move(a)};
    CHECK_FALSE(a.owns());
    CHECK(b.owns());
    CHECK(closes == 0);
  }
  CHECK(closes == 1);
}

TEST_CASE("nexenne::utility::unique_resource reset replaces the resource") {
  std::vector<int> closed;
  auto r{util::unique_resource{1, [&](int v) { closed.push_back(v); }}};
  r.reset(2);
  CHECK(r.get() == 2);
  CHECK(closed == std::vector{1});
  r.reset();
  CHECK_FALSE(r.owns());
  CHECK(closed == std::vector{1, 2});
}

TEST_CASE("nexenne::utility::make_unique_resource_checked skips the invalid sentinel") {
  int closes{0};
  auto const closer{[&](int) { ++closes; }};
  {
    auto bad{util::make_unique_resource_checked(-1, -1, closer)};
    CHECK_FALSE(bad.owns());
  }
  CHECK(closes == 0);
  {
    auto good{util::make_unique_resource_checked(3, -1, closer)};
    CHECK(good.owns());
  }
  CHECK(closes == 1);
}

TEST_CASE("nexenne::utility::unique_resource gives pointer access") {
  int obj{42};
  bool closed{false};
  auto r{util::unique_resource{&obj, [&](int*) { closed = true; }}};
  CHECK(*r == 42);
  CHECK(r.get() == &obj);

  struct gadget {
    int v{0};

    auto value() const -> int {
      return v;
    }
  };

  gadget g{5};
  auto gr{util::unique_resource{&g, [](gadget*) {}}};
  CHECK(gr->value() == 5);
}

TEST_CASE("nexenne::utility::unique_resource move-assign releases the old resource") {
  int closes_a{0};
  int closes_b{0};
  {
    // Same deleter type (std::function) so the two are the same move-assignable type.
    using owner = util::unique_resource<int, std::function<void(int)>>;
    owner a{1, [&](int) { ++closes_a; }};
    owner b{2, [&](int) { ++closes_b; }};
    a = std::move(b);
    CHECK(closes_a == 1);  // a's original resource released on assignment
    CHECK(a.get() == 2);
    CHECK_FALSE(b.owns());
  }
  CHECK(closes_b == 1);  // a (now owning b's resource and deleter) released at scope exit
}

TEST_CASE("nexenne::utility::unique_resource self-move and double-reset are safe") {
  int closes{0};
  auto r{util::unique_resource{1, [&](int) { ++closes; }}};
  auto& alias{r};
  r = std::move(alias);  // guarded self-move
  CHECK(r.owns());
  CHECK(closes == 0);

  r.reset();
  r.reset();  // idempotent: deleter does not run twice
  CHECK(closes == 1);
}

}  // namespace
