#pragma once

/**
 * @file
 * @brief NDJSON / JSON-Lines sink: one structured JSON record per line.
 *
 * Emits each log record as a single-line JSON object terminated with a newline.
 * This is the format every log-aggregation pipeline speaks fluently (Elastic,
 * Loki, Datadog, Splunk, fluentd, vector.dev), so the output flows straight from
 * the file or pipe into the indexer without a transformation step.
 *
 * Field schema:
 *
 * \code
 * {
 *   "ts":     "2026-05-26T12:34:56.789Z",
 *   "level":  "INFO",
 *   "logger": "net",
 *   "file":   "foo.cpp",
 *   "line":   42,
 *   "tid":    "140245123",
 *   "msg":    "connect failed: timeout"
 * }
 * \endcode
 *
 * The "level" field is the canonical unpadded token from \c to_token: one of
 * TRACE, DEBUG, INFO, WARN, ERROR, CRITICAL, OFF, the same vocabulary
 * \c pattern_formatter emits. The "tid" field is the producing thread's id as a
 * platform-defined string. The "ts" field is RFC 3339 UTC with millisecond
 * precision. The destination is
 * either a path (opened in append mode) or an externally-owned FILE* (for
 * example \c stdout for container deployments that ship logs via the runtime).
 *
 * String fields are JSON-escaped: \c " , \c \\ , and the control characters
 * \c \\b \c \\f \c \\n \c \\r \c \\t are encoded as their short escapes; other
 * control bytes below \c 0x20 use the \c \\uXXXX form. Bytes at or above
 * \c 0x20 pass through unchanged (the consumer is responsible for UTF-8).
 *
 * The module depends only on \c container, so the JSON escaping is hand-rolled
 * here rather than delegated to a serialization module.
 */

#include <chrono>
#include <cstdio>
#include <format>
#include <string>
#include <string_view>
#include <thread>

#include <nexenne/logging/sink.hpp>
#include <nexenne/utility/discard.hpp>

namespace nexenne::logging {

/**
 * @brief NDJSON (JSON Lines) sink: one structured JSON object per log record.
 *
 * Each record is serialised as a single-line JSON object terminated with a
 * newline. String fields are JSON-escaped. The destination is either a path
 * opened in append mode (owned and closed by the sink) or an externally-owned
 * FILE* (the caller retains ownership).
 *
 * @pre None.
 * @post None.
 */
class json_sink final : public sink {
public:
  /**
   * @brief Opens \p path in append mode and writes JSON lines to it.
   *
   * The file is owned by the sink and closed by the destructor. Never throws on
   * a failed open; check \c is_open() afterward instead.
   *
   * @param path Filesystem path to append to.
   *
   * @pre None.
   * @post \c is_open() reports whether the file was opened successfully.
   */
  explicit json_sink(std::string_view const path) noexcept : m_owns_file{true} {
    m_file = std::fopen(std::string{path}.c_str(), "ab");
  }

  /**
   * @brief Writes JSON lines to an externally-owned FILE*.
   *
   * Typically used with \c stdout or \c stderr. The sink does not close the
   * file; the caller owns its lifetime.
   *
   * @param out Externally-owned FILE* to write to, valid open or null.
   *
   * @pre None.
   * @post The sink writes to \p out without taking ownership.
   */
  explicit json_sink(std::FILE* const out) noexcept : m_file{out} {}

  json_sink(json_sink const&) = delete;
  auto operator=(json_sink const&) -> json_sink& = delete;

  // Move operations are intentionally not declared: a sink is always owned
  // through a shared_ptr, never moved by value.

  /**
   * @brief Destructor: flushes and closes the file when owned.
   *
   * @pre None.
   * @post Any owned file has been flushed and closed.
   */
  ~json_sink() noexcept override {
    if (m_owns_file && m_file != nullptr) {
      nexenne::utility::discard(std::fflush(m_file));
      nexenne::utility::discard(std::fclose(m_file));
    }
  }

  /**
   * @brief Whether the sink has a valid open file.
   *
   * @return \c true if the underlying handle is non-null.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto is_open() const noexcept -> bool {
    return m_file != nullptr;
  }

protected:
  /**
   * @brief Serialises \p r as one JSON line and writes it to the file.
   *
   * Escapes every string field and terminates the object with a newline. A null
   * file handle makes the call a no-op.
   *
   * @param r Record to serialise.
   *
   * @pre None.
   * @post One NDJSON line for \p r has been written when the file is open.
   *
   * @complexity \c O(|record|).
   */
  auto write_out(record const& r) noexcept -> void override {
    if (m_file == nullptr) {
      return;
    }
    auto line{std::string{}};
    line.reserve(256);

    line += R"({"ts":")";
    line += format_timestamp(r.timestamp);

    line += R"(","level":")";
    // Emit the canonical unpadded token shared with pattern_formatter, so a
    // severity spells the same across both structured emitters.
    line += to_token(r.severity);

    line += R"(","logger":")";
    append_escaped(line, r.logger_name);

    line += R"(","file":")";
    auto const* const file{r.location.file_name() != nullptr ? r.location.file_name() : "?"};
    append_escaped(line, file);

    line += std::format(R"(","line":{})", r.location.line());

    // Thread id as a string: its textual form is platform-defined and may not be
    // a bare integer, so quoting keeps the field valid JSON everywhere.
    line += std::format(R"(,"tid":"{}")", r.thread_id);

    line += R"(,"msg":")";
    append_escaped(line, r.message);
    line += R"("})";
    line += '\n';

    nexenne::utility::discard(std::fwrite(line.data(), 1, line.size(), m_file));
  }

  /**
   * @brief Flushes the underlying file when one is open.
   *
   * @pre None.
   * @post Any buffered bytes have been flushed to the file.
   */
  auto flush_out() noexcept -> void override {
    if (m_file != nullptr) {
      nexenne::utility::discard(std::fflush(m_file));
    }
  }

private:
  /**
   * @brief Appends a JSON-escaped copy of \p s to \p out.
   *
   * Encodes \c " , \c \\ , and the short control escapes; other bytes below
   * \c 0x20 become \c \\uXXXX. Bytes at or above \c 0x20 pass through verbatim.
   *
   * @param out Destination string the escaped bytes are appended to.
   * @param s Source bytes to escape.
   *
   * @pre None.
   * @post \p out has the escaped form of \p s appended.
   *
   * @complexity \c O(|s|).
   */
  static auto append_escaped(std::string& out, std::string_view const s) -> void {
    for (auto const c : s) {
      auto const byte{static_cast<unsigned char>(c)};
      switch (byte) {
        case '"':
          out += "\\\"";
          break;
        case '\\':
          out += "\\\\";
          break;
        case '\b':
          out += "\\b";
          break;
        case '\f':
          out += "\\f";
          break;
        case '\n':
          out += "\\n";
          break;
        case '\r':
          out += "\\r";
          break;
        case '\t':
          out += "\\t";
          break;
        default:
          if (byte < 0x20) {
            out += std::format("\\u{:04x}", static_cast<unsigned int>(byte));
          } else {
            out.push_back(c);
          }
          break;
      }
    }
  }

  /**
   * @brief Formats \p tp as RFC 3339 UTC with millisecond precision.
   *
   * Produces "YYYY-MM-DDTHH:MM:SS.mmmZ".
   *
   * @param tp Time point to format.
   *
   * @return The formatted timestamp string.
   *
   * @pre None.
   * @post None.
   * @throws std::bad_alloc if the formatting allocation fails.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] static auto format_timestamp(std::chrono::system_clock::time_point const tp
  ) -> std::string {
    // Floor to whole seconds for the calendar part, then append the millisecond
    // fraction by hand. Formatting a sub-second time point with %T would already
    // print fractional seconds, so the explicit ".mmm" must be built from a
    // second-precision point to avoid a duplicated fraction.
    // floor (not time_point_cast, which truncates toward zero) so the second and
    // millisecond split stays correct and non-negative for pre-epoch timestamps.
    auto const tp_ms{std::chrono::floor<std::chrono::milliseconds>(tp)};
    auto const tp_sec{std::chrono::floor<std::chrono::seconds>(tp_ms)};
    auto const ms_part{(tp_ms - tp_sec).count()};
    return std::format("{:%FT%T}.{:03}Z", tp_sec, ms_part);
  }

  std::FILE* m_file{nullptr};  ///< Output handle; null when not open.
  bool m_owns_file{false};     ///< Whether the destructor must close \c m_file.
};

}  // namespace nexenne::logging
