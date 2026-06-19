/**
 * @file
 * @brief Tests for nexenne::container::slot_map.
 */

#include <doctest/doctest.h>

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
  m.insert(1);                         // fills slot 0, generation 1
  CHECK(m.find(null_key) == nullptr);  // null key (generation 0) still misses
}

TEST_CASE("nexenne::container::slot_map erase invalidates only that key") {
  map_t m;
  auto const a{m.insert(10)};
  auto const b{m.insert(20)};
  CHECK(m.erase(a));
  CHECK(m.find(a) == nullptr);  // a is gone
  CHECK_FALSE(m.contains(a));
  REQUIRE(m.find(b) != nullptr);  // b still valid
  CHECK(*m.find(b) == 20);
  CHECK(m.size() == 1);
  CHECK_FALSE(m.erase(a));  // double erase is a no-op
}

TEST_CASE("nexenne::container::slot_map recycles a slot but the old key stays stale (ABA guard)") {
  map_t m;
  auto const a{m.insert(10)};
  CHECK(m.erase(a));
  auto const b{m.insert(99)};     // reuses slot 0 with a bumped generation
  CHECK(b.index() == a.index());  // same slot
  CHECK(b.generation() != a.generation());
  CHECK(m.find(a) == nullptr);  // the stale key does not see the new occupant
  REQUIRE(m.find(b) != nullptr);
  CHECK(*m.find(b) == 99);
}

TEST_CASE("nexenne::container::slot_map emplace constructs in place") {
  cn::slot_map<std::string> m;
  auto const k{m.emplace(3, 'x')};  // std::string(3, 'x') == "xxx"
  REQUIRE(m.find(k) != nullptr);
  CHECK(*m.find(k) == "xxx");
}

TEST_CASE("nexenne::container::slot_map iterates only live elements, skipping vacancies") {
  map_t m;
  auto const a{m.insert(1)};
  m.insert(2);
  auto const c{m.insert(3)};
  m.erase(a);  // slot 0 vacant
  m.erase(c);  // last slot vacant; only 2 remains in the middle
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
  CHECK(m.capacity() == cap);  // storage retained
  // slots are reusable after clear
  auto const c{m.insert(3)};
  CHECK(*m.find(c) == 3);
}

TEST_CASE("nexenne::container::slot_map swap exchanges contents, keys stay valid against their map"
) {
  map_t a;
  auto const ka{a.insert(1)};
  map_t b;
  auto const kb{b.insert(9)};
  b.insert(8);
  swap(a, b);
  CHECK(a.size() == 2);
  CHECK(b.size() == 1);
  REQUIRE(a.find(kb) != nullptr);  // kb now resolves against a
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
  CHECK_FALSE(m.erase(null_key));  // erasing from empty is a no-op
  m.clear();                       // clear on empty is a no-op
  CHECK(m.empty());
}

TEST_CASE("nexenne::container::slot_map default key fields are zero") {
  map_t::key const null_key;
  CHECK(null_key.index() == 0);
  CHECK(null_key.generation() == 0);  // generation 0 is the null sentinel
}

TEST_CASE("nexenne::container::slot_map insert(const&) copies an lvalue") {
  map_t m;
  int const value{42};
  auto const k{m.insert(value)};  // lvalue overload
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
  // same slot, off-by-one generation: must read as absent, never UB
  map_t::key const forged{a.index(), a.generation() + 1};
  CHECK(m.find(forged) == nullptr);
  CHECK_FALSE(m.contains(forged));
  CHECK_FALSE(m.erase(forged));
  // an out-of-range index is bounds-checked before any vector access
  map_t::key const far{1000, 1};
  CHECK(m.find(far) == nullptr);
  CHECK_FALSE(m.erase(far));
  // the genuine key still works
  CHECK(*m.find(a) == 10);
}

TEST_CASE("nexenne::container::slot_map repeated recycle bumps the generation each time") {
  map_t m;
  auto prev{m.insert(0)};
  auto const slot{prev.index()};
  for (int i{1}; i <= 5; ++i) {
    REQUIRE(m.erase(prev));
    auto const next{m.insert(i)};  // recycles the same slot
    CHECK(next.index() == slot);
    CHECK(next.generation() != prev.generation());
    CHECK(m.find(prev) == nullptr);  // every older key remains stale
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
  m.erase(a);                 // slot a.index() goes on the free list
  auto const c{m.insert(3)};  // should reuse a's slot, not allocate a new one
  CHECK(c.index() == a.index());
  CHECK(c.generation() != a.generation());
  CHECK(m.size() == 2);
}

TEST_CASE("nexenne::container::slot_map iterator: operator->, post-increment, mutate") {
  cn::slot_map<std::pair<int, int>> m;
  m.insert({1, 10});
  m.insert({2, 20});
  // operator-> reaches members
  int first_sum{0};
  for (auto it{m.begin()}; it != m.end(); ++it) {
    first_sum += it->first;
  }
  CHECK(first_sum == 3);
  // post-increment yields the pre-advance position
  auto it{m.begin()};
  auto const snap{it++};
  CHECK(snap != it);
  // mutate through the iterator's reference
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
  map_t::const_iterator ci{m.begin()};  // convert mutable to const
  REQUIRE(ci != m.end());
  CHECK(*ci == 5);
}

TEST_CASE("nexenne::container::slot_map constructor reserves and reserve grows capacity") {
  map_t m{16};
  CHECK(m.capacity() >= 16);
  CHECK(m.empty());
  m.reserve(64);
  CHECK(m.capacity() >= 64);
  auto const k{m.insert(1)};  // keys remain valid after a reserve
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
  auto const a{m.insert(std::string(40, 'a'))};  // heap-allocating string
  auto const b{m.emplace(40, 'b')};
  REQUIRE(m.find(a) != nullptr);
  CHECK(*m.find(a) == std::string(40, 'a'));
  CHECK(m.erase(a));                             // destroys the string
  auto const c{m.insert(std::string(40, 'c'))};  // reuses the slot
  REQUIRE(m.find(c) != nullptr);
  CHECK(*m.find(c) == std::string(40, 'c'));
  CHECK(*m.find(b) == std::string(40, 'b'));
  m.clear();  // destroys the rest
  CHECK(m.empty());
}

}  // namespace
