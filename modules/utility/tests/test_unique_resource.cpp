/**
 * @file
 * @brief Tests for nexenne::utility::unique_resource.
 */

#include <doctest/doctest.h>

#include <functional>
#include <memory>
#include <type_traits>
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
  CHECK(closed == 7);  // deleter ran with the right resource value
}

TEST_CASE("nexenne::utility::unique_resource default-constructed owns nothing and runs no deleter"
) {
  int closes{0};
  {
    util::unique_resource<int, std::function<void(int)>> r;
    CHECK_FALSE(r.owns());
    r = util::unique_resource<int, std::function<void(int)>>{};  // still nothing
    CHECK_FALSE(r.owns());
  }
  CHECK(closes == 0);
}

TEST_CASE("nexenne::utility::unique_resource deleter runs exactly once, never twice") {
  int closes{0};
  {
    auto r{util::unique_resource{7, [&](int) { ++closes; }}};
    static_cast<void>(r);
  }
  CHECK(closes == 1);
}

TEST_CASE("nexenne::utility::unique_resource release suppresses the deleter and returns the value"
) {
  int closed{-1};
  {
    auto r{util::unique_resource{7, [&](int fd) { closed = fd; }}};
    auto const fd{r.release()};
    CHECK(fd == 7);  // release returns the owned resource
    CHECK_FALSE(r.owns());
  }
  CHECK(closed == -1);  // deleter never ran
}

TEST_CASE("nexenne::utility::unique_resource double release is safe and idempotent") {
  int closes{0};
  auto r{util::unique_resource{7, [&](int) { ++closes; }}};
  auto const first{r.release()};
  auto const second{r.release()};  // already non-owning
  CHECK(first == 7);
  CHECK(second == 7);  // resource value still readable, deleter disarmed
  CHECK_FALSE(r.owns());
  CHECK(closes == 0);
}

TEST_CASE("nexenne::utility::unique_resource get and get_deleter access the stored members") {
  auto const deleter{[](int) {}};
  auto r{util::unique_resource{7, deleter}};
  CHECK(r.get() == 7);
  // get_deleter returns a reference to the stored deleter; verify it is callable.
  r.get_deleter()(r.get());
  CHECK(r.owns());  // calling get_deleter() does not change ownership
}

TEST_CASE(
  "nexenne::utility::unique_resource move-construct transfers ownership; only the destination fires"
) {
  int closes{0};
  {
    auto a{util::unique_resource{7, [&](int) { ++closes; }}};
    auto b{std::move(a)};
    CHECK_FALSE(a.owns());  // source disarmed
    CHECK(b.owns());
    CHECK(b.get() == 7);
    CHECK(closes == 0);
  }
  CHECK(closes == 1);  // only b's deleter ran
}

TEST_CASE("nexenne::utility::unique_resource moving from a non-owning source stays non-owning") {
  int closes{0};
  {
    auto a{util::unique_resource{7, [&](int) { ++closes; }}};
    static_cast<void>(a.release());  // a now owns nothing
    auto b{std::move(a)};
    CHECK_FALSE(a.owns());
    CHECK_FALSE(b.owns());  // ownership flag transferred faithfully
  }
  CHECK(closes == 0);
}

TEST_CASE("nexenne::utility::unique_resource reset() releases without adopting a new resource") {
  std::vector<int> closed;
  auto r{util::unique_resource{1, [&](int v) { closed.push_back(v); }}};
  r.reset();
  CHECK_FALSE(r.owns());
  CHECK(closed == std::vector{1});
}

TEST_CASE(
  "nexenne::utility::unique_resource reset(value) runs the old deleter and adopts the new value"
) {
  std::vector<int> closed;
  auto r{util::unique_resource{1, [&](int v) { closed.push_back(v); }}};
  r.reset(2);
  CHECK(r.owns());
  CHECK(r.get() == 2);
  CHECK(closed == std::vector{1});  // old resource released, new one adopted
  r.reset();
  CHECK_FALSE(r.owns());
  CHECK(closed == std::vector{1, 2});  // new resource released too, via the same deleter
}

TEST_CASE(
  "nexenne::utility::unique_resource reset(value) on a non-owning instance adopts without deleting"
) {
  std::vector<int> closed;
  auto r{util::unique_resource{1, [&](int v) { closed.push_back(v); }}};
  static_cast<void>(r.release());  // non-owning, holds no live resource
  r.reset(9);
  CHECK(r.owns());
  CHECK(r.get() == 9);
  CHECK(closed.empty());  // nothing was owned, so nothing was deleted on adopt
  r.reset();
  CHECK(closed == std::vector{9});
}

TEST_CASE("nexenne::utility::make_unique_resource_checked skips the invalid sentinel") {
  int closes{0};
  auto const closer{[&](int) { ++closes; }};
  {
    auto bad{util::make_unique_resource_checked(-1, -1, closer)};
    CHECK_FALSE(bad.owns());  // resource == invalid: ownership released
  }
  CHECK(closes == 0);
  {
    auto good{util::make_unique_resource_checked(3, -1, closer)};
    CHECK(good.owns());
    CHECK(good.get() == 3);
  }
  CHECK(closes == 1);
}

TEST_CASE("nexenne::utility::make_unique_resource_checked compares with a heterogeneous sentinel") {
  int closes{0};
  auto const closer{[&](long) { ++closes; }};
  // Resource is long, invalid sentinel is int; comparison is via operator==.
  auto bad{util::make_unique_resource_checked(long{-1}, -1, closer)};
  CHECK_FALSE(bad.owns());
  auto good{util::make_unique_resource_checked(long{5}, -1, closer)};
  CHECK(good.owns());
  static_cast<void>(good.release());  // avoid the +1 from good
  CHECK(closes == 0);
}

TEST_CASE("nexenne::utility::unique_resource gives pointer access") {
  int obj{42};
  bool closed{false};
  auto r{util::unique_resource{&obj, [&](int*) { closed = true; }}};
  CHECK(*r == 42);
  CHECK(r.get() == &obj);

  // operator* yields a mutable reference: writes go through to the object.
  *r = 7;
  CHECK(obj == 7);

  struct gadget {
    int v{0};

    auto value() const -> int {
      return v;
    }
  };

  gadget g{5};
  auto gr{util::unique_resource{&g, [](gadget*) {}}};
  CHECK(gr->value() == 5);
  CHECK(gr.get() == &g);
}

TEST_CASE("nexenne::utility::unique_resource owns a unique_ptr as a move-only resource") {
  auto held{std::make_unique<int>(3)};
  int observed{0};
  auto r{util::unique_resource{std::move(held), [&](std::unique_ptr<int> const& p) {
                                 if (p) {
                                   observed = *p;
                                 }
                               }}};
  CHECK(r.owns());
  CHECK(*r.get() == 3);
  r.reset();
  CHECK(observed == 3);  // deleter saw the live resource before reset cleared owns
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

TEST_CASE("nexenne::utility::unique_resource move-assign from a non-owning source disarms target") {
  int closes_a{0};
  int closes_b{0};
  using owner = util::unique_resource<int, std::function<void(int)>>;
  owner a{1, [&](int) { ++closes_a; }};
  owner b{2, [&](int) { ++closes_b; }};
  static_cast<void>(b.release());  // b owns nothing
  a = std::move(b);
  CHECK(closes_a == 1);   // a's old resource was released
  CHECK_FALSE(a.owns());  // and a took over b's non-owning state
  CHECK(closes_b == 0);
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

TEST_CASE("nexenne::utility::unique_resource deleter observes the current resource at delete time"
) {
  std::vector<int> closed;
  auto r{util::unique_resource{10, [&](int v) { closed.push_back(v); }}};
  r.reset(20);  // deletes 10
  r.reset(30);  // deletes 20
  CHECK(r.get() == 30);
  CHECK(closed == std::vector{10, 20});
  r.reset();  // deletes 30
  CHECK(closed == std::vector{10, 20, 30});
}

// unique_resource is move-only, never copyable.
static_assert(
  !std::is_copy_constructible_v<util::unique_resource<int, std::function<void(int)>>>,
  "unique_resource is non-copyable"
);
static_assert(
  !std::is_copy_assignable_v<util::unique_resource<int, std::function<void(int)>>>,
  "unique_resource is non-copy-assignable"
);
static_assert(
  std::is_move_constructible_v<util::unique_resource<int, std::function<void(int)>>>,
  "unique_resource is move-constructible"
);
static_assert(
  std::is_move_assignable_v<util::unique_resource<int, std::function<void(int)>>>,
  "unique_resource is move-assignable"
);

// Nested type aliases.
static_assert(
  std::is_same_v<util::unique_resource<int, void (*)(int)>::resource_type, int>,
  "unique_resource exposes resource_type"
);
static_assert(
  std::is_same_v<util::unique_resource<int, void (*)(int)>::deleter_type, void (*)(int)>,
  "unique_resource exposes deleter_type"
);

// CTAD from a resource and deleter; make_unique_resource_checked yields the same type.
static_assert(
  std::is_same_v<
    decltype(util::unique_resource{std::declval<int>(), std::declval<void (*)(int)>()}),
    util::unique_resource<int, void (*)(int)>>,
  "unique_resource CTAD deduces R and D from its arguments"
);

// operator-> exists for a pointer resource (the non-pointer absence is a
// constrained-away member; its negative is compiler-fragile to assert inline,
// and is covered behaviourally by the pointer-resource access tests above).
static_assert(requires(util::unique_resource<int*, void (*)(int*)> r) { r.operator->(); });

}  // namespace
