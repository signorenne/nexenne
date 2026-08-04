#include <string>
#include <thread>
#include <version>

#include <nexenne/logging/record.hpp>

// P2693 added std::formatter<std::thread::id> in C++23, but libstdc++ only
// ships it from GCC 15, and a clang build against an older libstdc++ (the
// stock toolchain on Ubuntu 24.04, which CI's clang job uses) has no formatter
// for the type at all. The stream inserter is guaranteed since C++11, so it is
// the portable fallback.
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
