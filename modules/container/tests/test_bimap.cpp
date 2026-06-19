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
  b.clear();  // no-op on empty
  CHECK(b.empty());
}

TEST_CASE("nexenne::container::bimap erase_left aliasing the r_to_l entry is UAF-safe") {
  bimap_t b;
  b.insert(1, "one");
  b.insert(2, "two");
  // find_by_right returns a pointer into m_r_to_l's storage; erase_left then
  // erases from m_r_to_l first, which would dangle that pointer if not copied.
  auto const* const aliased{b.find_by_right("one")};
  REQUIRE(aliased != nullptr);
  CHECK(b.erase_left(*aliased));  // *aliased aliases the entry being erased
  CHECK_FALSE(b.contains_left(1));
  CHECK_FALSE(b.contains_right("one"));
  CHECK(b.contains_left(2));  // the unrelated pair is intact
  CHECK(*b.find_by_left(2) == "two");
  CHECK(b.size() == 1);
}

TEST_CASE("nexenne::container::bimap erase_right aliasing the l_to_r entry is UAF-safe") {
  bimap_t b;
  b.insert(1, "one");
  b.insert(2, "two");
  // find_by_left returns a pointer into m_l_to_r's storage; erase_right erases
  // from m_l_to_r first, aliasing that pointer.
  auto const* const aliased{b.find_by_left(2)};
  REQUIRE(aliased != nullptr);
  CHECK(b.erase_right(*aliased));  // *aliased aliases the entry being erased
  CHECK_FALSE(b.contains_right("two"));
  CHECK_FALSE(b.contains_left(2));
  CHECK(b.contains_left(1));
  CHECK(b.size() == 1);
}

TEST_CASE("nexenne::container::bimap replace overwrites an existing right binding consistently") {
  bimap_t b;
  b.insert(1, "one");
  // bind a new left to the already-bound right "one": displaces 1 (the right side)
  CHECK(b.replace(2, "one") == 1);
  CHECK(*b.find_by_right("one") == 2);  // right now points at 2
  CHECK(*b.find_by_left(2) == "one");
  CHECK(b.find_by_left(1) == nullptr);  // 1's old binding is gone
  CHECK(b.size() == 1);
}

TEST_CASE("nexenne::container::bimap replace rebinding the identical pair is idempotent") {
  bimap_t b;
  b.insert(1, "one");
  // left 1 and right "one" name the SAME existing pair, so exactly one binding
  // is displaced (deduplicated), not two.
  CHECK(b.replace(1, "one") == 1);
  CHECK(*b.find_by_left(1) == "one");  // still consistent afterwards
  CHECK(*b.find_by_right("one") == 1);
  CHECK(b.size() == 1);
}

TEST_CASE("nexenne::container::bimap bidirectional invariant holds across mixed mutations") {
  bimap_t b;
  b.insert(1, "one");
  b.insert(2, "two");
  b.insert(3, "three");
  b.replace(1, "uno");  // rebind left 1
  b.erase_right("two");
  b.insert(4, "four");
  b.replace(3, "tres");
  // verify both directions agree for every surviving pair
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
  CHECK(*b.find_by_left(1) == "one");  // bindings preserved across reserve
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
  CHECK_FALSE(b.insert(std::string(40, 'a'), std::string(40, 'z')));  // left clash
  REQUIRE(b.find_by_left(std::string(40, 'a')) != nullptr);
  CHECK(*b.find_by_left(std::string(40, 'a')) == std::string(40, 'x'));
  CHECK(b.replace(std::string(40, 'a'), std::string(40, 'y')) == 2);  // merge two pairs
  CHECK(*b.find_by_right(std::string(40, 'y')) == std::string(40, 'a'));
  CHECK(b.size() == 1);
  b.clear();
  CHECK(b.empty());
}

}  // namespace
