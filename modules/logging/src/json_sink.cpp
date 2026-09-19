/**
 * @file
 * @brief Out-of-line parts of the JSON-lines sink.
 */

#include <chrono>
#include <cstdio>
#include <format>
#include <string>
#include <string_view>

#include <nexenne/logging/json_sink.hpp>
#include <nexenne/logging/level.hpp>
#include <nexenne/logging/record.hpp>
#include <nexenne/utility/ignore.hpp>

namespace nexenne::logging {

json_sink::json_sink(std::string_view const path) noexcept : m_owns_file{true} {
  m_file = std::fopen(std::string{path}.c_str(), "ab");
}

json_sink::json_sink(std::FILE* const out) noexcept : m_file{out} {}

json_sink::~json_sink() noexcept {
  if (m_owns_file && m_file != nullptr) {
    nexenne::utility::ignore(std::fflush(m_file));
    nexenne::utility::ignore(std::fclose(m_file));
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
  line += to_token(r.severity);

  line += R"(","logger":")";
  append_escaped(line, r.logger_name);

  line += R"(","file":")";
  auto const* const file{r.location.file_name() != nullptr ? r.location.file_name() : "?"};
  append_escaped(line, file);

  line += std::format(R"(","line":{})", r.location.line());

  line += R"(,"tid":")";
  append_escaped(line, detail::thread_id_to_string(r.thread_id));
  line += R"(")";

  line += R"(,"msg":")";
  append_escaped(line, r.message);
  line += R"("})";
  line += '\n';

  nexenne::utility::ignore(std::fwrite(line.data(), 1, line.size(), m_file));
}

auto json_sink::flush_out() noexcept -> void {
  if (m_file != nullptr) {
    nexenne::utility::ignore(std::fflush(m_file));
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
  // %T on whole seconds avoids a duplicate fraction; floor, unlike a cast, is right pre-epoch.
  auto const tp_ms{std::chrono::floor<std::chrono::milliseconds>(tp)};
  auto const tp_sec{std::chrono::floor<std::chrono::seconds>(tp_ms)};
  auto const ms_part{(tp_ms - tp_sec).count()};
  return std::format("{:%FT%T}.{:03}Z", tp_sec, ms_part);
}

}  // namespace nexenne::logging
