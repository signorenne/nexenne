/**
 * @file
 * @brief Tests for nexenne::chrono tick_clock and manual_clock.
 */

#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <limits>
#include <ratio>
#include <type_traits>
#include <utility>

#include <nexenne/chrono/concepts.hpp>
#include <nexenne/chrono/manual_clock.hpp>
#include <nexenne/chrono/tick_clock.hpp>

namespace {

namespace ch = nexenne::chrono;
using namespace std::chrono_literals;

// A backend with a settable tick source so tests are deterministic.
struct fake_backend {
  using rep = std::int64_t;
  using period = std::micro;
  static constexpr bool is_steady = true;
  static inline rep value{0};

  static auto ticks() noexcept -> rep {
    return value;
  }
};

using fake_clock = ch::tick_clock<fake_backend>;

static_assert(ch::steady_clock_like<fake_clock>);

// A second, independent backend with a different rep/period to exercise the
// alias plumbing and to prove tick_clock is fully type-driven by the backend.
struct milli_backend {
  using rep = std::int32_t;
  using period = std::milli;
  static constexpr bool is_steady = false;
  static inline rep value{0};

  static auto ticks() noexcept -> rep {
    return value;
  }
};

using milli_clock = ch::tick_clock<milli_backend>;

// A backend with the integer-second period (ratio<1>) and is_steady that is a
// non-bool convertible-to-bool value, to stress is_steady's static_cast<bool>.
struct second_backend {
  using rep = std::int64_t;
  using period = std::ratio<1>;
  static constexpr int is_steady = 7;  // truthy but not a bool

  static auto ticks() noexcept -> rep {
    return 0;
  }
};

using second_clock = ch::tick_clock<second_backend>;

static_assert(std::is_same_v<fake_clock::backend_type, fake_backend>);
static_assert(std::is_same_v<fake_clock::rep, std::int64_t>);
static_assert(std::is_same_v<fake_clock::period, std::micro>);
static_assert(std::
                is_same_v<fake_clock::duration, std::chrono::duration<std::int64_t, std::micro>>);
static_assert(std::is_same_v<
              fake_clock::time_point,
              std::chrono::time_point<fake_clock, fake_clock::duration>>);
static_assert(fake_clock::is_steady == true);

static_assert(std::is_same_v<milli_clock::backend_type, milli_backend>);
static_assert(std::is_same_v<milli_clock::rep, std::int32_t>);
static_assert(std::is_same_v<milli_clock::period, std::milli>);
static_assert(std::
                is_same_v<milli_clock::duration, std::chrono::duration<std::int32_t, std::milli>>);
static_assert(milli_clock::is_steady == false);

// is_steady is static_cast<bool> of the backend's value (7 -> true).
static_assert(second_clock::is_steady == true);
static_assert(std::is_same_v<decltype(second_clock::is_steady), bool const>);

// now()/from_ticks/to_ticks signatures and noexcept-ness.
static_assert(noexcept(fake_clock::now()));
static_assert(noexcept(fake_clock::from_ticks(0)));
// declval isolates to_ticks's own noexcept: a literal `time_point{}` argument
// is not itself noexcept-constructible in libstdc++ (its chrono default ctors
// are unmarked), which would otherwise poison the noexcept expression.
static_assert(noexcept(fake_clock::to_ticks(std::declval<fake_clock::time_point>())));
static_assert(std::is_same_v<decltype(fake_clock::now()), fake_clock::time_point>);
static_assert(std::is_same_v<decltype(fake_clock::from_ticks(0)), fake_clock::time_point>);
static_assert(std::is_same_v<
              decltype(fake_clock::to_ticks(fake_clock::time_point{})),
              fake_clock::rep>);

// from_ticks/to_ticks are constexpr: prove a compile-time round trip.
static_assert(fake_clock::to_ticks(fake_clock::from_ticks(123456)) == 123456);
static_assert(fake_clock::to_ticks(fake_clock::from_ticks(0)) == 0);
static_assert(fake_clock::to_ticks(fake_clock::from_ticks(-99)) == -99);

TEST_CASE("nexenne::chrono::tick_clock wraps a backend as a chrono clock") {
  fake_backend::value = 1'500'000;  // 1.5 s worth of microseconds
  auto const tp{fake_clock::now()};
  CHECK(std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()) == 1500ms);
  CHECK(fake_clock::to_ticks(tp) == 1'500'000);
  CHECK(fake_clock::to_ticks(fake_clock::from_ticks(42)) == 42);
}

TEST_CASE("nexenne::chrono::tick_clock now() reflects the live backend value") {
  fake_backend::value = 0;
  CHECK(fake_clock::to_ticks(fake_clock::now()) == 0);

  fake_backend::value = 1;
  CHECK(fake_clock::to_ticks(fake_clock::now()) == 1);

  fake_backend::value = 999'999;
  CHECK(fake_clock::to_ticks(fake_clock::now()) == 999'999);
}

TEST_CASE("nexenne::chrono::tick_clock from_ticks/to_ticks round trip across ranges") {
  fake_clock::rep const samples[]{
    0,
    1,
    -1,
    42,
    -42,
    1'000'000,
    -1'000'000,
    std::numeric_limits<fake_clock::rep>::max(),
    std::numeric_limits<fake_clock::rep>::min(),
  };
  for (auto const t : samples) {
    CHECK(fake_clock::to_ticks(fake_clock::from_ticks(t)) == t);
  }
}

TEST_CASE("nexenne::chrono::tick_clock from_ticks builds the correct time_point") {
  auto const tp{fake_clock::from_ticks(2'000'000)};  // 2 s in micros
  CHECK(tp.time_since_epoch().count() == 2'000'000);
  CHECK(std::chrono::duration_cast<std::chrono::seconds>(tp.time_since_epoch()) == 2s);
}

TEST_CASE("nexenne::chrono::tick_clock negative and zero ticks are representable") {
  fake_backend::value = 0;
  CHECK(fake_clock::to_ticks(fake_clock::now()) == 0);
  CHECK(fake_clock::now().time_since_epoch() == fake_clock::duration::zero());

  fake_backend::value = -250;  // before the epoch
  auto const tp{fake_clock::now()};
  CHECK(fake_clock::to_ticks(tp) == -250);
  CHECK(tp.time_since_epoch().count() == -250);
}

TEST_CASE("nexenne::chrono::tick_clock handles extreme tick values") {
  auto const hi{std::numeric_limits<fake_clock::rep>::max()};
  auto const lo{std::numeric_limits<fake_clock::rep>::min()};

  fake_backend::value = hi;
  CHECK(fake_clock::to_ticks(fake_clock::now()) == hi);

  fake_backend::value = lo;
  CHECK(fake_clock::to_ticks(fake_clock::now()) == lo);
}

TEST_CASE("nexenne::chrono::tick_clock time_points compare and subtract as chrono expects") {
  auto const a{fake_clock::from_ticks(100)};
  auto const b{fake_clock::from_ticks(300)};
  CHECK(a < b);
  CHECK(b > a);
  CHECK(a != b);
  CHECK(a == fake_clock::from_ticks(100));
  CHECK((b - a) == fake_clock::duration{200});
  CHECK((b - a).count() == 200);
}

TEST_CASE("nexenne::chrono::tick_clock with a milli backend uses backend's rep/period") {
  milli_backend::value = 0;
  CHECK(milli_clock::to_ticks(milli_clock::now()) == 0);

  milli_backend::value = 1'500;  // 1.5 s in milliseconds
  auto const tp{milli_clock::now()};
  CHECK(std::chrono::duration_cast<std::chrono::seconds>(tp.time_since_epoch()) == 1s);
  CHECK(milli_clock::to_ticks(tp) == 1'500);
  // is_steady = false on this backend; it is still clock_like but not steady.
  CHECK(milli_clock::is_steady == false);
}

static_assert(std::is_same_v<ch::manual_clock, ch::basic_manual_clock<>>);
static_assert(std::is_same_v<ch::manual_clock::rep, std::int64_t>);
static_assert(std::is_same_v<ch::manual_clock::period, std::nano>);
static_assert(std::is_same_v<
              ch::manual_clock::duration,
              std::chrono::duration<std::int64_t, std::nano>>);
static_assert(std::is_same_v<
              ch::manual_clock::time_point,
              std::chrono::time_point<ch::manual_clock, ch::manual_clock::duration>>);
static_assert(ch::manual_clock::is_steady == true);
static_assert(std::is_same_v<decltype(ch::manual_clock::is_steady), bool const>);

static_assert(ch::steady_clock_like<ch::manual_clock>);
static_assert(ch::clock_like<ch::manual_clock>);
static_assert(ch::chrono_duration<ch::manual_clock::duration>);

// Distinct tags yield distinct types.
static_assert(!std::is_same_v<
              ch::basic_manual_clock<struct sa_tag>,
              ch::basic_manual_clock<struct sb_tag>>);

// now() signature / noexcept.
static_assert(noexcept(ch::manual_clock::now()));
static_assert(std::is_same_v<decltype(ch::manual_clock::now()), ch::manual_clock::time_point>);

TEST_CASE("nexenne::chrono::manual_clock advances only when told to") {
  using clk = ch::basic_manual_clock<struct test_advance_tag>;
  clk::reset();
  CHECK(clk::now().time_since_epoch() == 0ns);
  clk::advance(100ms);
  CHECK(
    std::chrono::duration_cast<std::chrono::milliseconds>(clk::now().time_since_epoch()) == 100ms
  );
  clk::advance(50ms);
  CHECK(
    std::chrono::duration_cast<std::chrono::milliseconds>(clk::now().time_since_epoch()) == 150ms
  );
  clk::reset();
  CHECK(clk::now().time_since_epoch() == 0ns);
}

TEST_CASE("nexenne::chrono::manual_clock set and distinct tags are independent") {
  using a = ch::basic_manual_clock<struct tag_a>;
  using b = ch::basic_manual_clock<struct tag_b>;
  a::reset();
  b::reset();
  a::advance(1s);
  CHECK(a::now().time_since_epoch() != 0ns);
  CHECK(b::now().time_since_epoch() == 0ns);  // b unaffected by a

  b::set(b::time_point{2s});
  CHECK(std::chrono::duration_cast<std::chrono::seconds>(b::now().time_since_epoch()) == 2s);
}

TEST_CASE("nexenne::chrono::manual_clock now() is idempotent without advancing") {
  using clk = ch::basic_manual_clock<struct idempotent_tag>;
  clk::reset();
  auto const a{clk::now()};
  auto const b{clk::now()};
  auto const c{clk::now()};
  CHECK(a == b);
  CHECK(b == c);
  clk::advance(7ms);
  // After advancing, repeated reads still agree with each other.
  CHECK(clk::now() == clk::now());
}

TEST_CASE("nexenne::chrono::manual_clock advancing by zero is a no-op") {
  using clk = ch::basic_manual_clock<struct zero_tag>;
  clk::reset();
  clk::advance(500ms);
  auto const before{clk::now()};
  clk::advance(0ns);
  CHECK(clk::now() == before);
  clk::advance(0s);
  CHECK(clk::now() == before);
  clk::advance(ch::manual_clock::duration::zero());
  CHECK(clk::now() == before);
}

TEST_CASE("nexenne::chrono::manual_clock is monotonic non-decreasing under positive advances") {
  using clk = ch::basic_manual_clock<struct monotonic_tag>;
  clk::reset();
  auto prev{clk::now()};
  std::chrono::nanoseconds const steps[]{1ns, 0ns, 1000000ns, 0ns, 1s, 250ms};
  for (auto const step : steps) {
    clk::advance(step);
    auto const cur{clk::now()};
    CHECK(cur >= prev);
    prev = cur;
  }
}

TEST_CASE("nexenne::chrono::manual_clock accumulates many small advances exactly") {
  using clk = ch::basic_manual_clock<struct accumulate_tag>;
  clk::reset();
  for (int i{0}; i < 1000; i = i + 1) {
    clk::advance(1ms);
  }
  CHECK(std::chrono::duration_cast<std::chrono::seconds>(clk::now().time_since_epoch()) == 1s);
  CHECK(clk::now().time_since_epoch() == 1000ms);
}

TEST_CASE("nexenne::chrono::manual_clock advance accepts heterogeneous duration types") {
  using clk = ch::basic_manual_clock<struct hetero_tag>;
  clk::reset();
  clk::advance(1s);           // seconds
  clk::advance(500ms);        // milliseconds
  clk::advance(250000us);     // microseconds
  clk::advance(250000000ns);  // nanoseconds -> exact in nano storage
  // 1s + 0.5s + 0.25s + 0.25s = 2s
  CHECK(clk::now().time_since_epoch() == 2s);
}

TEST_CASE("nexenne::chrono::manual_clock advance with sub-period duration truncates") {
  using clk = ch::basic_manual_clock<struct truncate_tag>;
  clk::reset();
  // Storage is nanoseconds; a fractional-nanosecond floating duration is cast
  // toward zero by duration_cast.
  clk::advance(std::chrono::duration<double, std::nano>{2.9});
  CHECK(clk::now().time_since_epoch().count() == 2);  // truncated, not rounded
  clk::reset();
  clk::advance(std::chrono::duration<double, std::nano>{-2.9});
  CHECK(clk::now().time_since_epoch().count() == -2);  // truncation toward zero
}

TEST_CASE("nexenne::chrono::manual_clock advance backward (documented hazard)") {
  using clk = ch::basic_manual_clock<struct backward_tag>;
  clk::reset();
  clk::advance(1s);
  clk::advance(-400ms);
  CHECK(
    std::chrono::duration_cast<std::chrono::milliseconds>(clk::now().time_since_epoch()) == 600ms
  );
  clk::advance(-600ms);
  CHECK(clk::now().time_since_epoch() == 0ns);
  clk::advance(-1ns);  // before the epoch
  CHECK(clk::now().time_since_epoch().count() == -1);
}

TEST_CASE("nexenne::chrono::manual_clock set to an arbitrary absolute point") {
  using clk = ch::basic_manual_clock<struct set_tag>;
  clk::reset();
  clk::set(clk::time_point{12345ns});
  CHECK(clk::now().time_since_epoch().count() == 12345);

  // set overwrites, it does not accumulate.
  clk::set(clk::time_point{50ns});
  CHECK(clk::now().time_since_epoch().count() == 50);

  // set the clock to the epoch.
  clk::set(clk::time_point{});
  CHECK(clk::now().time_since_epoch() == 0ns);

  // set to a point before the epoch.
  clk::set(clk::time_point{-7ns});
  CHECK(clk::now().time_since_epoch().count() == -7);
}

TEST_CASE("nexenne::chrono::manual_clock set then advance composes") {
  using clk = ch::basic_manual_clock<struct set_advance_tag>;
  clk::reset();
  clk::set(clk::time_point{1s});
  clk::advance(500ms);
  CHECK(
    std::chrono::duration_cast<std::chrono::milliseconds>(clk::now().time_since_epoch()) == 1500ms
  );
}

TEST_CASE("nexenne::chrono::manual_clock handles extreme absolute values") {
  using clk = ch::basic_manual_clock<struct extreme_tag>;
  clk::reset();
  auto const hi{std::numeric_limits<clk::rep>::max()};
  auto const lo{std::numeric_limits<clk::rep>::min()};

  clk::set(clk::time_point{clk::duration{hi}});
  CHECK(clk::now().time_since_epoch().count() == hi);

  clk::set(clk::time_point{clk::duration{lo}});
  CHECK(clk::now().time_since_epoch().count() == lo);
  clk::reset();
}

TEST_CASE("nexenne::chrono::manual_clock large advance does not lose precision in nanos") {
  using clk = ch::basic_manual_clock<struct large_tag>;
  clk::reset();
  // One hour expressed in nanoseconds is well within int64 range.
  clk::advance(1h);
  CHECK(std::chrono::duration_cast<std::chrono::hours>(clk::now().time_since_epoch()) == 1h);
  CHECK(clk::now().time_since_epoch() == std::chrono::nanoseconds{3'600'000'000'000});
}

TEST_CASE("nexenne::chrono::manual_clock reset clears state regardless of prior history") {
  using clk = ch::basic_manual_clock<struct reset_tag>;
  clk::set(clk::time_point{999s});
  clk::advance(-12345ns);
  clk::reset();
  CHECK(clk::now().time_since_epoch() == 0ns);
  // reset is idempotent.
  clk::reset();
  CHECK(clk::now().time_since_epoch() == 0ns);
}

TEST_CASE("nexenne::chrono::manual_clock three independent tags do not interfere") {
  using a = ch::basic_manual_clock<struct triad_a>;
  using b = ch::basic_manual_clock<struct triad_b>;
  using c = ch::basic_manual_clock<struct triad_c>;
  a::reset();
  b::reset();
  c::reset();

  a::advance(10ns);
  b::set(b::time_point{20ns});
  // c is untouched.

  CHECK(a::now().time_since_epoch().count() == 10);
  CHECK(b::now().time_since_epoch().count() == 20);
  CHECK(c::now().time_since_epoch().count() == 0);

  // Mutating one leaves the others fixed.
  c::advance(30ns);
  CHECK(a::now().time_since_epoch().count() == 10);
  CHECK(b::now().time_since_epoch().count() == 20);
  CHECK(c::now().time_since_epoch().count() == 30);
}

TEST_CASE("nexenne::chrono::manual_clock the default-tag alias is its own shared clock") {
  ch::manual_clock::reset();
  CHECK(ch::manual_clock::now().time_since_epoch() == 0ns);
  ch::manual_clock::advance(5ms);
  // basic_manual_clock<> is literally the same type as manual_clock.
  CHECK(ch::basic_manual_clock<>::now().time_since_epoch() == 5ms);
  ch::manual_clock::reset();
}

}  // namespace
