/**
 * @file
 * @brief Tests for nexenne::utility::lazy.
 */

#include <doctest/doctest.h>

#include <array>
#include <atomic>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <nexenne/utility/lazy.hpp>

namespace {

namespace util = nexenne::utility;

static_assert(
  !std::is_move_constructible_v<util::lazy<int (*)()>>, "lazy is non-movable (std::once_flag)"
);
static_assert(
  !std::is_copy_constructible_v<util::lazy<int (*)()>>, "lazy is non-copyable (std::once_flag)"
);
static_assert(!std::is_move_assignable_v<util::lazy<int (*)()>>, "lazy is non-move-assignable");
static_assert(!std::is_copy_assignable_v<util::lazy<int (*)()>>, "lazy is non-copy-assignable");

// Deduction guide and the published member aliases.
static_assert(
  std::is_same_v<decltype(util::lazy{[] { return 0; }})::value_type, int>,
  "value_type deduces from the factory's return"
);

TEST_CASE("nexenne::utility::lazy runs the factory once on first access") {
  int runs{0};
  auto value{util::lazy{[&] {
    ++runs;
    return 42;
  }}};

  CHECK_FALSE(value.has_value());
  CHECK(runs == 0);

  CHECK(*value == 42);
  CHECK(value.has_value());
  CHECK(runs == 1);

  CHECK(value.get() == 42);
  CHECK(runs == 1);  // cached, not re-run
}

TEST_CASE("nexenne::utility::lazy operator-> and const access materialise") {
  auto pair{util::lazy{[] { return std::pair{1, 2}; }}};
  CHECK(pair->first == 1);

  auto const& cref{pair};
  CHECK(cref->second == 2);
  CHECK(cref.has_value());
}

TEST_CASE("nexenne::utility::lazy runs the factory exactly once under contention") {
  constexpr std::size_t thread_count{16};
  std::atomic<int> runs{0};
  auto value{util::lazy{[&] {
    runs.fetch_add(1, std::memory_order_relaxed);
    return 7;
  }}};

  std::array<int, thread_count> results{};
  std::vector<std::thread> threads;
  threads.reserve(thread_count);
  for (std::size_t i{0}; i < thread_count; ++i) {
    threads.emplace_back([&value, &results, i] { results[i] = *value; });
  }
  for (auto& t : threads) {
    t.join();
  }

  CHECK(runs.load() == 1);
  for (auto const r : results) {
    CHECK(r == 7);
  }
}

TEST_CASE("nexenne::utility::lazy retries after the factory throws") {
  int attempts{0};
  auto value{util::lazy{[&]() -> int {
    if (++attempts == 1) {
      throw std::runtime_error{"first attempt fails"};
    }
    return 99;
  }}};

  CHECK_THROWS_AS(static_cast<void>(*value), std::runtime_error);
  CHECK_FALSE(value.has_value());

  CHECK(*value == 99);  // call_once did not latch the throwing attempt
  CHECK(attempts == 2);
}

TEST_CASE("nexenne::utility::lazy every accessor returns the SAME stored object") {
  auto value{util::lazy{[] { return std::string{"shared"}; }}};

  auto& via_get{value.get()};
  auto* via_arrow{value.operator->()};
  auto& via_star{*value};

  // All accessors alias one and the same cached object.
  CHECK(&via_get == via_arrow);
  CHECK(&via_get == &via_star);
  CHECK(&via_star == via_arrow);

  // Mutating through one accessor is visible through the others.
  via_get += "!";
  CHECK(*via_arrow == "shared!");
  CHECK(via_star == "shared!");
}

TEST_CASE("nexenne::utility::lazy const accessors return the SAME stored object") {
  auto const value{util::lazy{[] { return std::string{"c"}; }}};

  auto const& via_get{value.get()};
  auto const* via_arrow{value.operator->()};
  auto const& via_star{*value};

  CHECK(&via_get == via_arrow);
  CHECK(&via_get == &via_star);
  CHECK(value.has_value());
}

TEST_CASE("nexenne::utility::lazy materialises only on access, never on construction") {
  int runs{0};
  auto value{util::lazy{[&] {
    ++runs;
    return 1;
  }}};

  // Several non-materialising observations.
  CHECK(runs == 0);
  CHECK_FALSE(value.has_value());
  CHECK_FALSE(value.has_value());
  CHECK(runs == 0);

  // Single materialisation.
  static_cast<void>(value.get());
  CHECK(runs == 1);

  // Many subsequent accesses through all accessors: still one run.
  static_cast<void>(value.get());
  static_cast<void>(*value);
  static_cast<void>(value.operator->());
  static_cast<void>(std::as_const(value).get());
  static_cast<void>(*std::as_const(value));
  static_cast<void>(std::as_const(value).operator->());
  CHECK(runs == 1);
}

TEST_CASE("nexenne::utility::lazy const wrapper still materialises on first access") {
  int runs{0};
  auto const value{util::lazy{[&] {
    ++runs;
    return 314;
  }}};

  CHECK_FALSE(value.has_value());
  CHECK(runs == 0);
  CHECK(value.get() == 314);  // mutable members let a const lazy materialise
  CHECK(value.has_value());
  CHECK(runs == 1);
  CHECK(*value == 314);
  CHECK(*value.operator->() == 314);  // operator-> on const wrapper
  CHECK(runs == 1);
}

TEST_CASE("nexenne::utility::lazy carries captured state into the factory") {
  std::string const prefix{"id-"};
  int const suffix{77};
  auto value{util::lazy{[prefix, suffix] { return prefix + std::to_string(suffix); }}};

  CHECK_FALSE(value.has_value());
  CHECK(*value == "id-77");
  CHECK(value.has_value());
}

TEST_CASE(
  "nexenne::utility::lazy captures by reference and reads it at first access, not at construction"
) {
  int source{10};
  auto value{util::lazy{[&source] { return source * 2; }}};

  // The factory has not run, so the value reflects whatever source is at access time.
  source = 21;
  CHECK_FALSE(value.has_value());
  CHECK(*value == 42);  // 21 * 2, read at first access

  // Later mutations of the captured reference do not change the cached value.
  source = 0;
  CHECK(*value == 42);
}

TEST_CASE("nexenne::utility::lazy stores and returns a move-only value type") {
  auto value{util::lazy{[] { return std::make_unique<int>(123); }}};

  CHECK_FALSE(value.has_value());
  REQUIRE(value.get() != nullptr);  // unique_ptr is non-null
  CHECK(**value == 123);
  CHECK(value.has_value());

  // Mutate the pointee through the cached, stable object.
  *value.get() = 456;
  CHECK(**value == 456);
}

TEST_CASE("nexenne::utility::lazy works with a plain function pointer factory") {
  struct helper {
    static auto make() -> int {
      return 2024;
    }
  };

  util::lazy<int (*)()> value{&helper::make};
  CHECK_FALSE(value.has_value());
  CHECK(*value == 2024);
  CHECK(value.has_value());
}

TEST_CASE("nexenne::utility::lazy value_type matches the factory return type") {
  auto str_lazy{util::lazy{[] { return std::string{"x"}; }}};
  static_assert(std::is_same_v<decltype(str_lazy)::value_type, std::string>);
  CHECK(str_lazy->size() == 1);

  auto vec_lazy{util::lazy{[] { return std::vector<int>{1, 2, 3}; }}};
  static_assert(std::is_same_v<decltype(vec_lazy)::value_type, std::vector<int>>);
  CHECK(vec_lazy->size() == 3);
  CHECK((*vec_lazy)[2] == 3);
}

TEST_CASE("nexenne::utility::lazy retries multiple times until the factory succeeds") {
  int attempts{0};
  auto value{util::lazy{[&]() -> int {
    ++attempts;
    if (attempts < 3) {
      throw std::runtime_error{"not yet"};
    }
    return 500;
  }}};

  CHECK_THROWS_AS(static_cast<void>(*value), std::runtime_error);
  CHECK_FALSE(value.has_value());
  CHECK_THROWS_AS(static_cast<void>(value.get()), std::runtime_error);
  CHECK_FALSE(value.has_value());
  CHECK(attempts == 2);

  CHECK(*value == 500);  // third attempt latches
  CHECK(value.has_value());
  CHECK(attempts == 3);

  // Once latched, no further attempts even after the earlier throws.
  CHECK(value.get() == 500);
  CHECK(attempts == 3);
}

TEST_CASE("nexenne::utility::lazy materialises exactly once under concurrent first access") {
  std::atomic<bool> go{false};
  std::atomic<int> factory_runs{0};
  auto value{util::lazy{[&factory_runs] {
    factory_runs.fetch_add(1, std::memory_order_relaxed);
    return 1234;
  }}};

  std::vector<std::thread> threads;
  std::atomic<int> mismatches{0};
  threads.reserve(8);
  for (int i{0}; i < 8; ++i) {
    threads.emplace_back([&] {
      while (!go.load(std::memory_order_acquire)) {}
      // All eight race to FORCE materialisation; each must observe 1234 fully.
      for (int j{0}; j < 1000; ++j) {
        if (*value != 1234) {
          mismatches.fetch_add(1, std::memory_order_relaxed);
        }
      }
    });
  }
  go.store(true, std::memory_order_release);
  for (auto& t : threads) {
    t.join();
  }
  CHECK(mismatches.load() == 0);
  CHECK(value.has_value());
  CHECK(*value == 1234);
  CHECK(factory_runs.load() == 1);  // the factory ran once despite eight racers
}

}  // namespace
