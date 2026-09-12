/**
 * @file
 * @brief Tests for nexenne::container::flat_hash_map.
 */

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nexenne/container/flat_hash_map.hpp>
#include <nexenne/utility/ignore.hpp>

namespace {

namespace cn = nexenne::container;
using map_t = cn::flat_hash_map<int, int>;

struct transparent_string_hash {
  using is_transparent = void;

  [[nodiscard]] auto operator()(std::string_view const s) const noexcept -> std::size_t {
    return std::hash<std::string_view>{}(s);
  }
};

struct colliding_hash {
  [[nodiscard]] auto operator()(int) const noexcept -> std::size_t {
    return 0;
  }
};

TEST_CASE("nexenne::container::flat_hash_map insert keeps, insert_or_assign overwrites") {
  map_t m;
  CHECK(m.insert(1, 10));
  CHECK_FALSE(m.insert(1, 99));
  CHECK(*m.find(1) == 10);
  CHECK_FALSE(m.insert_or_assign(1, 99));
  CHECK(*m.find(1) == 99);
  CHECK(m.insert_or_assign(2, 20));
  CHECK(m.size() == 2);
}

TEST_CASE("nexenne::container::flat_hash_map find, contains, count, at") {
  map_t m;
  m.insert(5, 50);
  REQUIRE(m.find(5) != nullptr);
  CHECK(*m.find(5) == 50);
  CHECK(m.find(99) == nullptr);
  CHECK(m.contains(5));
  CHECK_FALSE(m.contains(99));
  CHECK(m.count(5) == 1);
  CHECK(m.count(99) == 0);
  REQUIRE(m.at(5) != nullptr);
  CHECK(*m.at(5) == 50);
  CHECK(m.at(99) == nullptr);
}

TEST_CASE("nexenne::container::flat_hash_map operator[] accesses and default-inserts") {
  map_t m;
  m[1] = 10;
  CHECK(m[1] == 10);
  m[1] = 11;
  CHECK(m[1] == 11);
  CHECK(m[99] == 0);
  CHECK(m.size() == 2);
}

TEST_CASE("nexenne::container::flat_hash_map emplace constructs but does not overwrite") {
  cn::flat_hash_map<int, std::string> m;
  CHECK(m.emplace(1, "hello"));
  CHECK(*m.find(1) == "hello");
  CHECK_FALSE(m.emplace(1, "world"));
  CHECK(*m.find(1) == "hello");
}

TEST_CASE("nexenne::container::flat_hash_map erase frees a reusable slot") {
  map_t m;
  m.insert(1, 10);
  m.insert(2, 20);
  m.insert(3, 30);
  CHECK(m.erase(2));
  CHECK(m.size() == 2);
  CHECK_FALSE(m.contains(2));
  CHECK_FALSE(m.erase(99));

  CHECK(m.insert(4, 40));
  CHECK(m.contains(4));
  CHECK(m.contains(1));
  CHECK(m.contains(3));
}

TEST_CASE("nexenne::container::flat_hash_map grows and rehashes, keeping every entry") {
  map_t m;
  for (int i{0}; i < 100; ++i) {
    CHECK(m.insert(i, i * 10));
  }
  CHECK(m.size() == 100);
  CHECK(m.capacity() >= 100);
  for (int i{0}; i < 100; ++i) {
    REQUIRE(m.find(i) != nullptr);
    CHECK(*m.find(i) == i * 10);
  }
}

TEST_CASE("nexenne::container::flat_hash_map iterates every entry once") {
  map_t m;
  m.insert(1, 10);
  m.insert(2, 20);
  m.insert(3, 30);
  int count{0};
  int key_sum{0};
  int value_sum{0};
  for (auto const& [k, v] : m) {
    ++count;
    key_sum += k;
    value_sum += v;
  }
  CHECK(count == 3);
  CHECK(key_sum == 6);
  CHECK(value_sum == 60);
}

TEST_CASE("nexenne::container::flat_hash_map reserve avoids a rehash") {
  map_t m;
  m.reserve(100);
  auto const reserved{m.capacity()};
  CHECK(reserved >= 100);
  for (int i{0}; i < 50; ++i) {
    m.insert(i, i);
  }
  CHECK(m.capacity() == reserved);
}

TEST_CASE("nexenne::container::flat_hash_map clear and shrink_to_fit") {
  map_t m;
  for (int i{0}; i < 10; ++i) {
    m.insert(i, i);
  }
  m.clear();
  CHECK(m.empty());
  CHECK_FALSE(m.contains(5));
  m.insert(1, 1);
  m.shrink_to_fit();
  CHECK(m.contains(1));
}

TEST_CASE("nexenne::container::flat_hash_map swap") {
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

TEST_CASE("nexenne::container::flat_hash_map equality is order-independent") {
  map_t a;
  a.insert(1, 10);
  a.insert(2, 20);
  map_t b;
  b.insert(2, 20);
  b.insert(1, 10);
  map_t c;
  c.insert(1, 10);
  c.insert(2, 99);
  CHECK(a == b);
  CHECK_FALSE(a == c);
}

TEST_CASE("nexenne::container::flat_hash_map works with string keys") {
  cn::flat_hash_map<std::string, int> m;
  m.insert("alpha", 1);
  m.insert("beta", 2);
  CHECK(m.contains("alpha"));
  REQUIRE(m.find("beta") != nullptr);
  CHECK(*m.find("beta") == 2);
  CHECK(m["gamma"] == 0);
}

TEST_CASE("nexenne::container::flat_hash_map holds a move-only value") {
  cn::flat_hash_map<int, std::unique_ptr<int>> m;
  m.insert(1, std::make_unique<int>(10));
  m.emplace(2, std::make_unique<int>(20));
  REQUIRE(m.find(1) != nullptr);
  CHECK(**m.find(1) == 10);
  CHECK(m.size() == 2);
}

TEST_CASE("nexenne::container::flat_hash_map operator[] with a moved-from key does not SEGV") {
  cn::flat_hash_map<std::string, int> m;
  std::string key{"a key well past the small-string optimisation buffer length"};
  m[std::move(key)] = 42;
  CHECK(m.size() == 1);
  REQUIRE(m.find("a key well past the small-string optimisation buffer length") != nullptr);
  CHECK(*m.find("a key well past the small-string optimisation buffer length") == 42);
  auto const stored{m.begin()->first};
  CHECK(m[stored] == 42);
  CHECK(m.size() == 1);
}

TEST_CASE("nexenne::container::flat_hash_map insert_or_assign self-aliasing is safe") {
  cn::flat_hash_map<int, std::string> m;
  m.insert_or_assign(1, "a stored value comfortably longer than the SSO buffer");
  m.insert_or_assign(1, *m.at(1));
  REQUIRE(m.find(1) != nullptr);
  CHECK(*m.find(1) == "a stored value comfortably longer than the SSO buffer");
  CHECK(m.size() == 1);
}

TEST_CASE("nexenne::container::flat_hash_map resolves heavy collisions correctly") {
  cn::flat_hash_map<int, int, colliding_hash> m;
  for (int i{0}; i < 50; ++i) {
    CHECK(m.insert(i, i * 100));
  }
  CHECK(m.size() == 50);
  for (int i{0}; i < 50; ++i) {
    REQUIRE(m.find(i) != nullptr);
    CHECK(*m.find(i) == i * 100);
  }
  for (int i{0}; i < 50; i += 2) {
    CHECK(m.erase(i));
  }
  CHECK(m.size() == 25);
  for (int i{0}; i < 50; ++i) {
    CHECK(m.contains(i) == (i % 2 != 0));
  }
  for (int i{0}; i < 50; i += 2) {
    CHECK(m.insert(i, i * 100));
  }
  CHECK(m.size() == 50);
  for (int i{0}; i < 50; ++i) {
    REQUIRE(m.find(i) != nullptr);
    CHECK(*m.find(i) == i * 100);
  }
}

TEST_CASE("nexenne::container::flat_hash_map erase-then-reinsert churn keeps lookups exact") {
  map_t m;
  for (int i{0}; i < 30; ++i) {
    m.insert(i, i);
  }
  for (int round{0}; round < 40; ++round) {
    for (int i{0}; i < 30; ++i) {
      CHECK(m.erase(i));
    }
    CHECK(m.empty());
    for (int i{0}; i < 30; ++i) {
      CHECK(m.insert(i, i + round));
    }
    CHECK(m.size() == 30);
    for (int i{0}; i < 30; ++i) {
      REQUIRE(m.find(i) != nullptr);
      CHECK(*m.find(i) == i + round);
    }
  }
}

TEST_CASE("nexenne::container::flat_hash_map load_factor and capacity edges") {
  map_t empty;
  CHECK(empty.capacity() == 0);
  CHECK(empty.load_factor() == doctest::Approx(0.0));

  map_t m;
  m.insert(1, 1);
  CHECK(m.capacity() == map_t::initial_capacity);
  CHECK(m.load_factor() > 0.0);
  CHECK(m.load_factor() < 1.0);
  for (int i{0}; i < 14; ++i) {
    m.insert(100 + i, i);
  }
  CHECK(m.capacity() > map_t::initial_capacity);
  CHECK(m.load_factor() <= 0.875);
}

TEST_CASE("nexenne::container::flat_hash_map shrink_to_fit releases an oversized table") {
  map_t m;
  for (int i{0}; i < 200; ++i) {
    m.insert(i, i);
  }
  auto const grown{m.capacity()};
  for (int i{0}; i < 190; ++i) {
    CHECK(m.erase(i));
  }
  CHECK(m.size() == 10);
  m.shrink_to_fit();
  CHECK(m.capacity() < grown);
  for (int i{190}; i < 200; ++i) {
    REQUIRE(m.find(i) != nullptr);
    CHECK(*m.find(i) == i);
  }
  m.insert(0, 0);
  CHECK(m.contains(0));
}

TEST_CASE("nexenne::container::flat_hash_map empty map queries return misses, never UB") {
  map_t m;
  CHECK(m.empty());
  CHECK(m.size() == 0);
  CHECK(m.find(1) == nullptr);
  CHECK_FALSE(m.contains(1));
  CHECK(m.count(1) == 0);
  CHECK(m.at(1) == nullptr);
  CHECK_FALSE(m.erase(1));
  CHECK(m.begin() == m.end());
  auto const& cm{m};
  CHECK(cm.find(1) == nullptr);
  CHECK(cm.begin() == cm.end());
}

TEST_CASE("nexenne::container::flat_hash_map const iteration and accessors") {
  map_t m;
  m.insert(1, 10);
  m.insert(2, 20);
  auto const& cm{m};
  int count{0};
  int key_sum{0};
  for (auto const& [k, v] : cm) {
    ++count;
    key_sum += k;
    CHECK(v == k * 10);
  }
  CHECK(count == 2);
  CHECK(key_sum == 3);
  CHECK(cm.cbegin() != cm.cend());
  CHECK(cm.hash_function()(7) == std::hash<int>{}(7));
  CHECK(cm.key_eq()(3, 3));
  CHECK_FALSE(cm.key_eq()(3, 4));
}

TEST_CASE("nexenne::container::flat_hash_map with string keys and values survives churn") {
  cn::flat_hash_map<std::string, std::string> m;
  m.insert("alpha", "value-alpha-past-the-small-string-buffer");
  m.insert_or_assign("beta", "value-beta-past-the-small-string-buffer");
  m["gamma"] = "value-gamma-past-the-small-string-buffer";
  CHECK(m.size() == 3);
  m.insert_or_assign("alpha", "alpha-overwritten-and-also-fairly-long-here");
  REQUIRE(m.find("alpha") != nullptr);
  CHECK(*m.find("alpha") == "alpha-overwritten-and-also-fairly-long-here");
  CHECK(m.erase("beta"));
  CHECK(m.find("beta") == nullptr);
  CHECK(m.size() == 2);
}

TEST_CASE("nexenne::container::flat_hash_map differential against std::unordered_map") {
  cn::flat_hash_map<std::string, int> flat;
  std::unordered_map<std::string, int> ref;
  std::mt19937 rng{20260622};
  std::uniform_int_distribution<int> key_dist{0, 80};
  std::uniform_int_distribution<int> val_dist{0, 1000};
  std::uniform_int_distribution<int> op_dist{0, 4};
  for (int step{0}; step < 6000; ++step) {
    auto const key{"k" + std::to_string(key_dist(rng))};
    auto const val{val_dist(rng)};
    switch (op_dist(rng)) {
      case 0: {
        auto const flat_new{flat.insert(key, val)};
        auto const ref_new{ref.insert({key, val}).second};
        CHECK(flat_new == ref_new);
        break;
      }
      case 1: {
        flat.insert_or_assign(key, val);
        ref[key] = val;
        break;
      }
      case 2: {
        flat[key] = val;
        ref[key] = val;
        break;
      }
      case 3: {
        CHECK(flat.erase(key) == (ref.erase(key) != 0));
        break;
      }
      default: {
        auto const* const p{flat.find(key)};
        auto const it{ref.find(key)};
        if (it == ref.end()) {
          CHECK(p == nullptr);
        } else {
          REQUIRE(p != nullptr);
          CHECK(*p == it->second);
        }
        break;
      }
    }
    CHECK(flat.size() == ref.size());
  }
  for (auto const& [k, v] : ref) {
    auto const* const p{flat.find(k)};
    REQUIRE(p != nullptr);
    CHECK(*p == v);
  }
  std::size_t flat_count{0};
  for ([[maybe_unused]] auto const& entry : flat) {
    ++flat_count;
  }
  CHECK(flat_count == ref.size());
}

TEST_CASE("nexenne::container::flat_hash_map churn at constant live size keeps capacity bounded") {
  map_t m;
  CHECK(m.insert(-1, -1));
  for (int i{0}; i < 100000; ++i) {
    CHECK(m.insert(i, i));
    CHECK(m.erase(i));
  }
  CHECK(m.size() == 1);
  CHECK(m.contains(-1));
  CHECK(m.capacity() <= 64);
}

TEST_CASE(
  "nexenne::container::flat_hash_map inserting an existing key does not rehash or invalidate"
) {
  map_t m;
  for (int i{0}; i < 14; ++i) {
    CHECK(m.insert(i, i * 10));
  }
  auto const cap_before{m.capacity()};
  int const* const pinned{m.find(3)};
  REQUIRE(pinned != nullptr);

  CHECK_FALSE(m.insert(3, 999));
  CHECK(m.capacity() == cap_before);
  CHECK(m.find(3) == pinned);
  CHECK(*pinned == 30);

  CHECK_FALSE(m.insert_or_assign(3, 31));
  CHECK(m.capacity() == cap_before);
  CHECK(m.find(3) == pinned);
  CHECK(*pinned == 31);

  CHECK_FALSE(m.emplace(3, 40));
  CHECK(m.capacity() == cap_before);
  CHECK(m.find(3) == pinned);

  CHECK_FALSE(m.try_emplace(3, 50));
  CHECK(m.capacity() == cap_before);
  CHECK(m.find(3) == pinned);
}

TEST_CASE("nexenne::container::flat_hash_map heterogeneous lookup with transparent functors") {
  cn::flat_hash_map<std::string, int, transparent_string_hash, std::equal_to<>> m;
  CHECK(m.insert("alpha", 1));
  CHECK(m.insert("beta", 2));
  CHECK(m.contains(std::string_view{"alpha"}));
  CHECK(m.count(std::string_view{"beta"}) == 1);
  CHECK_FALSE(m.contains(std::string_view{"gamma"}));
  REQUIRE(m.find(std::string_view{"beta"}) != nullptr);
  CHECK(*m.find(std::string_view{"beta"}) == 2);
  CHECK(m.at(std::string_view{"alpha"}) != nullptr);
  CHECK(m.erase(std::string_view{"alpha"}));
  CHECK_FALSE(m.contains(std::string_view{"alpha"}));
}

TEST_CASE("nexenne::container::flat_hash_map moved-from source is empty and reusable") {
  map_t src;
  for (int i{0}; i < 5; ++i) {
    CHECK(src.insert(i, i * 10));
  }
  map_t const dst{std::move(src)};
  CHECK(dst.size() == 5);
  CHECK(src.empty());
  CHECK(src.size() == 0);
  CHECK(src.capacity() == 0);
  CHECK(src == map_t{});
  CHECK(src.insert(7, 70));
  CHECK(src.size() == 1);
  CHECK(src.capacity() == map_t::initial_capacity);

  map_t assigned;
  CHECK(assigned.insert(1, 1));
  assigned = std::move(src);
  CHECK(assigned.size() == 1);
  CHECK(src.empty());

  map_t copy;
  copy = dst;
  CHECK(copy == dst);
}

TEST_CASE("nexenne::container::flat_hash_map spreads keys that differ only in high bits") {
  cn::flat_hash_map<std::uint64_t, int> m;
  std::vector<std::uint64_t> inserted;
  for (std::uint64_t i{1}; i <= 8; ++i) {
    inserted.push_back(i << 32U);
    CHECK(m.insert(inserted.back(), 0));
  }
  std::vector<std::uint64_t> iterated;
  for (auto const& [key, value] : m) {
    iterated.push_back(key);
  }
  CHECK(iterated.size() == inserted.size());
  CHECK(iterated != inserted);
}

TEST_CASE("nexenne::container::flat_hash_map reserve after erasing everything keeps entries put") {
  map_t m;
  for (int i{0}; i < 12; ++i) {
    CHECK(m.insert(i, i));
  }
  for (int i{0}; i < 12; ++i) {
    CHECK(m.erase(i));
  }
  m.reserve(4);
  CHECK(m.insert(101, 1));
  auto const* const first{m.find(101)};
  REQUIRE(first != nullptr);
  CHECK(m.insert(102, 2));
  CHECK(m.insert(103, 3));
  CHECK(m.insert(104, 4));
  CHECK(m.find(101) == first);
}

template <typename Map, typename K>
concept subscriptable = requires(Map& m, K k) { m[std::move(k)]; };
static_assert(subscriptable<cn::flat_hash_map<std::unique_ptr<int>, int>, std::unique_ptr<int>>);
static_assert(subscriptable<cn::flat_hash_map<std::string, int>, std::string>);

struct counting_hash {
  static inline std::uint64_t calls{0};

  [[nodiscard]] auto operator()(int const key) const noexcept -> std::size_t {
    ++calls;
    return std::hash<int>{}(key);
  }
};

[[nodiscard]] auto hashes_taken() noexcept -> std::uint64_t {
  return std::exchange(counting_hash::calls, 0);
}

TEST_CASE("nexenne::container::flat_hash_map hashes a key once per operation") {
  cn::flat_hash_map<int, int, counting_hash> m;
  nexenne::utility::ignore(hashes_taken());
  CHECK(m.insert(1, 10));
  CHECK(hashes_taken() == 1);
  CHECK_FALSE(m.insert(1, 11));
  CHECK(hashes_taken() == 1);
  CHECK(m.insert_or_assign(2, 20));
  CHECK(hashes_taken() == 1);
  CHECK_FALSE(m.insert_or_assign(2, 21));
  CHECK(hashes_taken() == 1);
  CHECK(m.emplace(3, 30));
  CHECK(hashes_taken() == 1);
  CHECK(m.try_emplace(4, 40));
  CHECK(hashes_taken() == 1);
  m[5] = 50;
  CHECK(hashes_taken() == 1);
  m[5] = 51;
  CHECK(hashes_taken() == 1);
  CHECK(m.find(5) != nullptr);
  CHECK(hashes_taken() == 1);
  CHECK(m.contains(4));
  CHECK(hashes_taken() == 1);
  CHECK(m.count(3) == 1);
  CHECK(hashes_taken() == 1);
  CHECK(m.at(2) != nullptr);
  CHECK(hashes_taken() == 1);
  CHECK(m.erase(1));
  CHECK(hashes_taken() == 1);
  CHECK_FALSE(m.erase(1));
  CHECK(hashes_taken() == 1);
  for (int i{100}; i < 1100; ++i) {
    CHECK(m.insert(i, i));
  }
  CHECK(hashes_taken() == 1000);
  CHECK(m.size() == 1004);
  CHECK(*m.find(5) == 51);
}

TEST_CASE("nexenne::container::flat_hash_map operator[] takes a move-only key") {
  cn::flat_hash_map<std::unique_ptr<int>, int> m;
  m[std::make_unique<int>(7)] = 3;
  REQUIRE(m.size() == 1);
  auto const& [key, value]{*m.begin()};
  REQUIRE(key != nullptr);
  CHECK(*key == 7);
  CHECK(value == 3);
}

struct alive_counter {
  static inline int alive{0};
  int value{0};

  explicit alive_counter(int const v) noexcept : value{v} {
    ++alive;
  }

  alive_counter(alive_counter const& other) noexcept : value{other.value} {
    ++alive;
  }

  alive_counter(alive_counter&& other) noexcept : value{other.value} {
    ++alive;
  }

  auto operator=(alive_counter const&) noexcept -> alive_counter& = default;
  auto operator=(alive_counter&&) noexcept -> alive_counter& = default;

  ~alive_counter() {
    --alive;
  }
};

TEST_CASE("nexenne::container::flat_hash_map slots own their entries' lifetimes") {
  REQUIRE(alive_counter::alive == 0);
  {
    cn::flat_hash_map<int, alive_counter> m;
    for (int i{0}; i < 200; ++i) {
      CHECK(m.emplace(i, i));
    }
    CHECK(alive_counter::alive == 200);
    for (int i{0}; i < 200; i += 2) {
      CHECK(m.erase(i));
    }
    CHECK(alive_counter::alive == 100);
    CHECK(m.insert_or_assign(1, alive_counter{-1}) == false);
    CHECK(alive_counter::alive == 100);
    {
      auto copy{m};
      CHECK(alive_counter::alive == 200);
      REQUIRE(copy.find(1) != nullptr);
      CHECK(copy.find(1)->value == -1);
      auto moved{std::move(copy)};
      CHECK(alive_counter::alive == 200);
    }
    CHECK(alive_counter::alive == 100);
    m.shrink_to_fit();
    CHECK(alive_counter::alive == 100);
    m.clear();
    CHECK(alive_counter::alive == 0);
    CHECK(m.emplace(7, 7));
    CHECK(alive_counter::alive == 1);
  }
  CHECK(alive_counter::alive == 0);
}

// Places a key by its hundreds: key / 100 is its home bucket in a 16-slot
// table. The home bucket is the top log2(capacity) bits of
// hash * 0x9e3779b97f4a7c15 (the documented Fibonacci reduction), so the hash
// is (home << 60) times that multiplier's inverse modulo 2^64. Only meaningful
// for a 64-bit size_t.
struct placed_hash {
  [[nodiscard]] static constexpr auto inverse_golden() noexcept -> std::uint64_t {
    constexpr auto golden{std::uint64_t{0x9e3779b97f4a7c15ULL}};
    auto inverse{golden};  // Newton's iteration doubles the correct low bits
    for (int i{0}; i < 5; ++i) {
      inverse *= std::uint64_t{2} - (golden * inverse);
    }
    return inverse;
  }

  [[nodiscard]] auto operator()(int const key) const noexcept -> std::size_t {
    auto const home{static_cast<std::uint64_t>(key / 100)};
    std::size_t const hash{(home << 60U) * inverse_golden()};
    return hash;
  }
};

static_assert(placed_hash::inverse_golden() * std::uint64_t{0x9e3779b97f4a7c15ULL} == 1U);

TEST_CASE("nexenne::container::flat_hash_map erase shifts its probe run back") {
  if constexpr (sizeof(std::size_t) == 8) {
    cn::flat_hash_map<int, int, placed_hash> m;
    for (auto const k : {200, 201, 300, 500}) {
      CHECK(m.insert(k, k));
    }
    REQUIRE(m.capacity() == 16);
    auto const* const at2{m.find(200)};
    auto const* const at3{m.find(201)};
    auto const* const at5{m.find(500)};
    CHECK(m.erase(200));
    CHECK(m.find(201) == at2);
    CHECK(m.find(300) == at3);
    CHECK(m.find(500) == at5);
    CHECK_FALSE(m.contains(200));
    CHECK(m.size() == 3);
    CHECK(m.capacity() == 16);
  }
}

TEST_CASE("nexenne::container::flat_hash_map erase shifts across the table's end") {
  if constexpr (sizeof(std::size_t) == 8) {
    cn::flat_hash_map<int, int, placed_hash> m;
    for (auto const k : {1500, 1501, 1502, 100}) {
      CHECK(m.insert(k, k));
    }
    REQUIRE(m.capacity() == 16);
    auto const* const at15{m.find(1500)};
    auto const* const at0{m.find(1501)};
    auto const* const at1{m.find(1502)};
    CHECK(m.erase(1500));
    CHECK(m.find(1501) == at15);
    CHECK(m.find(1502) == at0);
    CHECK(m.find(100) == at1);
    for (auto const k : {1501, 1502, 100}) {
      REQUIRE(m.find(k) != nullptr);
      CHECK(*m.find(k) == k);
    }
    CHECK(m.begin()->first == 1502);
  }
}

TEST_CASE("nexenne::container::flat_hash_map erase by collected keys, not while iterating") {
  cn::flat_hash_map<int, int, colliding_hash> m;
  for (int i{0}; i < 40; ++i) {
    CHECK(m.insert(i, i));
  }
  auto doomed{std::vector<int>{}};
  for (auto const& [key, value] : m) {
    if (value % 3 == 0) {
      doomed.push_back(key);
    }
  }
  for (auto const key : doomed) {
    CHECK(m.erase(key));
  }
  CHECK(m.size() == 26);
  for (int i{0}; i < 40; ++i) {
    CHECK(m.contains(i) == (i % 3 != 0));
  }
}

TEST_CASE("nexenne::container::flat_hash_map random erase matches std::unordered_map") {
  struct weak_hash {
    [[nodiscard]] auto operator()(int const k) const noexcept -> std::size_t {
      return static_cast<std::size_t>(k % 8) * std::size_t{0x9e3779b9U};
    }
  };

  cn::flat_hash_map<int, int, weak_hash> m;
  std::unordered_map<int, int> ref;
  auto state{std::uint32_t{31}};
  for (int step{0}; step < 20000; ++step) {
    state = (state * 1664525U) + 1013904223U;
    auto const k{static_cast<int>(state % 301U)};
    if (step % 3 == 0) {
      CHECK(m.erase(k) == (ref.erase(k) == 1));
    } else {
      CHECK(m.insert_or_assign(k, step) == ref.insert_or_assign(k, step).second);
    }
  }
  REQUIRE(m.size() == ref.size());
  for (auto const& [k, v] : ref) {
    REQUIRE(m.find(k) != nullptr);
    CHECK(*m.find(k) == v);
  }
}

TEST_CASE("nexenne::container::flat_hash_map churn at a reserved live size never rehashes") {
  map_t m;
  m.reserve(8);
  auto const capacity{m.capacity()};
  CHECK(m.insert(-1, -1));
  for (int i{0}; i < 7; ++i) {
    CHECK(m.insert(i, i));
  }
  for (int i{7}; i < 100000; ++i) {
    CHECK(m.erase(i - 7));
    CHECK(m.insert(i, i));
  }
  CHECK(m.capacity() == capacity);
  CHECK(m.size() == 8);
  CHECK(m.find(-1) != nullptr);
  CHECK(*m.find(-1) == -1);
}

}  // namespace
