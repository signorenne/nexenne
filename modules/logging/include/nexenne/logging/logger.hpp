#pragma once

/**
 * @file
 * @brief Named log emitter, parameterised on a \c config.
 *
 * Every \c info() / \c warn() / etc. call formats its arguments with
 * \c std::format on the calling thread and submits a \c record to
 * \c basic_manager<Config>::instance(). Whether that manager is async (backend
 * thread) or sync (inline dispatch) is decided at compile time by
 * \c Config::async. A logger and its manager must share the same \c Config.
 *
 * The source location is captured at the call site through the \c format_string
 * wrapper, so callers never pass file/line. Two filter layers apply: the
 * compile-time \c NEXENNE_LOG_MIN_LEVEL via the \c LOG_* macros, and the runtime
 * per-logger level checked here (a below-threshold call costs one relaxed load).
 */

#include <atomic>
#include <deque>
#include <format>
#include <mutex>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <nexenne/logging/config.hpp>
#include <nexenne/logging/format_string.hpp>
#include <nexenne/logging/level.hpp>
#include <nexenne/logging/manager.hpp>
#include <nexenne/logging/record.hpp>
#include <nexenne/utility/discard.hpp>

namespace nexenne::logging {

namespace detail {

/**
 * @brief Interns a logger name into process-lifetime storage, returning a view.
 *
 * Names are deduplicated and never freed, so the returned view stays valid for
 * the whole program. This lets a \c record borrow the name as a \c string_view
 * (no per-call allocation) while remaining safe even when the record outlives
 * its logger in the async queue. The \c std::deque node storage keeps existing
 * elements pinned across growth, so earlier views never dangle.
 *
 * @param name Logger name to intern.
 *
 * @return A stable view of the interned name.
 *
 * @pre None.
 * @post The name is present in the intern table.
 * @throws std::bad_alloc if the table cannot grow.
 */
[[nodiscard]] inline auto intern_name(std::string_view const name) -> std::string_view {
  static std::mutex mutex;
  static std::deque<std::string> storage;
  auto const guard{std::lock_guard{mutex}};
  for (auto const& s : storage) {
    if (s == name) {
      return s;
    }
  }
  return storage.emplace_back(name);
}

}  // namespace detail

/**
 * @brief Named log emitter parameterised on a \c config.
 *
 * Each level call formats its arguments with \c std::format on the calling
 * thread and submits a \c record to \c basic_manager<Config>::instance(). The
 * logger and its manager must share the same \p Config.
 *
 * @tparam Config Configuration policy satisfying \c config_like.
 *
 * @pre None.
 * @post None.
 */
template <config_like Config = default_config>
class basic_logger {
public:
  using config_type = Config;
  using manager_type = basic_manager<Config>;

private:
  std::string_view m_name;  ///< Interned (process-lifetime) name; never reallocated.
  std::atomic<level> m_min_level;

public:
  /**
   * @brief Constructs a named logger with a runtime minimum level.
   *
   * The name is interned once into process-lifetime storage; thereafter every
   * record borrows it as a \c string_view, so a log call allocates nothing for
   * the name (only the formatted message). Not \c noexcept: interning the name
   * the first time may allocate.
   *
   * @param name Logger name carried on every emitted record.
   * @param min Initial minimum severity; defaults to \c level::trace.
   *
   * @pre None.
   * @post \c name() is \p name and \c min_level() is \p min.
   */
  explicit basic_logger(std::string_view const name, level const min = level::trace)
      : m_name{detail::intern_name(name)}, m_min_level{min} {}

  /**
   * @brief Logger name.
   *
   * @return A view of the name, valid for the logger's lifetime.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto name() const noexcept -> std::string_view {
    return m_name;
  }

  /**
   * @brief Sets the runtime minimum severity filter.
   *
   * @param l New minimum level; calls below it are dropped cheaply.
   *
   * @pre None.
   * @post \c min_level() returns \p l.
   */
  auto set_min_level(level const l) noexcept -> void {
    m_min_level.store(l, std::memory_order_relaxed);
  }

  /**
   * @brief Current runtime minimum severity filter.
   *
   * @return The level below which log calls are dropped.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto min_level() const noexcept -> level {
    return m_min_level.load(std::memory_order_relaxed);
  }

  /**
   * @brief Whether a record at level \p l would be emitted.
   *
   * @param l Candidate severity.
   *
   * @return \c true if \p l is at or above \c min_level().
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto enabled(level const l) const noexcept -> bool {
    return l >= min_level();
  }

  /**
   * @brief Emits a record at the explicit level \p lvl.
   *
   * Drops the call when \p lvl is below \c min_level(); otherwise formats the
   * arguments and pushes a record to the manager. The logger name is borrowed
   * (interned), so the only hot-path allocation is the formatted message. Not
   * \c noexcept: formatting the message may allocate and \c bad_alloc propagates
   * per the error policy.
   *
   * @tparam Args Argument types matching \p fmt.
   * @param lvl Severity of the record.
   * @param fmt Format string with captured source location.
   * @param args Arguments to format.
   *
   * @pre None.
   * @post A record is pushed to the manager exactly when \c enabled(lvl).
   */
  template <typename... Args>
  auto log(level const lvl, format_string<std::type_identity_t<Args>...> const fmt, Args&&... args)
    -> void {
    if (!enabled(lvl)) {
      return;
    }
    auto msg{std::format(fmt.fmt, std::forward<Args>(args)...)};
    nexenne::utility::discard(
      manager_type::instance().push(record{lvl, fmt.loc, m_name, std::move(msg)})
    );
  }

  /**
   * @brief Emits a record at \c level::trace.
   *
   * @tparam Args Argument types matching \p fmt.
   * @param fmt Format string with captured source location.
   * @param args Arguments to format.
   *
   * @pre None.
   * @post A record is pushed exactly when \c enabled(level::trace).
   */
  template <typename... Args>
  auto trace(format_string<std::type_identity_t<Args>...> const fmt, Args&&... args) -> void {
    log(level::trace, fmt, std::forward<Args>(args)...);
  }

  /**
   * @brief Emits a record at \c level::debug.
   *
   * @tparam Args Argument types matching \p fmt.
   * @param fmt Format string with captured source location.
   * @param args Arguments to format.
   *
   * @pre None.
   * @post A record is pushed exactly when \c enabled(level::debug).
   */
  template <typename... Args>
  auto debug(format_string<std::type_identity_t<Args>...> const fmt, Args&&... args) -> void {
    log(level::debug, fmt, std::forward<Args>(args)...);
  }

  /**
   * @brief Emits a record at \c level::info.
   *
   * @tparam Args Argument types matching \p fmt.
   * @param fmt Format string with captured source location.
   * @param args Arguments to format.
   *
   * @pre None.
   * @post A record is pushed exactly when \c enabled(level::info).
   */
  template <typename... Args>
  auto info(format_string<std::type_identity_t<Args>...> const fmt, Args&&... args) -> void {
    log(level::info, fmt, std::forward<Args>(args)...);
  }

  /**
   * @brief Emits a record at \c level::warn.
   *
   * @tparam Args Argument types matching \p fmt.
   * @param fmt Format string with captured source location.
   * @param args Arguments to format.
   *
   * @pre None.
   * @post A record is pushed exactly when \c enabled(level::warn).
   */
  template <typename... Args>
  auto warn(format_string<std::type_identity_t<Args>...> const fmt, Args&&... args) -> void {
    log(level::warn, fmt, std::forward<Args>(args)...);
  }

  /**
   * @brief Emits a record at \c level::error.
   *
   * @tparam Args Argument types matching \p fmt.
   * @param fmt Format string with captured source location.
   * @param args Arguments to format.
   *
   * @pre None.
   * @post A record is pushed exactly when \c enabled(level::error).
   */
  template <typename... Args>
  auto error(format_string<std::type_identity_t<Args>...> const fmt, Args&&... args) -> void {
    log(level::error, fmt, std::forward<Args>(args)...);
  }

  /**
   * @brief Emits a record at \c level::critical.
   *
   * @tparam Args Argument types matching \p fmt.
   * @param fmt Format string with captured source location.
   * @param args Arguments to format.
   *
   * @pre None.
   * @post A record is pushed exactly when \c enabled(level::critical).
   */
  template <typename... Args>
  auto critical(format_string<std::type_identity_t<Args>...> const fmt, Args&&... args) -> void {
    log(level::critical, fmt, std::forward<Args>(args)...);
  }
};

/**
 * @brief Default logger type backed by \c default_config.
 *
 * @pre None.
 * @post None.
 */
using logger = basic_logger<default_config>;

/**
 * @brief Process-wide default logger named "main" for a given config.
 *
 * Each distinct \p Config has its own default logger backed by its own manager
 * singleton.
 *
 * @tparam Config Configuration policy satisfying \c config_like.
 *
 * @return A reference to the shared default logger for \p Config.
 *
 * @pre None.
 * @post The same reference is returned on every call for a given \p Config.
 */
template <config_like Config = default_config>
[[nodiscard]] inline auto default_logger() -> basic_logger<Config>& {
  static basic_logger<Config> s_default{"main"};
  return s_default;
}

}  // namespace nexenne::logging
