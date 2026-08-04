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
#include <mutex>
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
  rotating_file_sink(std::string_view path, std::size_t max_bytes, std::size_t max_files);

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
  ~rotating_file_sink() noexcept override;

  /**
   * @brief Whether the active file is open.
   *
   * @return \c true if the underlying handle is valid.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto is_open() const noexcept -> bool;

  /**
   * @brief Bytes written to the current active file.
   *
   * @return The running byte count for the current, not yet rotated, file.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto current_size() const noexcept -> std::size_t;

  /**
   * @brief Base path of the active log file.
   *
   * @return A view of the base path, valid for the sink's lifetime.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto base_path() const noexcept -> std::string_view;

  /**
   * @brief Forces an immediate rotation regardless of current size.
   *
   * Useful at process startup or on a SIGHUP-style external signal. Takes the
   * internal mutex, so it is safe to call from a thread other than the one
   * driving writes: it will not race the backend's \c write_out or \c flush_out.
   *
   * @pre None.
   * @post A new active file has been opened and the previous file archived.
   *
   * @warning Do not call from an actual signal handler: it runs \c stdio and
   *          allocation, which are not async-signal-safe. A SIGHUP handler should
   *          set a flag the owning thread observes and then calls this.
   */
  auto force_rotate() noexcept -> void;

protected:
  /**
   * @brief Writes \p r to the active file, rotating first if it would overflow.
   *
   * Formats the record, rotates when the running size plus the line would cross
   * \c max_bytes (so a record is never split), then appends the line and bumps
   * the running counter. A null handle or a failed re-open after rotation makes
   * the call a no-op.
   *
   * @param r Record to write.
   *
   * @pre None.
   * @post The line has been appended and \c current_size() updated, unless the
   *       file is closed.
   */
  auto write_out(record const& r) noexcept -> void override;

  /**
   * @brief Flushes the active file under the sink mutex.
   *
   * @pre None.
   * @post Any buffered bytes have been flushed to the active file.
   */
  auto flush_out() noexcept -> void override;

private:
  /**
   * @brief Builds the path of the \p n-th rotated backup.
   *
   * @param n One-based backup index.
   *
   * @return The path \c base_path().n.
   *
   * @pre None.
   * @post None.
   * @throws std::bad_alloc if the result string cannot be allocated.
   *
   * @complexity \c O(|base_path|).
   */
  [[nodiscard]] auto rotated_name(std::size_t n) const -> std::string;

  /**
   * @brief Opens the active file in append mode and seeds the size counter.
   *
   * Seeks to the end of an existing file so a restart keeps the rotation
   * budget. Leaves \c m_file null on a failed open.
   *
   * @pre None.
   * @post \c m_file is the open handle or null, and \c m_current_size reflects
   *       the existing file size.
   */
  auto open_current() noexcept -> void;

  /**
   * @brief Flushes and closes the active file if one is open.
   *
   * @pre None.
   * @post \c m_file is null.
   */
  auto close_current() noexcept -> void;

  /**
   * @brief Shifts rotated files down and opens a fresh active log.
   *
   * Closes the active file, deletes the oldest backup beyond \c max_files,
   * renames each backup one step down, moves the active file to \c .1, and
   * opens a new empty active file. With \c max_files of 0 the active file is
   * truncated rather than archived.
   *
   * @pre None.
   * @post A fresh active file is open and the previous generations have shifted.
   */
  auto rotate() noexcept -> void;

  // Guards m_file and m_current_size against a force_rotate from another thread
  // racing the backend's write_out/flush_out, per the sink cross-thread contract.
  mutable std::mutex m_mutex;
  std::string m_base_path;
  std::size_t m_max_bytes;
  std::size_t m_max_files;
  std::FILE* m_file{nullptr};     ///< Active file handle, or null when closed.
  std::size_t m_current_size{0};  ///< Bytes written to the active file.
};

}  // namespace nexenne::logging
