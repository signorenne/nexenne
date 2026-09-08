/**
 * @file
 * @brief Tests for nexenne::utility::unique_resource.
 */

#include <doctest/doctest.h>

#include <functional>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include <nexenne/utility/ignore.hpp>
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

TEST_CASE(
  "nexenne::utility::unique_resource default-constructed owns nothing and runs no deleter"
) {
  int closes{0};
  {
    util::unique_resource<int, std::function<void(int)>> r;
    CHECK_FALSE(r.owns());
    r = util::unique_resource<int, std::function<void(int)>>{};
    CHECK_FALSE(r.owns());
  }
  CHECK(closes == 0);
}

TEST_CASE("nexenne::utility::unique_resource deleter runs exactly once, never twice") {
  int closes{0};
  {
    [[maybe_unused]] auto r{util::unique_resource{7, [&](int) { ++closes; }}};
  }
  CHECK(closes == 1);
}

TEST_CASE(
  "nexenne::utility::unique_resource release suppresses the deleter and returns the value"
) {
  int closed{-1};
  {
    auto r{util::unique_resource{7, [&](int fd) { closed = fd; }}};
    auto const fd{r.release()};
    CHECK(fd == 7);
    CHECK_FALSE(r.owns());
  }
  CHECK(closed == -1);
}

TEST_CASE("nexenne::utility::unique_resource double release is safe and idempotent") {
  int closes{0};
  auto r{util::unique_resource{7, [&](int) { ++closes; }}};
  auto const first{r.release()};
  auto const second{r.release()};
  CHECK(first == 7);
  CHECK(second == 7);
  CHECK_FALSE(r.owns());
  CHECK(closes == 0);
}

TEST_CASE("nexenne::utility::unique_resource get and get_deleter access the stored members") {
  auto const deleter{[](int) {}};
  auto r{util::unique_resource{7, deleter}};
  CHECK(r.get() == 7);
  r.get_deleter()(r.get());
  CHECK(r.owns());
}

TEST_CASE(
  "nexenne::utility::unique_resource move-construct transfers ownership; only the destination fires"
) {
  int closes{0};
  {
    auto a{util::unique_resource{7, [&](int) { ++closes; }}};
    auto b{std::move(a)};
    CHECK_FALSE(a.owns());
    CHECK(b.owns());
    CHECK(b.get() == 7);
    CHECK(closes == 0);
  }
  CHECK(closes == 1);
}

TEST_CASE("nexenne::utility::unique_resource moving from a non-owning source stays non-owning") {
  int closes{0};
  {
    auto a{util::unique_resource{7, [&](int) { ++closes; }}};
    util::ignore(a.release());
    auto b{std::move(a)};
    CHECK_FALSE(a.owns());
    CHECK_FALSE(b.owns());
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
  CHECK(closed == std::vector{1});
  r.reset();
  CHECK_FALSE(r.owns());
  CHECK(closed == std::vector{1, 2});
}

TEST_CASE(
  "nexenne::utility::unique_resource reset(value) on a non-owning instance adopts without deleting"
) {
  std::vector<int> closed;
  auto r{util::unique_resource{1, [&](int v) { closed.push_back(v); }}};
  util::ignore(r.release());
  r.reset(9);
  CHECK(r.owns());
  CHECK(r.get() == 9);
  CHECK(closed.empty());
  r.reset();
  CHECK(closed == std::vector{9});
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
    CHECK(good.get() == 3);
  }
  CHECK(closes == 1);
}

TEST_CASE("nexenne::utility::make_unique_resource_checked compares with a heterogeneous sentinel") {
  int closes{0};
  auto const closer{[&](long) { ++closes; }};
  auto bad{util::make_unique_resource_checked(long{-1}, -1, closer)};
  CHECK_FALSE(bad.owns());
  auto good{util::make_unique_resource_checked(long{5}, -1, closer)};
  CHECK(good.owns());
  util::ignore(good.release());
  CHECK(closes == 0);
}

TEST_CASE("nexenne::utility::unique_resource gives pointer access") {
  int obj{42};
  bool closed{false};
  auto r{util::unique_resource{&obj, [&](int*) { closed = true; }}};
  CHECK(*r == 42);
  CHECK(r.get() == &obj);

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
  CHECK(observed == 3);
}

TEST_CASE(
  "nexenne::utility::unique_resource move-construct transfers a move-only resource faithfully"
) {
  int observed{0};
  {
    auto a{util::unique_resource{std::make_unique<int>(5), [&](std::unique_ptr<int>& p) {
                                   if (p) {
                                     observed = *p;
                                   }
                                 }}};
    auto b{std::move(a)};
    CHECK_FALSE(a.owns());
    CHECK(a.get() == nullptr);
    CHECK(b.owns());
    REQUIRE(b.get() != nullptr);
    CHECK(*b.get() == 5);
    CHECK(observed == 0);
  }
  CHECK(observed == 5);
}

TEST_CASE("nexenne::utility::unique_resource move-assign transfers a move-only resource") {
  std::vector<int> observed;
  using owner =
    util::unique_resource<std::unique_ptr<int>, std::function<void(std::unique_ptr<int>&)>>;
  {
    owner a{std::make_unique<int>(1), [&](std::unique_ptr<int>& p) {
              if (p) {
                observed.push_back(*p);
              }
            }};
    owner b{std::make_unique<int>(2), [&](std::unique_ptr<int>& p) {
              if (p) {
                observed.push_back(100 + *p);
              }
            }};
    a = std::move(b);
    CHECK(observed == std::vector{1});
    CHECK(a.owns());
    REQUIRE(a.get() != nullptr);
    CHECK(*a.get() == 2);
    CHECK_FALSE(b.owns());
  }
  CHECK(observed == std::vector{1, 102});
}

TEST_CASE(
  "nexenne::utility::unique_resource release on a move-only resource leaves get() moved-from"
) {
  auto r{util::unique_resource{std::make_unique<int>(9), [](std::unique_ptr<int>&) {}}};
  auto const held{r.release()};
  REQUIRE(held != nullptr);
  CHECK(*held == 9);
  CHECK_FALSE(r.owns());
  CHECK(r.get() == nullptr);
}

TEST_CASE("nexenne::utility::unique_resource move-assign releases the old resource") {
  int closes_a{0};
  int closes_b{0};
  {
    using owner = util::unique_resource<int, std::function<void(int)>>;
    owner a{1, [&](int) { ++closes_a; }};
    owner b{2, [&](int) { ++closes_b; }};
    a = std::move(b);
    CHECK(closes_a == 1);
    CHECK(a.get() == 2);
    CHECK_FALSE(b.owns());
  }
  CHECK(closes_b == 1);
}

TEST_CASE("nexenne::utility::unique_resource move-assign from a non-owning source disarms target") {
  int closes_a{0};
  int closes_b{0};
  using owner = util::unique_resource<int, std::function<void(int)>>;
  owner a{1, [&](int) { ++closes_a; }};
  owner b{2, [&](int) { ++closes_b; }};
  util::ignore(b.release());
  a = std::move(b);
  CHECK(closes_a == 1);
  CHECK_FALSE(a.owns());
  CHECK(closes_b == 0);
}

TEST_CASE(
  "nexenne::utility::unique_resource move-assign rebuilds a non-assignable lambda deleter"
) {
  std::vector<int> closed;
  auto const make{[&closed](int const v) {
    return util::unique_resource{v, [&closed](int const x) { closed.push_back(x); }};
  }};

  auto a{make(1)};
  auto b{make(2)};
  static_assert(!std::is_nothrow_move_assignable_v<decltype(a)>);
  static_assert(std::is_nothrow_move_constructible_v<decltype(a)>);

  a = std::move(b);
  CHECK(closed == std::vector{1});
  CHECK(a.owns());
  CHECK(a.get() == 2);
  CHECK_FALSE(b.owns());
  a.reset();
  CHECK(closed == std::vector{1, 2});
}

TEST_CASE("nexenne::utility::unique_resource self-move and double-reset are safe") {
  int closes{0};
  auto r{util::unique_resource{1, [&](int) { ++closes; }}};
  auto& alias{r};
  r = std::move(alias);
  CHECK(r.owns());
  CHECK(closes == 0);

  r.reset();
  r.reset();
  CHECK(closes == 1);
}

TEST_CASE(
  "nexenne::utility::unique_resource deleter observes the current resource at delete time"
) {
  std::vector<int> closed;
  auto r{util::unique_resource{10, [&](int v) { closed.push_back(v); }}};
  r.reset(20);
  r.reset(30);
  CHECK(r.get() == 30);
  CHECK(closed == std::vector{10, 20});
  r.reset();
  CHECK(closed == std::vector{10, 20, 30});
}

struct throwing_move_deleter {
  throwing_move_deleter() = default;
  throwing_move_deleter(throwing_move_deleter&&) noexcept(false);
  throwing_move_deleter(throwing_move_deleter const&) = default;
  auto operator=(throwing_move_deleter&&) noexcept(false) -> throwing_move_deleter&;
  auto operator=(throwing_move_deleter const&) -> throwing_move_deleter& = default;
  ~throwing_move_deleter() = default;

  auto operator()(int) const -> void;
};

static_assert(
  std::is_nothrow_move_constructible_v<util::unique_resource<int, void (*)(int)>>,
  "nothrow-movable members give a noexcept move constructor"
);
static_assert(
  std::is_nothrow_move_assignable_v<util::unique_resource<int, void (*)(int) noexcept>>,
  "nothrow-assignable members and a noexcept deleter give a noexcept move assignment"
);
static_assert(
  !std::is_nothrow_move_assignable_v<util::unique_resource<int, void (*)(int)>>,
  "move assignment releases the old resource, so a deleter that may throw makes it throwing"
);
static_assert(
  !std::is_nothrow_move_constructible_v<util::unique_resource<int, throwing_move_deleter>>,
  "a throwing-move deleter gives a potentially-throwing move constructor"
);
static_assert(
  !std::is_nothrow_move_assignable_v<util::unique_resource<int, throwing_move_deleter>>,
  "a throwing-assign deleter gives a potentially-throwing move assignment"
);

struct throwing_copy_deleter {
  throwing_copy_deleter() = default;
  throwing_copy_deleter(throwing_copy_deleter&&) noexcept(false);
  throwing_copy_deleter(throwing_copy_deleter const&) noexcept(false);
  auto operator=(throwing_copy_deleter&&) noexcept(false) -> throwing_copy_deleter&;
  auto operator=(throwing_copy_deleter const&) noexcept(false) -> throwing_copy_deleter&;
  ~throwing_copy_deleter() = default;

  auto operator()(int) const -> void;
};

static_assert(
  std::is_nothrow_constructible_v<util::unique_resource<int, void (*)(int)>, int, void (*)(int)>,
  "nothrow-movable members give a noexcept owning constructor"
);
static_assert(
  std::is_nothrow_constructible_v<
    util::unique_resource<int, throwing_move_deleter>,
    int,
    throwing_move_deleter>,
  "a throwing-move deleter with a nothrow copy is copied in, so the constructor is noexcept"
);
static_assert(
  !std::is_nothrow_constructible_v<
    util::unique_resource<int, throwing_copy_deleter>,
    int,
    throwing_copy_deleter>,
  "a deleter whose copy can throw gives a potentially-throwing owning constructor"
);

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

static_assert(
  std::is_same_v<util::unique_resource<int, void (*)(int)>::resource_type, int>,
  "unique_resource exposes resource_type"
);
static_assert(
  std::is_same_v<util::unique_resource<int, void (*)(int)>::deleter_type, void (*)(int)>,
  "unique_resource exposes deleter_type"
);

static_assert(
  std::is_same_v<
    decltype(util::unique_resource{std::declval<int>(), std::declval<void (*)(int)>()}),
    util::unique_resource<int, void (*)(int)>>,
  "unique_resource CTAD deduces R and D from its arguments"
);

struct fragile_handle {
  int fd{-1};

  fragile_handle() = default;

  explicit fragile_handle(int const f) noexcept : fd{f} {}

  fragile_handle(fragile_handle const&) = default;

  // NOLINTNEXTLINE(performance-noexcept-move-constructor): models a handle whose move throws
  fragile_handle(fragile_handle&& other) : fd{other.fd} {
    if (fd == 13) {
      throw std::runtime_error{"fragile_handle: move"};
    }
  }

  auto operator=(fragile_handle const&) -> fragile_handle& = default;

  auto operator=(fragile_handle&&) noexcept -> fragile_handle& = default;

  ~fragile_handle() = default;
};

struct counting_closer {
  std::vector<int>* closed{nullptr};
  bool copy_throws{false};

  counting_closer(std::vector<int>* const sink, bool const throws) noexcept
      : closed{sink}, copy_throws{throws} {}

  counting_closer(counting_closer const& other)
      : closed{other.closed}, copy_throws{other.copy_throws} {
    if (copy_throws) {
      throw std::runtime_error{"counting_closer: copy"};
    }
  }

  // Models a deleter whose move throws like its copy, hence the delegation.
  // NOLINTBEGIN(performance-noexcept-move-constructor,cert-oop11-cpp,performance-move-constructor-init)
  counting_closer(counting_closer&& other) noexcept(false)
      : counting_closer{static_cast<counting_closer const&>(other)} {}

  // NOLINTEND(performance-noexcept-move-constructor,cert-oop11-cpp,performance-move-constructor-init)

  auto operator=(counting_closer const&) -> counting_closer& = default;

  auto operator=(counting_closer&&) -> counting_closer& = default;

  ~counting_closer() = default;

  auto operator()(int const fd) const noexcept -> void {
    closed->push_back(fd);
  }
};

TEST_CASE("nexenne::utility::unique_resource construction releases the handle when a copy throws") {
  std::vector<int> closed;
  counting_closer const closer{&closed, true};
  CHECK_THROWS_AS((util::unique_resource<int, counting_closer>{4, closer}), std::runtime_error);
  CHECK(closed == std::vector{4});

  closed.clear();
  counting_closer moved{&closed, true};
  CHECK_THROWS_AS(
    (util::unique_resource<int, counting_closer>{5, std::move(moved)}), std::runtime_error
  );
  CHECK(closed == std::vector{5});
}

TEST_CASE("nexenne::utility::unique_resource copies a deleter whose move can throw") {
  std::vector<int> closed;
  {
    counting_closer source{&closed, false};
    util::unique_resource<int, counting_closer> const r{6, std::move(source)};
    CHECK(r.owns());
  }
  CHECK(closed == std::vector{6});
}

TEST_CASE("nexenne::utility::make_unique_resource_checked never deletes the sentinel") {
  std::vector<int> closed;
  counting_closer const closer{&closed, true};
  CHECK_THROWS_AS(
    util::ignore(util::make_unique_resource_checked(7, -1, closer)), std::runtime_error
  );
  CHECK(closed == std::vector{7});
  closed.clear();
  CHECK_THROWS_AS(
    util::ignore(util::make_unique_resource_checked(-1, -1, closer)), std::runtime_error
  );
  CHECK(closed.empty());
  auto const quiet{util::make_unique_resource_checked(-1, -1, counting_closer{&closed, false})};
  CHECK_FALSE(quiet.owns());
  CHECK(quiet.get() == -1);
  CHECK(closed.empty());
}

TEST_CASE("nexenne::utility::unique_resource release keeps ownership when the move throws") {
  auto closed{0};
  auto const closer{[&closed](fragile_handle const&) noexcept { ++closed; }};
  {
    util::unique_resource<fragile_handle, decltype(closer)> r{fragile_handle{5}, closer};
    r.reset(fragile_handle{13});
    static_assert(!noexcept(r.release()), "release can throw exactly when the move can");
    CHECK_THROWS_AS(util::ignore(r.release()), std::runtime_error);
    CHECK(r.owns());
    CHECK(r.get().fd == 13);
    CHECK(closed == 1);
  }
  CHECK(closed == 2);
}

static_assert(requires(util::unique_resource<int*, void (*)(int*)> r) { r.operator->(); });

TEST_CASE("nexenne::utility::unique_resource reset is noexcept exactly when its deleter is") {
  auto const quiet{[](int) noexcept {}};
  auto const loud{[](int) {}};
  using quiet_resource = util::unique_resource<int, std::remove_const_t<decltype(quiet)>>;
  using loud_resource = util::unique_resource<int, std::remove_const_t<decltype(loud)>>;
  static_assert(noexcept(std::declval<quiet_resource&>().reset()));
  static_assert(!noexcept(std::declval<loud_resource&>().reset()));
  static_assert(std::is_nothrow_destructible_v<loud_resource>);
  CHECK(true);
}

TEST_CASE("nexenne::utility::unique_resource reset propagates a throwing deleter, disarmed") {
  auto calls{0};
  auto r{util::unique_resource{7, [&calls](int) {
                                 ++calls;
                                 throw std::runtime_error{"close failed"};
                               }}};
  CHECK_THROWS_AS(r.reset(), std::runtime_error);
  CHECK_FALSE(r.owns());
  CHECK(calls == 1);
  r.reset();
  CHECK(calls == 1);
}

TEST_CASE("nexenne::utility::unique_resource owns a void pointer handle") {
  auto released{static_cast<void*>(nullptr)};
  auto storage{0};
  {
    auto r{util::unique_resource{static_cast<void*>(&storage), [&released](void* const p) noexcept {
                                   released = p;
                                 }}};
    CHECK(r.get() == &storage);
    CHECK(r.operator->() == &storage);
  }
  CHECK(released == &storage);
}

}  // namespace
