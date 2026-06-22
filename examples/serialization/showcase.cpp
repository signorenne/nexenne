/**
 * @file
 * @brief A guided tour of nexenne::serialization through one realistic task:
 *        saving and loading a small game state to a byte buffer.
 *
 * This is the shape of a real save file. We take an in-memory aggregate (a
 * player, a vector of inventory items, and a map of named stats), pack it into
 * a flat little-endian buffer behind a version envelope, then read it back and
 * prove the round-trip is exact. We also exercise the failure paths, because a
 * loader that only works on perfect input is not a loader.
 *
 * Why binary, not JSON, here: a save file is written once and read by the same
 * application, so it does not need to be human-readable or self-describing. The
 * schema-driven binary writer emits no field names and no type tags, both sides
 * just walk the fields in the same order, so the payload is about as small as
 * the data itself. Strings carry a varint length prefix; small integers can be
 * zigzag-packed; everything is little-endian regardless of host so the file
 * moves between machines unchanged.
 *
 * Why a version envelope: the code that writes a save outlives no release. Next
 * year v2 adds a field. The 8-byte envelope (magic + version) lets the loader
 * recognise the format and dispatch to the right decode routine before it
 * touches the body, so old saves keep loading. That is the entire point of
 * versioned.hpp.
 *
 * Why no exceptions: every fallible call returns std::expected<T, error>. We
 * check each one. Nothing here throws, nothing here allocates on the hot path,
 * so the same code drops onto a microcontroller reading the blob out of flash.
 * Read it top to bottom.
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <nexenne/serialization/serialization.hpp>

namespace ser = nexenne::serialization;

namespace {

// The application tag stamped into every save's envelope. read_header rejects a
// blob whose magic does not match, so a config file or a stray buffer can never
// be mistaken for a save and half-decoded.
constexpr std::uint32_t save_magic{0x53415645};  // ASCII "SAVE" (0x53='S' .. 0x45='E')
constexpr std::uint16_t save_version{1};

// The in-memory state we want to persist. Plain data, no serialization logic on
// it: the codec below owns the wire format, the struct stays oblivious.
struct item {
  std::string name{};
  std::uint16_t quantity{0};
};

struct game_state {
  std::string player{};
  std::int32_t level{0};
  double health{0.0};
  std::vector<item> inventory{};
  std::map<std::string, std::int32_t> stats{};  // ordered, so the wire order is stable
};

// Serialize a game_state into buf behind a version envelope. Every step is a
// fallible binary write; we thread the std::expected through so the first
// failure (a buffer too small) stops us and surfaces the error. There is no
// length-prefixed "object" on the wire: a container is just its count followed
// by that many elements, which the reader mirrors exactly.
auto save(game_state const& gs, std::span<std::byte> const buf)
  -> std::expected<std::size_t, ser::error> {
  auto w{ser::binary::writer{buf}};

  // 1. The envelope first, so the loader knows the version before the body.
  if (auto const r{ser::write_header(w, save_magic, save_version)}; !r) {
    return std::unexpected{r.error()};
  }

  // 2. Scalars. write() picks the encoding from the type: a string gets a
  // varint length prefix, an int32 / double get fixed little-endian bytes.
  if (auto const r{w.write(std::string_view{gs.player})}; !r) {
    return std::unexpected{r.error()};
  }
  if (auto const r{w.write(gs.level)}; !r) {
    return std::unexpected{r.error()};
  }
  if (auto const r{w.write(gs.health)}; !r) {
    return std::unexpected{r.error()};
  }

  // 3. The inventory vector: a varint count, then each element's fields in
  // order. No per-element tag, the reader knows an item is {string, uint16}.
  if (auto const r{w.write_varint(gs.inventory.size())}; !r) {
    return std::unexpected{r.error()};
  }
  for (auto const& it : gs.inventory) {
    if (auto const r{w.write(std::string_view{it.name})}; !r) {
      return std::unexpected{r.error()};
    }
    if (auto const r{w.write(it.quantity)}; !r) {
      return std::unexpected{r.error()};
    }
  }

  // 4. The stats map: same count-then-pairs shape. std::map iterates in key
  // order, so two saves of equal data produce byte-identical buffers, which is
  // exactly what you want for a content hash or a diff. The signed values are
  // zigzag-packed: a stat near zero costs one byte whether it is +3 or -3.
  if (auto const r{w.write_varint(gs.stats.size())}; !r) {
    return std::unexpected{r.error()};
  }
  for (auto const& [key, value] : gs.stats) {
    if (auto const r{w.write(std::string_view{key})}; !r) {
      return std::unexpected{r.error()};
    }
    if (auto const r{w.write_zigzag(value)}; !r) {
      return std::unexpected{r.error()};
    }
  }

  return w.bytes_written();
}

// The codec passed to decode_with. versioned.hpp validates the envelope, then
// hands us the body reader and the parsed version so we only own the
// version-dispatch. A real format would keep decode_v1 around forever and add
// decode_v2 alongside it; here v1 is all there is.
struct save_codec {
  [[nodiscard]] auto decode(ser::binary::reader& r, std::uint16_t const version) const
    -> std::expected<game_state, ser::error> {
    if (version != save_version) {
      return std::unexpected{ser::error::invalid_input};  // unknown future version
    }
    return decode_v1(r);
  }

private:
  // Read the body in the same order save() wrote it. read_string() returns a
  // view straight into the buffer (zero copy); we materialise std::strings here
  // because the game_state outlives the buffer. Every read is bounds-checked,
  // so a truncated blob fails cleanly instead of reading past the end.
  [[nodiscard]] static auto decode_v1(ser::binary::reader& r
  ) -> std::expected<game_state, ser::error> {
    auto gs{game_state{}};

    auto const player{r.read_string()};
    if (!player) {
      return std::unexpected{player.error()};
    }
    gs.player = std::string{*player};

    auto const level{r.read<std::int32_t>()};
    if (!level) {
      return std::unexpected{level.error()};
    }
    gs.level = *level;

    auto const health{r.read<double>()};
    if (!health) {
      return std::unexpected{health.error()};
    }
    gs.health = *health;

    auto const inv_count{r.read_varint()};
    if (!inv_count) {
      return std::unexpected{inv_count.error()};
    }
    for (auto i{std::uint64_t{0}}; i < *inv_count; ++i) {
      auto const name{r.read_string()};
      if (!name) {
        return std::unexpected{name.error()};
      }
      auto const qty{r.read<std::uint16_t>()};
      if (!qty) {
        return std::unexpected{qty.error()};
      }
      gs.inventory.push_back(item{.name = std::string{*name}, .quantity = *qty});
    }

    auto const stat_count{r.read_varint()};
    if (!stat_count) {
      return std::unexpected{stat_count.error()};
    }
    for (auto i{std::uint64_t{0}}; i < *stat_count; ++i) {
      auto const key{r.read_string()};
      if (!key) {
        return std::unexpected{key.error()};
      }
      auto const value{r.read_zigzag()};
      if (!value) {
        return std::unexpected{value.error()};
      }
      gs.stats.emplace(std::string{*key}, static_cast<std::int32_t>(*value));
    }

    return gs;
  }
};

// Compare two states field by field so the round-trip claim is checked, not
// asserted on faith.
auto states_equal(game_state const& a, game_state const& b) -> bool {
  if (a.player != b.player || a.level != b.level || a.health != b.health) {
    return false;
  }
  if (a.inventory.size() != b.inventory.size()) {
    return false;
  }
  for (auto i{std::size_t{0}}; i < a.inventory.size(); ++i) {
    if (a.inventory[i].name != b.inventory[i].name
        || a.inventory[i].quantity != b.inventory[i].quantity) {
      return false;
    }
  }
  return a.stats == b.stats;
}

}  // namespace

auto main() -> int {
  auto const original{game_state{
    .player = "Ada",
    .level = 7,
    .health = 87.5,
    .inventory =
      {
        item{.name = "potion", .quantity = 3},
        item{.name = "iron sword", .quantity = 1},
        item{.name = "torch", .quantity = 12},
      },
    .stats = {{"gold", 240}, {"karma", -15}, {"renown", 8}},
  }};

  // 1. Save. A fixed-size stack buffer, no heap: the writer reports buffer_full
  // rather than overrunning, so we never need to guess generously.
  std::println("== 1. Save ==");
  auto buf{std::array<std::byte, 256>{}};
  auto const written{save(original, buf)};
  if (!written) {
    std::println("  save failed: {}", ser::to_string(written.error()));
    return 1;
  }
  std::println(
    "  player '{}' level {} -> {} bytes on the wire", original.player, original.level, *written
  );
  std::println("  (header 8 + body {} = the whole save)", *written - ser::versioned_header_size);

  // 2. Load. decode_with reads the envelope, checks the magic, and routes the
  // body to save_codec::decode for the stored version. One call covers the
  // version handshake and the body decode.
  std::println("== 2. Load and verify ==");
  auto reader{ser::binary::reader{std::span<std::byte const>{buf.data(), *written}}};
  auto const loaded{ser::decode_with(reader, save_magic, save_codec{})};
  if (!loaded) {
    std::println("  load failed: {}", ser::to_string(loaded.error()));
    return 1;
  }
  std::println(
    "  loaded player '{}' level {} health {}", loaded->player, loaded->level, loaded->health
  );
  std::println(
    "  inventory: {} items, stats: {} entries", loaded->inventory.size(), loaded->stats.size()
  );
  std::println("  round-trip exact: {}", states_equal(original, *loaded));

  // 3. Wrong magic. Point the loader at a buffer carrying a different tag and
  // it refuses before decoding a single body byte. This is the guard that keeps
  // a config blob from being parsed as a save.
  std::println("== 3. Error path: wrong magic ==");
  auto other{std::array<std::byte, 16>{}};
  {
    auto w{ser::binary::writer{other}};
    static_cast<void>(ser::write_header(w, 0x434F4E46, save_version));  // 'CONF'
  }
  auto bad_reader{ser::binary::reader{other}};
  auto const rejected{ser::decode_with(bad_reader, save_magic, save_codec{})};
  std::println(
    "  decode rejected: {} (error={})", !rejected.has_value(), ser::to_string(rejected.error())
  );

  // 4. Truncated input. Hand the loader only the first 20 bytes of a good save.
  // The header and the first fields decode, then a read runs off the end and
  // returns buffer_underrun instead of reading uninitialised memory. A loader
  // built on this reader can never be tricked into an over-read.
  std::println("== 4. Error path: truncated save ==");
  auto const cut{std::size_t{20}};
  auto trunc_reader{ser::binary::reader{std::span<std::byte const>{buf.data(), cut}}};
  auto const partial{ser::decode_with(trunc_reader, save_magic, save_codec{})};
  std::println(
    "  {} of {} bytes -> decode failed: {} (error={})",
    cut,
    *written,
    !partial.has_value(),
    ser::to_string(partial.error())
  );

  // 5. Capacity check. Saving into a buffer too small to hold the payload fails
  // with buffer_full at the first write that does not fit, and the partial
  // write is harmless because the cursor only advances on success.
  std::println("== 5. Error path: buffer too small ==");
  auto tiny{std::array<std::byte, 12>{}};  // room for the header, not the body
  auto const overflow{save(original, tiny)};
  std::println(
    "  save into {} bytes failed: {} (error={})",
    tiny.size(),
    !overflow.has_value(),
    ser::to_string(overflow.error())
  );

  std::println("\nThat is a whole persistence layer: a version envelope, a flat");
  std::println("binary body with nested containers, an exact round-trip, and every");
  std::println("failure surfaced as a value - no tags, no heap, no exceptions.");
  return 0;
}
