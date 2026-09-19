/**
 * @file
 * @brief Out-of-line parts of the log record.
 */

#include <string>
#include <thread>
#include <version>

#include <nexenne/logging/record.hpp>

// P2693 formatter<thread::id> is libstdc++ 15+ only; the C++11 inserter is the fallback.
#if defined(__cpp_lib_formatters) && __cpp_lib_formatters >= 202302L
#include <format>
#else
#include <sstream>
#endif

namespace nexenne::logging::detail {

auto thread_id_to_string(std::thread::id const id) -> std::string {
#if defined(__cpp_lib_formatters) && __cpp_lib_formatters >= 202302L
  return std::format("{}", id);
#else
  auto out{std::ostringstream{}};
  out << id;
  return out.str();
#endif
}

}  // namespace nexenne::logging::detail
