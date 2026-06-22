/**
 * @file
 * @brief A guided tour of nexenne::algorithm through one realistic task: a tiny
 *        asset-pack build pipeline, console-only.
 *
 * Pretend we are the build step of a game or an embedded firmware image. We are
 * handed a set of asset records, and we must turn them into a deterministic,
 * verifiable, query-able pack. Nothing is drawn or written to disk; every number
 * a real packer would compute is computed here and printed, so you can see how
 * the module's facilities fit together in one cohesive job:
 *
 *   1. Ingest + sort   -> radix_sort the records by id (integer keys, no compares).
 *   2. Lookups         -> find_sorted / exponential_search / interpolation_search.
 *   3. Integrity       -> a CRC over the table plus an xxHash content fingerprint.
 *   4. Wire format     -> hex + base64url for the digest, COBS to frame a packet.
 *   5. Metadata search -> kmp_find / z_find_all / levenshtein over asset names.
 *   6. Dependencies    -> a build-order topological_sort and a dijkstra cost.
 *   7. Statistics      -> running_stats over the asset sizes, neumaier_sum total.
 *
 * Each step says WHY this algorithm is the right tool and what it costs. Read it
 * top to bottom. Library calls never throw: the fallible ones return expected /
 * optional, and we handle every one.
 */

#include <array>
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

namespace alg = nexenne::algorithm;
namespace nc = nexenne::container;

namespace {

// One asset in the pack. The id is a stable content handle; the size is the
// uncompressed byte count we will checksum, sort, and gather statistics over.
struct asset {
  std::uint32_t id{};
  std::string_view name;
  std::uint32_t size{};
};

}  // namespace

auto main() -> int {
  // The raw, unsorted ingest. Ids are sparse on purpose: they are content
  // handles, not array indices, so we cannot just bucket them by position.
  std::array<asset, 7> assets{
    asset{4096, "hero_idle.anim", 12'880},
    asset{17, "title.ogg", 220'400},
    asset{512, "tileset.png", 65'536},
    asset{4097, "hero_run.anim", 13'120},
    asset{9, "ui_atlas.png", 48'000},
    asset{1024, "ambient.ogg", 980'200},
    asset{256, "font.ttf", 30'720},
  };

  // 1. Ingest and order. The pack table must be sorted by id so a reader can
  // binary-search it. The keys are 32-bit unsigned integers, so radix_sort beats
  // a comparison sort: it is O(W*N) with W = 4 byte-passes, no element compares
  // at all, and it is stable. We sort a parallel array of ids and reorder the
  // records to match (radix_sort moves keys, not whole structs).
  std::println("== 1. Ingest and sort by id ==");
  std::array<std::uint32_t, 7> ids{};
  for (std::size_t i{0}; i < assets.size(); ++i) {
    ids[i] = assets[i].id;
  }
  alg::radix_sort(std::span<std::uint32_t>{ids});

  // Rebuild the record table in id order. With seven items a simple selection by
  // id is clearer than threading a permutation through the sort.
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

  // 2. Lookups into the sorted table. Three search variants, each the right pick
  // for a different access pattern. All return found_index: an optional index we
  // can use to address the table directly, with no iterator round-trip.
  std::println("== 2. Lookups ==");

  // find_sorted is the general O(log N) workhorse: lower_bound plus an equality
  // confirm. Use it when you know nothing about where the key sits.
  if (auto const at{alg::find_sorted(ids, 1024u)}) {
    std::println("  find_sorted(1024)         -> table[{}] = {}", *at, table[*at].name);
  }

  // exponential_search gallops from the front, so its cost scales with the
  // target's distance from index 0, not with N. It shines for keys known to live
  // near the front of a huge table; here id 9 is the very first element.
  if (auto const at{alg::exponential_search(ids, 9u)}) {
    std::println("  exponential_search(9)     -> table[{}] = {}", *at, table[*at].name);
  }

  // interpolation_search predicts the probe from the key's linear position in
  // the value range, giving O(log log N) on roughly uniform numeric data. Our
  // ids are arithmetic, so the constraint is satisfied; a miss returns empty.
  if (!alg::interpolation_search(ids, 1000u).has_value()) {
    std::println("  interpolation_search(1000)-> not found (no asset has id 1000)");
  }

  // 3. Integrity. Two different jobs, two different tools.
  //
  // A CRC catches accidental corruption of the on-wire table (a flipped bit on a
  // flash read or a UART link). It is a linear checksum, cheap and table-driven,
  // not a fingerprint. We run CRC-32C (Castagnoli) over the id+size columns.
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

  // A content fingerprint must spread well enough to use as a cache key or to
  // detect "did this asset change?" across builds. xxHash is fast and high
  // quality (not cryptographic), so it is the right fingerprint here. We hash the
  // concatenated names as a stand-in for the pack contents.
  std::string blob;
  for (auto const& a : table) {
    blob += a.name;
  }
  std::uint64_t const fingerprint{alg::xxhash<64>(blob)};
  std::println("  xxhash64(contents)        = 0x{:016x}", fingerprint);

  // 4. Wire format for the 8-byte digest = CRC (4 bytes) then a fingerprint
  // prefix (4 bytes). Two text encodings and one binary framing, each chosen for
  // where the bytes are going.
  std::println("== 4. Wire encodings ==");
  std::array<std::uint8_t, 8> digest{};
  for (int b{0}; b < 4; ++b) {
    digest[static_cast<std::size_t>(b)] = static_cast<std::uint8_t>((table_crc >> (8 * b)) & 0xFFu);
    digest[static_cast<std::size_t>(b + 4)] =
      static_cast<std::uint8_t>((fingerprint >> (8 * b)) & 0xFFu);
  }

  // Hex is the human-readable form for a log line or a filename: fixed 2 chars
  // per byte, trivially reversible, case-stable.
  std::println(
    "  digest hex                = {}", alg::hex_encode(std::span<std::uint8_t const>{digest})
  );

  // base64url packs the same bytes ~33% denser and is safe in a URL or a JSON
  // field (it uses '-' and '_', never '+' '/' or padding-that-needs-escaping).
  std::println(
    "  digest base64url          = {}", alg::base64url_encode(std::span<std::uint8_t const>{digest})
  );

  // COBS frames the raw digest for a byte-oriented link (UART, RS-485). It
  // removes every 0x00 from the payload so a lone 0x00 can delimit packets,
  // costing at most one byte per 254. It is heap-free: we size the output with
  // cobs_encoded_max_size and it returns the count written (or buffer_too_small).
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

  // 5. Metadata search over the asset names. Three string tools, three needs.
  std::println("== 5. Metadata search ==");

  // Join the names so we can scan them as one corpus.
  std::string corpus;
  for (auto const& a : table) {
    corpus += a.name;
    corpus.push_back('\n');
  }

  // kmp_find locates one fixed substring in O(N + M) with no backtracking, ideal
  // for a single-keyword filter. It returns npos on a miss.
  if (auto const at{alg::kmp_find(corpus, "hero")}; at != std::string_view::npos) {
    std::println("  kmp_find(\"hero\")          -> first match at offset {}", at);
  }

  // z_find_all reports every occurrence in one linear pass via the Z-function,
  // the tool when you need all hits, not just the first. We count the ".anim"
  // extension to learn how many animation clips shipped.
  auto const anim_hits{alg::z_find_all(corpus, ".anim")};
  std::println("  z_find_all(\".anim\")       -> {} clip(s)", anim_hits.size());

  // levenshtein gives the edit distance between two short strings, the basis of
  // a "did you mean?" suggestion. A user asked for "tilesett.png"; we find the
  // nearest real asset name by minimum edit distance. It is O(N*M).
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

  // 6. Build dependencies. Assets reference one another (a run animation reuses
  // the idle's skeleton; both atlases share a tileset), so we must build them in
  // an order that respects the references. Model it as a directed graph over the
  // table positions and ask for a valid build order.
  std::println("== 6. Build order ==");
  auto const index_of{[&](std::string_view const name) -> std::uint32_t {
    for (std::uint32_t i{0}; i < table.size(); ++i) {
      if (table[i].name == name) {
        return i;
      }
    }
    return 0;  // every name below is present, so this never fires
  }};

  // Edge direction is "dependency -> dependent": link(dep, user) adds dep -> user,
  // so a dependency points at everything that needs it. topological_sort then
  // lists each dependency ahead of its users, which is exactly a build order. The
  // edge weight is the dependency's byte size, the cost its users inherit.
  nc::graph<double, std::uint32_t> deps{static_cast<std::uint32_t>(table.size())};
  auto const link{[&](std::string_view const dep, std::string_view const user) {
    auto const d{index_of(dep)};
    auto const u{index_of(user)};
    deps.add_edge(d, u, static_cast<double>(table[d].size));
  }};
  link("hero_idle.anim", "hero_run.anim");  // run reuses the idle skeleton
  link("tileset.png", "hero_idle.anim");    // idle samples the tileset
  link("tileset.png", "ui_atlas.png");      // atlas packs the tileset
  link("font.ttf", "title.ogg");            // the title screen needs the font

  // topological_sort yields a linear order with every dependency ahead of its
  // dependents, in O(V + E). It returns an error if the graph has a cycle (a
  // circular dependency would be a build bug); we handle that case explicitly.
  if (auto const order{alg::topological_sort(deps)}) {
    std::print("  build order               :");
    for (auto const v : *order) {
      std::print(" {}", table[v].name);
    }
    std::println("");
  } else {
    std::println("  cyclic dependency detected; cannot build");
  }

  // dijkstra gives the least-cost path from one node to every other in
  // O((V+E) log V). Following dependency edges from tileset, dist[hero_run] is the
  // total bytes loaded along the chain tileset -> hero_idle -> hero_run.
  if (auto const dist{alg::dijkstra(deps, index_of("tileset.png"))}) {
    auto const hero_run{index_of("hero_run.anim")};
    std::println(
      "  tileset -> hero_run cost  = {} bytes (transitive load)",
      static_cast<std::uint64_t>((*dist)[hero_run])
    );
  }

  // 7. Pack statistics. running_stats (Welford) accumulates mean, stddev, min,
  // and max in one streaming O(1)-per-sample pass, numerically stable even for
  // large values, so we never hold the whole size list to compute a variance.
  std::println("== 7. Statistics ==");
  alg::running_stats<double> sizes;
  for (auto const& a : table) {
    sizes.push(static_cast<double>(a.size));
  }
  std::println("  size mean/stddev          = {:.0f} / {:.0f} bytes", sizes.mean(), sizes.stddev());
  std::println("  size min/max              = {:.0f} / {:.0f} bytes", sizes.min(), sizes.max());

  // neumaier_sum (compensated summation) tracks the rounding error naive
  // addition drops, so a total over mixed-magnitude sizes stays exact. For a few
  // integers it agrees with a plain sum, but the habit pays off at scale.
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
