/**
 * @file
 * @brief gap_buffer as a tiny line editor: type, move the cursor, backspace.
 *
 * Editing clusters around the cursor, so inserting and deleting there is O(1);
 * the cursor move that repositions the gap is the only shift.
 */

#include <print>
#include <string>

#include <nexenne/container/gap_buffer.hpp>

namespace {

namespace cn = nexenne::container;

auto to_string(cn::gap_buffer<char> const& line) -> std::string {
  return std::string(line.begin(), line.end());
}

}  // namespace

auto main() -> int {
  cn::gap_buffer<char> line;
  for (char const ch : {'h', 'e', 'l', 'o'}) {
    line.insert(ch);  // append at the cursor
  }
  std::println("typed:     {}", to_string(line));

  if (line.move_cursor_to(2).has_value()) {  // between 'e' and 'l'
    line.insert('l');                        // fix the typo
  }
  std::println("fixed:     {}", to_string(line));

  if (line.move_cursor_to(line.size()).has_value()) {
    static_cast<void>(line.erase_backward());  // backspace the last char
  }
  std::println("backspace: {}", to_string(line));
  // typed:     helo
  // fixed:     hello
  // backspace: hell
  return 0;
}
