/**
 * @file
 * @brief Tests for nexenne::container::bimap.
 */

#include <doctest/doctest.h>

#include <cctype>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

#include <nexenne/container/bimap.hpp>
#include <nexenne/utility/ignore.hpp>

namespace {

namespace cn = nexenne::container;
using bimap_t = cn::bimap<int, std::string>;

TEST_CASE("nexenne::container::bimap insert binds both sides, rejects a clash") {
  bimap_t b;
  CHECK(b.insert(1, "one"));
  CHECK(b.insert(2, "two"));
  CHECK(b.size() == 2);
  CHECK_FALSE(b.insert(1, "uno"));
  CHECK_FALSE(b.insert(3, "one"));
  CHECK(b.size() == 2);
  CHECK(*b.find_by_left(1) == "one");
}

TEST_CASE("nexenne::container::bimap lookups both ways") {
  bimap_t b;
  b.insert(1, "one");
  REQUIRE(b.find_by_left(1) != nullptr);
  CHECK(*b.find_by_left(1) == "one");
  REQUIRE(b.find_by_right("one") != nullptr);
  CHECK(*b.find_by_right("one") == 1);
  CHECK(b.find_by_left(9) == nullptr);
  CHECK(b.find_by_right("nope") == nullptr);
  CHECK(b.contains_left(1));
  CHECK(b.contains_right("one"));
  CHECK_FALSE(b.contains_left(9));
}

TEST_CASE("nexenne::container::bimap replace overwrites an existing left binding consistently") {
  bimap_t b;
  b.insert(1, "one");
  CHECK(b.replace(1, "uno") == 1);
  REQUIRE(b.find_by_left(1) != nullptr);
  CHECK(*b.find_by_left(1) == "uno");
  REQUIRE(b.find_by_right("uno") != nullptr);
  CHECK(*b.find_by_right("uno") == 1);
  CHECK(b.find_by_right("one") == nullptr);
  CHECK(b.size() == 1);
}

TEST_CASE("nexenne::container::bimap replace of a fresh pair displaces nothing") {
  bimap_t b;
  CHECK(b.replace(1, "one") == 0);
  CHECK(*b.find_by_left(1) == "one");
  CHECK(b.size() == 1);
}

TEST_CASE("nexenne::container::bimap replace merging two pairs displaces two") {
  bimap_t b;
  b.insert(1, "one");
  b.insert(2, "two");
  CHECK(b.replace(1, "two") == 2);
  CHECK(*b.find_by_left(1) == "two");
  CHECK(*b.find_by_right("two") == 1);
  CHECK(b.find_by_left(2) == nullptr);
  CHECK(b.find_by_right("one") == nullptr);
  CHECK(b.size() == 1);
}

TEST_CASE("nexenne::container::bimap erase from either side removes the whole pair") {
  bimap_t b;
  b.insert(1, "one");
  b.insert(2, "two");
  CHECK(b.erase_left(1));
  CHECK(b.find_by_left(1) == nullptr);
  CHECK(b.find_by_right("one") == nullptr);
  CHECK_FALSE(b.erase_left(1));
  CHECK(b.erase_right("two"));
  CHECK(b.find_by_right("two") == nullptr);
  CHECK(b.find_by_left(2) == nullptr);
  CHECK(b.empty());
}

TEST_CASE("nexenne::container::bimap iterates left-to-right pairs") {
  bimap_t b;
  b.insert(1, "one");
  b.insert(2, "two");
  int key_sum{0};
  std::size_t count{0};
  for ([[maybe_unused]] auto const& [l, r] : b) {
    key_sum += l;
    ++count;
  }
  CHECK(key_sum == 3);
  CHECK(count == 2);
}

TEST_CASE("nexenne::container::bimap clear, shrink_to_fit, swap") {
  bimap_t a;
  a.insert(1, "one");
  a.insert(2, "two");
  a.clear();
  CHECK(a.empty());
  a.insert(5, "five");
  a.shrink_to_fit();
  CHECK(a.contains_left(5));

  bimap_t c;
  c.insert(9, "nine");
  swap(a, c);
  CHECK(a.contains_left(9));
  CHECK(c.contains_left(5));
}

TEST_CASE("nexenne::container::bimap equality is pair-set equality") {
  bimap_t a;
  a.insert(1, "one");
  a.insert(2, "two");
  bimap_t b;
  b.insert(2, "two");
  b.insert(1, "one");
  bimap_t c;
  c.insert(1, "one");
  c.insert(2, "deux");
  CHECK(a == b);
  CHECK_FALSE(a == c);
}

TEST_CASE("nexenne::container::bimap empty queries are well-defined") {
  bimap_t b;
  CHECK(b.empty());
  CHECK(b.size() == 0);
  CHECK(b.max_size() > 0);
  CHECK(b.find_by_left(0) == nullptr);
  CHECK(b.find_by_right("x") == nullptr);
  CHECK_FALSE(b.contains_left(0));
  CHECK_FALSE(b.contains_right("x"));
  CHECK_FALSE(b.erase_left(0));
  CHECK_FALSE(b.erase_right("x"));
  CHECK(b.begin() == b.end());
  b.clear();
  CHECK(b.empty());
}

TEST_CASE("nexenne::container::bimap erase_left aliasing the r_to_l entry is UAF-safe") {
  bimap_t b;
  b.insert(1, "one");
  b.insert(2, "two");
  auto const* const aliased{b.find_by_right("one")};
  REQUIRE(aliased != nullptr);
  CHECK(b.erase_left(*aliased));
  CHECK_FALSE(b.contains_left(1));
  CHECK_FALSE(b.contains_right("one"));
  CHECK(b.contains_left(2));
  CHECK(*b.find_by_left(2) == "two");
  CHECK(b.size() == 1);
}

TEST_CASE("nexenne::container::bimap erase_right aliasing the l_to_r entry is UAF-safe") {
  bimap_t b;
  b.insert(1, "one");
  b.insert(2, "two");
  auto const* const aliased{b.find_by_left(2)};
  REQUIRE(aliased != nullptr);
  CHECK(b.erase_right(*aliased));
  CHECK_FALSE(b.contains_right("two"));
  CHECK_FALSE(b.contains_left(2));
  CHECK(b.contains_left(1));
  CHECK(b.size() == 1);
}

TEST_CASE("nexenne::container::bimap replace overwrites an existing right binding consistently") {
  bimap_t b;
  b.insert(1, "one");
  CHECK(b.replace(2, "one") == 1);
  CHECK(*b.find_by_right("one") == 2);
  CHECK(*b.find_by_left(2) == "one");
  CHECK(b.find_by_left(1) == nullptr);
  CHECK(b.size() == 1);
}

TEST_CASE("nexenne::container::bimap replace rebinding the identical pair is idempotent") {
  bimap_t b;
  b.insert(1, "one");
  CHECK(b.replace(1, "one") == 1);
  CHECK(*b.find_by_left(1) == "one");
  CHECK(*b.find_by_right("one") == 1);
  CHECK(b.size() == 1);
}

TEST_CASE("nexenne::container::bimap bidirectional invariant holds across mixed mutations") {
  bimap_t b;
  b.insert(1, "one");
  b.insert(2, "two");
  b.insert(3, "three");
  b.replace(1, "uno");
  b.erase_right("two");
  b.insert(4, "four");
  b.replace(3, "tres");
  for (auto const& [l, r] : b) {
    REQUIRE(b.find_by_left(l) != nullptr);
    CHECK(*b.find_by_left(l) == r);
    REQUIRE(b.find_by_right(r) != nullptr);
    CHECK(*b.find_by_right(r) == l);
  }
  CHECK(b.size() == 3);
  CHECK(*b.find_by_left(1) == "uno");
  CHECK(*b.find_by_left(3) == "tres");
  CHECK(*b.find_by_left(4) == "four");
  CHECK_FALSE(b.contains_left(2));
}

TEST_CASE("nexenne::container::bimap constructor reserves, reserve grows, cbegin/cend") {
  bimap_t b{16};
  CHECK(b.capacity() >= 16);
  b.insert(1, "one");
  b.insert(2, "two");
  b.reserve(64);
  CHECK(b.capacity() >= 64);
  CHECK(*b.find_by_left(1) == "one");
  int key_sum{0};
  for (auto it{b.cbegin()}; it != b.cend(); ++it) {
    key_sum += it->first;
  }
  CHECK(key_sum == 3);
}

TEST_CASE("nexenne::container::bimap self swap is a no-op") {
  bimap_t b;
  b.insert(1, "one");
  b.insert(2, "two");
  b.swap(b);
  CHECK(b.size() == 2);
  CHECK(*b.find_by_left(1) == "one");
  CHECK(*b.find_by_right("two") == 2);
}

TEST_CASE("nexenne::container::bimap with non-trivial types on both sides") {
  cn::bimap<std::string, std::string> b;
  CHECK(b.insert(std::string(40, 'a'), std::string(40, 'x')));
  CHECK(b.insert(std::string(40, 'b'), std::string(40, 'y')));
  CHECK_FALSE(b.insert(std::string(40, 'a'), std::string(40, 'z')));
  REQUIRE(b.find_by_left(std::string(40, 'a')) != nullptr);
  CHECK(*b.find_by_left(std::string(40, 'a')) == std::string(40, 'x'));
  CHECK(b.replace(std::string(40, 'a'), std::string(40, 'y')) == 2);
  CHECK(*b.find_by_right(std::string(40, 'y')) == std::string(40, 'a'));
  CHECK(b.size() == 1);
  b.clear();
  CHECK(b.empty());
}

TEST_CASE("nexenne::container::bimap rolling-registry churn keeps both indexes bounded") {
  cn::bimap<int, int> b;
  CHECK(b.insert(-1, -1));
  for (int i{0}; i < 100000; ++i) {
    CHECK(b.insert(i, i));
    CHECK(b.erase_left(i));
  }
  CHECK(b.size() == 1);
  REQUIRE(b.find_by_left(-1) != nullptr);
  CHECK(*b.find_by_left(-1) == -1);
  CHECK(b.capacity() <= 64);
}

TEST_CASE("nexenne::container::bimap moved-from source is empty") {
  bimap_t src;
  CHECK(src.insert(1, "one"));
  bimap_t const dst{std::move(src)};
  CHECK(dst.size() == 1);
  // NOLINTBEGIN(clang-analyzer-cplusplus.Move): the moved-from state is under test
  CHECK(src.empty());
  CHECK(src.size() == 0);
  CHECK(src.insert(1, "uno"));
  // NOLINTEND(clang-analyzer-cplusplus.Move)
}

std::size_t left_hashes{0};
std::size_t right_hashes{0};

struct left_counting_hash {
  auto operator()(int const value) const noexcept -> std::size_t {
    ++left_hashes;
    return std::hash<int>{}(value);
  }
};

struct right_counting_hash {
  auto operator()(std::string const& value) const noexcept -> std::size_t {
    ++right_hashes;
    return std::hash<std::string>{}(value);
  }
};

TEST_CASE("nexenne::container::bimap hashes each argument key once per mutation") {
  cn::bimap<int, std::string, left_counting_hash, right_counting_hash> b{};
  b.reserve(8);

  left_hashes = 0;
  right_hashes = 0;
  CHECK(b.insert(1, "one"));
  CHECK(left_hashes == 1);
  CHECK(right_hashes == 1);

  left_hashes = 0;
  right_hashes = 0;
  CHECK_FALSE(b.insert(1, "uno"));
  CHECK(left_hashes == 1);
  CHECK(right_hashes == 1);

  left_hashes = 0;
  right_hashes = 0;
  CHECK(b.replace(1, "uno") == 1);
  CHECK(left_hashes == 1);
  CHECK(right_hashes == 2);

  left_hashes = 0;
  right_hashes = 0;
  CHECK(b.erase_left(1));
  CHECK(left_hashes == 1);
  CHECK(right_hashes == 1);
  CHECK(b.empty());
}

auto fold(char const c) noexcept -> char {
  return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

struct caseless_hash {
  auto operator()(std::string_view const text) const noexcept -> std::size_t {
    std::string folded(text.size(), '\0');
    for (std::size_t i{0}; i < text.size(); ++i) {
      folded[i] = fold(text[i]);
    }
    return std::hash<std::string>{}(folded);
  }
};

struct caseless_equal {
  auto operator()(std::string_view const a, std::string_view const b) const noexcept -> bool {
    if (a.size() != b.size()) {
      return false;
    }
    for (std::size_t i{0}; i < a.size(); ++i) {
      if (fold(a[i]) != fold(b[i])) {
        return false;
      }
    }
    return true;
  }
};

TEST_CASE("nexenne::container::bimap takes a key equality per side") {
  cn::bimap<std::string, int, caseless_hash, std::hash<int>, caseless_equal> names{};
  CHECK(names.insert("Alpha", 1));
  CHECK(names.contains_left("ALPHA"));
  CHECK_FALSE(names.insert("alpha", 2));
  REQUIRE(names.find_by_left("aLpHa") != nullptr);
  CHECK(*names.find_by_left("aLpHa") == 1);
  CHECK(names.erase_left("ALPHA"));
  CHECK(names.empty());
}

}  // namespace
