#pragma once

/**
 * @file
 * @brief Fixed-capacity, heap-free type-erased callable.
 *
 * \c in_place_function<R(Args...), Capacity> stores any callable (lambda,
 * function pointer, functor) that fits within \p Capacity bytes of inline
 * storage; a callable that is too large is rejected by the converting
 * constructor's \c requires clause at compile time, with no silent heap
 * fallback. Use it instead of \c std::function on embedded targets or in hot
 * paths where allocation is unacceptable.
 *
 * It is move-only (a moved-from instance is empty), calling an empty instance
 * asserts in debug, \c explicit \c operator \c bool tests for non-empty, and
 * the default \p Capacity is 64 bytes.
 *
 * \code
 * using callback = nexenne::utility::in_place_function<int(int), 32>;
 * auto cb{callback{[x = 42](int y) { return x + y; }}};
 * cb(10);  // 52
 * \endcode
 */

#include <cassert>
#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>

namespace nexenne::utility {

/**
 * @brief Primary template; only the \c R(Args...) specialisation is defined.
 *
 * @tparam Sig Function signature of the form \c R(Args...).
 * @tparam Capacity Inline storage in bytes.
 *
 * @pre \p Sig is a function type \c R(Args...).
 * @post None.
 */
template <typename Sig, std::size_t Capacity = 64>
class in_place_function;

/**
 * @brief Fixed-capacity, heap-free type-erased callable with signature \c R(Args...).
 *
 * Stores any callable that fits in \p Capacity bytes of inline,
 * \c max_align_t-aligned storage and dispatches through a small vtable.
 * Callables that are too large or over-aligned are rejected by the converting
 * constructor's \c requires clause. Move-only: a moved-from instance is empty.
 *
 * @tparam R Return type of the call.
 * @tparam Args Argument types of the call.
 * @tparam Capacity Inline storage in bytes.
 *
 * @pre None.
 * @post A default-constructed instance is empty; \c operator \c bool is \c false.
 */
template <typename R, typename... Args, std::size_t Capacity>
class in_place_function<R(Args...), Capacity> {
public:
  using result_type = R;
  static constexpr std::size_t capacity{Capacity};

private:
  using invoke_fn = R (*)(void*, Args...);
  using destroy_fn = void (*)(void*) noexcept;
  using move_fn = void (*)(void*, void*) noexcept;

  struct vtable {
    invoke_fn invoke;
    destroy_fn destroy;
    move_fn move;
  };

  alignas(std::max_align_t) std::byte m_storage[Capacity]{};
  vtable const* m_vt{nullptr};

  template <typename F>
  static constexpr vtable const s_vtable{
    .invoke = [](void* p, Args... args) -> R {
      return (*static_cast<F*>(p))(std::forward<Args>(args)...);
    },
    .destroy = [](void* p) noexcept { std::destroy_at(static_cast<F*>(p)); },
    .move =
      [](void* dst, void* src) noexcept {
        std::construct_at(static_cast<F*>(dst), std::move(*static_cast<F*>(src)));
        std::destroy_at(static_cast<F*>(src));
      },
  };

public:
  /**
   * @brief Constructs an empty function holding no callable.
   *
   * @pre None.
   * @post \c operator \c bool is \c false; calling it asserts in debug.
   */
  constexpr in_place_function() noexcept = default;

  /**
   * @brief Constructs an empty function from \c nullptr.
   *
   * @pre None.
   * @post \c operator \c bool is \c false.
   */
  // NOLINTNEXTLINE(hicpp-explicit-conversions): nullptr yields an empty function
  constexpr in_place_function(std::nullptr_t) noexcept {}

  /**
   * @brief Constructs from any callable that fits in the inline storage.
   *
   * Constructs a decayed copy of \p f in place and wires up the vtable. The
   * \c requires clause rejects callables that exceed \p Capacity bytes or are
   * more strictly aligned than \c std::max_align_t.
   *
   * @tparam F Callable type invocable as \c R(Args...).
   * @param f Callable to store; decayed and moved into inline storage.
   *
   * @pre \c sizeof(std::decay_t<F>) is at most \p Capacity and its alignment is
   *      at most \c alignof(std::max_align_t).
   * @post \c operator \c bool is \c true and calling the function invokes the
   *       stored callable.
   *
   * @throws Anything the callable's selected constructor throws.
   */
  template <typename F>
    requires(!std::same_as<std::decay_t<F>, in_place_function>)
            && std::invocable<std::decay_t<F>&, Args...> && (sizeof(std::decay_t<F>) <= Capacity)
            && (alignof(std::decay_t<F>) <= alignof(std::max_align_t))
  // NOLINTNEXTLINE(hicpp-explicit-conversions): a callable wrapper binds implicitly
  in_place_function(F&& f) noexcept(std::is_nothrow_constructible_v<std::decay_t<F>, F&&>) {
    using fn = std::decay_t<F>;
    std::construct_at(reinterpret_cast<fn*>(m_storage), std::forward<F>(f));
    m_vt = &s_vtable<fn>;
  }

  /**
   * @brief Destroys the stored callable if present.
   *
   * @pre None.
   * @post Any stored callable has been destroyed.
   */
  ~in_place_function() noexcept {
    if (m_vt != nullptr) {
      m_vt->destroy(m_storage);
    }
  }

  /**
   * @brief Move-constructs from \p other, leaving \p other empty.
   *
   * @param other Source function, emptied by the move.
   *
   * @pre None.
   * @post This holds \p other's previous callable (if any); \p other is empty.
   */
  in_place_function(in_place_function&& other) noexcept {
    if (other.m_vt != nullptr) {
      other.m_vt->move(m_storage, other.m_storage);
      m_vt = other.m_vt;
      other.m_vt = nullptr;
    }
  }

  /**
   * @brief Move-assigns from \p other, leaving \p other empty.
   *
   * Destroys any current callable, then takes over \p other's. A self-move is a
   * no-op.
   *
   * @param other Source function, emptied by the move unless it is \c *this.
   *
   * @return Reference to \c *this.
   *
   * @pre None.
   * @post This holds \p other's previous callable (if any); \p other is empty
   *       unless it is \c *this.
   */
  auto operator=(in_place_function&& other) noexcept -> in_place_function& {
    if (this != &other) {
      if (m_vt != nullptr) {
        m_vt->destroy(m_storage);
      }
      m_vt = nullptr;
      if (other.m_vt != nullptr) {
        other.m_vt->move(m_storage, other.m_storage);
        m_vt = other.m_vt;
        other.m_vt = nullptr;
      }
    }
    return *this;
  }

  in_place_function(in_place_function const&) = delete;
  auto operator=(in_place_function const&) -> in_place_function& = delete;

  /**
   * @brief Invokes the stored callable.
   *
   * @param args Arguments forwarded to the stored callable.
   *
   * @return Whatever the stored callable returns.
   *
   * @pre \c operator \c bool is \c true; calling an empty function asserts in
   *      debug and is undefined behaviour in release.
   * @post None.
   *
   * @throws Anything the stored callable throws.
   */
  auto operator()(Args... args) -> R {
    assert(m_vt != nullptr && "in_place_function: calling an empty function");
    return m_vt->invoke(m_storage, std::forward<Args>(args)...);
  }

  /**
   * @brief Invokes the stored callable through a \c const wrapper.
   *
   * Mirrors \c std::function, whose \c operator() is \c const even though the
   * stored callable may mutate its own captures; the storage is the invocation
   * target, not part of the wrapper's observable \c const state.
   *
   * @param args Arguments forwarded to the stored callable.
   *
   * @return Whatever the stored callable returns.
   *
   * @pre \c operator \c bool is \c true; calling an empty function asserts in
   *      debug and is undefined behaviour in release.
   * @post None.
   *
   * @throws Anything the stored callable throws.
   */
  auto operator()(Args... args) const -> R {
    assert(m_vt != nullptr && "in_place_function: calling an empty function");
    return m_vt->invoke(const_cast<std::byte*>(m_storage), std::forward<Args>(args)...);
  }

  /**
   * @brief Reports whether a callable is stored.
   *
   * @return \c true when a callable is stored, \c false when empty.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] explicit operator bool() const noexcept {
    return m_vt != nullptr;
  }

  /**
   * @brief Destroys any stored callable and leaves the function empty.
   *
   * @pre None.
   * @post \c operator \c bool is \c false.
   */
  auto reset() noexcept -> void {
    if (m_vt != nullptr) {
      m_vt->destroy(m_storage);
      m_vt = nullptr;
    }
  }
};

}  // namespace nexenne::utility
