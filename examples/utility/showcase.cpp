/**
 * @file
 * @brief A guided tour of nexenne::utility: a console-only device-fleet provisioner.
 *
 * No real I/O happens here: the program models what a provisioning service does
 * and prints every decision, so you can see how the module's small primitives
 * snap together in one cohesive program. Read it top to bottom:
 *
 *   1. Identify things: \c strong_typedef brands device ids and slots so the
 *      compiler rejects mixing them; an \c identifier has no arithmetic, a
 *      \c quantity (the image size in kilobytes) does.
 *   2. Describe capability: \c flags is a type-safe option bitmask that never
 *      decays into raw integer arithmetic.
 *   3. Fail without throwing: \c std::expected threads a status enum through
 *      the pipeline as a value.
 *   4. Hold a scarce handle: \c make_unique_resource_checked leases a flash
 *      programmer, treats -1 as a failed lease, and releases a good lease
 *      exactly once on every exit path.
 *   5. Undo on early exit: \c scope_guard rolls a tentative registry append
 *      back unless dismissed, so the commit is the absence of a rollback.
 *   6. Take a callback: \c function_ref accepts any reporter without a template
 *      or an allocation; the viewed lambda lives for the whole of main.
 *   7. Demand a dependency: \c non_null makes "the registry is present" part of
 *      the type, so passing nullptr does not compile.
 *   8. Narrow on purpose: \c narrow_cast converts the flash page count and
 *      asserts in debug that it fits, where \c static_cast would wrap silently.
 *   9. Dispatch a command: \c overloaded turns lambdas into one visitor over a
 *      closed \c std::variant of requests.
 *  10. Name an enum: \c enum_to_string reflects a status into diagnostics with
 *      no hand-written switch.
 *
 * The provisioner rejects a remote-wipe device without secure boot, a busy
 * slot, and an image whose count of 8 KB flash pages does not fit one byte.
 */

#include <cstdint>
#include <expected>
#include <print>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <nexenne/utility/enum_to_string.hpp>
#include <nexenne/utility/expected_utils.hpp>
#include <nexenne/utility/flags.hpp>
#include <nexenne/utility/format.hpp>
#include <nexenne/utility/function_ref.hpp>
#include <nexenne/utility/narrow_cast.hpp>
#include <nexenne/utility/non_null.hpp>
#include <nexenne/utility/overloaded.hpp>
#include <nexenne/utility/scope_guard.hpp>
#include <nexenne/utility/strong_typedef.hpp>
#include <nexenne/utility/unique_resource.hpp>

namespace util = nexenne::utility;

namespace {

using device_id = util::identifier<struct device_id_tag, std::uint16_t>;
using slot_index = util::identifier<struct slot_index_tag, std::uint8_t>;
using kilobytes = util::quantity<struct kilobytes_tag, std::uint32_t>;

enum class capability : std::uint8_t {
  telemetry = 1U << 0U,
  ota_update = 1U << 1U,
  secure_boot = 1U << 2U,
  remote_wipe = 1U << 3U,
};
using capabilities = util::flags<capability>;

enum class provision_status : std::uint8_t {
  ok = 0,
  slot_busy,
  image_too_large,
  insecure_capabilities,
};

using result = std::expected<void, provision_status>;

auto lease_programmer(device_id const dev) noexcept -> int {
  std::println("  [hw] leased flash programmer for device {}", dev.get());
  return 42;
}

auto release_programmer(int const lease) noexcept -> void {
  std::println("  [hw] released flash programmer (lease {})", lease);
}

using reporter = util::function_ref<void(std::string_view)>;

struct device_record {
  device_id id;
  slot_index active_slot;
  capabilities caps;
  kilobytes image_size;
};

auto provision(
  util::non_null<std::vector<device_record>*> registry,
  device_record const& request,
  reporter report
) -> result {
  report("validating request");

  if (request.caps.has(capability::remote_wipe) && !request.caps.has(capability::secure_boot)) {
    return std::unexpected{provision_status::insecure_capabilities};
  }

  constexpr kilobytes page{8};
  auto const pages_wide{request.image_size / page};
  if (pages_wide > 0xFFU) {
    return std::unexpected{provision_status::image_too_large};
  }
  auto const pages{util::narrow_cast<std::uint8_t>(pages_wide)};
  report(std::format("image occupies {} flash page(s)", pages));

  for (device_record const& existing : *registry) {
    if (existing.active_slot == request.active_slot) {
      return std::unexpected{provision_status::slot_busy};
    }
  }

  auto programmer{util::make_unique_resource_checked(
    lease_programmer(request.id), -1, [](int const lease) noexcept { release_programmer(lease); }
  )};
  if (!programmer.owns()) {
    return std::unexpected{provision_status::slot_busy};
  }

  auto const mark{registry->size()};
  registry->push_back(request);
  auto rollback{util::scope_guard{[registry, mark] {
    registry->resize(mark);
    std::println("  [registry] rolled back to {} record(s)", mark);
  }}};

  report("writing firmware image");

  rollback.dismiss();
  report("committed");
  return {};
}

struct provision_cmd {
  device_record request;
};

struct query_caps_cmd {
  device_id id;
};

struct retire_cmd {
  device_id id;
};

using command = std::variant<provision_cmd, query_caps_cmd, retire_cmd>;

auto run_command(
  util::non_null<std::vector<device_record>*> registry, command const& cmd, reporter report
) -> result {
  return std::visit(
    util::overloaded{
      [&](provision_cmd const& c) -> result { return provision(registry, c.request, report); },
      [&](query_caps_cmd const& c) -> result {
        for (device_record const& r : *registry) {
          if (r.id == c.id) {
            report(std::format("device {} caps raw = 0b{:04b}", c.id.get(), r.caps.raw()));
            return {};
          }
        }
        report(std::format("device {} not found", c.id.get()));
        return {};
      },
      [&](retire_cmd const& c) -> result {
        auto const before{registry->size()};
        std::erase_if(*registry, [&](device_record const& r) { return r.id == c.id; });
        report(std::format("retired {} record(s)", before - registry->size()));
        return {};
      },
    },
    cmd
  );
}

}  // namespace

auto main() -> int {
  auto const log{[](std::string_view msg) { std::println("    - {}", msg); }};

  std::vector<device_record> fleet;

  std::vector<command> const batch{
    provision_cmd{
      {device_id{1001}, slot_index{0}, capabilities{} | capability::telemetry, kilobytes{120}}
    },
    provision_cmd{
      {device_id{1002}, slot_index{0}, capabilities{} | capability::ota_update, kilobytes{64}}
    },
    provision_cmd{
      {device_id{1003}, slot_index{1}, capabilities{} | capability::remote_wipe, kilobytes{64}}
    },
    provision_cmd{
      {device_id{1004},
       slot_index{2},
       capabilities{} | capability::ota_update | capability::secure_boot,
       kilobytes{4096}}
    },
    query_caps_cmd{device_id{1001}},
    retire_cmd{device_id{1001}},
  };

  for (std::size_t i{0}; i < batch.size(); ++i) {
    std::println("== command {} ==", i);
    if (result const r{run_command(&fleet, batch[i], log)}; !r) {
      std::println("    ! failed: {}", util::enum_to_string(r.error()));
    }
  }

  std::println("\n== final fleet ({} device(s)) ==", fleet.size());
  for (device_record const& r : fleet) {
    std::println(
      "  device {} -> slot {}, caps 0b{:04b}, {} KB",
      r.id.get(),
      static_cast<unsigned>(r.active_slot.get()),
      r.caps.raw(),
      r.image_size.get()
    );
  }

  std::println("\nThat is the module in one workflow: branded ids, capability");
  std::println("masks, value-based errors, owned handles, rollback guards, a");
  std::println("callback view, a non-null contract, a checked narrowing, a");
  std::println("variant visitor, and reflected enum diagnostics.");
  return 0;
}
