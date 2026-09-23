#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <format>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <nexenne/algorithm/checksum/crc.hpp>
#include <nexenne/algorithm/checksum/modular_sum.hpp>
#include <nexenne/algorithm/encoding/alphabet.hpp>
#include <nexenne/algorithm/encoding/base_n.hpp>
#include <nexenne/algorithm/encoding/codec_error.hpp>
#include <nexenne/algorithm/format.hpp>
#include <nexenne/algorithm/graph/lca.hpp>
#include <nexenne/algorithm/hash/fnv.hpp>
#include <nexenne/algorithm/hash/xxhash.hpp>
#include <nexenne/algorithm/numerical/interpolation.hpp>
#include <nexenne/algorithm/numerical/numerical_error.hpp>
#include <nexenne/algorithm/numerical/online_stats.hpp>
#include <nexenne/algorithm/string/aho_corasick.hpp>

namespace algorithm = nexenne::algorithm;

TEST_CASE("std::format on codec_error matches to_string") {
  CHECK(
    std::format("{}", algorithm::codec_error::invalid_input)
    == algorithm::to_string(algorithm::codec_error::invalid_input)
  );
  CHECK(
    std::format("{}", algorithm::codec_error::buffer_too_small)
    == algorithm::to_string(algorithm::codec_error::buffer_too_small)
  );
}

TEST_CASE("std::format on numerical_error matches to_string") {
  CHECK(
    std::format("{}", algorithm::numerical_error::not_bracketed)
    == algorithm::to_string(algorithm::numerical_error::not_bracketed)
  );
  CHECK(
    std::format("{}", algorithm::numerical_error::no_convergence)
    == algorithm::to_string(algorithm::numerical_error::no_convergence)
  );
}

TEST_CASE("operator<< on the error enums prints the to_string name") {
  auto os{std::ostringstream{}};
  os << algorithm::codec_error::buffer_too_small << ' '
     << algorithm::numerical_error::no_convergence;
  CHECK(
    os.str()
    == std::format(
      "{} {}",
      algorithm::to_string(algorithm::codec_error::buffer_too_small),
      algorithm::to_string(algorithm::numerical_error::no_convergence)
    )
  );
}

TEST_CASE("std::format on codec_error covers incomplete_input") {
  CHECK(
    std::format("{}", algorithm::codec_error::incomplete_input)
    == algorithm::to_string(algorithm::codec_error::incomplete_input)
  );
}

TEST_CASE("std::format honours width and alignment specs on the error name") {
  CHECK(std::format("{:>16}", algorithm::codec_error::invalid_input) == "   invalid_input");
}

TEST_CASE("format of a_star_result agrees across the three layers") {
  auto const r{algorithm::a_star_result<std::uint32_t, int>{.path = {0, 1, 2}, .cost = 5}};
  CHECK(algorithm::to_string(r) == "a_star_result(path=[0, 1, 2], cost=5)");
  CHECK(std::format("{}", r) == algorithm::to_string(r));
  auto os{std::ostringstream{}};
  os << r;
  CHECK(os.str() == algorithm::to_string(r));
}

TEST_CASE("format of scc_result agrees across the three layers") {
  auto const r{algorithm::scc_result<std::uint32_t>{.labels = {0, 0, 1}, .num_components = 2}};
  CHECK(algorithm::to_string(r) == "scc_result(labels=[0, 0, 1], num_components=2)");
  CHECK(std::format("{}", r) == algorithm::to_string(r));
  auto os{std::ostringstream{}};
  os << r;
  CHECK(os.str() == algorithm::to_string(r));
}

TEST_CASE("format of components_result agrees across the three layers") {
  auto const r{
    algorithm::components_result<void, std::uint32_t>{.labels = {0, 1, 1}, .num_components = 2}
  };
  CHECK(algorithm::to_string(r) == "components_result(labels=[0, 1, 1], num_components=2)");
  CHECK(std::format("{}", r) == algorithm::to_string(r));
  auto os{std::ostringstream{}};
  os << r;
  CHECK(os.str() == algorithm::to_string(r));
}

TEST_CASE("format of floyd_warshall_result agrees across the three layers") {
  auto const r{
    algorithm::floyd_warshall_result<std::uint32_t, int>{.distances = {0, 1, 1, 0}, .n = 2}
  };
  CHECK(algorithm::to_string(r) == "floyd_warshall_result(n=2, distances=[0, 1, 1, 0])");
  CHECK(std::format("{}", r) == algorithm::to_string(r));
  auto os{std::ostringstream{}};
  os << r;
  CHECK(os.str() == algorithm::to_string(r));
}

TEST_CASE("format of mst_edge agrees across the three layers") {
  auto const e{algorithm::mst_edge<int, std::uint32_t>{.from = 0, .to = 1, .weight = 7}};
  CHECK(algorithm::to_string(e) == "mst_edge(from=0, to=1, weight=7)");
  CHECK(std::format("{}", e) == algorithm::to_string(e));
  auto os{std::ostringstream{}};
  os << e;
  CHECK(os.str() == algorithm::to_string(e));
}

TEST_CASE("std::format honours width and alignment specs on an mst_edge") {
  auto const e{algorithm::mst_edge<int, std::uint32_t>{.from = 3, .to = 4, .weight = 9}};
  CHECK(std::format("{}", e) == "mst_edge(from=3, to=4, weight=9)");
}

TEST_CASE("format of crc_spec agrees across the three layers") {
  auto const spec{algorithm::crc16_modbus_spec};
  CHECK(
    std::format("{}", spec)
    == "crc_spec(width=16, poly=0x8005, init=0xffff, ref_in=true, ref_out=true, xor_out=0x0000)"
  );
  CHECK(algorithm::to_string(spec) == std::format("{}", spec));
  auto os{std::ostringstream{}};
  os << spec;
  CHECK(os.str() == algorithm::to_string(spec));
}

TEST_CASE("format of modular_sum_spec agrees across the three layers") {
  auto const spec{algorithm::adler32_spec};
  CHECK(
    std::format("{}", spec) == "modular_sum_spec(unit_bytes=1, sum_bits=16, modulus=65521, init1=1)"
  );
  CHECK(algorithm::to_string(spec) == std::format("{}", spec));
  auto os{std::ostringstream{}};
  os << spec;
  CHECK(os.str() == algorithm::to_string(spec));
}

TEST_CASE("format of codec_alphabet agrees across the three layers") {
  auto const alphabet{algorithm::base16_lower_spec.alphabet};
  CHECK(std::format("{}", alphabet) == "codec_alphabet(\"0123456789abcdef\")");
  CHECK(algorithm::to_string(alphabet) == std::format("{}", alphabet));
  auto os{std::ostringstream{}};
  os << alphabet;
  CHECK(os.str() == algorithm::to_string(alphabet));
}

TEST_CASE("format of base_n_spec agrees across the three layers") {
  auto const spec{algorithm::base16_upper_spec};
  CHECK(
    std::format("{}", spec)
    == "base_n_spec(symbols=16, alphabet=\"0123456789ABCDEF\", padded=false, pad='=', "
       "case_insensitive=true)"
  );
  CHECK(algorithm::to_string(spec) == std::format("{}", spec));
  auto os{std::ostringstream{}};
  os << spec;
  CHECK(os.str() == algorithm::to_string(spec));
}

TEST_CASE("format of crc_ctx prints the running CRC as width-padded hex") {
  auto c{algorithm::crc_ctx<algorithm::crc32_ieee_spec>{}};
  c.update(std::string_view{"123456789"});
  CHECK(std::format("{}", c) == "crc_ctx(value=0xcbf43926)");
  CHECK(algorithm::to_string(c) == std::format("{}", c));
  auto os{std::ostringstream{}};
  os << c;
  CHECK(os.str() == algorithm::to_string(c));

  auto const fresh{algorithm::crc_ctx<algorithm::crc16_xmodem_spec>{}};
  CHECK(std::format("{}", fresh) == "crc_ctx(value=0x0000)");
}

TEST_CASE("format of fnv1a_ctx prints the running hash as width-padded hex") {
  auto c{algorithm::fnv1a_ctx<32>{}};
  c.update(std::string_view{"a"});
  CHECK(std::format("{}", c) == "fnv1a_ctx(value=0xe40c292c)");
  CHECK(algorithm::to_string(c) == std::format("{}", c));
  auto os{std::ostringstream{}};
  os << c;
  CHECK(os.str() == algorithm::to_string(c));

  auto const fresh{algorithm::fnv1a_ctx<64>{}};
  CHECK(std::format("{}", fresh) == "fnv1a_ctx(value=0xcbf29ce484222325)");
}

TEST_CASE("format of xxhash_ctx prints the digest as width-padded hex") {
  auto const c{algorithm::xxhash_ctx<32>{}};
  CHECK(std::format("{}", c) == "xxhash_ctx(value=0x02cc5d05)");
  CHECK(algorithm::to_string(c) == std::format("{}", c));
  auto os{std::ostringstream{}};
  os << c;
  CHECK(os.str() == algorithm::to_string(c));

  auto const wide{algorithm::xxhash_ctx<64>{}};
  CHECK(std::format("{}", wide) == "xxhash_ctx(value=0xef46db3751d8e999)");
}

TEST_CASE("format of running_stats agrees across the three layers") {
  auto s{algorithm::running_stats<double>{}};
  CHECK(std::format("{}", s) == "running_stats(count=0, mean=0, stddev=0, min=0, max=0)");
  s.push(1.0);
  s.push(3.0);
  CHECK(std::format("{}", s) == "running_stats(count=2, mean=2, stddev=1, min=1, max=3)");
  CHECK(algorithm::to_string(s) == std::format("{}", s));
  auto os{std::ostringstream{}};
  os << s;
  CHECK(os.str() == algorithm::to_string(s));
}

TEST_CASE("format of histogram agrees across the three layers") {
  auto h{algorithm::histogram<double, 4>{0.0, 4.0}};
  h.push(-1.0);
  h.push(0.5);
  h.push(1.5);
  h.push(1.75);
  h.push(9.0);
  CHECK(
    std::format("{}", h) == "histogram(total=5, underflow=1, overflow=1, buckets=[1, 2, 0, 0])"
  );
  CHECK(algorithm::to_string(h) == std::format("{}", h));
  auto os{std::ostringstream{}};
  os << h;
  CHECK(os.str() == algorithm::to_string(h));
}

TEST_CASE("format of ema_stats agrees across the three layers") {
  auto s{algorithm::ema_stats<double>{0.5}};
  s.push(2.0);
  s.push(4.0);
  CHECK(std::format("{}", s) == "ema_stats(mean=3, stddev=1)");
  CHECK(algorithm::to_string(s) == std::format("{}", s));
  auto os{std::ostringstream{}};
  os << s;
  CHECK(os.str() == algorithm::to_string(s));
}

TEST_CASE("format of linear_interpolator prints its knot count and domain") {
  auto const xs{std::array<double, 3>{0.0, 1.0, 2.0}};
  auto const ys{std::array<double, 3>{0.0, 10.0, 20.0}};
  auto const f{algorithm::linear_interpolator<double>{xs, ys}};
  CHECK(std::format("{}", f) == "linear_interpolator(knots=3, domain=[0, 2])");
  CHECK(algorithm::to_string(f) == std::format("{}", f));
  auto os{std::ostringstream{}};
  os << f;
  CHECK(os.str() == algorithm::to_string(f));

  auto const none{std::span<double const>{}};
  auto const empty{algorithm::linear_interpolator<double>{none, none}};
  CHECK(std::format("{}", empty) == "linear_interpolator(knots=0, domain=[])");
}

TEST_CASE("format of cubic_spline prints its knot count and domain") {
  auto const xs{std::array<double, 4>{0.0, 0.5, 1.5, 3.0}};
  auto const ys{std::array<double, 4>{1.0, 2.0, 0.0, 4.0}};
  auto const f{algorithm::cubic_spline<double>{xs, ys}};
  CHECK(std::format("{}", f) == "cubic_spline(knots=4, domain=[0, 3])");
  CHECK(algorithm::to_string(f) == std::format("{}", f));
  auto os{std::ostringstream{}};
  os << f;
  CHECK(os.str() == algorithm::to_string(f));
}

TEST_CASE("format of aho_corasick prints its pattern and node counts") {
  auto m{algorithm::aho_corasick{}};
  CHECK(std::format("{}", m) == "aho_corasick(patterns=0, nodes=1)");
  m.add_pattern("he");
  m.add_pattern("she");
  m.add_pattern("his");
  m.add_pattern("hers");
  m.build();
  CHECK(std::format("{}", m) == "aho_corasick(patterns=4, nodes=10)");
  CHECK(algorithm::to_string(m) == std::format("{}", m));
  auto os{std::ostringstream{}};
  os << m;
  CHECK(os.str() == algorithm::to_string(m));
}

TEST_CASE("format of lca prints its node count") {
  auto index{algorithm::lca<std::int32_t>{}};
  CHECK(std::format("{}", index) == "lca(nodes=0)");
  auto const parent{std::vector<std::int32_t>{0, 0, 0, 1, 1}};
  index.build(std::span<std::int32_t const>{parent}, 0);
  CHECK(std::format("{}", index) == "lca(nodes=5)");
  CHECK(algorithm::to_string(index) == std::format("{}", index));
  auto os{std::ostringstream{}};
  os << index;
  CHECK(os.str() == algorithm::to_string(index));
}
