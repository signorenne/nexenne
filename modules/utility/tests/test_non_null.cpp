/**
 * @file
 * @brief Tests for nexenne::utility::non_null.
 */

#include <doctest/doctest.h>

#include <compare>
#include <cstddef>
#include <functional>
#include <memory>
#include <type_traits>
#include <unordered_set>
#include <utility>

#include <nexenne/utility/non_null.hpp>

namespace {

using nexenne::utility::non_null;

struct widget {
  int value{0};

  auto bump() -> void {
    ++value;
  }

  [[nodiscard]] auto peek() const -> int {
    return value;
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

TEST_CASE("nexenne::utility::non_null compares by pointer, unequal to nullptr while valid") {
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
  CHECK(nullptr != first);        // reversed + synthesized
}

TEST_CASE("nexenne::utility::non_null compares against a raw pointer in both directions") {
  widget a{};
  widget b{};
  non_null<widget*> const nn{&a};

  // The exact-match overload resolves what used to be an ambiguous comparison
  // and never routes the raw pointer through the asserting constructor.
  CHECK(nn == &a);
  CHECK(&a == nn);  // rewritten candidate: raw == wrapper
  CHECK_FALSE(nn == &b);
  CHECK_FALSE(&b == nn);
  CHECK(nn != &b);  // synthesized from ==
  CHECK(&b != nn);

  widget* const null_raw{nullptr};
  CHECK_FALSE(nn == null_raw);  // a null raw pointer compares safely: no assert
  CHECK(null_raw != nn);
}

TEST_CASE("nexenne::utility::non_null inequality is synthesized from equality") {
  widget a{};
  widget b{};
  non_null<widget*> const first{&a};
  non_null<widget*> const same{&a};
  non_null<widget*> const other{&b};

  CHECK_FALSE(first != same);
  CHECK(first != other);
}

TEST_CASE("nexenne::utility::non_null owns a unique_ptr without copying it") {
  non_null<std::unique_ptr<widget>> const nn{std::make_unique<widget>(41)};

  CHECK(nn->value == 41);  // operator-> chains through unique_ptr, no copy
  nn->bump();
  CHECK((*nn).value == 42);
  CHECK(nn.get() != nullptr);  // get() returns a reference, no copy
}

TEST_CASE("nexenne::utility::non_null exposes the wrapped unique_ptr as a reference") {
  non_null<std::unique_ptr<widget>> const nn{std::make_unique<widget>(3)};

  // get() returns a const reference: no copy is made (unique_ptr is non-copyable
  // so a by-value return would not even compile).
  static_assert(std::is_same_v<decltype(nn.get()), std::unique_ptr<widget> const&>);
  static_assert(std::is_same_v<decltype(nn.operator->()), std::unique_ptr<widget> const&>);
  CHECK(nn.get().get() != nullptr);  // unique_ptr::get on the underlying handle
  CHECK((*nn).value == 3);
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

TEST_CASE("nexenne::utility::non_null shared_ptr copies refcount on construction") {
  auto sp{std::make_shared<widget>(1)};
  auto const before{sp.use_count()};

  non_null<std::shared_ptr<widget>> const nn{sp};  // copies the handle: +1 ref
  CHECK(sp.use_count() == before + 1);

  // Implicit conversion yields a copy of the shared_ptr: +1 ref while it lives.
  {
    std::shared_ptr<widget> const taken{nn};
    CHECK(taken.get() == sp.get());
    CHECK(sp.use_count() == before + 2);
  }
  CHECK(sp.use_count() == before + 1);
}

TEST_CASE("nexenne::utility::non_null preserves the pointee's constness") {
  widget w{5};
  non_null<widget const*> const nn{&w};
  static_assert(std::is_same_v<decltype(*nn), widget const&>);
  CHECK((*nn).value == 5);
  CHECK(nn->peek() == 5);  // only const members reachable
}

TEST_CASE("nexenne::utility::non_null is copyable and copies the wrapped pointer") {
  widget w{12};
  non_null<widget*> const original{&w};
  non_null<widget*> const copy{original};  // copy construction

  CHECK(copy.get() == original.get());
  CHECK(copy == original);

  non_null<widget*> assigned{&w};
  widget other{99};
  non_null<widget*> const src{&other};
  assigned = src;  // copy assignment
  CHECK(assigned.get() == &other);
  CHECK(assigned == src);
}

TEST_CASE("nexenne::utility::non_null is movable, moving a unique_ptr through it") {
  non_null<std::unique_ptr<widget>> source{std::make_unique<widget>(55)};
  widget* const raw{source.get().get()};

  non_null<std::unique_ptr<widget>> const moved{std::move(source)};
  CHECK(moved.get().get() == raw);  // ownership transferred to the new wrapper
  CHECK(moved->value == 55);

  // Contract: the moved-from wrapper holds null; its invariant is suspended
  // and the only valid operations are destruction and reassignment. The
  // nullptr comparison is the one honest, non-asserting probe of that state
  // (every accessor asserts in debug builds when used after a move).
  CHECK(source == nullptr);  // NOLINT(bugprone-use-after-move): the contract under test
  CHECK(nullptr == source);

  // Reassignment restores the invariant and makes the wrapper usable again.
  source = std::make_unique<widget>(7);
  CHECK(source != nullptr);
  CHECK(source->value == 7);
}

TEST_CASE("nexenne::utility::non_null hashes like the pointer it wraps") {
  widget a{};
  widget b{};
  non_null<widget*> const nn{&a};

  // std::hash<non_null<T>> forwards to std::hash<T>, so a wrapper and its raw
  // pointer land in the same bucket.
  CHECK(std::hash<non_null<widget*>>{}(nn) == std::hash<widget*>{}(&a));

  // And that makes non_null usable directly as an unordered key.
  std::unordered_set<non_null<widget*>> const watched{nn};
  CHECK(watched.contains(nn));
  CHECK_FALSE(watched.contains(non_null<widget*>{&b}));
}

TEST_CASE("nexenne::utility::non_null wraps a pointer to const-qualified data round-trip") {
  int const datum{77};
  non_null<int const*> const nn{&datum};

  CHECK(*nn == 77);
  int const* const back{nn};  // implicit conversion preserves const
  CHECK(back == &datum);
  static_assert(std::is_same_v<decltype(*nn), int const&>);
}

TEST_CASE("nexenne::utility::non_null mutating through the pointee writes through") {
  widget w{0};
  non_null<widget*> const nn{&w};
  (*nn).value = 314;
  CHECK(w.value == 314);
  nn->value = 159;
  CHECK(w.value == 159);
}

TEST_CASE("nexenne::utility::non_null is usable in a constexpr context") {
  static constexpr int storage{42};
  constexpr non_null<int const*> nn{&storage};
  static_assert(*nn == 42);
  static_assert(nn.get() == &storage);
  constexpr int const* raw{nn};
  static_assert(raw == &storage);
  // nn == nullptr compares the pointer, which the undefined-behavior sanitizer
  // instruments into a non-constant form, so this leg is checked at runtime.
  CHECK(!(nn == nullptr));
}

// compile-time properties

static_assert(std::is_same_v<non_null<widget*>::value_type, widget*>);
static_assert(
  std::is_same_v<non_null<std::unique_ptr<widget>>::value_type, std::unique_ptr<widget>>
);
static_assert(std::is_same_v<non_null<std::shared_ptr<widget>>::element_type, widget>);
static_assert(std::is_same_v<non_null<widget*>::element_type, widget>);
static_assert(std::is_same_v<non_null<widget*>::pointer_type, widget*>);
// pointer_traits<widget const*>::element_type is `widget const`, not `widget`.
static_assert(std::is_same_v<non_null<widget const*>::element_type, widget const>);

// Constructing or assigning from nullptr is rejected at compile time.
static_assert(
  !std::is_constructible_v<non_null<int*>, std::nullptr_t>,
  "non_null must reject nullptr at compile time"
);
static_assert(
  !std::is_assignable_v<non_null<int*>&, std::nullptr_t>,
  "non_null must reject nullptr assignment at compile time"
);

// A non-const non_null is still constructible/assignable from its pointer type.
static_assert(std::is_constructible_v<non_null<widget*>, widget*>);

// Implicit conversion from a raw pointer is intended (single-argument ctor).
static_assert(std::is_convertible_v<widget*, non_null<widget*>>);

// And the implicit conversion back to the pointer type exists.
static_assert(std::is_convertible_v<non_null<widget*>, widget*>);

// Trivially copyable when the wrapped pointer is (a raw pointer): registers.
static_assert(std::is_trivially_copyable_v<non_null<widget*>>);

// Construction is noexcept (the assert is debug-only and does not throw).
static_assert(std::is_nothrow_constructible_v<non_null<widget*>, widget*>);

}  // namespace
