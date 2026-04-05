/**
 * @file
 * @brief Tests for nexenne::chrono concepts.
 */

#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <ratio>

#include <nexenne/chrono/concepts.hpp>
#include <nexenne/chrono/manual_clock.hpp>
#include <nexenne/chrono/tick_clock.hpp>

namespace {

namespace ch = nexenne::chrono;

struct micro_backend {
  using rep = std::int64_t;
  using period = std::micro;
  static constexpr bool is_steady = true;

  static auto ticks() noexcept -> rep {
    return 0;
  }
};

struct not_steady_clock {
  using rep = int;
  using period = std::milli;
  using duration = std::chrono::milliseconds;
  using time_point = std::chrono::time_point<not_steady_clock>;
  static constexpr bool is_steady = false;

  static auto now() noexcept -> time_point {
    return time_point{};
  }
};

// Positive: every std::chrono duration specialisation, integral and floating,
// signed and unsigned, exotic periods, and cv/ref qualified forms.
static_assert(ch::chrono_duration<std::chrono::nanoseconds>);
static_assert(ch::chrono_duration<std::chrono::microseconds>);
static_assert(ch::chrono_duration<std::chrono::milliseconds>);
static_assert(ch::chrono_duration<std::chrono::seconds>);
static_assert(ch::chrono_duration<std::chrono::minutes>);
static_assert(ch::chrono_duration<std::chrono::hours>);
static_assert(ch::chrono_duration<std::chrono::days>);
static_assert(ch::chrono_duration<std::chrono::weeks>);
static_assert(ch::chrono_duration<std::chrono::months>);
static_assert(ch::chrono_duration<std::chrono::years>);
static_assert(ch::chrono_duration<std::chrono::duration<double>>);
static_assert(ch::chrono_duration<std::chrono::duration<float, std::milli>>);
static_assert(ch::chrono_duration<std::chrono::duration<unsigned, std::ratio<3, 7>>>);
static_assert(ch::chrono_duration<std::chrono::duration<long long, std::ratio<1, 1000000000000>>>);
// cv / reference qualified are stripped by remove_cvref_t.
static_assert(ch::chrono_duration<std::chrono::seconds const>);
static_assert(ch::chrono_duration<std::chrono::seconds volatile>);
static_assert(ch::chrono_duration<std::chrono::seconds&>);
static_assert(ch::chrono_duration<std::chrono::seconds const&>);
static_assert(ch::chrono_duration<std::chrono::seconds&&>);
static_assert(ch::chrono_duration<ch::manual_clock::duration>);

// Negative: fundamentals, time_points, ratios, clocks, and look-alikes that
// have rep/period members but are not the exact duration specialisation.
namespace {

// Has the right member names but is not std::chrono::duration<rep, period>.
struct fake_duration {
  using rep = int;
  using period = std::milli;
};

}  // namespace

static_assert(!ch::chrono_duration<int>);
static_assert(!ch::chrono_duration<double>);
static_assert(!ch::chrono_duration<void>);
static_assert(!ch::chrono_duration<int*>);
static_assert(!ch::chrono_duration<std::ratio<1, 2>>);
static_assert(!ch::chrono_duration<std::chrono::steady_clock>);
static_assert(!ch::chrono_duration<std::chrono::steady_clock::time_point>);
static_assert(!ch::chrono_duration<fake_duration>);  // members present, wrong identity

// Positive: standard clocks and the module's own clocks.
static_assert(ch::clock_like<std::chrono::steady_clock>);
static_assert(ch::clock_like<std::chrono::system_clock>);
static_assert(ch::clock_like<std::chrono::high_resolution_clock>);
static_assert(ch::clock_like<not_steady_clock>);
static_assert(ch::clock_like<ch::manual_clock>);
static_assert(ch::clock_like<ch::basic_manual_clock<struct cl_tag>>);
static_assert(ch::clock_like<ch::tick_clock<micro_backend>>);

// Negative: fundamentals and types missing one of the requirements.
namespace {

// now() returns the wrong type (not its own time_point).
struct wrong_now_type {
  using rep = int;
  using period = std::milli;
  using duration = std::chrono::milliseconds;
  using time_point = std::chrono::time_point<wrong_now_type>;
  [[maybe_unused]] static constexpr bool is_steady = true;

  static auto now() noexcept -> int {  // not time_point
    return 0;
  }
};

// Missing the static now() entirely.
struct no_now {
  using rep = int;
  using period = std::milli;
  using duration = std::chrono::milliseconds;
  using time_point = std::chrono::time_point<no_now>;
};

// now() is non-static (not callable as C::now()).
struct nonstatic_now {
  using rep = int;
  using period = std::milli;
  using duration = std::chrono::milliseconds;
  using time_point = std::chrono::time_point<nonstatic_now>;

  auto now() const noexcept -> time_point {
    return time_point{};
  }
};

// Missing the time_point alias.
struct no_time_point {
  using rep = int;
  using period = std::milli;
  using duration = std::chrono::milliseconds;

  static auto now() noexcept -> int {
    return 0;
  }
};

}  // namespace

static_assert(!ch::clock_like<int>);
static_assert(!ch::clock_like<void>);
static_assert(!ch::clock_like<std::chrono::seconds>);  // a duration is not a clock
static_assert(!ch::clock_like<wrong_now_type>);
static_assert(!ch::clock_like<no_now>);
static_assert(!ch::clock_like<nonstatic_now>);
static_assert(!ch::clock_like<no_time_point>);

// Positive: clocks that are clock_like AND advertise is_steady == true.
static_assert(ch::steady_clock_like<std::chrono::steady_clock>);
static_assert(ch::steady_clock_like<ch::manual_clock>);
static_assert(ch::steady_clock_like<ch::tick_clock<micro_backend>>);

// Negative: not steady, or not clock_like at all.
static_assert(!ch::steady_clock_like<not_steady_clock>);  // is_steady == false
static_assert(!ch::steady_clock_like<int>);
static_assert(!ch::steady_clock_like<std::chrono::seconds>);

namespace {

// clock_like but with no is_steady member at all.
struct steadyless_clock {
  using rep = int;
  using period = std::milli;
  using duration = std::chrono::milliseconds;
  using time_point = std::chrono::time_point<steadyless_clock>;

  static auto now() noexcept -> time_point {
    return time_point{};
  }
};

// is_steady present and truthy but as a non-bool integer (convertible_to<bool>
// plus a truthy runtime value -> should satisfy).
struct int_steady_clock {
  using rep = int;
  using period = std::milli;
  using duration = std::chrono::milliseconds;
  using time_point = std::chrono::time_point<int_steady_clock>;
  static constexpr int is_steady = 1;

  static auto now() noexcept -> time_point {
    return time_point{};
  }
};

// is_steady present but zero -> the (C::is_steady) clause is false.
struct zero_steady_clock {
  using rep = int;
  using period = std::milli;
  using duration = std::chrono::milliseconds;
  using time_point = std::chrono::time_point<zero_steady_clock>;
  static constexpr int is_steady = 0;

  static auto now() noexcept -> time_point {
    return time_point{};
  }
};

}  // namespace

static_assert(!ch::steady_clock_like<steadyless_clock>);
static_assert(ch::steady_clock_like<int_steady_clock>);    // truthy convertible-to-bool
static_assert(!ch::steady_clock_like<zero_steady_clock>);  // falsey value

// system_clock is not steady on the platforms we build for; assert the
// refinement tracks the clock's own is_steady flag exactly.
static_assert(ch::clock_like<std::chrono::system_clock>);
static_assert(std::chrono::system_clock::is_steady == ch::steady_clock_like<std::chrono::system_clock>);

// Positive: a few well-formed backends with differing reps/periods.
static_assert(ch::tick_backend<micro_backend>);

namespace {

struct nano_backend {
  using rep = std::int64_t;
  using period = std::nano;
  static constexpr bool is_steady = true;

  static auto ticks() noexcept -> rep {
    return 0;
  }
};

struct second_ratio_backend {
  using rep = std::int32_t;
  using period = std::ratio<1>;
  static constexpr int is_steady = 5;  // convertible to bool

  static auto ticks() noexcept -> rep {
    return 0;
  }
};

struct odd_ratio_backend {
  using rep = long long;
  using period = std::ratio<7, 13>;
  static constexpr bool is_steady = false;  // steadiness not required by tick_backend

  static auto ticks() noexcept -> rep {
    return 0;
  }
};

}  // namespace

static_assert(ch::tick_backend<nano_backend>);
static_assert(ch::tick_backend<second_ratio_backend>);
static_assert(ch::tick_backend<odd_ratio_backend>);  // is_steady==false still a valid backend

// Negative: every requirement broken in isolation.
static_assert(!ch::tick_backend<int>);
static_assert(!ch::tick_backend<void>);

namespace {

// Unsigned rep violates signed_integral.
struct unsigned_rep_backend {
  using rep = unsigned;
  using period = std::micro;
  [[maybe_unused]] static constexpr bool is_steady = true;

  static auto ticks() noexcept -> rep {
    return 0;
  }
};

// Floating rep violates signed_integral.
struct float_rep_backend {
  using rep = double;
  using period = std::micro;
  [[maybe_unused]] static constexpr bool is_steady = true;

  static auto ticks() noexcept -> rep {
    return 0;
  }
};

// Negative-numerator ratio is not a positive period.
struct negative_period_backend {
  using rep = std::int64_t;
  using period = std::ratio<-1, 1000>;
  [[maybe_unused]] static constexpr bool is_steady = true;

  static auto ticks() noexcept -> rep {
    return 0;
  }
};

// Zero-numerator ratio is not a positive period.
struct zero_period_backend {
  using rep = std::int64_t;
  using period = std::ratio<0, 1>;
  [[maybe_unused]] static constexpr bool is_steady = true;

  static auto ticks() noexcept -> rep {
    return 0;
  }
};

// period is not a std::ratio at all.
struct nonratio_period_backend {
  using rep = std::int64_t;
  using period = int;
  [[maybe_unused]] static constexpr bool is_steady = true;

  static auto ticks() noexcept -> rep {
    return 0;
  }
};

// Missing is_steady member.
struct no_is_steady_backend {
  using rep = std::int64_t;
  using period = std::micro;

  static auto ticks() noexcept -> rep {
    return 0;
  }
};

// ticks() is not noexcept.
struct throwing_ticks_backend {
  using rep = std::int64_t;
  using period = std::micro;
  static constexpr bool is_steady = true;

  static auto ticks() -> rep {  // not noexcept
    return 0;
  }
};

// ticks() returns the wrong type.
struct wrong_ticks_type_backend {
  using rep = std::int64_t;
  using period = std::micro;
  static constexpr bool is_steady = true;

  static auto ticks() noexcept -> int {  // not rep (which is int64_t)
    return 0;
  }
};

// ticks() is non-static.
struct nonstatic_ticks_backend {
  using rep = std::int64_t;
  using period = std::micro;
  static constexpr bool is_steady = true;

  auto ticks() const noexcept -> rep {
    return 0;
  }
};

// Missing rep alias.
struct no_rep_backend {
  using period = std::micro;
  [[maybe_unused]] static constexpr bool is_steady = true;

  static auto ticks() noexcept -> std::int64_t {
    return 0;
  }
};

// Missing period alias.
struct no_period_backend {
  using rep = std::int64_t;
  [[maybe_unused]] static constexpr bool is_steady = true;

  static auto ticks() noexcept -> rep {
    return 0;
  }
};

// is_steady is a class type with no implicit conversion to bool.
struct not_bool_convertible {
  explicit operator bool() const {  // explicit: not std::convertible_to<bool>
    return true;
  }
};

struct non_bool_is_steady_backend {
  using rep = std::int64_t;
  using period = std::micro;
  [[maybe_unused]] static inline not_bool_convertible is_steady{};

  static auto ticks() noexcept -> rep {
    return 0;
  }
};

}  // namespace

static_assert(!ch::tick_backend<unsigned_rep_backend>);
static_assert(!ch::tick_backend<float_rep_backend>);
static_assert(!ch::tick_backend<negative_period_backend>);
static_assert(!ch::tick_backend<zero_period_backend>);
static_assert(!ch::tick_backend<nonratio_period_backend>);
static_assert(!ch::tick_backend<no_is_steady_backend>);
static_assert(!ch::tick_backend<throwing_ticks_backend>);
static_assert(!ch::tick_backend<wrong_ticks_type_backend>);
static_assert(!ch::tick_backend<nonstatic_ticks_backend>);
static_assert(!ch::tick_backend<no_rep_backend>);
static_assert(!ch::tick_backend<no_period_backend>);
static_assert(!ch::tick_backend<non_bool_is_steady_backend>);

// A clock type is not itself a tick_backend (no ticks(), wrong period kind).
static_assert(!ch::tick_backend<std::chrono::steady_clock>);
static_assert(!ch::tick_backend<ch::manual_clock>);

static_assert(ch::clock_like<ch::tick_clock<micro_backend>>);
static_assert(ch::clock_like<ch::tick_clock<odd_ratio_backend>>);
static_assert(ch::steady_clock_like<ch::tick_clock<nano_backend>>);
static_assert(!ch::steady_clock_like<ch::tick_clock<odd_ratio_backend>>);  // backend not steady

TEST_CASE("nexenne::chrono::chrono_duration accepts durations, rejects others") {
  CHECK(ch::chrono_duration<std::chrono::seconds>);
  CHECK(ch::chrono_duration<std::chrono::duration<double>>);
  CHECK_FALSE(ch::chrono_duration<int>);
  CHECK_FALSE(ch::chrono_duration<std::ratio<1, 2>>);
  CHECK_FALSE(ch::chrono_duration<fake_duration>);
}

TEST_CASE("nexenne::chrono::clock_like accepts clocks, rejects non-clocks") {
  CHECK(ch::clock_like<std::chrono::steady_clock>);
  CHECK(ch::clock_like<ch::manual_clock>);
  CHECK(ch::clock_like<not_steady_clock>);
  CHECK_FALSE(ch::clock_like<int>);
  CHECK_FALSE(ch::clock_like<wrong_now_type>);
  CHECK_FALSE(ch::clock_like<no_time_point>);
}

TEST_CASE("nexenne::chrono::steady_clock_like distinguishes monotonic clocks") {
  CHECK(ch::steady_clock_like<std::chrono::steady_clock>);
  CHECK(ch::steady_clock_like<ch::manual_clock>);
  CHECK(ch::steady_clock_like<int_steady_clock>);
  CHECK_FALSE(ch::steady_clock_like<not_steady_clock>);
  CHECK_FALSE(ch::steady_clock_like<zero_steady_clock>);
  CHECK_FALSE(ch::steady_clock_like<steadyless_clock>);
}

TEST_CASE("nexenne::chrono::tick_backend validates backend contracts") {
  CHECK(ch::tick_backend<micro_backend>);
  CHECK(ch::tick_backend<nano_backend>);
  CHECK(ch::tick_backend<odd_ratio_backend>);
  CHECK_FALSE(ch::tick_backend<int>);
  CHECK_FALSE(ch::tick_backend<unsigned_rep_backend>);
  CHECK_FALSE(ch::tick_backend<negative_period_backend>);
  CHECK_FALSE(ch::tick_backend<zero_period_backend>);
  CHECK_FALSE(ch::tick_backend<throwing_ticks_backend>);
  CHECK_FALSE(ch::tick_backend<wrong_ticks_type_backend>);
}

}  // namespace
