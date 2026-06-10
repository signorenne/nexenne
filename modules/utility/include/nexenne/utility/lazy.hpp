#pragma once

/**
 * @file
 * @brief Thread-safe deferred initialisation: compute once on first access.
 *
 * Defers expensive construction until the value is actually needed. The factory
 * runs at most once even under concurrent first access (synchronised with
 * \c std::call_once); subsequent accesses return the cached value. The factory
 * is templated, so a captureless or capturing lambda inlines without
 * \c std::function indirection.
 *
 * \code
 * auto loaded{nexenne::utility::lazy{[] {
 *   return load_huge_config_from_disk();  // only runs if accessed
 * }}};
 * if (need_config) {
 *   auto const& cfg{*loaded};  // factory fires here, exactly once
 *   use(cfg);
 * }
 * \endcode
 *
 * The synchronisation primitive (\c std::once_flag) is neither movable nor
 * resettable, so \c lazy is non-movable and offers no way to re-run the factory
 * once it has succeeded. If the factory throws, no value is cached and the next
 * access retries.
 */

#include <atomic>
#include <concepts>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>

namespace nexenne::utility {

/**
 * @brief Thread-safe deferred-initialisation wrapper that computes a value on
 *        first access and caches it.
 *
 * Stores a factory callable and an empty \c std::optional. The first access
 * runs the factory under \c std::call_once, caches its result, and returns a
 * reference; later accesses return the cached value without re-running the
 * factory. Concurrent first accesses are safe and run the factory exactly once.
 *
 * @tparam Factory Callable invocable with no arguments, returning the wrapped
 *                 value.
 *
 * @pre None.
 * @post A freshly constructed \c lazy has not yet run its factory.
 */
template <std::invocable Factory>
class lazy {
public:
  using value_type = std::invoke_result_t<Factory&>;
  using factory_type = Factory;

private:
  mutable std::once_flag m_once;
  mutable std::atomic<bool> m_ready{false};
  mutable std::optional<value_type> m_value{};
  mutable factory_type m_factory;

  auto materialise() const -> value_type& {
    std::call_once(m_once, [this] {
      m_value.emplace(m_factory());
      m_ready.store(true, std::memory_order_release);
    });
    return *m_value;
  }

public:
  /**
   * @brief Constructs the wrapper, taking ownership of the factory \p f.
   *
   * @param f Factory callable, moved into the wrapper; not invoked here.
   *
   * @pre None.
   * @post \c has_value() returns \c false until the first access.
   */
  explicit lazy(factory_type f) noexcept(std::is_nothrow_move_constructible_v<factory_type>)
      : m_factory{std::move(f)} {}

  /**
   * @brief The cached value, running the factory on first call.
   *
   * @return Mutable reference to the cached value.
   *
   * @pre None.
   * @post \c has_value() returns \c true.
   *
   * @throws Anything the factory throws on the first call; nothing is cached
   *         and the next access retries.
   */
  [[nodiscard]] auto get() -> value_type& {
    return materialise();
  }

  /**
   * @brief The cached value through a \c const wrapper, running the factory on
   *        first call.
   *
   * The cache and factory are \c mutable, so a \c const \c lazy can still
   * materialise on first access.
   *
   * @return Read-only reference to the cached value.
   *
   * @pre None.
   * @post \c has_value() returns \c true.
   *
   * @throws Anything the factory throws on the first call.
   */
  [[nodiscard]] auto get() const -> value_type const& {
    return materialise();
  }

  /**
   * @brief Dereferences to the cached value, running the factory on first call.
   *
   * @return Mutable reference to the cached value.
   *
   * @pre None.
   * @post \c has_value() returns \c true.
   *
   * @throws Anything the factory throws on the first call.
   */
  [[nodiscard]] auto operator*() -> value_type& {
    return materialise();
  }

  /**
   * @brief Dereferences a \c const wrapper to the cached value.
   *
   * @return Read-only reference to the cached value.
   *
   * @pre None.
   * @post \c has_value() returns \c true.
   *
   * @throws Anything the factory throws on the first call.
   */
  [[nodiscard]] auto operator*() const -> value_type const& {
    return materialise();
  }

  /**
   * @brief Member access on the cached value, running the factory on first call.
   *
   * @return Pointer to the cached value.
   *
   * @pre None.
   * @post \c has_value() returns \c true.
   *
   * @throws Anything the factory throws on the first call.
   */
  [[nodiscard]] auto operator->() -> value_type* {
    return &materialise();
  }

  /**
   * @brief Member access on the cached value through a \c const wrapper.
   *
   * @return Pointer to the read-only cached value.
   *
   * @pre None.
   * @post \c has_value() returns \c true.
   *
   * @throws Anything the factory throws on the first call.
   */
  [[nodiscard]] auto operator->() const -> value_type const* {
    return &materialise();
  }

  /**
   * @brief Reports whether the factory has run and a value is cached.
   *
   * Safe to call concurrently with an access on another thread; it returns
   * \c true only once the cached value is fully visible.
   *
   * @return \c true once the value has been materialised, \c false before.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto has_value() const noexcept -> bool {
    return m_ready.load(std::memory_order_acquire);
  }
};

/**
 * @brief Deduces \c lazy's \c Factory from the constructor argument.
 *
 * @tparam Factory Callable type of the constructor argument.
 *
 * @pre None.
 * @post \c lazy{f} deduces \c lazy<Factory>.
 */
template <typename Factory>
lazy(Factory) -> lazy<Factory>;

}  // namespace nexenne::utility
