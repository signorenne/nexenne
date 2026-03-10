#pragma once

/**
 * @file
 * @brief Sink interface and the bundled console, file, and in-memory sinks.
 *
 * A sink is where a formatted log line ends up. The library owns the transport
 * (records flow producer to backend); a sink owns the destination (stdout, a
 * file, an in-memory ring, the network). Multiple sinks attach to one logger or
 * to the global manager; each applies its own minimum-level filter.
 *
 * Concurrency: the backend invokes \c write serially, so a \c write_out may
 * assume single-threaded access for its duration. A sink also touched from
 * other threads (for reconfiguration, snapshots) must guard that itself.
 *
 * Customisation: derive from \c sink and implement \c write_out and
 * \c flush_out; optionally reuse \c default_format, which emits
 * "[YYYY-MM-DD HH:MM:SS.mmm] [logger] [LEVEL] file.cpp:line -- msg".
 */

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <format>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <nexenne/container/ring_buffer.hpp>
#include <nexenne/logging/level.hpp>
#include <nexenne/logging/record.hpp>

namespace nexenne::logging {

/**
 * @brief Abstract base for a destination that formatted log lines are written to.
 *
 * Derive and implement \c write_out and \c flush_out; optionally reuse or
 * replace \c default_format. The base owns a per-sink minimum-level filter
 * applied by \c write.
 *
 * @pre None.
 * @post A default-constructed sink has a minimum level of \c level::trace.
 */
class sink {
protected:
  /**
   * @brief Writes a record to the destination, with no level filtering.
   *
   * Called by \c write only for records that pass the filter. It is \c noexcept
   * because the backend thread has nowhere to propagate an exception; a
   * formatting allocation failure terminates rather than unwinding the backend.
   *
   * @param r Record to write.
   *
   * @pre None.
   * @post Implementation-defined: the record has been emitted.
   */
  virtual auto write_out(record const& r) noexcept -> void = 0;

  /**
   * @brief Flushes any buffered output to the destination.
   *
   * @pre None.
   * @post Implementation-defined: buffered output has been flushed.
   */
  virtual auto flush_out() noexcept -> void = 0;

  /**
   * @brief Default record-to-string formatter.
   *
   * Emits "[YYYY-MM-DD HH:MM:SS.mmm] [logger] [LEVEL] file.cpp:line -- msg\n".
   * Override in a derived sink for a different line shape.
   *
   * @param r Record to render.
   *
   * @return The formatted, newline-terminated line.
   *
   * @pre None.
   * @post None.
   * @throws std::bad_alloc if the formatting allocation fails.
   *
   * @complexity \c O(line length).
   */
  [[nodiscard]] static auto default_format(record const& r) -> std::string {
    // Floor to whole seconds for the date-time part, then append the
    // milliseconds by hand: formatting %T on the full-precision time point
    // would already print a fraction, duplicating the sub-second digits.
    auto const tp{r.timestamp};
    auto const tp_sec{std::chrono::time_point_cast<std::chrono::seconds>(tp)};
    auto const tp_ms{std::chrono::time_point_cast<std::chrono::milliseconds>(tp)};
    auto const ms_part{(tp_ms - tp_sec).count()};
    auto const* const file{r.location.file_name() != nullptr ? r.location.file_name() : "?"};
    return std::format(
      "[{:%F %T}.{:03}] [{}] [{}] {}:{} -- {}\n",
      tp_sec,
      ms_part,
      r.logger_name,
      to_string(r.severity),
      file,
      r.location.line(),
      r.message
    );
  }

private:
  std::atomic<level> m_min_level{level::trace};

public:
  sink() noexcept = default;
  sink(sink const&) = delete;
  auto operator=(sink const&) -> sink& = delete;
  // Move operations are intentionally not declared: the atomic m_min_level is
  // not movable, and a sink is always owned through a shared_ptr, never moved
  // by value.
  virtual ~sink() noexcept = default;

  /**
   * @brief Writes \p r through this sink, applying the level filter.
   *
   * Records below the sink's minimum level are dropped silently.
   *
   * @param r Record to write.
   *
   * @pre None.
   * @post \c write_out was invoked exactly when \p r's severity is at or above
   *       \c min_level().
   */
  auto write(record const& r) noexcept -> void {
    if (r.severity < m_min_level.load(std::memory_order_relaxed)) {
      return;
    }
    write_out(r);
  }

  /**
   * @brief Flushes any buffered output for durability.
   *
   * @pre None.
   * @post Buffered output has been flushed.
   */
  auto flush() noexcept -> void {
    flush_out();
  }

  /**
   * @brief Sets the per-sink minimum severity filter.
   *
   * @param l New minimum level; records below it are dropped.
   *
   * @pre None.
   * @post \c min_level() returns \p l.
   */
  auto set_min_level(level const l) noexcept -> void {
    m_min_level.store(l, std::memory_order_relaxed);
  }

  /**
   * @brief Current minimum severity filter.
   *
   * @return The level below which records are dropped.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto min_level() const noexcept -> level {
    return m_min_level.load(std::memory_order_relaxed);
  }
};

/**
 * @brief Sink that writes formatted lines to stdout and/or stderr.
 *
 * @pre None.
 * @post None.
 */
class console_sink final : public sink {
public:
  /**
   * @brief Stream-routing policy for a \c console_sink.
   *
   * @pre None.
   * @post None.
   */
  enum class stream : std::uint8_t {
    stdout_only,  ///< Always write to stdout.
    stderr_only,  ///< Always write to stderr.
    auto_split,   ///< stdout for info-and-below, stderr for warn-and-up.
  };

  /**
   * @brief Constructs a console sink with the given routing policy.
   *
   * @param s Stream-routing policy; defaults to \c stream::auto_split.
   *
   * @pre None.
   * @post The sink routes output according to \p s.
   */
  explicit console_sink(stream const s = stream::auto_split) noexcept : m_stream{s} {}

protected:
  auto write_out(record const& r) noexcept -> void override {
    auto const line{default_format(r)};
    auto* const out{pick_stream(r.severity)};
    // fwrite is the smallest portable atomic write for FILE*; glibc serialises
    // a full fwrite call.
    static_cast<void>(std::fwrite(line.data(), 1, line.size(), out));
  }

  auto flush_out() noexcept -> void override {
    static_cast<void>(std::fflush(stdout));
    static_cast<void>(std::fflush(stderr));
  }

private:
  [[nodiscard]] auto pick_stream(level const sev) const noexcept -> std::FILE* {
    switch (m_stream) {
      case stream::stdout_only:
        return stdout;
      case stream::stderr_only:
        return stderr;
      case stream::auto_split:
        return sev >= level::warn ? stderr : stdout;
    }
    return stdout;
  }

  stream m_stream;
};

/**
 * @brief Sink that appends each formatted line to a file.
 *
 * Opens the file in append mode at construction (via \c std::fopen rather than
 * the C++ \c fstream stream classes, to stay light and embedded-portable) and
 * closes it on destruction. Move-only: ownership of the handle transfers and the
 * moved-from sink is left closed.
 *
 * @pre None.
 * @post None.
 */
class file_sink final : public sink {
public:
  /**
   * @brief Opens \p path in append mode.
   *
   * Never throws on a failed open; check \c is_open afterward instead.
   *
   * @param path Filesystem path to append to.
   *
   * @pre None.
   * @post \c is_open() reports whether the file was opened successfully.
   */
  explicit file_sink(std::string_view const path) noexcept {
    m_file = std::fopen(std::string{path}.c_str(), "ab");
  }

  file_sink(file_sink const&) = delete;
  auto operator=(file_sink const&) -> file_sink& = delete;

  file_sink(file_sink&& other) noexcept : m_file{other.m_file} {
    other.m_file = nullptr;
  }

  auto operator=(file_sink&& other) noexcept -> file_sink& {
    if (this != &other) {
      close();
      m_file = other.m_file;
      other.m_file = nullptr;
    }
    return *this;
  }

  ~file_sink() noexcept override {
    close();
  }

  /**
   * @brief Whether the file is open.
   *
   * @return \c true if the underlying handle is valid.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto is_open() const noexcept -> bool {
    return m_file != nullptr;
  }

protected:
  auto write_out(record const& r) noexcept -> void override {
    if (m_file == nullptr) {
      return;
    }
    auto const line{default_format(r)};
    static_cast<void>(std::fwrite(line.data(), 1, line.size(), m_file));
  }

  auto flush_out() noexcept -> void override {
    if (m_file != nullptr) {
      static_cast<void>(std::fflush(m_file));
    }
  }

private:
  auto close() noexcept -> void {
    if (m_file != nullptr) {
      static_cast<void>(std::fflush(m_file));
      static_cast<void>(std::fclose(m_file));
      m_file = nullptr;
    }
  }

  std::FILE* m_file{nullptr};
};

/**
 * @brief In-memory sink retaining the last \p N formatted lines.
 *
 * Backed by \c container::ring_buffer, so older lines are overwritten once full.
 * Useful for crash diagnostics (dump the most recent lines). A mutex guards the
 * backend's writes against \c snapshot and \c size calls from other threads.
 *
 * @tparam N Number of lines retained.
 *
 * @pre None.
 * @post A default-constructed ring sink is empty.
 */
template <std::size_t N>
class ring_sink final : public sink {
public:
  /**
   * @brief Snapshot of the buffered lines, oldest first.
   *
   * Briefly acquires the internal mutex; safe to call from any thread.
   *
   * @return A copy of the retained lines in arrival order.
   *
   * @pre None.
   * @post None.
   * @throws std::bad_alloc if the snapshot vector cannot be allocated.
   *
   * @complexity \c O(N).
   */
  [[nodiscard]] auto snapshot() const -> std::vector<std::string> {
    auto const guard{std::lock_guard{m_mutex}};
    auto out{std::vector<std::string>{}};
    out.reserve(m_buf.size());
    for (auto const& s : m_buf) {
      out.push_back(s);
    }
    return out;
  }

  /**
   * @brief Number of lines currently buffered.
   *
   * Briefly acquires the internal mutex; safe to call from any thread.
   *
   * @return The buffered line count, at most \p N.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto size() const noexcept -> std::size_t {
    auto const guard{std::lock_guard{m_mutex}};
    return m_buf.size();
  }

protected:
  auto write_out(record const& r) noexcept -> void override {
    auto const guard{std::lock_guard{m_mutex}};
    m_buf.push_overwrite(default_format(r));
  }

  auto flush_out() noexcept -> void override {}

private:
  // Covers concurrent access between the backend's single-threaded write and
  // snapshot/size calls from arbitrary threads.
  mutable std::mutex m_mutex;
  nexenne::container::ring_buffer<std::string, N> m_buf{};
};

}  // namespace nexenne::logging
