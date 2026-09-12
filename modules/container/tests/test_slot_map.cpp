/**
 * @file
 * @brief Tests for nexenne::container::slot_map.
 */

#include <doctest/doctest.h>

#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <nexenne/container/slot_map.hpp>

namespace {

namespace cn = nexenne::container;
using map_t = cn::slot_map<int>;

TEST_CASE("nexenne::container::slot_map insert returns a stable key, find resolves it") {
  map_t m;
  auto const a{m.insert(10)};
  auto const b{m.insert(20)};
  CHECK(m.size() == 2);
  REQUIRE(m.find(a) != nullptr);
  CHECK(*m.find(a) == 10);
  CHECK(*m.find(b) == 20);
  CHECK(m.contains(a));
}

TEST_CASE("nexenne::container::slot_map a default key never matches") {
  map_t m;
  map_t::key const null_key;
  CHECK(m.find(null_key) == nullptr);
  m.insert(1);
  CHECK(m.find(null_key) == nullptr);
}

TEST_CASE("nexenne::container::slot_map erase invalidates only that key") {
  map_t m;
  auto const a{m.insert(10)};
  auto const b{m.insert(20)};
  CHECK(m.erase(a));
  CHECK(m.find(a) == nullptr);
  CHECK_FALSE(m.contains(a));
  REQUIRE(m.find(b) != nullptr);
  CHECK(*m.find(b) == 20);
  CHECK(m.size() == 1);
  CHECK_FALSE(m.erase(a));
}

TEST_CASE("nexenne::container::slot_map recycles a slot but the old key stays stale (ABA guard)") {
  map_t m;
  auto const a{m.insert(10)};
  CHECK(m.erase(a));
  auto const b{m.insert(99)};
  CHECK(b.index() == a.index());
  CHECK(b.generation() != a.generation());
  CHECK(m.find(a) == nullptr);
  REQUIRE(m.find(b) != nullptr);
  CHECK(*m.find(b) == 99);
}

TEST_CASE("nexenne::container::slot_map emplace constructs in place") {
  cn::slot_map<std::string> m;
  auto const k{m.emplace(3, 'x')};
  REQUIRE(m.find(k) != nullptr);
  CHECK(*m.find(k) == "xxx");
}

TEST_CASE("nexenne::container::slot_map iterates only live elements, skipping vacancies") {
  map_t m;
  auto const a{m.insert(1)};
  m.insert(2);
  auto const c{m.insert(3)};
  m.erase(a);
  m.erase(c);
  int count{0};
  int sum{0};
  for (int const v : m) {
    ++count;
    sum += v;
  }
  CHECK(count == 1);
  CHECK(sum == 2);
}

TEST_CASE("nexenne::container::slot_map clear invalidates every key, retains capacity") {
  map_t m;
  m.reserve(8);
  auto const cap{m.capacity()};
  auto const a{m.insert(1)};
  auto const b{m.insert(2)};
  m.clear();
  CHECK(m.empty());
  CHECK(m.find(a) == nullptr);
  CHECK(m.find(b) == nullptr);
  CHECK(m.capacity() == cap);
  auto const c{m.insert(3)};
  CHECK(*m.find(c) == 3);
}

TEST_CASE(
  "nexenne::container::slot_map swap exchanges contents, keys stay valid against their map"
) {
  map_t a;
  auto const ka{a.insert(1)};
  map_t b;
  auto const kb{b.insert(9)};
  b.insert(8);
  swap(a, b);
  CHECK(a.size() == 2);
  CHECK(b.size() == 1);
  REQUIRE(a.find(kb) != nullptr);
  CHECK(*a.find(kb) == 9);
  CHECK(*b.find(ka) == 1);
}

TEST_CASE("nexenne::container::slot_map holds a move-only type") {
  cn::slot_map<std::unique_ptr<int>> m;
  auto const a{m.insert(std::make_unique<int>(10))};
  auto const b{m.emplace(std::make_unique<int>(20))};
  REQUIRE(m.find(a) != nullptr);
  CHECK(**m.find(a) == 10);
  CHECK(**m.find(b) == 20);
  CHECK(m.erase(a));
  CHECK(m.size() == 1);
}

TEST_CASE("nexenne::container::slot_map const iteration and key comparison") {
  map_t m;
  m.insert(5);
  m.insert(7);
  map_t const& cm{m};
  int total{0};
  for (int const v : cm) {
    total += v;
  }
  CHECK(total == 12);

  map_t::key const k1{1, 2};
  map_t::key const k2{1, 2};
  map_t::key const k3{1, 3};
  CHECK(k1 == k2);
  CHECK(k1 != k3);
}

TEST_CASE("nexenne::container::slot_map empty map queries are well-defined") {
  map_t m;
  CHECK(m.empty());
  CHECK(m.size() == 0);
  CHECK(m.capacity() == 0);
  CHECK(m.max_size() > 0);
  CHECK(m.begin() == m.end());
  map_t::key const null_key;
  CHECK(m.find(null_key) == nullptr);
  CHECK_FALSE(m.contains(null_key));
  CHECK_FALSE(m.erase(null_key));
  m.clear();
  CHECK(m.empty());
}

TEST_CASE("nexenne::container::slot_map default key fields are zero") {
  map_t::key const null_key;
  CHECK(null_key.index() == 0);
  CHECK(null_key.generation() == 0);
}

TEST_CASE("nexenne::container::slot_map insert(const&) copies an lvalue") {
  map_t m;
  int const value{42};
  auto const k{m.insert(value)};
  REQUIRE(m.find(k) != nullptr);
  CHECK(*m.find(k) == 42);
}

TEST_CASE("nexenne::container::slot_map find const overload and contains const") {
  map_t m;
  auto const k{m.insert(7)};
  map_t const& cm{m};
  REQUIRE(cm.find(k) != nullptr);
  CHECK(*cm.find(k) == 7);
  CHECK(cm.contains(k));
  static_assert(std::is_const_v<std::remove_pointer_t<decltype(cm.find(k))>>);
}

TEST_CASE("nexenne::container::slot_map a forged key with the wrong generation misses") {
  map_t m;
  auto const a{m.insert(10)};
  map_t::key const forged{a.index(), a.generation() + 1};
  CHECK(m.find(forged) == nullptr);
  CHECK_FALSE(m.contains(forged));
  CHECK_FALSE(m.erase(forged));
  map_t::key const far{1000, 1};
  CHECK(m.find(far) == nullptr);
  CHECK_FALSE(m.erase(far));
  CHECK(*m.find(a) == 10);
}

TEST_CASE("nexenne::container::slot_map repeated recycle bumps the generation each time") {
  map_t m;
  auto prev{m.insert(0)};
  auto const slot{prev.index()};
  for (int i{1}; i <= 5; ++i) {
    REQUIRE(m.erase(prev));
    auto const next{m.insert(i)};
    CHECK(next.index() == slot);
    CHECK(next.generation() != prev.generation());
    CHECK(m.find(prev) == nullptr);
    REQUIRE(m.find(next) != nullptr);
    CHECK(*m.find(next) == i);
    prev = next;
  }
  CHECK(m.size() == 1);
}

TEST_CASE("nexenne::container::slot_map free list reuses a freed slot before growing") {
  map_t m;
  auto const a{m.insert(1)};
  auto const b{m.insert(2)};
  CHECK(b.index() == a.index() + 1);
  m.erase(a);
  auto const c{m.insert(3)};
  CHECK(c.index() == a.index());
  CHECK(c.generation() != a.generation());
  CHECK(m.size() == 2);
}

TEST_CASE("nexenne::container::slot_map iterator: operator->, post-increment, mutate") {
  cn::slot_map<std::pair<int, int>> m;
  m.insert({1, 10});
  m.insert({2, 20});
  int first_sum{0};
  for (auto it{m.begin()}; it != m.end(); ++it) {
    first_sum += it->first;
  }
  CHECK(first_sum == 3);
  auto it{m.begin()};
  auto const snap{it++};
  CHECK(snap != it);
  for (auto& p : m) {
    p.second += 1;
  }
  int second_sum{0};
  for (auto const& p : m) {
    second_sum += p.second;
  }
  CHECK(second_sum == 32);
}

TEST_CASE("nexenne::container::slot_map mutable-to-const iterator conversion") {
  map_t m;
  m.insert(5);
  map_t::const_iterator ci{m.begin()};
  REQUIRE(ci != m.end());
  CHECK(*ci == 5);
}

TEST_CASE("nexenne::container::slot_map constructor reserves and reserve grows capacity") {
  map_t m{16};
  CHECK(m.capacity() >= 16);
  CHECK(m.empty());
  m.reserve(64);
  CHECK(m.capacity() >= 64);
  auto const k{m.insert(1)};
  m.reserve(128);
  REQUIRE(m.find(k) != nullptr);
  CHECK(*m.find(k) == 1);
}

TEST_CASE("nexenne::container::slot_map shrink_to_fit keeps keys valid") {
  map_t m;
  m.reserve(32);
  auto const a{m.insert(1)};
  auto const b{m.insert(2)};
  m.erase(a);
  m.shrink_to_fit();
  REQUIRE(m.find(b) != nullptr);
  CHECK(*m.find(b) == 2);
  CHECK(m.find(a) == nullptr);
}

TEST_CASE("nexenne::container::slot_map self swap is a no-op") {
  map_t m;
  auto const a{m.insert(1)};
  auto const b{m.insert(2)};
  m.swap(m);
  CHECK(m.size() == 2);
  CHECK(*m.find(a) == 1);
  CHECK(*m.find(b) == 2);
}

TEST_CASE("nexenne::container::slot_map non-trivial string values, recycle keeps no leak") {
  cn::slot_map<std::string> m;
  auto const a{m.insert(std::string(40, 'a'))};
  auto const b{m.emplace(40, 'b')};
  REQUIRE(m.find(a) != nullptr);
  CHECK(*m.find(a) == std::string(40, 'a'));
  CHECK(m.erase(a));
  auto const c{m.insert(std::string(40, 'c'))};
  REQUIRE(m.find(c) != nullptr);
  CHECK(*m.find(c) == std::string(40, 'c'));
  CHECK(*m.find(b) == std::string(40, 'b'));
  m.clear();
  CHECK(m.empty());
}

TEST_CASE("nexenne::container::slot_map max_size is capped by the index_type range") {
  cn::slot_map<char> m;
  CHECK(m.max_size() == std::numeric_limits<std::uint32_t>::max());
}

[[nodiscard]] constexpr auto constexpr_slot_map_scenario() -> bool {
  map_t m{4};
  if (m.capacity() < 4) {
    return false;
  }
  auto const a{m.insert(10)};
  auto const b{m.emplace(20)};
  auto const c{m.insert(30)};
  if (m.size() != 3 || *m.find(b) != 20 || !m.contains(c)) {
    return false;
  }
  if (!m.erase(b) || m.erase(b) || m.contains(b) || m.find(b) != nullptr) {
    return false;
  }
  auto const d{m.insert(40)};
  if (d.index() != b.index() || d.generation() == b.generation() || m.contains(b)) {
    return false;
  }
  auto sum{0};
  for (auto const& value : m) {
    sum += value;
  }
  auto const& cm{m};
  auto count{0};
  for (auto it{cm.cbegin()}; it != cm.cend(); ++it) {
    ++count;
  }
  if (sum != 10 + 30 + 40 || count != 3 || *cm.find(a) != 10) {
    return false;
  }
  m.reserve(16);
  m.clear();
  return m.empty() && !m.contains(a) && !m.contains(d) && m.capacity() >= 16;
}

static_assert(constexpr_slot_map_scenario());

TEST_CASE("nexenne::container::slot_map constexpr scenario also holds at run time") {
  CHECK(constexpr_slot_map_scenario());
}

}  // namespace
