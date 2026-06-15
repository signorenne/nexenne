#pragma once

/**
 * @file
 * @brief RAII emit suppression for a signal, Qt's \c QSignalBlocker
 *        equivalent.
 *
 * Construction saves the signal's prior \c blocked state and calls
 * \c block(). Destruction restores the prior state. This means
 * blockers nest correctly: an inner blocker's destructor restores
 * the outer's block, not the unblocked baseline.
 *
 * \code
 * signal::emit_blocker outer{sig};   // outer: was=false, now=true
 * {
 *     signal::emit_blocker inner{sig};   // inner: was=true, now=true
 *     // ...
 * }                                       // ~inner: restore true
 * sig.emit();                             // still blocked by outer
 * \endcode
 *
 * Move-only. Zero heap. Cheap enough to drop into hot paths.
 */

#include <concepts>
#include <utility>

namespace nexenne::signal {

/**
 * @brief A type whose emission can be suppressed and queried, the contract
 *        \c emit_blocker drives.
 *
 * Satisfied by \c nexenne::signal::signal and any type exposing the same three
 * members.
 *
 * @tparam S Candidate signal type.
 *
 * @pre None.
 * @post None.
 */
template <typename S>
concept blockable = requires(S& s) {
  { s.is_blocked() } -> std::convertible_to<bool>;
  s.block();
  s.unblock();
};

/**
 * @brief RAII guard that blocks a signal's emission for a scope.
 *
 * See the file-level documentation for the nesting semantics. Move-only
 * and zero heap.
 *
 * @tparam Signal A \c blockable type providing \c is_blocked, \c block, and
 *         \c unblock (typically \c nexenne::signal::signal).
 */
template <blockable Signal>
class emit_blocker {
private:
  Signal* m_signal{nullptr};
  bool m_prior_blocked{false};

public:
  /**
   * @brief Saves \p s's prior blocked state and blocks it.
   *
   * @param s  Signal to block. Must outlive the blocker.
   *
   * @pre \p s outlives this blocker.
   * @post \p s is blocked; its prior state is recorded for restore.
   */
  explicit emit_blocker(Signal& s) noexcept : m_signal{&s}, m_prior_blocked{s.is_blocked()} {
    s.block();
  }

  /// @brief Deleted copy constructor; the guard is unique (move-only).
  emit_blocker(emit_blocker const&) = delete;
  /// @brief Deleted copy assignment; the guard is unique (move-only).
  auto operator=(emit_blocker const&) -> emit_blocker& = delete;

  /**
   * @brief Move-constructs, taking over the guard from \p other.
   *
   * @param other  Source guard; left inert (restores nothing).
   *
   * @pre None.
   * @post This guard now owns the restore; \p other does nothing on
   *       destruction.
   */
  emit_blocker(emit_blocker&& other) noexcept
      : m_signal{std::exchange(other.m_signal, nullptr)}, m_prior_blocked{other.m_prior_blocked} {}

  /**
   * @brief Move-assigns: restores this guard's signal, then adopts
   *        \p other's.
   *
   * @param other  Source guard; left inert.
   *
   * @return Reference to \c *this.
   *
   * @pre None. Self-assignment is a no-op.
   * @post This guard's previously held signal (if any) is restored to
   *       its prior state; this guard now owns \p other's restore and
   *       \p other does nothing on destruction.
   */
  auto operator=(emit_blocker&& other) noexcept -> emit_blocker& {
    if (this != &other) {
      release();
      m_signal = std::exchange(other.m_signal, nullptr);
      m_prior_blocked = other.m_prior_blocked;
    }
    return *this;
  }

  /**
   * @brief Destroys the guard, restoring the signal's prior state.
   *
   * @pre None.
   * @post The held signal (if any) is back to the blocked state it had
   *       before this guard was constructed.
   */
  ~emit_blocker() noexcept {
    release();
  }

  /**
   * @brief Restores the signal's prior state immediately.
   *
   * Idempotent: a second call does nothing. After this the guard holds
   * no signal, so destruction is a no-op.
   *
   * @pre None.
   * @post The held signal (if any) is restored to its pre-construction
   *       blocked state and the guard holds no signal.
   */
  auto release() noexcept -> void {
    if (m_signal != nullptr) {
      if (m_prior_blocked) {
        m_signal->block();
      } else {
        m_signal->unblock();
      }
      m_signal = nullptr;
    }
  }
};

/**
 * @brief Deduction guide: deduce \c Signal from the constructor argument.
 *
 * @tparam Signal The signal type, deduced from \c emit_blocker{sig}.
 */
template <blockable Signal>
emit_blocker(Signal&) -> emit_blocker<Signal>;

}  // namespace nexenne::signal
