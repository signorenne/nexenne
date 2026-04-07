#pragma once

/**
 * @file
 * @brief Size-based rotating file sink that keeps a bounded number of backups.
 *
 * Solves the "my log file grew to 200 GB" problem: when the active file would
 * exceed \c max_bytes, it is closed and renamed, older files shift down
 * (\c foo.log.1 to \c foo.log.2, and so on), the oldest beyond \c max_files is
 * deleted, and writing resumes into a fresh \c foo.log.
 *
 * Rotation scheme with \c max_files = 3, on each rotate:
 *
 *     foo.log.3 -> deleted
 *     foo.log.2 -> foo.log.3
 *     foo.log.1 -> foo.log.2
 *     foo.log   -> foo.log.1
 *     new empty foo.log opened
 *
 * Common settings:
 *   - \c max_bytes = 10 MiB, \c max_files = 5 keeps the last ~50 MiB of logs
 *     across five generations.
 *   - \c max_bytes = 100 MiB, \c max_files = 10 for noisier services.
 *
 * The rotation check runs before each \c write_out that would cross the limit;
 * a record is never split across two files. The size is tracked with a running
 * counter, so no \c stat call is needed per write. Like \c file_sink, this uses
 * \c std::fopen rather than the C++ \c fstream stream classes to stay light and
 * embedded-portable.
 */

#include <cstddef>
#include <cstdio>
#include <format>
#include <string>
#include <string_view>

#include <nexenne/logging/sink.hpp>
#include <nexenne/utility/discard.hpp>

namespace nexenne::logging {

/**
 * @brief File sink that rotates by size, keeping a bounded number of backups.
 *
 * When the active file would exceed \c max_bytes it is closed and renamed,
 * older files shift down (\c foo.log.1 to \c foo.log.2, and so on), the oldest
 * beyond \c max_files is deleted, and writing resumes into a fresh \c foo.log.
 * The rotation check runs before any write that would cross the limit, so each
 * rotated file holds at most \c max_bytes and no record is ever split. The size
 * is tracked with a running counter, so no \c stat call is needed per write.
 *
 * Not copyable or movable: the sink is always owned through a \c shared_ptr.
 *
 * @pre None.
 * @post A constructed sink reports its open state via \c is_open().
 */
class rotating_file_sink final : public sink {
public:
  /**
   * @brief Constructs and opens the active log file in append mode.
   *
   * Never throws on a failed open; check \c is_open() afterward instead. When
   * the file already exists its current size seeds the running counter, so a
   * restart does not lose the rotation budget.
   *
   * @param path Base path for the active log file. Rotated files are named
   *             \c path.1, \c path.2, and so on.
   * @param max_bytes Maximum size in bytes a single rotated file may reach.
   *                  Rotation always happens between records.
   * @param max_files Number of rotated backups to keep. \c 0 means truncate
   *                  rather than archive.
   *
   * @pre None.
   * @post \c is_open() reports whether the file was opened successfully.
   *
   * @complexity \c O(|path|).
   */
  rotating_file_sink(
    std::string_view const path, std::size_t const max_bytes, std::size_t const max_files
  )
      : m_base_path{path}, m_max_bytes{max_bytes}, m_max_files{max_files} {
    open_current();
  }

  rotating_file_sink(rotating_file_sink const&) = delete;
  auto operator=(rotating_file_sink const&) -> rotating_file_sink& = delete;
  rotating_file_sink(rotating_file_sink&&) = delete;
  auto operator=(rotating_file_sink&&) -> rotating_file_sink& = delete;

  /**
   * @brief Flushes and closes the active file.
   *
   * @pre None.
   * @post The active file has been flushed and closed.
   */
  ~rotating_file_sink() noexcept override {
    close_current();
  }

  /**
   * @brief Whether the active file is open.
   *
   * @return \c true if the underlying handle is valid.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto is_open() const noexcept -> bool {
    return m_file != nullptr;
  }

  /**
   * @brief Bytes written to the current active file.
   *
   * @return The running byte count for the current, not yet rotated, file.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto current_size() const noexcept -> std::size_t {
    return m_current_size;
  }

  /**
   * @brief Base path of the active log file.
   *
   * @return A view of the base path, valid for the sink's lifetime.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto base_path() const noexcept -> std::string_view {
    return m_base_path;
  }

  /**
   * @brief Forces an immediate rotation regardless of current size.
   *
   * Useful at process startup or on a SIGHUP-style external signal.
   *
   * @pre None.
   * @post A new active file has been opened and the previous file archived.
   */
  auto force_rotate() noexcept -> void {
    rotate();
  }

protected:
  auto write_out(record const& r) noexcept -> void override {
    if (m_file == nullptr) {
      return;
    }
    auto const line{default_format(r)};
    // Rotate before the write that would cross the limit, so a record is never
    // split. The size guard lets a single record larger than max_bytes land in
    // a fresh file rather than rotating forever.
    if (m_current_size > 0 && m_current_size + line.size() > m_max_bytes) {
      rotate();
      if (m_file == nullptr) {
        return;  // re-open failed
      }
    }
    nexenne::utility::discard(std::fwrite(line.data(), 1, line.size(), m_file));
    m_current_size += line.size();
  }

  auto flush_out() noexcept -> void override {
    if (m_file != nullptr) {
      nexenne::utility::discard(std::fflush(m_file));
    }
  }

private:
  [[nodiscard]] auto rotated_name(std::size_t const n) const -> std::string {
    return std::format("{}.{}", m_base_path, n);
  }

  auto open_current() noexcept -> void {
    m_file = std::fopen(m_base_path.c_str(), "ab");
    m_current_size = 0;
    if (m_file != nullptr) {
      // Seek to the end to pick up the size of a pre-existing file.
      nexenne::utility::discard(std::fseek(m_file, 0, SEEK_END));
      auto const pos{std::ftell(m_file)};
      m_current_size = pos > 0 ? static_cast<std::size_t>(pos) : 0;
    }
  }

  auto close_current() noexcept -> void {
    if (m_file != nullptr) {
      nexenne::utility::discard(std::fflush(m_file));
      nexenne::utility::discard(std::fclose(m_file));
      m_file = nullptr;
    }
  }

  /// @brief Shifts rotated files down and opens a fresh active log.
  auto rotate() noexcept -> void {
    close_current();
    if (m_max_files > 0) {
      // Drop the oldest backup so the rename chain stays within the cap.
      auto const oldest{rotated_name(m_max_files)};
      nexenne::utility::discard(std::remove(oldest.c_str()));
      // Shift: foo.log.{N-1} -> foo.log.N, down to foo.log.1 -> foo.log.2.
      for (std::size_t i{m_max_files}; i > 1; i = i - 1) {
        auto const src{rotated_name(i - 1)};
        auto const dst{rotated_name(i)};
        nexenne::utility::discard(std::rename(src.c_str(), dst.c_str()));
      }
      // Active foo.log -> foo.log.1.
      nexenne::utility::discard(std::rename(m_base_path.c_str(), rotated_name(1).c_str()));
    } else {
      // max_files == 0 means "truncate" rather than archive.
      nexenne::utility::discard(std::remove(m_base_path.c_str()));
    }
    open_current();
  }

  std::string m_base_path;
  std::size_t m_max_bytes;
  std::size_t m_max_files;
  std::FILE* m_file{nullptr};     ///< Active file handle, or null when closed.
  std::size_t m_current_size{0};  ///< Bytes written to the active file.
};

}  // namespace nexenne::logging
