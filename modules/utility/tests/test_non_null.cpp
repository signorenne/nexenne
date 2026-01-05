/**
 * @file
 * @brief Tests for nexenne::utility::non_null.
 */

#include <doctest/doctest.h>

#include <memory>
#include <type_traits>

#include <nexenne/utility/non_null.hpp>

namespace {

using nexenne::utility::non_null;

struct widget {
  int value{0};

  auto bump() -> void {
    ++value;
  }
};

TEST_CASE("nexenne::utility::non_null wraps and forwards a raw pointer") {
  widget w{41};
  non_null<widget*> const nn{&w};

  CHECK(nn.get() == &w);
  CHECK((*nn).value == 41);
  nn->bump();
  CHECK(w.value == 42);

  widget* const raw{nn};  // implicit conversion
  CHECK(raw == &w);
}

TEST_CASE("nexenne::utility::non_null compares by pointer, never equals nullptr") {
  widget a{};
  widget b{};
  non_null<widget*> const first{&a};
  non_null<widget*> const same{&a};
  non_null<widget*> const other{&b};

  CHECK(first == same);
  CHECK_FALSE(first == other);

  CHECK_FALSE(first == nullptr);
  CHECK_FALSE(nullptr == first);  // reversed candidate
  CHECK(first != nullptr);        // synthesized from ==
}

TEST_CASE("nexenne::utility::non_null owns a unique_ptr without copying it") {
  non_null<std::unique_ptr<widget>> const nn{std::make_unique<widget>(41)};

  CHECK(nn->value == 41);  // operator-> chains through unique_ptr, no copy
  nn->bump();
  CHECK((*nn).value == 42);
  CHECK(nn.get() != nullptr);  // get() returns a reference, no copy
}

TEST_CASE("nexenne::utility::non_null does not churn a shared_ptr's refcount") {
  auto sp{std::make_shared<widget>(7)};
  non_null<std::shared_ptr<widget>> const nn{sp};
  auto const refs{sp.use_count()};

  nn->bump();
  (*nn).bump();
  CHECK(nn.get()->value == 9);
  CHECK(sp.use_count() == refs);  // access added no references
}

TEST_CASE("nexenne::utility::non_null preserves the pointee's constness") {
  widget w{5};
  non_null<widget const*> const nn{&w};
  static_assert(std::is_same_v<decltype(*nn), widget const&>);
  CHECK((*nn).value == 5);
}

static_assert(std::is_same_v<non_null<std::shared_ptr<widget>>::element_type, widget>);
static_assert(std::is_same_v<non_null<widget*>::element_type, widget>);
static_assert(
  !std::is_constructible_v<non_null<int*>, std::nullptr_t>,
  "non_null must reject nullptr at compile time"
);

}  // namespace
