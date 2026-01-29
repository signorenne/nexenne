/**
 * @file
 * @brief Tests for nexenne::container::dense_map.
 */

#include <doctest/doctest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <nexenne/container/dense_map.hpp>

namespace {

namespace cn = nexenne::container;
using map_t = cn::dense_map<std::uint32_t, int>;

// Exercise the constexpr surface: build, look up, erase, all at compile time.
static_assert([] {
  cn::dense_map<std::uint32_t, int> m;
  bool ok{m.insert(2, 20) && m.insert(5, 50)};
  ok = ok && !m.insert(2, 999);  // present: kept, returns false
  auto const* const v{m.at(2)};
  ok = ok && v != nullptr && *v == 20;
  ok = ok && m.insert_or_assign(2, 21) == false && *m.at(2) == 21;
  ok = ok && m.erase(5) && !m.contains(5) && m.size() == 1;
  return ok;
}());

TEST_CASE("nexenne::container::dense_map insert keeps, insert_or_assign overwrites") {
  map_t m;
  CHECK(m.insert(1, 10));                  // new
  CHECK_FALSE(m.insert(1, 99));            // present: not overwritten
  CHECK(*m.at(1) == 10);                   // original kept
  CHECK_FALSE(m.insert_or_assign(1, 99));  // overwritten (false = not new)
  CHECK(*m.at(1) == 99);
  CHECK(m.insert_or_assign(2, 20));  // new (true)
  CHECK(m.size() == 2);
}

TEST_CASE("nexenne::container::dense_map emplace constructs but does not overwrite") {
  cn::dense_map<std::uint32_t, std::string> m;
  CHECK(m.emplace(1, "hello"));
  CHECK(*m.at(1) == "hello");
  CHECK_FALSE(m.emplace(1, "world"));  // present: not overwritten
  CHECK(*m.at(1) == "hello");
}

TEST_CASE("nexenne::container::dense_map at, find, contains, count") {
  map_t m;
  m.insert(5, 50);
  CHECK(m.contains(5));
  CHECK_FALSE(m.contains(9));
  CHECK(m.count(5) == 1);
  CHECK(m.count(9) == 0);
  REQUIRE(m.at(5) != nullptr);
  CHECK(*m.at(5) == 50);
  CHECK(m.at(9) == nullptr);
  REQUIRE(m.find(5) != m.end());
  CHECK((*m.find(5)).second == 50);
  CHECK(m.find(9) == m.end());
}

TEST_CASE("nexenne::container::dense_map erase swap-pop keeps keys and values in sync") {
  map_t m;
  m.insert(1, 10);
  m.insert(2, 20);
  m.insert(3, 30);
  CHECK(m.erase(1));  // 3 swaps into slot 0
  CHECK(m.size() == 2);
  CHECK_FALSE(m.contains(1));
  CHECK_FALSE(m.erase(99));
  // every surviving key still maps to its own value after the swap-pop
  for (auto const k : m.keys()) {
    REQUIRE(m.at(k) != nullptr);
    CHECK(*m.at(k) == static_cast<int>(k) * 10);
  }
}

TEST_CASE("nexenne::container::dense_map keys and values spans stay parallel") {
  map_t m;
  m.insert(7, 70);
  m.insert(4, 40);
  m.insert(9, 90);
  auto const ks{m.keys()};
  auto const vs{m.values()};
  REQUIRE(ks.size() == vs.size());
  for (std::size_t i{0}; i < ks.size(); ++i) {
    CHECK(static_cast<int>(ks[i]) * 10 == vs[i]);
  }
}

TEST_CASE("nexenne::container::dense_map iterates (key, value) entries") {
  map_t m;
  m.insert(1, 10);
  m.insert(2, 20);
  m.insert(3, 30);
  int key_sum{0};
  int value_sum{0};
  for (auto const [k, v] : m) {
    key_sum += static_cast<int>(k);
    value_sum += v;
  }
  CHECK(key_sum == 6);
  CHECK(value_sum == 60);

  // mutate through the iterator's value reference
  for (auto [k, v] : m) {
    v += 1;
  }
  CHECK(*m.at(1) == 11);
}

TEST_CASE("nexenne::container::dense_map const iteration and mutable-to-const conversion") {
  map_t m;
  m.insert(1, 10);
  m.insert(2, 20);
  map_t::const_iterator ci{m.begin()};  // convert mutable to const
  CHECK(ci != m.end());
  map_t const& cm{m};
  int total{0};
  for (auto const [k, v] : cm) {
    static_cast<void>(k);
    total += v;
  }
  CHECK(total == 30);
}

TEST_CASE("nexenne::container::dense_map index_of, clear, shrink_to_fit") {
  map_t m;
  m.insert(1, 10);
  m.insert(2, 20);
  REQUIRE(m.index_of(1).has_value());
  CHECK(m.index_of(99) == std::nullopt);
  m.clear();
  CHECK(m.empty());
  m.insert(1, 1);
  m.shrink_to_fit();
  CHECK(m.contains(1));
}

TEST_CASE("nexenne::container::dense_map swap") {
  map_t a;
  a.insert(1, 1);
  a.insert(2, 2);
  map_t b;
  b.insert(9, 9);
  swap(a, b);
  CHECK(a.size() == 1);
  CHECK(a.contains(9));
  CHECK(b.size() == 2);
}

TEST_CASE("nexenne::container::dense_map holds a move-only value") {
  cn::dense_map<std::uint32_t, std::unique_ptr<int>> m;
  m.insert(1, std::make_unique<int>(10));
  m.emplace(2, std::make_unique<int>(20));
  REQUIRE(m.at(1) != nullptr);
  CHECK(**m.at(1) == 10);
  m.erase(1);  // swap-pop must move the unique_ptr, not copy
  CHECK(m.size() == 1);
  REQUIRE(m.at(2) != nullptr);
  CHECK(**m.at(2) == 20);
}

TEST_CASE("nexenne::container::dense_map<Key, void> is a tag set") {
  cn::dense_map<std::uint32_t, void> tags;
  CHECK(tags.insert(3));
  CHECK_FALSE(tags.insert(3));  // already present
  CHECK(tags.insert(7));
  CHECK(tags.contains(3));
  CHECK(tags.count(7) == 1);
  REQUIRE(tags.find(3) != tags.end());
  CHECK(tags.erase(3));
  CHECK_FALSE(tags.contains(3));
  CHECK(tags.size() == 1);

  std::vector<std::uint32_t> seen;
  for (auto const k : tags) {
    seen.push_back(k);
  }
  CHECK(seen == std::vector<std::uint32_t>{7});
}

}  // namespace
