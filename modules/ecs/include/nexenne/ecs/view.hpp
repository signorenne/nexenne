#pragma once

/**
 * @file
 * @brief Multi-component view over a registry, with iterator and
 *        range support and an optional exclude list.
 *
 * \c view<C1, C2, ...> visits every entity carrying all of
 * \c C1, \c C2, ... and skips the rest. The view supports three
 * idioms:
 *
 *   1. The functional form, \c view.each(callback):
 *
 *      \code
 *      reg.view<position, velocity>().each(
 *          [](entity_id e, position& p, velocity const& v) noexcept {
 *              p.x += v.x;
 *              p.y += v.y;
 *          }
 *      );
 *      \endcode
 *
 *      The callback can take either \c (entity_id, Cs&...) or just
 *      \c (Cs&...). Selected at compile time via \c std::is_invocable.
 *
 *   2. The range-for form, where \c view satisfies \c std::ranges::range:
 *
 *      \code
 *      for (auto [e, p, v] : reg.view<position, velocity>()) {
 *          // ...
 *      }
 *      \endcode
 *
 *      \c operator* returns a \c std::tuple<entity_id, Cs&...>.
 *
 *   3. The exclude form, chaining \c .exclude<T...>() to filter out
 *      entities carrying any of the listed types:
 *
 *      \code
 *      reg.view<position, velocity>().exclude<dead, frozen>().each(...)
 *      \endcode
 *
 * Iteration strategy:
 *
 *   - The view picks the smallest include storage as the driver at
 *     construction time and walks its slots: indices \c [0, slot_count()),
 *     skipping tombstoned slots. Because the storage pool is pointer-stable,
 *     this is safe even when the loop body adds or removes components or
 *     destroys entities: live components keep their addresses, and a slot
 *     appended mid-iteration falls beyond the slot count captured at the
 *     start, so it is not visited.
 *
 *   - The driver is one of several heterogeneous include storages, so its
 *     slot walk (slot count, liveness, key) goes through a small captured
 *     function-pointer cursor set once at construction (not a vtable). The
 *     per-entity membership tests on the other includes and excludes, and
 *     the component lookups, are typed and inline.
 *
 *   - For each live driver slot, the view checks that all other includes
 *     are present and that no exclude is present, both via O(1)
 *     sparse-set membership tests.
 *
 * For an empty component type \p T (\c std::is_empty_v<T> == true),
 * the callback still receives a (dummy) reference per the standard
 * iteration; use \c auto& to silently ignore it.
 */

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <ranges>
#include <tuple>
#include <type_traits>
#include <utility>

#include <nexenne/ecs/registry.hpp>

namespace nexenne::ecs {

namespace detail {

// \c type_list is declared in registry.hpp (forward-included), shared
// with the query builder. We add the membership helper here.

template <typename T, typename Tuple>
inline constexpr auto tuple_contains_v = false;

template <typename T, typename... Us>
inline constexpr auto tuple_contains_v<T, type_list<Us...>> = (std::same_as<T, Us> || ...);

}  // namespace detail

/**
 * @brief Multi-component view of a registry. Users name it via the
 *        \c view<...> alias.
 *
 * Parameterised on two type lists: includes (entities must carry all
 * of these) and excludes (entities must carry none of these). Built
 * via the registry's \c view<...>() / \c query() helpers and chained
 * through \c .exclude<...>().
 */
template <typename... Includes, typename... Excludes>
  requires(sizeof...(Includes) > 0)
class basic_view<detail::type_list<Includes...>, detail::type_list<Excludes...>> {
public:
  /**
   * @brief Tuple-of-references yielded by the iterator: the entity
   *        plus one reference per include type.
   */
  using value_type = std::tuple<entity_id, Includes&...>;

private:
  using include_list = detail::type_list<Includes...>;
  using exclude_list = detail::type_list<Excludes...>;
  using includes_storage = std::tuple<component_storage<Includes>*...>;
  using excludes_storage = std::tuple<component_storage<Excludes>*...>;
  // The driver is one of the heterogeneous include storages chosen at
  // runtime (the smallest), so its slot walk is reached through this captured
  // function-pointer cursor rather than a typed call. Set once at
  // construction by \c bind_driver; never null afterwards.
  using slot_count_fn = auto (*)(void const*) noexcept -> std::size_t;
  using is_live_fn = auto (*)(void const*, std::size_t) noexcept -> bool;
  using key_at_fn = auto (*)(void const*, std::size_t) noexcept -> std::uint32_t;

  registry* m_registry{nullptr};
  includes_storage m_includes{};
  excludes_storage m_excludes{};
  void const* m_driver{nullptr};
  slot_count_fn m_driver_slot_count{nullptr};
  is_live_fn m_driver_is_live{nullptr};
  key_at_fn m_driver_key_at{nullptr};

  template <typename Func>
  static constexpr auto wants_entity_id_v = std::is_invocable_v<Func&, entity_id, Includes&...>;

public:
  /**
   * @brief Constructs a view over \p reg and selects its driver.
   *
   * Caches a pointer to each include and exclude storage (creating
   * them lazily via \c registry::storage) and picks the smallest
   * include storage as the iteration driver.
   *
   * @param reg  Registry to view. Must outlive the view.
   *
   * @pre  \p reg outlives this view.
   * @post Storage exists for every include and exclude type. The driver
   *       cursor is bound to the smallest include storage.
   *
   * @complexity \c O(sizeof...(Includes)).
   */
  explicit basic_view(registry& reg) noexcept
      : m_registry{&reg}
      , m_includes{&reg.template storage<Includes>()...}
      , m_excludes{&reg.template storage<Excludes>()...} {
    bind_driver(select_driver_index(), std::index_sequence_for<Includes...>{});
  }

  /**
   * @brief Returns a new view with additional exclude filters.
   *
   * @tparam NewExcludes  Extra types to exclude. Must be DISJOINT from
   *                      the include list (enforced by a \c requires clause).
   *
   * @return A view over the same registry with the combined exclude
   *         list.
   *
   * @pre  None beyond the disjointness checked at compile time.
   * @post The original view is unchanged; the returned view re-reads
   *       the driver against the new exclude set.
   *
   * @complexity \c O(sizeof...(Includes)) to construct.
   */
  template <typename... NewExcludes>
  [[nodiscard]] auto exclude() const noexcept
    -> basic_view<include_list, detail::type_list<Excludes..., NewExcludes...>>
    requires((!detail::tuple_contains_v<NewExcludes, include_list> && ...))
  {
    return basic_view<include_list, detail::type_list<Excludes..., NewExcludes...>>{*m_registry};
  }

  /**
   * @brief Invokes \p f for every entity that passes the include and
   *        exclude filters.
   *
   * Equivalent to walking the iterators, but more efficient because
   * the hot loop is wholly within one function. The callback may take
   * either \c (entity_id, Includes&...) or just \c (Includes&...);
   * the form is selected at compile time.
   *
   * @tparam Func  Callable invocable with either accepted form.
   * @param  f     Callback invoked once per matching entity.
   *
   * @pre  None. Thanks to the pointer-stable storage, \p f may add or
   *       remove components and create or destroy entities during the loop
   *       without dangling the references it holds.
   * @post Every entity matching the filters at the start was passed to \p f
   *       exactly once, unless \p f removed it (or a required component)
   *       before it was reached. A component added by \p f during the loop
   *       is not guaranteed to be visited.
   *
   * @complexity \c O(D) driver slots times \c O(sizeof...(Includes)
   *             + sizeof...(Excludes)) membership tests each.
   */
  template <typename Func>
  auto each(Func&& f) const noexcept -> void {
    // Capture the slot count once: a slot appended by \p f during the loop
    // lies beyond it and is not visited.
    auto const count{m_driver_slot_count(m_driver)};
    for (auto slot{std::size_t{0}}; slot < count; ++slot) {
      if (!m_driver_is_live(m_driver, slot)) {
        continue;
      }
      auto const idx{m_driver_key_at(m_driver, slot)};
      if (!passes_filter(idx)) {
        continue;
      }
      if constexpr (wants_entity_id_v<Func>) {
        auto const gen{m_registry->generation_at(idx)};
        std::apply(
          [&f, idx, gen](auto*... s) noexcept {
            // passes_filter ensures every include holds idx, so try_get
            // never returns nullptr here.
            f(entity_id{idx, gen}, (*s->try_get(idx))...);
          },
          m_includes
        );
      } else {
        std::apply([&f, idx](auto*... s) noexcept { f((*s->try_get(idx))...); }, m_includes);
      }
    }
  }

  /**
   * @brief Input iterator over the entities matching this view.
   *
   * Dereferences to a \c value_type tuple of (entity, component
   * references). Filter-aware: construction and each increment skip
   * forward to the next matching driver entry.
   */
  class iterator {
  public:
    using value_type = basic_view::value_type;
    using reference = value_type;
    using difference_type = std::ptrdiff_t;
    // Input-iterator: \c operator* returns a tuple by value (the
    // tuple holds component references, but the tuple itself is
    // a prvalue). True forward-iterator semantics would require
    // \c reference to be an lvalue type, which the proxy pattern
    // does not provide.
    using iterator_category = std::input_iterator_tag;
    using iterator_concept = std::input_iterator_tag;

  private:
    basic_view const* m_view{nullptr};
    std::size_t m_pos{0};
    // Slot count captured at construction. Bounding advance by this fixed
    // value (rather than a live re-read) keeps a slot appended during
    // iteration out of range, so the iterator still terminates at \c end().
    std::size_t m_count{0};

    auto advance_to_valid() noexcept -> void {
      while (
        m_pos < m_count
        && (!m_view->driver_is_live(m_pos) || !m_view->passes_filter(m_view->driver_key_at(m_pos)))
      ) {
        ++m_pos;
      }
    }

  public:
    /**
     * @brief Constructs a singular iterator bound to no view.
     *
     * @pre  None.
     * @post Must not be dereferenced or incremented.
     */
    constexpr iterator() noexcept = default;

    /**
     * @brief Constructs an iterator into \p v at driver slot \p pos, then
     *        advances to the first matching live slot.
     *
     * @param v    View being iterated.
     * @param pos  Starting driver slot, in \c [0, slot count].
     *
     * @pre  \p pos is no greater than the driver slot count.
     * @post The iterator points at the first matching entry at or
     *       after \p pos, or equals \c end() if none.
     */
    explicit iterator(basic_view const& v, std::size_t const pos) noexcept
        : m_view{&v}, m_pos{pos}, m_count{v.driver_slot_count()} {
      advance_to_valid();
    }

    /**
     * @brief Returns the (entity, components) tuple at this position.
     *
     * @return A \c value_type holding the entity handle and one
     *         reference per include component.
     *
     * @pre  \c *this is dereferenceable (does not equal \c end()).
     * @post The iterator is unchanged.
     */
    [[nodiscard]] auto operator*() const noexcept -> value_type {
      auto const idx{m_view->driver_key_at(m_pos)};
      auto const gen{m_view->m_registry->generation_at(idx)};
      return std::apply(
        [idx, gen](auto*... s) noexcept -> value_type {
          // advance_to_valid guarantees every include holds idx, so
          // try_get never returns nullptr here.
          return value_type{entity_id{idx, gen}, (*s->try_get(idx))...};
        },
        m_view->m_includes
      );
    }

    /**
     * @brief Pre-increment: advances to the next matching entity.
     *
     * @return Reference to \c *this after advancing.
     *
     * @pre  \c *this does not equal \c end().
     * @post The iterator points at the next matching entry, or
     *       equals \c end().
     */
    auto operator++() noexcept -> iterator& {
      ++m_pos;
      advance_to_valid();
      return *this;
    }

    /**
     * @brief Post-increment: advances, returning the prior value.
     *
     * @return A copy of the iterator as it was before advancing.
     *
     * @pre  \c *this does not equal \c end().
     * @post The iterator points at the next matching entry, or
     *       equals \c end().
     */
    auto operator++(int) noexcept -> iterator {
      auto const tmp{*this};
      ++*this;
      return tmp;
    }

    /**
     * @brief Equality: same view and same driver position.
     *
     * @param a  Left operand.
     * @param b  Right operand.
     *
     * @return \c true iff both iterators name the same position in
     *         the same view.
     *
     * @pre  None.
     * @post None.
     */
    [[nodiscard]] friend constexpr auto
    operator==(iterator const& a, iterator const& b) noexcept -> bool {
      return a.m_pos == b.m_pos && a.m_view == b.m_view;
    }
  };

  /**
   * @brief Iterator to the first matching entity.
   *
   * @return An iterator advanced to the first entity passing the
   *         filters, equal to \c end() when none match.
   *
   * @pre  None.
   * @post The view is unchanged.
   *
   * @complexity \c O(D) in the worst case to skip to the first match.
   */
  [[nodiscard]] auto begin() const noexcept -> iterator {
    return iterator{*this, 0};
  }

  /**
   * @brief Past-the-end iterator for the view range.
   *
   * @return An iterator one past the last driver entry.
   *
   * @pre  None.
   * @post The view is unchanged.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] auto end() const noexcept -> iterator {
    return iterator{*this, driver_slot_count()};
  }

private:
  [[nodiscard]] auto passes_filter(std::uint32_t const idx) const noexcept -> bool {
    auto const include_ok{
      std::apply([idx](auto*... s) noexcept { return (... && s->contains(idx)); }, m_includes)
    };
    if (!include_ok) {
      return false;
    }
    if constexpr (sizeof...(Excludes) > 0) {
      auto const exclude_ok{
        std::apply([idx](auto*... s) noexcept { return (... && !s->contains(idx)); }, m_excludes)
      };
      return exclude_ok;
    }
    return true;
  }

  // Driver slot walk, reached through the captured cursor.
  [[nodiscard]] auto driver_slot_count() const noexcept -> std::size_t {
    return m_driver_slot_count(m_driver);
  }

  [[nodiscard]] auto driver_is_live(std::size_t const slot) const noexcept -> bool {
    return m_driver_is_live(m_driver, slot);
  }

  [[nodiscard]] auto driver_key_at(std::size_t const slot) const noexcept -> std::uint32_t {
    return m_driver_key_at(m_driver, slot);
  }

  // Index of the smallest include storage, used as the iteration driver.
  [[nodiscard]] auto select_driver_index() const noexcept -> std::size_t {
    if constexpr (sizeof...(Includes) == 1) {
      return 0;
    } else {
      using size_array = std::array<std::size_t, sizeof...(Includes)>;
      auto const sizes{
        std::apply([](auto*... s) noexcept { return size_array{s->size()...}; }, m_includes)
      };
      auto const min_it{std::min_element(sizes.begin(), sizes.end())};
      return static_cast<std::size_t>(min_it - sizes.begin());
    }
  }

  // Binds the cursor to include storage \c I (the chosen driver).
  template <std::size_t I>
  auto bind_driver_to() noexcept -> void {
    using storage_ptr = std::tuple_element_t<I, includes_storage>;
    using storage_type = std::remove_pointer_t<storage_ptr>;
    m_driver = std::get<I>(m_includes);
    m_driver_slot_count = +[](void const* d) noexcept -> std::size_t {
      return static_cast<storage_type const*>(d)->slot_count();
    };
    m_driver_is_live = +[](void const* d, std::size_t s) noexcept -> bool {
      return static_cast<storage_type const*>(d)->is_live(s);
    };
    m_driver_key_at = +[](void const* d, std::size_t s) noexcept -> std::uint32_t {
      return static_cast<storage_type const*>(d)->key_at(s);
    };
  }

  template <std::size_t... Is>
  auto bind_driver(std::size_t const driver_idx, std::index_sequence<Is...>) noexcept -> void {
    ((driver_idx == Is ? bind_driver_to<Is>() : static_cast<void>(0)), ...);
  }
};

/**
 * @brief Convenience alias for the common "includes only" case.
 *
 * @tparam Includes  Component types every visited entity must carry.
 */
template <typename... Includes>
using view = basic_view<detail::type_list<Includes...>, detail::type_list<>>;

/**
 * @brief Builder accumulating include / exclude lists at compile time.
 *
 * Returned by \c registry::query(). Chain \c .with<C>() and
 * \c .without<C>() to grow the lists, then \c .build() to obtain a
 * \c basic_view, or call \c .each(callback) directly.
 *
 * Each chain step constructs a new builder of a different type
 * (with C appended to the relevant pack), so the type system tracks
 * the include/exclude lists with zero runtime overhead.
 */
template <typename... Includes, typename... Excludes>
class typed_query_builder<detail::type_list<Includes...>, detail::type_list<Excludes...>> {
private:
  using include_list = detail::type_list<Includes...>;
  using exclude_list = detail::type_list<Excludes...>;

  registry* m_registry{nullptr};

public:
  /**
   * @brief Constructs a builder bound to registry \p r.
   *
   * @param r  Registry the eventual view will read. Must outlive the
   *           builder and any view it produces.
   *
   * @pre  None.
   * @post The builder carries the current (empty or accumulated)
   *       include and exclude lists for \p r.
   */
  explicit constexpr typed_query_builder(registry& r) noexcept : m_registry{&r} {}

  /**
   * @brief Adds \c C to the include list.
   *
   * @tparam C  Component type to require.
   *
   * @return A new builder with \c C appended to the include pack.
   *
   * @pre  None.
   * @post This builder is unchanged; the include list grows in the
   *       returned builder's type.
   *
   * @complexity \c O(1).
   */
  template <typename C>
  [[nodiscard]] auto
  with() const noexcept -> typed_query_builder<detail::type_list<Includes..., C>, exclude_list> {
    return typed_query_builder<detail::type_list<Includes..., C>, exclude_list>{*m_registry};
  }

  /**
   * @brief Adds \c C to the exclude list.
   *
   * @tparam C  Component type to forbid.
   *
   * @return A new builder with \c C appended to the exclude pack.
   *
   * @pre  None.
   * @post This builder is unchanged; the exclude list grows in the
   *       returned builder's type.
   *
   * @complexity \c O(1).
   */
  template <typename C>
  [[nodiscard]] auto
  without() const noexcept -> typed_query_builder<include_list, detail::type_list<Excludes..., C>> {
    return typed_query_builder<include_list, detail::type_list<Excludes..., C>>{*m_registry};
  }

  /**
   * @brief Builds the view from the accumulated include and exclude
   *        lists.
   *
   * @return A \c basic_view over the builder's registry.
   *
   * @pre  At least one include type has been added (a \c requires
   *       clause on \c basic_view enforces this).
   * @post Storage exists for every include and exclude type. The
   *       builder is unchanged.
   *
   * @complexity \c O(sizeof...(Includes)) to construct the view.
   */
  [[nodiscard]] auto build() const noexcept -> basic_view<include_list, exclude_list> {
    return basic_view<include_list, exclude_list>{*m_registry};
  }

  /**
   * @brief Convenience: build the view and run \c each in one call.
   *
   * @tparam Func  Callback accepted by \c basic_view::each.
   * @param  f     Callback invoked once per matching entity.
   *
   * @pre  Same as \c build (at least one include type) and \c each
   *       (\p f does not structurally modify the viewed storages).
   * @post Every matching entity at call time was passed to \p f once.
   *
   * @complexity Same as building the view plus iterating it.
   */
  template <typename Func>
  auto each(Func&& f) const noexcept -> void {
    build().each(std::forward<Func>(f));
  }
};

//
// Declared in registry.hpp; defined here so the body sees the full
// \c basic_view and \c typed_query_builder definitions.

template <typename... Includes>
[[nodiscard]] inline auto
registry::view() noexcept -> basic_view<detail::type_list<Includes...>, detail::type_list<>> {
  return basic_view<detail::type_list<Includes...>, detail::type_list<>>{*this};
}

[[nodiscard]] inline auto
registry::query() noexcept -> typed_query_builder<detail::type_list<>, detail::type_list<>> {
  return typed_query_builder<detail::type_list<>, detail::type_list<>>{*this};
}

}  // namespace nexenne::ecs
