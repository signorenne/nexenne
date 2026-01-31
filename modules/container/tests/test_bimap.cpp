/**
 * @file
 * @brief Tests for nexenne::container::bimap.
 */

#include <doctest/doctest.h>

#include <string>

#include <nexenne/container/bimap.hpp>

namespace {

namespace cn = nexenne::container;
using bimap_t = cn::bimap<int, std::string>;

TEST_CASE("nexenne::container::bimap insert binds both sides, rejects a clash") {
  bimap_t b;
  CHECK(b.insert(1, "one"));
  CHECK(b.insert(2, "two"));
  CHECK(b.size() == 2);
  CHECK_FALSE(b.insert(1, "uno"));     // left 1 already bound
  CHECK_FALSE(b.insert(3, "one"));     // right "one" already bound
  CHECK(b.size() == 2);                // unchanged on a clash
  CHECK(*b.find_by_left(1) == "one");  // original kept
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
  CHECK(b.replace(1, "uno") == 1);  // displaced the old right "one"
  REQUIRE(b.find_by_left(1) != nullptr);
  CHECK(*b.find_by_left(1) == "uno");  // forward updated
  REQUIRE(b.find_by_right("uno") != nullptr);
  CHECK(*b.find_by_right("uno") == 1);       // reverse updated
  CHECK(b.find_by_right("one") == nullptr);  // old right gone, no stale binding
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
  CHECK(b.replace(1, "two") == 2);  // left 1 bound, right "two" bound to 2
  CHECK(*b.find_by_left(1) == "two");
  CHECK(*b.find_by_right("two") == 1);
  CHECK(b.find_by_left(2) == nullptr);       // 2's old binding gone
  CHECK(b.find_by_right("one") == nullptr);  // "one"'s old binding gone
  CHECK(b.size() == 1);
}

TEST_CASE("nexenne::container::bimap erase from either side removes the whole pair") {
  bimap_t b;
  b.insert(1, "one");
  b.insert(2, "two");
  CHECK(b.erase_left(1));
  CHECK(b.find_by_left(1) == nullptr);
  CHECK(b.find_by_right("one") == nullptr);  // reverse side cleared too
  CHECK_FALSE(b.erase_left(1));              // already gone
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
  for (auto const& [l, r] : b) {
    key_sum += l;
    ++count;
    static_cast<void>(r);
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
  b.insert(2, "two");  // inserted in a different order
  b.insert(1, "one");
  bimap_t c;
  c.insert(1, "one");
  c.insert(2, "deux");
  CHECK(a == b);
  CHECK_FALSE(a == c);
}

}  // namespace
