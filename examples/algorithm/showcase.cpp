/**
 * @file
 * @brief A guided tour of nexenne::algorithm: a tiny asset-pack build pipeline.
 *
 * Pretend we are the build step of a game or an embedded firmware image. We are
 * handed a set of asset records, and we must turn them into a deterministic,
 * verifiable, query-able pack. Nothing is drawn or written to disk; every number
 * a real packer would compute is computed here and printed, so you can see how
 * the module's facilities fit together in one cohesive job. Library calls never
 * throw: the fallible ones return expected / optional, and we handle every one.
 *
 * The program walks seven steps, each with why its algorithm is the right tool
 * and what it costs:
 *
 * 1. Ingest and sort. The pack table must be sorted by id so a reader can
 *    binary-search it. The ids are sparse content handles, not array indices,
 *    held as 32-bit unsigned keys, so radix_sort beats a comparison sort: four
 *    stable byte passes, O(W * N), and no element compares at all. It sorts the
 *    keys, and the seven records are then gathered in id order by a plain
 *    selection, clearer at this size than threading a permutation.
 * 2. Lookups. find_sorted is the general O(log N) workhorse (lower_bound plus an
 *    equality confirm) for a key that could sit anywhere; exponential_search
 *    gallops from the front, so its cost scales with the key's distance from
 *    index 0 (id 9 is the first element); interpolation_search predicts the
 *    probe from the key's position in the value range, O(log log N) on roughly
 *    uniform numeric data. Each returns a found_index that addresses the table
 *    directly.
 * 3. Integrity: two jobs, two tools. CRC-32C (Castagnoli) over the id and size
 *    columns catches accidental corruption, a flipped bit on a flash read or a
 *    UART link: a cheap, table-driven linear checksum, not a fingerprint.
 *    xxHash64 over the names (a stand-in for the contents) is the fingerprint:
 *    fast and well spread, not cryptographic, fit for a cache key or a "did
 *    this asset change?" check across builds.
 * 4. Wire format for the 8-byte digest, the CRC then a fingerprint prefix. Hex
 *    is the human-readable form for a log line or a file name; base64url is
 *    about 33% denser and safe in a URL or a JSON field; COBS frames the raw
 *    bytes for a UART or RS-485 link, removing every 0x00 so a lone 0x00 can
 *    delimit packets, at most one extra byte per 254 and with no heap.
 * 5. Metadata search over the joined names. kmp_find locates one fixed
 *    substring in O(N + M) with no backtracking; z_find_all reports every
 *    occurrence in one linear pass, here counting the ".anim" clips;
 *    levenshtein edit distance, O(N * M), drives a "did you mean?" for a
 *    mistyped "tilesett.png".
 * 6. Build order. Assets reference one another (the run animation reuses the
 *    idle skeleton, both atlases share a tileset), modelled as edges from each
 *    dependency to its users, weighted by the dependency's byte size.
 *    topological_sort lists every dependency ahead of its users in O(V + E) and
 *    reports a cycle (a circular dependency is a build bug) as an error;
 *    dijkstra, O((V + E) log V), then totals the bytes loaded along tileset,
 *    hero_idle, hero_run.
 * 7. Statistics. running_stats (Welford) keeps the size mean, stddev, min and
 *    max in one numerically stable O(1)-per-sample pass, never holding the
 *    whole list; neumaier_sum totals the sizes by compensated summation, equal
 *    to a plain sum for a few integers, a habit that pays off at scale.
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <nexenne/algorithm/binary_search.hpp>
#include <nexenne/algorithm/checksum/crc.hpp>
#include <nexenne/algorithm/encoding/base_n.hpp>
#include <nexenne/algorithm/encoding/cobs.hpp>
#include <nexenne/algorithm/graph/dijkstra.hpp>
#include <nexenne/algorithm/graph/topological_sort.hpp>
#include <nexenne/algorithm/hash/xxhash.hpp>
#include <nexenne/algorithm/numerical/kahan_sum.hpp>
#include <nexenne/algorithm/numerical/online_stats.hpp>
#include <nexenne/algorithm/sort/radix_sort.hpp>
#include <nexenne/algorithm/string/kmp.hpp>
#include <nexenne/algorithm/string/levenshtein.hpp>
#include <nexenne/algorithm/string/z_function.hpp>
#include <nexenne/container/graph.hpp>
#include <nexenne/utility/ignore.hpp>

namespace alg = nexenne::algorithm;
namespace nc = nexenne::container;

namespace {

/// @brief One asset record in the pack.
struct asset {
  std::uint32_t id{};     ///< Stable content handle, sparse rather than an array index.
  std::string_view name;  ///< File name inside the pack.
  std::uint32_t size{};   ///< Uncompressed byte count.
};

}  // namespace

auto main() -> int {
  std::array<asset, 7> assets{
    asset{4096, "hero_idle.anim", 12'880},
    asset{17, "title.ogg", 220'400},
    asset{512, "tileset.png", 65'536},
    asset{4097, "hero_run.anim", 13'120},
    asset{9, "ui_atlas.png", 48'000},
    asset{1024, "ambient.ogg", 980'200},
    asset{256, "font.ttf", 30'720},
  };

  std::println("== 1. Ingest and sort by id ==");
  std::array<std::uint32_t, 7> ids{};
  for (std::size_t i{0}; i < assets.size(); ++i) {
    ids[i] = assets[i].id;
  }
  alg::radix_sort(std::span<std::uint32_t>{ids});

  std::array<asset, 7> table{};
  for (std::size_t i{0}; i < ids.size(); ++i) {
    for (auto const& a : assets) {
      if (a.id == ids[i]) {
        table[i] = a;
        break;
      }
    }
  }
  for (auto const& a : table) {
    std::println("  id {:>5}  {:<16} {:>7} bytes", a.id, a.name, a.size);
  }

  std::println("== 2. Lookups ==");

  if (auto const at{alg::find_sorted(ids, 1024u)}) {
    std::println("  find_sorted(1024)         -> table[{}] = {}", *at, table[*at].name);
  }

  if (auto const at{alg::exponential_search(ids, 9u)}) {
    std::println("  exponential_search(9)     -> table[{}] = {}", *at, table[*at].name);
  }

  if (!alg::interpolation_search(ids, 1000u).has_value()) {
    std::println("  interpolation_search(1000)-> not found (no asset has id 1000)");
  }

  std::println("== 3. Integrity ==");
  alg::crc_ctx<alg::crc32c_spec> crc;
  for (auto const& a : table) {
    std::array<std::uint8_t, 8> row{};
    for (int b{0}; b < 4; ++b) {
      row[static_cast<std::size_t>(b)] = static_cast<std::uint8_t>((a.id >> (8 * b)) & 0xFFu);
      row[static_cast<std::size_t>(b + 4)] = static_cast<std::uint8_t>((a.size >> (8 * b)) & 0xFFu);
    }
    crc.update(std::span<std::uint8_t const>{row});
  }
  std::uint32_t const table_crc{crc.value()};
  std::println("  crc32c(table)             = 0x{:08x}", table_crc);

  std::string blob;
  for (auto const& a : table) {
    blob += a.name;
  }
  std::uint64_t const fingerprint{alg::xxhash<64>(blob)};
  std::println("  xxhash64(contents)        = 0x{:016x}", fingerprint);

  std::println("== 4. Wire encodings ==");
  std::array<std::uint8_t, 8> digest{};
  for (int b{0}; b < 4; ++b) {
    digest[static_cast<std::size_t>(b)] = static_cast<std::uint8_t>((table_crc >> (8 * b)) & 0xFFu);
    digest[static_cast<std::size_t>(b + 4)] =
      static_cast<std::uint8_t>((fingerprint >> (8 * b)) & 0xFFu);
  }

  std::println(
    "  digest hex                = {}", alg::hex_encode(std::span<std::uint8_t const>{digest})
  );

  std::println(
    "  digest base64url          = {}", alg::base64url_encode(std::span<std::uint8_t const>{digest})
  );

  std::vector<std::uint8_t> frame(alg::cobs_encoded_max_size(digest.size()));
  auto const framed{
    alg::cobs_encode(std::span<std::uint8_t const>{digest}, std::span<std::uint8_t>{frame})
  };
  if (framed.has_value()) {
    frame.resize(*framed);
    std::print("  digest cobs frame         =");
    for (auto const b : frame) {
      std::print(" {:02X}", b);
    }
    std::println("  (no 0x00 inside)");
  } else {
    std::println("  cobs encode failed: {}", alg::to_string(framed.error()));
  }

  std::println("== 5. Metadata search ==");

  std::string corpus;
  for (auto const& a : table) {
    corpus += a.name;
    corpus.push_back('\n');
  }

  if (auto const at{alg::kmp_find(corpus, "hero")}; at != std::string_view::npos) {
    std::println("  kmp_find(\"hero\")          -> first match at offset {}", at);
  }

  auto const anim_hits{alg::z_find_all(corpus, ".anim")};
  std::println("  z_find_all(\".anim\")       -> {} clip(s)", anim_hits.size());

  std::string_view const query{"tilesett.png"};
  std::string_view best;
  std::size_t best_dist{query.size() + 1};
  for (auto const& a : table) {
    if (auto const d{alg::levenshtein(query, a.name)}; d < best_dist) {
      best_dist = d;
      best = a.name;
    }
  }
  std::println("  nearest to \"{}\"  -> \"{}\" (distance {})", query, best, best_dist);

  std::println("== 6. Build order ==");
  auto const index_of{[&](std::string_view const name) -> std::uint32_t {
    for (std::uint32_t i{0}; i < table.size(); ++i) {
      if (table[i].name == name) {
        return i;
      }
    }
    return 0;  // every name below is present, so this never fires
  }};

  nc::graph<double, std::uint32_t> deps;
  for (std::size_t i{0}; i < table.size(); ++i) {
    if (!deps.add_vertex()) {
      return 1;
    }
  }
  auto const link{[&](std::string_view const dep, std::string_view const user) {
    auto const d{index_of(dep)};
    auto const u{index_of(user)};
    nexenne::utility::ignore(deps.add_edge(d, u, static_cast<double>(table[d].size)));
  }};
  link("hero_idle.anim", "hero_run.anim");
  link("tileset.png", "hero_idle.anim");
  link("tileset.png", "ui_atlas.png");
  link("font.ttf", "title.ogg");

  if (auto const order{alg::topological_sort(deps)}) {
    std::print("  build order               :");
    for (auto const v : *order) {
      std::print(" {}", table[v].name);
    }
    std::println("");
  } else {
    std::println("  cyclic dependency detected; cannot build");
  }

  if (auto const dist{alg::dijkstra(deps, index_of("tileset.png"))}) {
    auto const hero_run{index_of("hero_run.anim")};
    std::println(
      "  tileset -> hero_run cost  = {} bytes (transitive load)",
      static_cast<std::uint64_t>((*dist)[hero_run])
    );
  }

  std::println("== 7. Statistics ==");
  alg::running_stats<double> sizes;
  for (auto const& a : table) {
    sizes.push(static_cast<double>(a.size));
  }
  std::println("  size mean/stddev          = {:.0f} / {:.0f} bytes", sizes.mean(), sizes.stddev());
  std::println("  size min/max              = {:.0f} / {:.0f} bytes", sizes.min(), sizes.max());

  std::array<double, 7> size_list{};
  for (std::size_t i{0}; i < table.size(); ++i) {
    size_list[i] = static_cast<double>(table[i].size);
  }
  std::println("  pack total (neumaier)     = {:.0f} bytes", alg::neumaier_sum(size_list));

  std::println("\nThat is one pack built end to end: integer sort, three search");
  std::println("variants, a CRC and a hash, hex/base64/COBS wire forms, string");
  std::println("matching, a dependency graph, and streaming statistics.");
  return 0;
}
