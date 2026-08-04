#include <cstddef>
#include <cstdio>
#include <format>
#include <mutex>
#include <string>
#include <string_view>

#include <nexenne/logging/record.hpp>
#include <nexenne/logging/rotating_file_sink.hpp>
#include <nexenne/utility/discard.hpp>

namespace nexenne::logging {

rotating_file_sink::rotating_file_sink(
  std::string_view const path, std::size_t const max_bytes, std::size_t const max_files
)
    : m_base_path{path}, m_max_bytes{max_bytes}, m_max_files{max_files} {
  open_current();
}

rotating_file_sink::~rotating_file_sink() noexcept {
  close_current();
}

auto rotating_file_sink::is_open() const noexcept -> bool {
  auto const guard{std::lock_guard{m_mutex}};
  return m_file != nullptr;
}

auto rotating_file_sink::current_size() const noexcept -> std::size_t {
  auto const guard{std::lock_guard{m_mutex}};
  return m_current_size;
}

auto rotating_file_sink::base_path() const noexcept -> std::string_view {
  return m_base_path;
}

auto rotating_file_sink::force_rotate() noexcept -> void {
  auto const guard{std::lock_guard{m_mutex}};
  rotate();
}

auto rotating_file_sink::write_out(record const& r) noexcept -> void {
  auto const guard{std::lock_guard{m_mutex}};
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

auto rotating_file_sink::flush_out() noexcept -> void {
  auto const guard{std::lock_guard{m_mutex}};
  if (m_file != nullptr) {
    nexenne::utility::discard(std::fflush(m_file));
  }
}

auto rotating_file_sink::rotated_name(std::size_t const n) const -> std::string {
  return std::format("{}.{}", m_base_path, n);
}

auto rotating_file_sink::open_current() noexcept -> void {
  m_file = std::fopen(m_base_path.c_str(), "ab");
  m_current_size = 0;
  if (m_file != nullptr) {
    // Seek to the end to pick up the size of a pre-existing file.
    nexenne::utility::discard(std::fseek(m_file, 0, SEEK_END));
    auto const pos{std::ftell(m_file)};
    m_current_size = pos > 0 ? static_cast<std::size_t>(pos) : 0;
  }
}

auto rotating_file_sink::close_current() noexcept -> void {
  if (m_file != nullptr) {
    nexenne::utility::discard(std::fflush(m_file));
    nexenne::utility::discard(std::fclose(m_file));
    m_file = nullptr;
  }
}

auto rotating_file_sink::rotate() noexcept -> void {
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

}  // namespace nexenne::logging
