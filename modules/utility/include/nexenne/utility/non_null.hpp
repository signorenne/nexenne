#pragma once

/**
 * @file
 * @brief Pointer wrapper that asserts a non-null contract on construction.
 */

#include <cassert>
#include <concepts>
#include <cstddef>
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

}  // namespace detail

/**
 * @brief Pointer wrapper that asserts a non-null contract on construction.
 *
 * Documents and, in debug, enforces that a pointer is never null. Constructing
 * or assigning from \c nullptr is a compile error; a runtime null asserts in
 * debug at the construction site, so the bug surfaces at the API boundary
 * rather than deep in a call chain. In release it is a zero-overhead wrapper.
 *
 * @tparam T Raw or smart pointer (dereferenceable and comparable to \c nullptr).
 *
 * @pre None at the class level; see the constructor.
 * @post A constructed \c non_null holds a non-null pointer.
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
  using pointer_type = T;
  using element_type = typename std::pointer_traits<pointer_type>::element_type;

private:
  pointer_type m_ptr;

public:
  /**
   * @brief Constructs from a pointer, asserting it is non-null.
   *
   * @param ptr Pointer to wrap.
   *
   * @pre \p ptr is not null. A null pointer asserts in debug and is a broken
   *      contract (undefined to dereference) in release.
   * @post \c get() returns the stored, non-null pointer.
   */
  // NOLINTNEXTLINE(hicpp-explicit-conversions): non_null is meant to convert implicitly
  constexpr non_null(pointer_type ptr) noexcept : m_ptr{std::move(ptr)} {
    if !consteval {
      assert(m_ptr != nullptr && "non_null: constructed with nullptr");
    }
  }

  /// @brief Deleted: constructing from \c nullptr is a compile-time error.
  non_null(std::nullptr_t) = delete;

  /// @brief Deleted: assigning \c nullptr is a compile-time error.
  auto operator=(std::nullptr_t) -> non_null& = delete;

  /**
   * @brief The wrapped pointer.
   *
   * Returned by reference so a smart-pointer \p T (e.g. \c std::unique_ptr) is
   * observed without a copy and a \c std::shared_ptr is not refcount-churned.
   *
   * @return Const reference to the stored, non-null pointer.
   *
   * @pre None.
   * @post The returned pointer is non-null.
   */
  [[nodiscard]] constexpr auto get() const noexcept -> pointer_type const& {
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
   * @pre None.
   * @post None.
   */
  constexpr auto operator->() const noexcept -> pointer_type const& {
    return m_ptr;
  }

  /**
   * @brief Dereferences the wrapped pointer.
   *
   * @return Reference to the pointed-to object.
   *
   * @pre The wrapped pointer still points at a live object.
   * @post None.
   */
  constexpr auto operator*() const noexcept -> element_type& {
    return *m_ptr;
  }

  /**
   * @brief Implicit conversion to the wrapped pointer type.
   *
   * Lets \c non_null<T> plug into APIs that take \c T.
   *
   * @return The stored, non-null pointer.
   *
   * @pre None.
   * @post The returned pointer is non-null.
   */
  // NOLINTNEXTLINE(hicpp-explicit-conversions): non_null is meant to convert implicitly
  constexpr operator pointer_type() const noexcept {
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
  [[nodiscard]] friend constexpr auto
  operator==(non_null const&, non_null const&) noexcept -> bool = default;

  /**
   * @brief Comparison against \c nullptr, always \c false.
   *
   * A \c non_null is never null by contract.
   *
   * @return \c false.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] friend constexpr auto operator==(non_null const&, std::nullptr_t) noexcept -> bool {
    return false;
  }
};

}  // namespace nexenne::utility
