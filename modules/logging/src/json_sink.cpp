#include <chrono>
#include <cstdio>
#include <format>
#include <string>
#include <string_view>

#include <nexenne/logging/json_sink.hpp>
#include <nexenne/logging/level.hpp>
#include <nexenne/logging/record.hpp>
#include <nexenne/utility/discard.hpp>

namespace nexenne::logging {

json_sink::json_sink(std::string_view const path) noexcept : m_owns_file{true} {
  m_file = std::fopen(std::string{path}.c_str(), "ab");
}

json_sink::json_sink(std::FILE* const out) noexcept : m_file{out} {}

json_sink::~json_sink() noexcept {
  if (m_owns_file && m_file != nullptr) {
    nexenne::utility::discard(std::fflush(m_file));
    nexenne::utility::discard(std::fclose(m_file));
  }
}

auto json_sink::is_open() const noexcept -> bool {
  return m_file != nullptr;
}

auto json_sink::write_out(record const& r) noexcept -> void {
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
  // a bare integer, so quoting keeps the field valid JSON everywhere, and
  // escaping it keeps an exotic form from breaking out of the field.
  line += R"(,"tid":")";
  append_escaped(line, detail::thread_id_to_string(r.thread_id));
  line += R"(")";

  line += R"(,"msg":")";
  append_escaped(line, r.message);
  line += R"("})";
  line += '\n';

  nexenne::utility::discard(std::fwrite(line.data(), 1, line.size(), m_file));
}

auto json_sink::flush_out() noexcept -> void {
  if (m_file != nullptr) {
    nexenne::utility::discard(std::fflush(m_file));
  }
}

auto json_sink::append_escaped(std::string& out, std::string_view const s) -> void {
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

auto json_sink::format_timestamp(std::chrono::system_clock::time_point const tp) -> std::string {
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

}  // namespace nexenne::logging
