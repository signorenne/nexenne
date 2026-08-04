#include <chrono>
#include <cstddef>
#include <ctime>
#include <format>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>

#include <nexenne/logging/level.hpp>
#include <nexenne/logging/pattern_formatter.hpp>
#include <nexenne/logging/record.hpp>
#include <nexenne/utility/discard.hpp>

namespace nexenne::logging {

auto pattern_formatter::basename_of(std::string_view const path) noexcept -> std::string_view {
  auto const pos{path.find_last_of("/\\")};
  return pos == std::string_view::npos ? path : path.substr(pos + 1);
}

auto pattern_formatter::level_short(level const l) noexcept -> char {
  switch (l) {
    case level::trace:
      return 'T';
    case level::debug:
      return 'D';
    case level::info:
      return 'I';
    case level::warn:
      return 'W';
    case level::error:
      return 'E';
    case level::critical:
      return 'C';
    case level::off:
      return '-';
  }
  return '?';
}

auto pattern_formatter::append_time(
  std::string& out, std::chrono::system_clock::time_point const t, bool const include_date
) -> void {
  // floor (not to_time_t / duration_cast, which truncate toward zero) so the
  // seconds and the millisecond remainder agree and stay non-negative for
  // pre-epoch timestamps.
  auto const secs{std::chrono::floor<std::chrono::seconds>(t)};
  auto const tt{std::chrono::system_clock::to_time_t(secs)};
  std::tm tm{};
#ifdef _WIN32
  nexenne::utility::discard(gmtime_s(&tm, &tt));
#else
  nexenne::utility::discard(gmtime_r(&tt, &tm));
#endif
  auto const ms{(std::chrono::floor<std::chrono::milliseconds>(t) - secs).count()};
  if (include_date) {
    std::format_to(
      std::back_inserter(out),
      "{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:03}Z",
      tm.tm_year + 1900,
      tm.tm_mon + 1,
      tm.tm_mday,
      tm.tm_hour,
      tm.tm_min,
      tm.tm_sec,
      ms
    );
  } else {
    std::format_to(
      std::back_inserter(out), "{:02}:{:02}:{:02}.{:03}", tm.tm_hour, tm.tm_min, tm.tm_sec, ms
    );
  }
}

pattern_formatter::pattern_formatter(std::string pattern) : m_pattern{std::move(pattern)} {}

auto pattern_formatter::set_pattern(std::string pattern) noexcept -> void {
  m_pattern = std::move(pattern);
}

auto pattern_formatter::pattern() const noexcept -> std::string const& {
  return m_pattern;
}

auto pattern_formatter::format(record const& r, std::string& out) const -> void {
  for (std::size_t i{0}; i < m_pattern.size(); ++i) {
    auto const c{m_pattern[i]};
    if (c != '%' || i + 1 >= m_pattern.size()) {
      out.push_back(c);
      continue;
    }
    ++i;
    switch (m_pattern[i]) {
      case 't':
        append_time(out, r.timestamp, true);
        break;
      case 'T':
        append_time(out, r.timestamp, false);
        break;
      case 'l':
        out += to_token(r.severity);
        break;
      case 'L':
        out.push_back(level_short(r.severity));
        break;
      case 'n':
        out += r.logger_name;
        break;
      case 'm':
        out += r.message;
        break;
      case 'f': {
        auto const* const file{r.location.file_name()};
        out += file != nullptr ? basename_of(file) : std::string_view{"?"};
        break;
      }
      case '#':
        std::format_to(std::back_inserter(out), "{}", r.location.line());
        break;
      case 's': {
        auto const* const fn{r.location.function_name()};
        out += fn != nullptr ? std::string_view{fn} : std::string_view{"?"};
        break;
      }
      case 'o':
        out += detail::thread_id_to_string(r.thread_id);
        break;
      case '%':
        out.push_back('%');
        break;
      default:
        out.push_back('%');
        out.push_back(m_pattern[i]);
        break;
    }
  }
}

auto pattern_formatter::format(record const& r) const -> std::string {
  auto out{std::string{}};
  out.reserve(64 + r.message.size());
  format(r, out);
  return out;
}

}  // namespace nexenne::logging
