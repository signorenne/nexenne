#pragma once

/**
 * @file
 * @brief Generic RAII owner for an arbitrary resource handle and its deleter.
 *
 * The resource-owning sibling of \c scope_guard / \c defer.
 * \c unique_resource<Resource, Deleter> ties an opaque resource value (a file
 * descriptor, a socket, a hardware register handle, a pointer) to a callable
 * that releases it, and runs that deleter exactly once at the end of the
 * owner's lifetime. It is the heap-free embedded analogue of P0052 /
 * \c std::experimental::unique_resource and behaves like \c std::unique_ptr
 * generalised to non-pointer handles.
 *
 * The type is move-only: ownership transfers on move and the source is left
 * non-owning, so the deleter never fires twice (no double free or close). A
 * moved-from or released instance holds no resource and runs no deleter.
 *
 * \code
 * extern auto posix_open(char const*) noexcept -> int;  // returns -1 on error
 * extern auto posix_close(int) noexcept -> void;
 * auto const closer{[](int const fd) noexcept { posix_close(fd); }};
 *
 * auto file{nexenne::utility::make_unique_resource_checked(
 *   posix_open("/dev/sensor"), -1, closer)};
 * if (file.owns()) {
 *   int const fd{file.get()};       // use the descriptor
 *   int const raw{file.release()};  // hand it off; closer will NOT run
 *   posix_close(raw);
 * }
 * \endcode
 */

#include <memory>
#include <type_traits>
#include <utility>

namespace nexenne::utility {

/**
 * @brief Move-only RAII owner pairing a resource handle with a deleter.
 *
 * Stores a \p Resource value, a \p Deleter callable, and an ownership flag.
 * While owning, the destructor and \c reset invoke \c deleter(resource) exactly
 * once. Ownership transfers by move and is surrendered by \c release; in both
 * cases the deleter is suppressed for the surrendered value so it can never run
 * twice.
 *
 * @tparam Resource The owned handle type (pointer, integer, or any movable
 *                  value identifying the resource).
 * @tparam Deleter Callable invocable as \c deleter(resource).
 *
 * @pre \p Deleter is invocable with an lvalue \p Resource.
 * @post A default-constructed instance owns nothing; \c owns() is \c false.
 */
template <typename Resource, typename Deleter>
class unique_resource {
public:
  using resource_type = Resource;
  using deleter_type = Deleter;

private:
  resource_type m_resource{};
  [[no_unique_address]] deleter_type m_deleter{};
  bool m_owns{false};

public:
  /**
   * @brief Constructs a non-owning instance holding no resource.
   *
   * @pre None.
   * @post \c owns() is \c false; the destructor runs no deleter.
   */
  constexpr unique_resource() noexcept = default;

  /**
   * @brief Constructs an owning instance from a resource and its deleter.
   *
   * Both arguments are moved in. After construction the instance owns the
   * resource and invokes \p deleter on it at destruction or \c reset, unless
   * ownership is first transferred or released.
   *
   * @param resource Handle to take ownership of, moved into the owner.
   * @param deleter Callable that releases \p resource, moved into the owner.
   *
   * @pre \p deleter is a valid releaser for \p resource.
   * @post \c owns() is \c true; \c get() returns the stored resource.
   */
  unique_resource(
    resource_type resource, deleter_type deleter
  ) noexcept(std::is_nothrow_move_constructible_v<resource_type> && std::is_nothrow_move_constructible_v<deleter_type>)
      : m_resource{std::move(resource)}, m_deleter{std::move(deleter)}, m_owns{true} {}

  /**
   * @brief Move-constructs from \p other, transferring ownership.
   *
   * @param other Source owner, left non-owning by the move.
   *
   * @pre None.
   * @post This holds \p other's previous resource and ownership state;
   *       \p other's \c owns() is \c false.
   */
  unique_resource(unique_resource&& other
  ) noexcept(std::is_nothrow_move_constructible_v<resource_type> && std::is_nothrow_move_constructible_v<deleter_type>)
      : m_resource{std::move(other.m_resource)}
      , m_deleter{std::move(other.m_deleter)}
      , m_owns{other.m_owns} {
    other.m_owns = false;
  }

  /**
   * @brief Move-assigns from \p other, transferring ownership.
   *
   * Releases any resource currently owned by \c *this (running its deleter),
   * then takes over \p other's resource, deleter, and ownership flag and clears
   * \p other's ownership. A self-move is a no-op.
   *
   * The deleter is re-established by move-construction (\c destroy_at +
   * \c construct_at) rather than move-assignment, so a capturing-lambda deleter
   * (which has no assignment operator) is supported.
   *
   * @param other Source owner, left non-owning unless it is \c *this.
   *
   * @return Reference to \c *this.
   *
   * @pre None.
   * @post This holds \p other's previous resource and ownership state; the
   *       resource previously owned by \c *this has been released; \p other's
   *       \c owns() is \c false unless \p other is \c *this.
   */
  auto operator=(unique_resource&& other
  ) noexcept(std::is_nothrow_move_assignable_v<resource_type> && std::is_nothrow_move_constructible_v<deleter_type>)
    -> unique_resource& {
    if (this != &other) {
      reset();
      m_resource = std::move(other.m_resource);
      std::destroy_at(std::addressof(m_deleter));
      std::construct_at(std::addressof(m_deleter), std::move(other.m_deleter));
      m_owns = other.m_owns;
      other.m_owns = false;
    }
    return *this;
  }

  unique_resource(unique_resource const&) = delete;
  auto operator=(unique_resource const&) -> unique_resource& = delete;

  /**
   * @brief Releases the owned resource, running the deleter if owning.
   *
   * @pre None.
   * @post \c owns() is \c false; any previously owned resource has had its
   *       deleter run exactly once.
   */
  ~unique_resource() noexcept {
    reset();
  }

  /**
   * @brief Releases the owned resource by invoking the deleter.
   *
   * If the instance owns a resource, invokes \c deleter(resource) and marks it
   * non-owning. Does nothing when already non-owning, so repeated calls never
   * double-release.
   *
   * @pre None.
   * @post \c owns() is \c false; the deleter ran exactly once for any resource
   *       owned on entry.
   */
  auto reset() noexcept -> void {
    if (m_owns) {
      m_deleter(m_resource);
      m_owns = false;
    }
  }

  /**
   * @brief Releases the current resource and takes ownership of a new one.
   *
   * Runs the deleter on any currently owned resource, then stores \p resource
   * (moved in) and resumes owning with the existing deleter.
   *
   * @param resource New handle to take ownership of, moved into the owner.
   *
   * @pre The stored deleter is a valid releaser for \p resource.
   * @post \c owns() is \c true; \c get() returns \p resource; the previously
   *       owned resource has had its deleter run.
   */
  auto reset(resource_type resource) -> void {
    reset();
    m_resource = std::move(resource);
    m_owns = true;
  }

  /**
   * @brief Relinquishes ownership and returns the resource WITHOUT releasing it.
   *
   * Transfers the resource to the caller and marks the instance non-owning: the
   * deleter is NOT invoked, so the caller becomes responsible for releasing the
   * returned handle. Mirrors \c std::unique_ptr::release.
   *
   * @return The owned resource, moved out of the owner.
   *
   * @pre None.
   * @post \c owns() is \c false; the deleter will not run for the returned
   *       resource.
   */
  [[nodiscard]] auto release() noexcept -> resource_type {
    m_owns = false;
    return std::move(m_resource);
  }

  /**
   * @brief A reference to the owned resource.
   *
   * @return Const reference to the stored resource.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto get() const noexcept -> resource_type const& {
    return m_resource;
  }

  /**
   * @brief A reference to the stored deleter.
   *
   * @return Const reference to the stored deleter.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto get_deleter() const noexcept -> deleter_type const& {
    return m_deleter;
  }

  /**
   * @brief Reports whether the instance currently owns a resource.
   *
   * @return \c true when a resource is owned and its deleter is pending.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto owns() const noexcept -> bool {
    return m_owns;
  }

  /**
   * @brief Member access on the owned resource, for pointer resources.
   *
   * @return The owned pointer.
   *
   * @pre \p Resource is a pointer type; the owned pointer is non-null and
   *      refers to a live object.
   * @post None.
   */
  [[nodiscard]] auto operator->() const noexcept -> resource_type
    requires std::is_pointer_v<resource_type>
  {
    return m_resource;
  }

  /**
   * @brief Dereferences the owned resource, for pointer resources.
   *
   * @return Reference to the object the owned pointer refers to.
   *
   * @pre \p Resource is a pointer type; the owned pointer is non-null and
   *      refers to a live object.
   * @post None.
   */
  [[nodiscard]] auto operator*() const noexcept -> std::remove_pointer_t<resource_type>&
    requires std::is_pointer_v<resource_type>
  {
    return *m_resource;
  }
};

/**
 * @brief Deduces \c unique_resource from a resource and a deleter.
 *
 * @tparam R Deduced resource type.
 * @tparam D Deduced deleter type.
 *
 * @pre None.
 * @post None.
 */
template <typename R, typename D>
unique_resource(R, D) -> unique_resource<R, D>;

/**
 * @brief Builds a \c unique_resource that does not release an invalid handle.
 *
 * Constructs a \c unique_resource owning \p resource with \p deleter. If
 * \p resource compares equal to \p invalid (the sentinel of a failed
 * acquisition, such as \c -1 from a failed \c open), ownership is immediately
 * released so the deleter is NOT invoked for the invalid handle.
 *
 * @tparam Resource The owned handle type.
 * @tparam Invalid Sentinel type comparable to \p Resource via \c ==.
 * @tparam Deleter Callable invocable as \c deleter(resource).
 * @param resource Handle to take ownership of, moved into the owner.
 * @param invalid Sentinel value denoting a failed acquisition.
 * @param deleter Callable that releases \p resource, moved into the owner.
 *
 * @return A \c unique_resource that owns \p resource when it differs from
 *         \p invalid, and owns nothing otherwise.
 *
 * @pre \p resource and \p invalid are comparable with \c operator==.
 * @post The result's \c owns() is \c false when \p resource equals \p invalid,
 *       and \c true otherwise.
 */
template <typename Resource, typename Invalid, typename Deleter>
[[nodiscard]] auto make_unique_resource_checked(
  Resource resource, Invalid const& invalid, Deleter deleter
) noexcept(std::is_nothrow_move_constructible_v<Resource> && std::is_nothrow_move_constructible_v<Deleter>)
  -> unique_resource<Resource, Deleter> {
  auto guard{unique_resource<Resource, Deleter>{std::move(resource), std::move(deleter)}};
  if (guard.get() == invalid) {
    static_cast<void>(guard.release());
  }
  return guard;
}

}  // namespace nexenne::utility
