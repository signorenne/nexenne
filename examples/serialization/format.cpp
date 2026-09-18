/**
 * @file
 * @brief Printing the nexenne::serialization types via format.hpp.
 *
 * The codec headers stay free of <format>; format.hpp adds a std::formatter, a
 * to_string and an operator<< for each type a caller gets back and may want to
 * show, all printing the same text. This prints a parsed document, a parse
 * failure with its location, a codec error, a versioned header and a peeked
 * CBOR token kind.
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <print>

#include <nexenne/serialization/cbor.hpp>
#include <nexenne/serialization/format.hpp>
#include <nexenne/serialization/json/parse.hpp>
#include <nexenne/serialization/versioned.hpp>

namespace {

namespace ser = nexenne::serialization;
namespace json = nexenne::serialization::json;

}  // namespace

auto main() -> int {
  if (auto const doc{json::parse(R"({"name":"probe","rates":[10,20]})")}) {
    std::cout << "document: " << *doc << '\n';
    std::println("kind:     {}", doc->type());
  }
  if (auto const bad{json::parse("{\"name\": \"open string")}; !bad) {
    std::println("failure:  {}", bad.error());
  }

  std::println("error:    [{:>16}]", ser::error::buffer_full);
  std::cout << "header:   " << ser::header{.magic = 0x4E455801U, .version = 2} << '\n';

  auto buf{std::array<std::byte, 16>{}};
  auto w{ser::cbor::writer{buf}};
  if (w.write_string("hi")) {
    auto r{ser::cbor::reader{w.written()}};
    if (auto const t{r.peek_type()}) {
      std::println("cbor:     {}", *t);
    }
  }
  return 0;
}
