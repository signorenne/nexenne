#pragma once

/**
 * @file
 * @brief Non-owning, type-erased callable view.
 *
 * Like \c std::function but holds a reference to the callable instead of owning
 * it: no heap allocation, no inline-storage size limit, and passing by value is
 * just two pointers. The callable must outlive the \c function_ref.
 *
 * Reach for it when accepting "any callable" as a parameter without templating
 * the whole signature (avoiding one instantiation per call site), when the
 * callable lives at the call site, and when it need not be stored long-term.
 * Use \c in_place_function instead to STORE a callable for later invocation.
 *
 * \code
 * auto count_if(std::span<int const> data,
 *               nexenne::utility::function_ref<bool(int)> pred) -> std::size_t {
 *   std::size_t n{0};
 *   for (auto const x : data) {
 *     if (pred(x)) {
 *       ++n;
 *     }
 *   }
 *   return n;
 * }
 * count_if(values, [](int x) { return x > 10; });
 * \endcode
 */

#include <memory>
#include <type_traits>
#include <utility>

namespace nexenne::utility {

/**
 * @brief Primary template; only the \c R(Args...) specialisation is defined.
 *
 * @tparam Sig Function signature of the form \c R(Args...).
 *
 * @pre \p Sig is a function type \c R(Args...).
 * @post None.
 */
template <typename Sig>
class function_ref;

/**
 * @brief Non-owning, type-erased view over a callable with signature \c R(Args...).
 *
 * Holds a pointer to the referenced callable plus a thunk that forwards the
 * call, so it does not allocate and copies as two pointers. The referenced
 * callable must outlive the view.
 *
 * @tparam R Return type of the call.
 * @tparam Args Argument types of the call.
 *
 * @pre None.
 * @post A default-constructed view is empty; \c operator \c bool is \c false.
 */
template <typename R, typename... Args>
class function_ref<R(Args...)> {
public:
  using signature_type = R(Args...);

private:
  using thunk_type = R (*)(void const*, Args...);
  using function_ptr = R (*)(Args...);

  void const* m_obj{nullptr};
  thunk_type m_thunk{nullptr};

  template <typename F>
  [[nodiscard]] static auto make_thunk() noexcept -> thunk_type {
    return [](void const* p, Args... args) -> R {
      // const_cast is sound: when constructed from a non-const F we stored the
      // const-stripped pointer in the void const*.
      using bare = std::remove_reference_t<F>;
      auto& f{*const_cast<bare*>(static_cast<bare const*>(p))};
      return f(std::forward<Args>(args)...);
    };
  }

public:
  /**
   * @brief Constructs an empty view that refers to no callable.
   *
   * @pre None.
   * @post \c operator \c bool is \c false; the view must not be called.
   */
  constexpr function_ref() noexcept = default;

  /**
   * @brief Binds the view to a callable that must outlive it.
   *
   * Stores the address of \p f and a thunk that forwards the call. Excluded for
   * \c function_ref arguments and for callables not invocable as \c R(Args...).
   *
   * @tparam F Callable type invocable as \c R(Args...).
   * @param f Callable to refer to; its address is captured and it must outlive
   *          this view.
   *
   * @pre \p f is invocable as \c R(Args...) and outlives this view.
   * @post \c operator \c bool is \c true; calling the view forwards to \p f.
   */
  template <typename F>
    requires(!std::is_same_v<std::remove_cvref_t<F>, function_ref>
             && std::is_invocable_r_v<R, F&, Args...>)
  // NOLINTNEXTLINE(hicpp-explicit-conversions): a callable view binds implicitly
  function_ref(F&& f) noexcept : m_obj{std::addressof(f)}, m_thunk{make_thunk<F>()} {}

  /**
   * @brief Binds the view to a free function pointer.
   *
   * @param fn Function pointer with signature \c R(Args...).
   *
   * @pre \p fn is non-null and remains valid for the lifetime of the view.
   * @post \c operator \c bool is \c true; calling the view invokes \p fn.
   */
  // NOLINTNEXTLINE(hicpp-explicit-conversions): a callable view binds implicitly
  function_ref(function_ptr fn) noexcept
      : m_obj{reinterpret_cast<void const*>(fn)}, m_thunk{[](void const* p, Args... args) -> R {
        auto const fp{reinterpret_cast<function_ptr>(const_cast<void*>(p))};
        return fp(std::forward<Args>(args)...);
      }} {}

  /**
   * @brief Reports whether the view refers to a callable.
   *
   * @return \c true when a callable is bound, \c false when empty.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return m_thunk != nullptr;
  }

  /**
   * @brief Invokes the referenced callable.
   *
   * @param args Arguments forwarded to the referenced callable.
   *
   * @return Whatever the referenced callable returns.
   *
   * @pre \c operator \c bool is \c true and the referenced callable is still
   *      alive; calling an empty view is undefined behaviour.
   * @post None.
   *
   * @throws Anything the referenced callable throws.
   */
  auto operator()(Args... args) const -> R {
    return m_thunk(m_obj, std::forward<Args>(args)...);
  }
};

}  // namespace nexenne::utility
