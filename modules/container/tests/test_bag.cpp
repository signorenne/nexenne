/**
 * @file
 * @brief Tests for nexenne::container::bag.
 */

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <nexenne/container/bag.hpp>

namespace {

namespace cn = nexenne::container;
using bag_t = cn::bag<int>;

static_assert([] {
  bag_t b;
  b.insert(1);
  b.insert(2);
  b.insert(3);
  auto const r{b.erase_at(0)};
  return r.has_value() && b.size() == 2;
}());

TEST_CASE("nexenne::container::bag insert, emplace, size") {
  bag_t b;
  CHECK(b.empty());
  b.insert(1);
  b.emplace(2);
  b.insert(3);
  CHECK(b.size() == 3);
  CHECK_FALSE(b.empty());
}

TEST_CASE("nexenne::container::bag erase_at swap-pop and out_of_range") {
  bag_t b{10, 20, 30, 40};
  CHECK(b.erase_at(1).has_value());
  CHECK(b.size() == 3);
  CHECK(std::ranges::find(b, 20) == b.end());
  CHECK(std::ranges::find(b, 40) != b.end());
  CHECK(b.erase_at(10).error() == cn::container_error::out_of_range);
}

TEST_CASE("nexenne::container::bag erase_first") {
  bag_t b{1, 2, 2, 3};
  CHECK(b.erase_first(2));
  CHECK(b.size() == 3);
  CHECK_FALSE(b.erase_first(99));
}

TEST_CASE("nexenne::container::bag erase_all removes every occurrence") {
  bag_t b{1, 2, 1, 3, 1};
  CHECK(b.erase_all(1) == 3);
  CHECK(b.size() == 2);
  CHECK(std::ranges::find(b, 1) == b.end());
  CHECK(b.erase_all(99) == 0);
}

TEST_CASE("nexenne::container::bag indexed access, data, span") {
  bag_t b{5, 6, 7};
  b[0] = 50;
  CHECK(b[0] == 50);
  CHECK(b.data()[1] == 6);
  CHECK(b.span().size() == 3);
}

TEST_CASE("nexenne::container::bag iteration forward and reverse") {
  bag_t b{1, 2, 3};
  int sum{0};
  for (int const x : b) {
    sum += x;
  }
  CHECK(sum == 6);
  std::vector<int> const reversed(b.rbegin(), b.rend());
  CHECK(reversed == std::vector{3, 2, 1});
}

TEST_CASE("nexenne::container::bag copy is deep, move works (rule of zero)") {
  bag_t a{1, 2, 3};
  bag_t b{a};
  CHECK(b.size() == 3);
  a[0] = 99;
  CHECK(b[0] == 1);

  bag_t c{std::move(a)};
  CHECK(c.size() == 3);
}

TEST_CASE("nexenne::container::bag swap and assign") {
  bag_t a{1, 2};
  bag_t b{7, 8, 9};
  swap(a, b);
  CHECK(a.size() == 3);
  CHECK(b.size() == 2);

  a.assign({1});
  CHECK(a.size() == 1);
  a.assign(std::size_t{3}, 5);
  CHECK(a.size() == 3);
  CHECK(a[2] == 5);
}

TEST_CASE("nexenne::container::bag CTAD from an iterator pair") {
  std::array<int, 3> const src{1, 2, 3};
  cn::bag b(src.begin(), src.end());
  CHECK(b.size() == 3);
}

TEST_CASE("nexenne::container::bag holds a move-only type") {
  cn::bag<std::unique_ptr<int>> b;
  b.insert(std::make_unique<int>(5));
  b.emplace(std::make_unique<int>(6));
  CHECK(b.size() == 2);
  CHECK(*b[0] == 5);
  CHECK(b.erase_at(0).has_value());
  CHECK(b.size() == 1);
}

namespace {
struct move_counter {
  int* copies;
  int* moves;

  move_counter(int* c, int* m) noexcept : copies{c}, moves{m} {}

  move_counter(move_counter const& other) noexcept : copies{other.copies}, moves{other.moves} {
    ++*copies;
  }

  move_counter(move_counter&& other) noexcept : copies{other.copies}, moves{other.moves} {
    ++*moves;
  }
};
}  // namespace

TEST_CASE("nexenne::container::bag insert picks copy vs move (no extra move)") {
  int copies{0};
  int moves{0};
  cn::bag<move_counter> b;
  b.reserve(4);
  move_counter c{&copies, &moves};

  b.insert(c);
  CHECK(copies == 1);
  CHECK(moves == 0);

  b.insert(std::move(c));
  CHECK(copies == 1);
  CHECK(moves == 1);
}

TEST_CASE("nexenne::container::bag erase the actual last element (swap_pop no-move branch)") {
  bag_t single{42};
  CHECK(single.erase_at(0).has_value());
  CHECK(single.empty());

  bag_t tail{1, 2, 3};
  CHECK(tail.erase_at(2).has_value());
  CHECK(tail.size() == 2);

  bag_t same{7, 7, 7};
  CHECK(same.erase_all(7) == 3);
  CHECK(same.empty());
}

TEST_CASE("nexenne::container::bag erase_at moves the former last into the gap") {
  bag_t b{10, 20, 30, 40, 50};
  CHECK(b.erase_at(1).has_value());
  CHECK(b.size() == 4);
  CHECK(b[1] == 50);
  CHECK(b[0] == 10);
  CHECK(b[2] == 30);
  CHECK(b[3] == 40);
  CHECK(std::ranges::find(b, 20) == b.end());
}

TEST_CASE("nexenne::container::bag erase_at on a two-element bag") {
  bag_t b{1, 2};
  CHECK(b.erase_at(0).has_value());
  CHECK(b.size() == 1);
  CHECK(b[0] == 2);
  CHECK(b.erase_at(0).has_value());
  CHECK(b.empty());
}

TEST_CASE("nexenne::container::bag erase_at out_of_range leaves the bag unchanged") {
  bag_t b{1, 2, 3};
  auto const r{b.erase_at(3)};
  CHECK_FALSE(r.has_value());
  CHECK(r.error() == cn::container_error::out_of_range);
  CHECK(b.size() == 3);
  CHECK(b[0] == 1);
  CHECK(b[1] == 2);
  CHECK(b[2] == 3);
}

TEST_CASE("nexenne::container::bag erase_at on an empty bag is out_of_range") {
  bag_t b;
  auto const r{b.erase_at(0)};
  CHECK_FALSE(r.has_value());
  CHECK(r.error() == cn::container_error::out_of_range);
  CHECK(b.empty());
}

TEST_CASE("nexenne::container::bag constructors: default, init-list, iterator pair") {
  bag_t const def;
  CHECK(def.empty());

  bag_t const il{1, 2, 3, 4};
  CHECK(il.size() == 4);
  CHECK(il[0] == 1);
  CHECK(il[3] == 4);

  std::array<int, 3> const src{7, 8, 9};
  bag_t const range(src.begin(), src.end());
  CHECK(range.size() == 3);
  CHECK(range[0] == 7);
  CHECK(range[2] == 9);
}

TEST_CASE("nexenne::container::bag emplace returns a reference to the new element") {
  bag_t b;
  auto& ref{b.emplace(42)};
  CHECK(ref == 42);
  ref = 99;
  CHECK(b[0] == 99);
}

TEST_CASE("nexenne::container::bag assign from an iterator pair replaces contents") {
  bag_t b{1, 2, 3, 4, 5};
  std::array<int, 2> const src{10, 20};
  b.assign(src.begin(), src.end());
  CHECK(b.size() == 2);
  CHECK(b[0] == 10);
  CHECK(b[1] == 20);
}

TEST_CASE("nexenne::container::bag reserve, capacity, shrink_to_fit, max_size") {
  bag_t b;
  CHECK(b.capacity() == 0);
  b.reserve(16);
  CHECK(b.capacity() >= 16);
  b.insert(1);
  b.insert(2);
  CHECK(b.size() == 2);
  b.shrink_to_fit();
  CHECK(b.capacity() >= 2);
  CHECK(b.size() == 2);
  CHECK(b.max_size() > 0);
}

TEST_CASE("nexenne::container::bag clear empties without changing capacity guarantees") {
  bag_t b{1, 2, 3};
  b.reserve(8);
  auto const cap_before{b.capacity()};
  b.clear();
  CHECK(b.empty());
  CHECK(b.size() == 0);
  CHECK(b.capacity() == cap_before);
  b.insert(9);
  CHECK(b.size() == 1);
  CHECK(b[0] == 9);
}

TEST_CASE("nexenne::container::bag member swap exchanges contents") {
  bag_t a{1, 2};
  bag_t b{7, 8, 9};
  a.swap(b);
  CHECK(a.size() == 3);
  CHECK(a[0] == 7);
  CHECK(b.size() == 2);
  CHECK(b[0] == 1);
}

TEST_CASE("nexenne::container::bag copy assignment is deep and independent") {
  bag_t a{1, 2, 3};
  bag_t b{9};
  b = a;
  CHECK(b.size() == 3);
  a[0] = 99;
  CHECK(b[0] == 1);
}

TEST_CASE("nexenne::container::bag move assignment transfers contents") {
  bag_t a{1, 2, 3};
  bag_t b{9};
  b = std::move(a);
  CHECK(b.size() == 3);
  CHECK(b[0] == 1);
}

TEST_CASE("nexenne::container::bag moved-from bag is valid and reusable") {
  bag_t a{1, 2, 3};
  bag_t const b{std::move(a)};
  CHECK(b.size() == 3);
  a.clear();
  CHECK(a.empty());
  a.insert(5);
  a.insert(6);
  CHECK(a.size() == 2);
  CHECK(a[0] == 5);
}

TEST_CASE("nexenne::container::bag self-aliasing insert from an existing element") {
  bag_t b;
  b.reserve(8);
  b.insert(11);
  b.insert(22);
  b.insert(b[0]);
  CHECK(b.size() == 3);
  CHECK(b[2] == 11);
  b.emplace(b[1]);
  CHECK(b.size() == 4);
  CHECK(b[3] == 22);
}

TEST_CASE("nexenne::container::bag self copy- and move-assignment are safe") {
  bag_t b{1, 2, 3};
  auto& self{b};
  b = self;
  CHECK(b.size() == 3);
  CHECK(b[0] == 1);
  CHECK(b[2] == 3);

  b = std::move(self);
  b.clear();
  b.insert(9);
  CHECK(b.size() == 1);
  CHECK(b[0] == 9);
}

TEST_CASE("nexenne::container::bag const iteration covers every live element once") {
  bag_t const b{4, 5, 6};
  int sum{0};
  std::size_t count{0};
  for (int const x : b) {
    sum += x;
    ++count;
  }
  CHECK(sum == 15);
  CHECK(count == 3);

  int csum{0};
  for (auto it{b.cbegin()}; it != b.cend(); ++it) {
    csum += *it;
  }
  CHECK(csum == 15);

  std::vector<int> const rev(b.crbegin(), b.crend());
  CHECK(rev == std::vector{6, 5, 4});
}

TEST_CASE("nexenne::container::bag const accessors: operator[], data, span") {
  bag_t const b{3, 4, 5};
  CHECK(b[1] == 4);
  CHECK(b.data()[2] == 5);
  auto const sp{b.span()};
  CHECK(sp.size() == 3);
  CHECK(sp[0] == 3);
}

TEST_CASE("nexenne::container::bag non-trivial std::string survives swap-pop erase") {
  cn::bag<std::string> b;
  b.insert(std::string(64, 'a'));
  b.insert(std::string(64, 'b'));
  b.insert(std::string(64, 'c'));
  b.emplace(64, 'd');
  CHECK(b.size() == 4);

  CHECK(b.erase_at(1).has_value());
  CHECK(b.size() == 3);
  CHECK(b[1] == std::string(64, 'd'));
  CHECK(std::ranges::find(b, std::string(64, 'b')) == b.end());

  CHECK(b.erase_first(std::string(64, 'a')));
  CHECK(b.size() == 2);
  CHECK(b.erase_all(std::string(64, 'c')) == 1);
  CHECK(b.size() == 1);

  b.clear();
  CHECK(b.empty());
}

TEST_CASE("nexenne::container::bag move-only unique_ptr: middle erase frees nothing twice") {
  cn::bag<std::unique_ptr<int>> b;
  b.insert(std::make_unique<int>(10));
  b.emplace(std::make_unique<int>(20));
  b.insert(std::make_unique<int>(30));
  b.insert(std::make_unique<int>(40));
  CHECK(b.size() == 4);

  CHECK(b.erase_at(1).has_value());
  CHECK(b.size() == 3);
  CHECK(*b[1] == 40);
  CHECK(*b[0] == 10);
  CHECK(*b[2] == 30);

  CHECK(b.erase_at(2).has_value());
  CHECK(b.size() == 2);

  b.clear();
  CHECK(b.empty());
}

namespace {
struct life_counter {
  static int alive;

  life_counter() noexcept {
    ++alive;
  }

  life_counter(life_counter const&) noexcept {
    ++alive;
  }

  life_counter(life_counter&&) noexcept {
    ++alive;
  }

  auto operator=(life_counter const&) noexcept -> life_counter& = default;
  auto operator=(life_counter&&) noexcept -> life_counter& = default;

  ~life_counter() {
    --alive;
  }
};

int life_counter::alive = 0;
}  // namespace

TEST_CASE("nexenne::container::bag clear and erase destroy elements (live count balances)") {
  CHECK(life_counter::alive == 0);
  {
    cn::bag<life_counter> b;
    b.emplace();
    b.emplace();
    b.emplace();
    CHECK(life_counter::alive == 3);

    CHECK(b.erase_at(0).has_value());
    CHECK(life_counter::alive == 2);

    b.clear();
    CHECK(life_counter::alive == 0);

    b.emplace();
    CHECK(life_counter::alive == 1);
  }
  CHECK(life_counter::alive == 0);
}

static_assert([] {
  bag_t b{10, 20, 30, 40};
  auto const r{b.erase_at(1)};
  return r.has_value() && b.size() == 3 && b[1] == 40 && b[0] == 10 && b[2] == 30;
}());

static_assert([] {
  bag_t b{1, 2};
  auto const r{b.erase_at(5)};
  return !r.has_value() && r.error() == cn::container_error::out_of_range && b.size() == 2;
}());

static_assert([] {
  bag_t a{1, 2, 3};
  bag_t b{9};
  a.swap(b);
  a.clear();
  a.insert(7);
  return a.size() == 1 && a[0] == 7 && b.size() == 3 && b[0] == 1;
}());

TEST_CASE("nexenne::container::bag erase_all accepts a reference to its own element") {
  cn::bag<int> ints{7, 5, 3, 7};
  CHECK(ints.erase_all(ints[1]) == 1);
  CHECK(ints.size() == 3);
  CHECK(std::ranges::count(ints, 7) == 2);
  CHECK(std::ranges::count(ints, 3) == 1);

  std::string const a(64, 'a');
  std::string const z(64, 'z');
  cn::bag<std::string> strings{a, z, a};
  CHECK(strings.erase_all(strings[2]) == 2);
  REQUIRE(strings.size() == 1);
  CHECK(strings[0] == z);

  cn::bag<int> same{4, 4, 4};
  CHECK(same.erase_all(same[0]) == 3);
  CHECK(same.empty());
}

}  // namespace
