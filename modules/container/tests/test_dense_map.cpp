/**
 * @file
 * @brief Tests for nexenne::container::dense_map.
 */

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <nexenne/container/dense_map.hpp>
#include <nexenne/utility/discard.hpp>

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
    nexenne::utility::discard(k);
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

TEST_CASE("nexenne::container::dense_map empty map: queries, iteration, spans") {
  map_t m;
  CHECK(m.empty());
  CHECK(m.size() == 0);
  CHECK_FALSE(m.contains(0));
  CHECK(m.count(0) == 0);
  CHECK(m.at(0) == nullptr);
  CHECK(m.find(0) == m.end());
  CHECK(m.index_of(0) == std::nullopt);
  CHECK(m.begin() == m.end());
  CHECK(m.keys().empty());
  CHECK(m.values().empty());
  CHECK(m.max_size() > 0);
  // erasing and clearing an empty map are well-defined no-ops
  CHECK_FALSE(m.erase(0));
  m.clear();
  m.shrink_to_fit();
  CHECK(m.empty());
}

TEST_CASE("nexenne::container::dense_map single entry boundary") {
  map_t m;
  CHECK(m.insert(0, 100));  // key 0 is valid and indexable
  CHECK(m.size() == 1);
  REQUIRE(m.at(0) != nullptr);
  CHECK(*m.at(0) == 100);
  CHECK(m.index_of(0) == std::optional<std::size_t>{0});
  // erasing the only entry takes the no-swap path (*pos == last_pos)
  CHECK(m.erase(0));
  CHECK(m.empty());
  CHECK(m.at(0) == nullptr);
}

TEST_CASE("nexenne::container::dense_map erase of the last dense entry skips the self-move") {
  map_t m;
  m.insert(1, 10);
  m.insert(2, 20);
  m.insert(3, 30);
  // key 3 is the last dense entry: *pos == last_pos, so no swap-move happens.
  CHECK(m.index_of(3) == std::optional<std::size_t>{2});
  CHECK(m.erase(3));
  CHECK(m.size() == 2);
  CHECK(*m.at(1) == 10);
  CHECK(*m.at(2) == 20);
  CHECK_FALSE(m.contains(3));
}

TEST_CASE("nexenne::container::dense_map sequential erases keep the sparse->dense map intact") {
  map_t m;
  for (std::uint32_t k{0}; k < 8; ++k) {
    m.insert(k, static_cast<int>(k) * 10);
  }
  // erase a scattered set, each time verifying every survivor still resolves
  for (std::uint32_t const victim : {3u, 0u, 6u, 1u}) {
    REQUIRE(m.contains(victim));
    CHECK(m.erase(victim));
    CHECK_FALSE(m.contains(victim));
    for (auto const k : m.keys()) {
      REQUIRE(m.at(k) != nullptr);
      CHECK(*m.at(k) == static_cast<int>(k) * 10);
      // the dense index reported by index_of must point at the matching value
      auto const pos{m.index_of(k)};
      REQUIRE(pos.has_value());
      CHECK(m.values()[*pos] == static_cast<int>(k) * 10);
    }
  }
  CHECK(m.size() == 4);
}

TEST_CASE("nexenne::container::dense_map const at and const find overloads") {
  map_t m;
  m.insert(1, 10);
  m.insert(2, 20);
  map_t const& cm{m};
  REQUIRE(cm.at(1) != nullptr);
  CHECK(*cm.at(1) == 10);
  CHECK(cm.at(99) == nullptr);
  REQUIRE(cm.find(2) != cm.end());
  CHECK((*cm.find(2)).second == 20);
  CHECK(cm.find(99) == cm.end());
  // const iterator's value reference is const
  static_assert(std::is_const_v<std::remove_reference_t<decltype((*cm.find(2)).second)>>);
}

TEST_CASE("nexenne::container::dense_map insert_or_assign on a fresh key inserts") {
  map_t m;
  CHECK(m.insert_or_assign(4, 40));  // fresh: returns true
  CHECK(m.size() == 1);
  CHECK(*m.at(4) == 40);
  CHECK_FALSE(m.insert_or_assign(4, 41));  // existing: returns false, overwrites
  CHECK(*m.at(4) == 41);
}

TEST_CASE("nexenne::container::dense_map emplace leaves existing args unused on a present key") {
  cn::dense_map<std::uint32_t, std::string> m;
  CHECK(m.emplace(1, 5, 'a'));  // std::string(5, 'a') == "aaaaa"
  CHECK(*m.at(1) == "aaaaa");
  CHECK_FALSE(m.emplace(1, 3, 'b'));  // present: args ignored, value untouched
  CHECK(*m.at(1) == "aaaaa");
  CHECK(m.size() == 1);
}

TEST_CASE("nexenne::container::dense_map reserve, max_size, cbegin/cend") {
  map_t m;
  m.reserve(64, 16);
  m.insert(1, 10);
  m.insert(2, 20);
  CHECK(m.max_size() > 0);
  int total{0};
  for (auto it{m.cbegin()}; it != m.cend(); ++it) {
    total += (*it).second;
  }
  CHECK(total == 30);
}

TEST_CASE("nexenne::container::dense_map iterator post-increment and default construction") {
  map_t m;
  m.insert(1, 10);
  m.insert(2, 20);
  auto it{m.begin()};
  auto const copy{it++};  // post-increment returns the pre-advance position
  CHECK((*copy).first != (*it).first);
  CHECK(++it == m.end());
  map_t::iterator const def{};  // default-constructed iterator is well-formed
  nexenne::utility::discard(def);
}

TEST_CASE("nexenne::container::dense_map self swap is a no-op") {
  map_t m;
  m.insert(1, 10);
  m.insert(2, 20);
  m.swap(m);
  CHECK(m.size() == 2);
  CHECK(*m.at(1) == 10);
  CHECK(*m.at(2) == 20);
}

TEST_CASE("nexenne::container::dense_map non-trivial string values survive erase shuffling") {
  cn::dense_map<std::uint32_t, std::string> m;
  m.insert(1, std::string(40, 'a'));  // long enough to heap-allocate
  m.insert(2, std::string(40, 'b'));
  m.insert(3, std::string(40, 'c'));
  // swap-pop must move-assign, not leak; erase the interior key
  CHECK(m.erase(1));  // value for key 3 moves into slot 0
  REQUIRE(m.at(3) != nullptr);
  CHECK(*m.at(3) == std::string(40, 'c'));
  REQUIRE(m.at(2) != nullptr);
  CHECK(*m.at(2) == std::string(40, 'b'));
  CHECK(m.size() == 2);
  m.clear();  // destroys remaining strings
  CHECK(m.empty());
}

TEST_CASE("nexenne::container::dense_map<Key, void> reserve, index_of, clear, swap, max_size") {
  cn::dense_map<std::uint32_t, void> tags;
  tags.reserve(32, 8);
  CHECK(tags.insert(2));
  CHECK(tags.insert(5));
  CHECK(tags.index_of(2).has_value());
  CHECK(tags.index_of(99) == std::nullopt);
  CHECK(tags.max_size() > 0);
  CHECK(tags.keys().size() == 2);
  std::size_t walked{0};
  for (auto it{tags.cbegin()}; it != tags.cend(); ++it) {
    ++walked;
  }
  CHECK(walked == 2);

  cn::dense_map<std::uint32_t, void> other;
  other.insert(9);
  swap(tags, other);
  CHECK(tags.contains(9));
  CHECK(other.contains(2));
  tags.shrink_to_fit();
  tags.clear();
  CHECK(tags.empty());
}

}  // namespace
