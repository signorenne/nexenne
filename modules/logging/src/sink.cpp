#include <chrono>
#include <cstdio>
#include <format>
#include <string>
#include <string_view>

#include <nexenne/logging/level.hpp>
#include <nexenne/logging/record.hpp>
#include <nexenne/logging/sink.hpp>
#include <nexenne/utility/discard.hpp>

namespace nexenne::logging {

auto sink::default_format(record const& r) -> std::string {
  // Floor to whole seconds for the date-time part, then append the
  // milliseconds by hand: formatting %T on the full-precision time point
  // would already print a fraction, duplicating the sub-second digits. floor
  // (not time_point_cast, which truncates toward zero) keeps the split correct
  // for pre-epoch timestamps.
  auto const tp{r.timestamp};
  auto const tp_sec{std::chrono::floor<std::chrono::seconds>(tp)};
  auto const tp_ms{std::chrono::floor<std::chrono::milliseconds>(tp)};
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

console_sink::console_sink(stream const s) noexcept : m_stream{s} {}

auto console_sink::write_out(record const& r) noexcept -> void {
  auto const line{default_format(r)};
  auto* const out{pick_stream(r.severity)};
  // fwrite is the smallest portable atomic write for FILE*; glibc serialises
  // a full fwrite call.
  nexenne::utility::discard(std::fwrite(line.data(), 1, line.size(), out));
}

auto console_sink::flush_out() noexcept -> void {
  nexenne::utility::discard(std::fflush(stdout));
  nexenne::utility::discard(std::fflush(stderr));
}

auto console_sink::pick_stream(level const sev) const noexcept -> std::FILE* {
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

file_sink::file_sink(std::string_view const path) noexcept {
  m_file = std::fopen(std::string{path}.c_str(), "ab");
}

file_sink::~file_sink() noexcept {
  close();
}

auto file_sink::is_open() const noexcept -> bool {
  return m_file != nullptr;
}

auto file_sink::write_out(record const& r) noexcept -> void {
  if (m_file == nullptr) {
    return;
  }
  auto const line{default_format(r)};
  nexenne::utility::discard(std::fwrite(line.data(), 1, line.size(), m_file));
}

auto file_sink::flush_out() noexcept -> void {
  if (m_file != nullptr) {
    nexenne::utility::discard(std::fflush(m_file));
  }
}

auto file_sink::close() noexcept -> void {
  if (m_file != nullptr) {
    nexenne::utility::discard(std::fflush(m_file));
    nexenne::utility::discard(std::fclose(m_file));
    m_file = nullptr;
  }
}

}  // namespace nexenne::logging
