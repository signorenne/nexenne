#pragma once

/**
 * @file
 * @brief Pointer wrapper that asserts a non-null contract on construction.
 */

#include <cassert>
#include <concepts>
#include <cstddef>
#include <functional>
#include <memory>
#include <utility>

namespace nexenne::utility {

namespace detail {

/**
 * @brief A type usable by \c non_null: dereferenceable and null-comparable.
 *
 * @tparam T Candidate pointer type.
 */
template <typename T>
concept pointer_like = requires(T const ptr) {
  *ptr;
  { ptr != nullptr } -> std::convertible_to<bool>;
};

/**
 * @brief A type whose values \c std::hash can hash.
 *
 * @tparam T Candidate type.
 */
template <typename T>
concept hashable = requires(T const value) {
  { std::hash<T>{}(value) } -> std::convertible_to<std::size_t>;
};

}  // namespace detail

/**
 * @brief Pointer wrapper that asserts a non-null contract on construction.
 *
 * Documents and, in debug, enforces that a pointer is never null. Constructing
 * or assigning from the literal \c nullptr is a compile error through a deleted
 * overload; a runtime null asserts in debug at the construction site, so the
 * bug surfaces at the API boundary rather than deep in a call chain. In release
 * it is a zero-overhead wrapper.
 *
 * @tparam T Raw or smart pointer (dereferenceable and comparable to \c nullptr).
 *
 * @pre None at the class level; see the constructor.
 * @post A constructed \c non_null holds a non-null pointer.
 *
 * @warning Moving from a \c non_null (possible when \p T is a move-only smart
 *          pointer such as \c std::unique_ptr) leaves the source holding null:
 *          the invariant is suspended and the only valid operations on the
 *          moved-from wrapper are destruction and reassignment. The accessors
 *          assert in debug builds when used after a move; comparing against
 *          \c nullptr is the one honest, non-asserting probe of that state.
 *
 * @par Example
 * \code
 * auto draw(nexenne::utility::non_null<widget*> target) -> void {
 *   target->paint();  // no null check: the contract guarantees it
 * }
 * \endcode
 */
template <detail::pointer_like T>
class non_null {
public:
  using value_type = T;
  using pointer_type = T;
  using element_type = typename std::pointer_traits<pointer_type>::element_type;

private:
  pointer_type m_ptr;

public:
  /**
   * @brief Constructs from a pointer, asserting it is non-null.
   *
   * The literal \c nullptr is rejected at compile time by a deleted overload;
   * this constructor guards a null value that arrives through a pointer
   * variable, asserting in debug at the construction site.
   *
   * The check is deliberately confined to the runtime path via \c if
   * \c !consteval. A pointer comparison is instrumented under
   * \c -fsanitize=undefined into a form that is not a constant expression, so
   * evaluating it while a block-scope \c constexpr \c non_null is initialised
   * would wrongly reject valid code in sanitizer builds. Skipping the check
   * during constant evaluation keeps such construction well formed everywhere;
   * do not hoist the assert out of the guard.
   *
   * @param ptr Pointer to wrap.
   *
   * @pre \p ptr is not null. A null pointer asserts in debug and is a broken
   *      contract (undefined to dereference) in release; the literal \c nullptr
   *      is a compile error through the deleted overload.
   * @post \c get() returns the stored, non-null pointer.
   */
  // NOLINTNEXTLINE(hicpp-explicit-conversions): non_null is meant to convert implicitly
  constexpr non_null(pointer_type ptr) noexcept : m_ptr{std::move(ptr)} {
    if !consteval {
      assert(m_ptr != nullptr && "non_null: constructed with nullptr");
    }
  }

  /**
   * @brief Deleted: constructing from \c nullptr is a compile-time error.
   *
   * @param ptr The literal \c nullptr, rejected at compile time.
   *
   * @pre None.
   * @post None.
   */
  non_null(std::nullptr_t ptr) = delete;

  /**
   * @brief Deleted: assigning \c nullptr is a compile-time error.
   *
   * @param ptr The literal \c nullptr, rejected at compile time.
   *
   * @return Nothing; the overload is deleted.
   *
   * @pre None.
   * @post None.
   */
  auto operator=(std::nullptr_t ptr) -> non_null& = delete;

  /**
   * @brief The wrapped pointer.
   *
   * Returned by reference so a smart-pointer \p T (e.g. \c std::unique_ptr) is
   * observed without a copy and a \c std::shared_ptr is not refcount-churned.
   *
   * @return Const reference to the stored, non-null pointer.
   *
   * @pre \c *this has not been moved from (asserted in debug).
   * @post The returned pointer is non-null.
   */
  [[nodiscard]] constexpr auto get() const noexcept -> pointer_type const& {
    if !consteval {
      assert(m_ptr != nullptr && "non_null: accessed after move");
    }
    return m_ptr;
  }

  /**
   * @brief Member access through the wrapped pointer.
   *
   * Returned by reference so member access on a smart-pointer \p T chains
   * through its own \c operator-> without copying the handle.
   *
   * @return Const reference to the stored, non-null pointer.
   *
   * @pre \c *this has not been moved from (asserted in debug).
   * @post None.
   */
  constexpr auto operator->() const noexcept -> pointer_type const& {
    if !consteval {
      assert(m_ptr != nullptr && "non_null: accessed after move");
    }
    return m_ptr;
  }

  /**
   * @brief Dereferences the wrapped pointer.
   *
   * @return Reference to the pointed-to object.
   *
   * @pre \c *this has not been moved from (asserted in debug), and the wrapped
   *      pointer still points at a live object.
   * @post None.
   */
  constexpr auto operator*() const noexcept -> element_type& {
    if !consteval {
      assert(m_ptr != nullptr && "non_null: accessed after move");
    }
    return *m_ptr;
  }

  /**
   * @brief Implicit conversion to the wrapped pointer type.
   *
   * Lets \c non_null<T> plug into APIs that take \c T.
   *
   * @return The stored, non-null pointer.
   *
   * @pre \c *this has not been moved from (asserted in debug).
   * @post The returned pointer is non-null.
   */
  // NOLINTNEXTLINE(hicpp-explicit-conversions): non_null is meant to convert implicitly
  constexpr operator pointer_type() const noexcept {
    if !consteval {
      assert(m_ptr != nullptr && "non_null: accessed after move");
    }
    return m_ptr;
  }

  /**
   * @brief Equality of two wrappers, comparing the wrapped pointers.
   *
   * @return \c true when both wrap the same pointer.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] friend constexpr auto operator==(non_null const&, non_null const&) noexcept
    -> bool = default;

  /**
   * @brief Equality of a wrapper and a plain pointer of the wrapped type.
   *
   * An exact-match overload: without it the comparison would be ambiguous
   * between converting the pointer into a \c non_null and converting the
   * \c non_null into a pointer, and the first route would send a null raw
   * pointer through the asserting constructor. The reversed and negated
   * candidates (\c ptr \c == \c nn, \c !=) are rewritten from this operator.
   *
   * @param lhs Wrapper to compare.
   * @param rhs Plain pointer to compare against; may be null.
   *
   * @return \c true when \p rhs equals the wrapped pointer.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] friend constexpr auto
  operator==(non_null const& lhs, pointer_type const& rhs) noexcept -> bool {
    return lhs.m_ptr == rhs;
  }

  /**
   * @brief Comparison against \c nullptr, false while the invariant holds.
   *
   * A \c non_null whose invariant holds is never null; only a moved-from
   * wrapper compares equal to \c nullptr. The comparison reports the stored
   * pointer honestly rather than hardcoding the invariant, so it is the one
   * accessor that is safe on a moved-from wrapper.
   *
   * @param lhs Wrapper to compare.
   *
   * @return \c true only when \p lhs has been moved from and holds null.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] friend constexpr auto operator==(non_null const& lhs, std::nullptr_t) noexcept
    -> bool {
    return lhs.m_ptr == nullptr;
  }
};

}  // namespace nexenne::utility

/**
 * @brief \c std::hash specialisation hashing the wrapped pointer.
 *
 * Forwards to \c std::hash of the wrapped pointer type, so a \c non_null keys
 * unordered containers and hashes identically to the pointer it wraps.
 *
 * @tparam T Wrapped pointer type; must itself be hashable by \c std::hash.
 */
template <nexenne::utility::detail::pointer_like T>
  requires nexenne::utility::detail::hashable<T>
struct std::hash<nexenne::utility::non_null<T>> {
  /**
   * @brief Hashes \p value by hashing its wrapped pointer.
   *
   * @param value Wrapper to hash.
   *
   * @return The hash of the wrapped pointer.
   *
   * @pre \p value has not been moved from.
   * @post None.
   */
  [[nodiscard]] auto operator()(nexenne::utility::non_null<T> const& value) const noexcept(
    noexcept(std::hash<T>{}(value.get()))
  ) -> std::size_t {
    return std::hash<T>{}(value.get());
  }
};
