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
 * Exception safety follows P0052: if storing the resource or the deleter
 * throws during construction, the deleter is invoked on the handle before the
 * exception propagates, so an acquired resource never leaks.
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

#include <concepts>
#include <memory>
#include <type_traits>
#include <utility>

namespace nexenne::utility {

/// @cond INTERNAL
namespace detail {

/**
 * @brief Whether initialising a \p Member from a \p From argument, P0052 style, cannot throw.
 *
 * P0052 initialises a member from the forwarded argument when that cannot
 * throw and copies from the caller's argument otherwise, so the
 * initialisation is non-throwing when either route is.
 *
 * @tparam Member Stored member type.
 * @tparam From Forwarded argument type, possibly an lvalue reference.
 */
template <typename Member, typename From>
inline constexpr bool nothrow_member_init_v{
  std::is_nothrow_constructible_v<Member, From> || std::is_nothrow_constructible_v<Member, From&>
};

/**
 * @brief Whether a \p Member can be initialised from a \p From argument, P0052 style.
 *
 * Either the forwarded argument initialises it without throwing, or a copy
 * from the caller's argument is available as the fallback.
 *
 * @tparam Member Stored member type.
 * @tparam From Forwarded argument type, possibly an lvalue reference.
 */
template <typename Member, typename From>
concept p0052_initialisable_from =
  std::constructible_from<Member, From>
  && (std::is_nothrow_constructible_v<Member, From> || std::constructible_from<Member, From&>);

/**
 * @brief Placeholder for whichever deleter member is not in use.
 *
 * \c unique_resource keeps its deleter in one of two members, so exactly one
 * holds a \c Deleter and the other is this empty type.
 *
 * @tparam Tag Distinguishes the two placeholders.
 */
template <int Tag>
struct unused_deleter_slot {
  /**
   * @brief Constructs the placeholder.
   *
   * @pre None.
   * @post None.
   */
  constexpr unused_deleter_slot() noexcept = default;
};

/**
 * @brief Runs \p make when \p Used is true, otherwise yields the placeholder.
 *
 * Lets a constructor initialise both deleter members from one expression while
 * evaluating it once, for the member in use.
 *
 * @tparam Used Whether this member holds the deleter.
 * @tparam Tag The placeholder tag for this member.
 * @tparam Make Callable producing the deleter.
 * @param make Produces the deleter; called only when \p Used is true.
 *
 * @return The deleter, or the empty placeholder.
 *
 * @pre None.
 * @post \p make ran exactly once when \p Used is true, and never otherwise.
 *
 * @throws Whatever \p make throws.
 */
template <bool Used, int Tag, typename Make>
constexpr auto
deleter_or_placeholder(Make&& make) noexcept(!Used || std::is_nothrow_invocable_v<Make>)
  -> decltype(auto) {
  if constexpr (Used) {
    return std::forward<Make>(make)();
  } else {
    return unused_deleter_slot<Tag>{};
  }
}

}  // namespace detail

/// @endcond

/**
 * @brief Move-only RAII owner pairing a resource handle with a deleter.
 *
 * Stores a \p Resource value, a \p Deleter callable, and an ownership flag.
 * While owning, the destructor and \c reset invoke \c deleter(resource) exactly
 * once. Ownership transfers by move and is surrendered by \c release; in both
 * cases the deleter is suppressed for the surrendered value so it can never run
 * twice. Following P0052, every operation that can fail while a live handle is
 * in flight (the owning constructor, the move operations, \c reset with a new
 * handle) disposes of that handle through the deleter before rethrowing, so a
 * resource is never silently leaked.
 *
 * @tparam Resource The owned handle type (pointer, integer, or any movable
 *                  value identifying the resource).
 * @tparam Deleter Callable invocable as \c deleter(resource).
 *
 * @pre \p Deleter is invocable with an lvalue \p Resource.
 * @post A default-constructed instance owns nothing; \c owns() is \c false.
 *
 * @note \c reset and move assignment run the deleter, so each is \c noexcept
 *       exactly when invoking the deleter is: a deleter declared \c noexcept
 *       makes them \c noexcept, and one that may throw lets its exception
 *       propagate. The destructor is always \c noexcept, so a deleter that
 *       throws there terminates the program: keep deleters non-throwing.
 */
template <typename Resource, typename Deleter>
class unique_resource {
public:
  using value_type = Resource;       ///< Owned handle type.
  using resource_type = value_type;  ///< Owned handle type, named for its role.
  using deleter_type = Deleter;      ///< Callable that releases the handle.

private:
  /// @brief Whether the deleter is moved by assignment and so may overlap its neighbours.
  static constexpr bool assignable_deleter{std::is_move_assignable_v<deleter_type>};

  /// @brief Whether running the deleter cannot throw: the \c noexcept of every release.
  static constexpr bool nothrow_release{std::is_nothrow_invocable_v<deleter_type&, resource_type&>};

  resource_type m_resource{};
  [[no_unique_address]] std::
    conditional_t<assignable_deleter, deleter_type, detail::unused_deleter_slot<0>>
      m_overlapping_deleter{};
  std::conditional_t<assignable_deleter, detail::unused_deleter_slot<1>, deleter_type>
    m_plain_deleter{};
  bool m_owns{false};

  /**
   * @brief The member that holds the deleter.
   *
   * @return A reference to the stored deleter.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto deleter_ref() noexcept -> deleter_type& {
    if constexpr (assignable_deleter) {
      return m_overlapping_deleter;
    } else {
      return m_plain_deleter;
    }
  }

  /**
   * @brief The member that holds the deleter, read-only.
   *
   * @return A const reference to the stored deleter.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto deleter_ref() const noexcept -> deleter_type const& {
    if constexpr (assignable_deleter) {
      return m_overlapping_deleter;
    } else {
      return m_plain_deleter;
    }
  }

  /**
   * @brief Initialises the resource member for an owning constructor, P0052 style.
   *
   * The forwarded argument is used when that cannot throw; otherwise the
   * member is copied from the caller's argument, so a failure leaves that
   * argument intact. If the initialisation throws while \p owns is set, the
   * caller's handle is disposed of through the caller's deleter before the
   * exception propagates. The result is elided straight into \c m_resource.
   *
   * @tparam RR Forwarded resource argument type.
   * @tparam DD Forwarded deleter argument type.
   * @param resource Caller's resource argument.
   * @param deleter Caller's deleter argument, invoked on \p resource on a throw.
   * @param owns Whether \p resource is a live handle; the checked factory
   *             passes \c false for the invalid sentinel.
   *
   * @return The resource value to store.
   *
   * @pre \p deleter is a valid releaser for \p resource.
   * @post On a throw with \p owns set, \p resource has been disposed of.
   *
   * @throws Anything initialising the resource throws.
   */
  template <typename RR, typename DD>
  [[nodiscard]] static auto init_resource(
    RR& resource, [[maybe_unused]] DD& deleter, [[maybe_unused]] bool const owns
  ) noexcept(detail::nothrow_member_init_v<resource_type, RR>) -> resource_type {
    if constexpr (std::is_nothrow_constructible_v<resource_type, RR>) {
      return static_cast<resource_type>(std::forward<RR>(resource));
    } else if constexpr (std::is_nothrow_constructible_v<resource_type, RR&>) {
      return static_cast<resource_type>(resource);
    } else {
      try {
        return static_cast<resource_type>(resource);
      } catch (...) {
        if (owns) {
          deleter(resource);
        }
        throw;
      }
    }
  }

  /**
   * @brief Initialises the deleter member for an owning constructor, P0052 style.
   *
   * The forwarded argument is used when that cannot throw; otherwise the
   * member is copied from the caller's argument, which therefore stays
   * intact. If the initialisation throws while \p owns is set, the already
   * stored handle is disposed of through that intact deleter before the
   * exception propagates.
   *
   * @tparam DD Forwarded deleter argument type.
   * @param deleter Caller's deleter argument.
   * @param resource The already stored resource member.
   * @param owns Whether \p resource is a live handle.
   *
   * @return The deleter value to store.
   *
   * @pre \p deleter is a valid releaser for \p resource.
   * @post On a throw with \p owns set, \p resource has been disposed of.
   *
   * @throws Anything initialising the deleter throws.
   */
  template <typename DD>
  [[nodiscard]] static auto init_deleter(
    DD& deleter, [[maybe_unused]] resource_type& resource, [[maybe_unused]] bool const owns
  ) noexcept(detail::nothrow_member_init_v<deleter_type, DD>) -> deleter_type {
    if constexpr (std::is_nothrow_constructible_v<deleter_type, DD>) {
      return static_cast<deleter_type>(std::forward<DD>(deleter));
    } else if constexpr (std::is_nothrow_constructible_v<deleter_type, DD&>) {
      return static_cast<deleter_type>(deleter);
    } else {
      try {
        return static_cast<deleter_type>(deleter);
      } catch (...) {
        if (owns) {
          deleter(resource);
        }
        throw;
      }
    }
  }

  /**
   * @brief Constructs from a resource, its deleter, and an explicit ownership flag.
   *
   * The shared body of the owning constructor and \c make_unique_resource_checked,
   * which passes \c false for an invalid sentinel so that no path, a failing
   * one included, ever hands the sentinel to the deleter.
   *
   * @tparam RR Forwarded resource argument type.
   * @tparam DD Forwarded deleter argument type.
   * @param resource Handle to store.
   * @param deleter Callable that releases \p resource.
   * @param owns Whether the new instance owns \p resource.
   *
   * @pre \p deleter is a valid releaser for \p resource.
   * @post \c owns() equals \p owns; \c get() returns the stored resource.
   *
   * @throws Anything initialising a member throws, after a live handle has been
   *         disposed of via \p deleter.
   */
  template <typename RR, typename DD>
  unique_resource(RR&& resource, DD&& deleter, bool const owns) noexcept(
    detail::nothrow_member_init_v<resource_type, RR>
    && detail::nothrow_member_init_v<deleter_type, DD>
  )
      : m_resource{init_resource<RR, DD>(resource, deleter, owns)}
      , m_overlapping_deleter{detail::deleter_or_placeholder<assignable_deleter, 0>([&] {
        return init_deleter<DD>(deleter, m_resource, owns);
      })}
      , m_plain_deleter{detail::deleter_or_placeholder<!assignable_deleter, 1>([&] {
        return init_deleter<DD>(deleter, m_resource, owns);
      })}
      , m_owns{owns} {}

  /**
   * @brief Steals the deleter for the move constructor, cleaning up on a throw.
   *
   * P0052 leak guard: \c m_resource was initialised with \c move_if_noexcept,
   * so a genuine move left \p other holding no handle. If initialising the
   * deleter then throws, the moved handle is disposed via \p other's deleter and
   * \p other is disarmed; when the resource was copied instead, \p other still
   * owns its handle and nothing is lost. The catch path exists only when the
   * deleter move can actually throw, so the \c noexcept instantiation contains
   * no unreachable rethrow.
   *
   * @param other Source owner whose deleter is being stolen.
   * @param resource This owner's resource member, disposed via \p other's
   *                 deleter on a throw when the resource was genuinely moved.
   *
   * @return The stolen deleter value.
   *
   * @pre None.
   * @post On success the returned value carries \p other's deleter; on a throw
   *       a genuinely moved handle has been disposed and \p other disarmed.
   *
   * @throws Anything the move construction of the deleter throws, after any
   *         genuinely moved handle has been disposed of.
   */
  [[nodiscard]] static auto guarded_deleter_steal(
    unique_resource& other, [[maybe_unused]] resource_type& resource
  ) noexcept(std::is_nothrow_move_constructible_v<deleter_type>) -> deleter_type {
    if constexpr (std::is_nothrow_move_constructible_v<deleter_type>) {
      return std::move_if_noexcept(other.deleter_ref());
    } else {
      try {
        return std::move_if_noexcept(other.deleter_ref());
      } catch (...) {
        if constexpr (std::is_nothrow_move_constructible_v<resource_type>) {
          if (other.m_owns) {
            other.deleter_ref()(resource);
            other.m_owns = false;
          }
        }
        throw;
      }
    }
  }

  /**
   * @brief Whether transferring a \p Member during move assignment cannot throw.
   *
   * True when an assignable member move-assigns without throwing, or when a
   * non-assignable member (a capturing lambda deleter) is nothrow move
   * constructible, since such a member is destroyed and re-created in place.
   *
   * @tparam Member Resource or deleter member type being transferred.
   *
   * @pre None.
   * @post None.
   */
  template <typename Member>
  static constexpr bool nothrow_transfer_v{
    std::is_move_assignable_v<Member> ? std::is_nothrow_move_assignable_v<Member>
                                      : std::is_nothrow_move_constructible_v<Member>
  };

  /**
   * @brief Transfers \p src into \p dst by move assignment or in-place rebuild.
   *
   * An assignable member is move-assigned; a non-assignable member (a capturing
   * lambda deleter) is destroyed and re-created in place from \p src, which is
   * sound only because that construction is required to be \c noexcept.
   *
   * @tparam Member Resource or deleter member type being transferred.
   * @param dst Member to overwrite with \p src.
   * @param src Member to move from.
   *
   * @pre When \p Member is not move-assignable it is nothrow move constructible.
   * @post \p dst holds the value moved out of \p src.
   */
  template <typename Member>
  static auto transfer_member(Member& dst, Member& src) noexcept(nothrow_transfer_v<Member>)
    -> void {
    if constexpr (std::is_move_assignable_v<Member>) {
      dst = std::move(src);
    } else {
      static_assert(
        std::is_nothrow_move_constructible_v<Member>,
        "unique_resource: a non-assignable resource or deleter must be nothrow move constructible"
      );
      std::destroy_at(std::addressof(dst));
      std::construct_at(std::addressof(dst), std::move(src));
    }
  }

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
   * P0052 construction: each member is initialised from the forwarded argument
   * when that cannot throw, and copied from the caller's argument otherwise, so
   * a failure never leaves an argument half-moved. If initialising either
   * member throws, the caller's deleter is invoked on the handle (the caller's
   * resource, or the one already stored) before the exception propagates, so an
   * acquired resource never leaks. The caller's arguments are only moved from
   * when those moves cannot throw.
   *
   * @tparam RR Resource argument type, forwarded.
   * @tparam DD Deleter argument type, forwarded.
   * @param resource Handle to take ownership of.
   * @param deleter Callable that releases \p resource.
   *
   * @pre \p deleter is a valid releaser for \p resource.
   * @post \c owns() is \c true; \c get() returns the stored resource.
   *
   * @throws Anything initialising the resource or the deleter throws, after the
   *         handle has been disposed of via \p deleter.
   */
  template <typename RR, typename DD>
    requires detail::p0052_initialisable_from<resource_type, RR>
             && detail::p0052_initialisable_from<deleter_type, DD>
  unique_resource(RR&& resource, DD&& deleter) noexcept(
    detail::nothrow_member_init_v<resource_type, RR>
    && detail::nothrow_member_init_v<deleter_type, DD>
  )
      : unique_resource{std::forward<RR>(resource), std::forward<DD>(deleter), true} {}

  /**
   * @brief Move-constructs from \p other, transferring ownership.
   *
   * Following P0052, the resource is moved only when its move constructor is
   * \c noexcept and copied otherwise, so a throwing resource transfer leaves
   * \p other fully intact. If initialising the deleter throws after the
   * resource was genuinely moved out of \p other, the moved handle is disposed
   * of via \p other's deleter and \p other is disarmed, so the resource is
   * neither leaked nor double-owned; the exception then propagates.
   *
   * @param other Source owner, left non-owning by a successful move.
   *
   * @pre None.
   * @post This holds \p other's previous resource and ownership state;
   *       \p other's \c owns() is \c false.
   *
   * @throws Anything the transfer of the resource or deleter throws; ownership
   *         stays consistent (exactly one live owner, or a disposed handle).
   */
  unique_resource(unique_resource&& other) noexcept(
    std::is_nothrow_move_constructible_v<resource_type>
    && std::is_nothrow_move_constructible_v<deleter_type>
  )
      : m_resource{std::move_if_noexcept(other.m_resource)}
      , m_overlapping_deleter{detail::deleter_or_placeholder<assignable_deleter, 0>([&] {
        return guarded_deleter_steal(other, m_resource);
      })}
      , m_plain_deleter{detail::deleter_or_placeholder<!assignable_deleter, 1>([&] {
        return guarded_deleter_steal(other, m_resource);
      })}
      , m_owns{std::exchange(other.m_owns, false)} {}

  /**
   * @brief Move-assigns from \p other, transferring ownership.
   *
   * Releases any resource currently owned by \c *this (running its deleter),
   * then takes over \p other's resource, deleter, and ownership flag and clears
   * \p other's ownership. A self-move is a no-op.
   *
   * The members are transferred in the P0052 order: whichever of the two can
   * throw is transferred first, by copy, so a failure leaves \p other still
   * owning an intact resource and deleter pair and leaves \c *this non-owning
   * and valid; no handle is leaked or double-owned. A member that is not
   * assignable (a capturing lambda deleter) is destroyed and re-created in
   * place instead, which requires its move construction to be \c noexcept.
   *
   * @param other Source owner, left non-owning unless it is \c *this.
   *
   * @return Reference to \c *this.
   *
   * @pre \p Resource and \p Deleter are move-assignable (or copy-assignable
   *      when the move can throw), or non-assignable and nothrow move
   *      constructible.
   * @post This holds \p other's previous resource and ownership state; the
   *       resource previously owned by \c *this has been released; \p other's
   *       \c owns() is \c false unless \p other is \c *this.
   *
   * @throws Anything the assignment of the resource or deleter throws; on a
   *         throw \c *this owns nothing and \p other still owns its resource.
   */
  auto operator=(unique_resource&& other) noexcept(
    nothrow_release && nothrow_transfer_v<resource_type> && nothrow_transfer_v<deleter_type>
  ) -> unique_resource& {
    if (this != &other) {
      reset();
      if constexpr (nothrow_transfer_v<resource_type>) {
        if constexpr (nothrow_transfer_v<deleter_type>) {
          transfer_member(m_resource, other.m_resource);
          transfer_member(deleter_ref(), other.deleter_ref());
        } else {
          // The deleter assignment can throw: do it first, by copy, so a
          // failure leaves other's resource and deleter pair untouched.
          deleter_ref() = std::as_const(other.deleter_ref());
          transfer_member(m_resource, other.m_resource);
        }
      } else {
        if constexpr (nothrow_transfer_v<deleter_type>) {
          // The resource assignment can throw: do it first, by copy, so a
          // failure leaves other still owning its intact resource.
          m_resource = std::as_const(other.m_resource);
          transfer_member(deleter_ref(), other.deleter_ref());
        } else {
          m_resource = std::as_const(other.m_resource);
          deleter_ref() = std::as_const(other.deleter_ref());
        }
      }
      m_owns = std::exchange(other.m_owns, false);
    }
    return *this;
  }

  /**
   * @brief Deleted copy constructor: \c unique_resource is move-only.
   *
   * @pre None.
   * @post None.
   */
  unique_resource(unique_resource const&) = delete;

  /**
   * @brief Deleted copy assignment: \c unique_resource is move-only.
   *
   * @return Never returns; the overload is deleted so copying does not compile.
   *
   * @pre None.
   * @post None.
   */
  auto operator=(unique_resource const&) -> unique_resource& = delete;

  /**
   * @brief Grants the checked factory the flag-taking constructor.
   *
   * @tparam R Forwarded resource argument type.
   * @tparam Invalid Sentinel type comparable to the resource.
   * @tparam D Forwarded deleter argument type.
   * @param resource Handle to take ownership of unless it equals \p invalid.
   * @param invalid Sentinel value denoting a failed acquisition.
   * @param deleter Callable that releases \p resource.
   *
   * @return See \c make_unique_resource_checked.
   *
   * @pre See \c make_unique_resource_checked.
   * @post See \c make_unique_resource_checked.
   */
  template <typename R, typename Invalid, typename D>
  friend auto
  make_unique_resource_checked(R&& resource, Invalid const& invalid, D&& deleter) noexcept(
    detail::nothrow_member_init_v<std::decay_t<R>, R>
    && detail::nothrow_member_init_v<std::decay_t<D>, D>
  ) -> unique_resource<std::decay_t<R>, std::decay_t<D>>;

  /**
   * @brief Releases the owned resource, running the deleter if owning.
   *
   * Always \c noexcept, as destructors are in this library: a deleter that
   * throws here terminates the program.
   *
   * @pre The deleter does not throw when invoked.
   * @post \c owns() is \c false; any previously owned resource has had its
   *       deleter run exactly once.
   */
  ~unique_resource() noexcept {
    if (m_owns) {
      m_owns = false;
      deleter_ref()(m_resource);
    }
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
   *
   * @throws Whatever the deleter throws, after which \c owns() is \c false;
   *         \c noexcept when invoking the deleter is.
   */
  auto reset() noexcept(nothrow_release) -> void {
    if (m_owns) {
      // Disarm first, so a throwing deleter never runs a second time.
      m_owns = false;
      deleter_ref()(m_resource);
    }
  }

  /**
   * @brief Releases the current resource and takes ownership of a new one.
   *
   * Runs the deleter on any currently owned resource, then stores \p resource
   * (moved in when the move cannot throw, copied otherwise, per P0052) and
   * resumes owning with the existing deleter. If storing \p resource throws,
   * the deleter is invoked on \p resource before the exception propagates, so
   * the incoming handle never leaks; \c *this is left valid and non-owning.
   *
   * @param resource New handle to take ownership of, moved into the owner.
   *
   * @pre The stored deleter is a valid releaser for \p resource.
   * @post \c owns() is \c true and \c get() returns \p resource; the
   *       previously owned resource has had its deleter run. On a throw,
   *       \c owns() is \c false and \p resource has been disposed of.
   *
   * @throws Anything the assignment of \p resource throws, after \p resource
   *         has been disposed of via the deleter, and whatever the deleter
   *         throws.
   */
  auto reset(
    resource_type resource
  ) noexcept(nothrow_release && std::is_nothrow_move_assignable_v<resource_type>) -> void {
    reset();
    if constexpr (std::is_nothrow_move_assignable_v<resource_type>) {
      m_resource = std::move(resource);
    } else {
      // P0052: assign from a const lvalue so the incoming handle is still
      // intact and can be disposed of when the assignment throws.
      try {
        m_resource = std::as_const(resource);
      } catch (...) {
        deleter_ref()(resource);
        throw;
      }
    }
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
   *       resource; \c get() refers to the moved-from stored value, which for
   *       a move-only \p Resource is its valid but unspecified moved-from
   *       state (a trivially copyable handle keeps its value).
   *
   * @throws Anything the move of the resource throws; ownership is then
   *         unchanged, so the deleter still runs for the kept resource.
   */
  [[nodiscard]] auto release() noexcept(std::is_nothrow_move_constructible_v<resource_type>)
    -> resource_type {
    // Move out before clearing m_owns: a throwing move must leave ownership intact.
    auto released{std::move(m_resource)};
    m_owns = false;
    return released;
  }

  /**
   * @brief A reference to the owned resource.
   *
   * @return Const reference to the stored resource.
   *
   * @pre None.
   * @post None.
   *
   * @note After \c release() the stored value is moved-from; check \c owns()
   *       before relying on \c get().
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
    return deleter_ref();
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
   * Absent for a pointer to \c void, which has nothing to dereference; the
   * return type is spelled so that such a resource still instantiates.
   *
   * @pre \p Resource is a pointer type; the owned pointer is non-null and
   *      refers to a live object.
   * @post None.
   */
  [[nodiscard]] auto operator*() const noexcept
    -> std::add_lvalue_reference_t<std::remove_pointer_t<resource_type>>
    requires std::is_pointer_v<resource_type>
             && (!std::is_void_v<std::remove_pointer_t<resource_type>>)
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
 * Constructs a \c unique_resource holding \p resource and \p deleter. When
 * \p resource compares equal to \p invalid (the sentinel of a failed
 * acquisition, such as \c -1 from a failed \c open), the result stores the
 * sentinel without owning it, so \c get() still reports it and the deleter is
 * never invoked on it: not at destruction, and not when building the result
 * throws. A valid handle is owned, with P0052's no-leak construction.
 *
 * @tparam R Resource argument type, forwarded; the stored type is its decay.
 * @tparam Invalid Sentinel type comparable to the resource via \c ==.
 * @tparam D Deleter argument type, forwarded; the stored type is its decay.
 * @param resource Handle to take ownership of unless it equals \p invalid.
 * @param invalid Sentinel value denoting a failed acquisition.
 * @param deleter Callable that releases \p resource.
 *
 * @return A \c unique_resource that owns \p resource when it differs from
 *         \p invalid, and holds it without owning it otherwise.
 *
 * @pre \p resource and \p invalid are comparable with \c operator==.
 * @post The result's \c owns() is \c false when \p resource equals \p invalid,
 *       and \c true otherwise; \c get() returns the stored resource.
 *
 * @throws Anything initialising the resource or the deleter throws, after a
 *         valid handle has been disposed of via \p deleter.
 */
template <typename R, typename Invalid, typename D>
[[nodiscard]] auto
make_unique_resource_checked(R&& resource, Invalid const& invalid, D&& deleter) noexcept(
  detail::nothrow_member_init_v<std::decay_t<R>, R>
  && detail::nothrow_member_init_v<std::decay_t<D>, D>
) -> unique_resource<std::decay_t<R>, std::decay_t<D>> {
  bool const owns{!(resource == invalid)};
  return unique_resource<std::decay_t<R>, std::decay_t<D>>{
    std::forward<R>(resource), std::forward<D>(deleter), owns
  };
}

}  // namespace nexenne::utility
