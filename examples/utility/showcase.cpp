/**
 * @file
 * @brief A guided tour of nexenne::utility through one realistic task: a tiny,
 *        console-only device-fleet provisioner.
 *
 * No real I/O happens here - we *model* what a provisioning service does and
 * print every decision, so you can see how the module's small primitives snap
 * together in one cohesive program:
 *
 *   1. Identify things   -> strong_typedef gives distinct, unmixable ID types.
 *   2. Describe capability -> flags<E> is a type-safe option bitmask.
 *   3. Fail without throwing -> expected_utils threads errors through a pipeline.
 *   4. Hold a scarce handle -> unique_resource closes it exactly once.
 *   5. Undo on early exit -> scope_guard rolls a registry edit back.
 *   6. Take a callback     -> function_ref accepts any reporter, no template.
 *   7. Demand a dependency -> non_null makes "must be present" a compile fact.
 *   8. Narrow on purpose   -> narrow_cast asserts the value really fits.
 *   9. Dispatch a command  -> overloaded visits a std::variant of requests.
 *  10. Name an enum        -> enum_to_string turns a status into diagnostics.
 *
 * Each step notes *why* a given primitive is the right tool and the bug it
 * prevents. Read it top to bottom.
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
#include <nexenne/utility/function_ref.hpp>
#include <nexenne/utility/narrow_cast.hpp>
#include <nexenne/utility/non_null.hpp>
#include <nexenne/utility/overloaded.hpp>
#include <nexenne/utility/scope_guard.hpp>
#include <nexenne/utility/strong_typedef.hpp>
#include <nexenne/utility/unique_resource.hpp>

namespace util = nexenne::utility;

namespace {

// 1. Identifiers and quantities that must never be confused.
//
// A device id and a firmware slot are both just integers, but mixing them is a
// real bug (provisioning device 7 into slot 3, say). strong_typedef brands each
// with its own tag, so the compiler rejects `dev == slot` and `dev + slot`.
//
//   - identifier<Tag, T>: comparable and hashable, but NO arithmetic - exactly
//     what an opaque handle wants (you never "add two device ids").
//   - quantity<Tag, T>:  arithmetic + comparison - a real measurable number.
using device_id = util::identifier<struct device_id_tag, std::uint16_t>;
using slot_index = util::identifier<struct slot_index_tag, std::uint8_t>;
using kilobytes = util::quantity<struct kilobytes_tag, std::uint32_t>;

// 2. A capability bitmask. A scoped enum keeps these bits from being OR'd into
//    an unrelated mask, and flags<E> gives readable has/set without ever
//    decaying into raw int arithmetic.
enum class capability : std::uint8_t {
  telemetry = 1U << 0U,
  ota_update = 1U << 1U,  // over-the-air firmware
  secure_boot = 1U << 2U,
  remote_wipe = 1U << 3U,
};
using capabilities = util::flags<capability>;

// 10. A status enum we will turn into human-readable diagnostics with no
//     hand-maintained name table (enum_to_string reflects it).
enum class provision_status : std::uint8_t {
  ok = 0,
  slot_busy,
  image_too_large,
  insecure_capabilities,
};

// The error channel for the whole pipeline: a status code carried by
// std::expected, so failures propagate as values rather than exceptions.
using result = std::expected<void, provision_status>;

// A stand-in for a scarce OS handle (a flash-programmer lease). open returns a
// negative sentinel on failure; the real one must be released exactly once.
auto lease_programmer(device_id const dev) noexcept -> int {
  std::println("  [hw] leased flash programmer for device {}", dev.get());
  return 42;  // pretend lease handle
}

auto release_programmer(int const lease) noexcept -> void {
  std::println("  [hw] released flash programmer (lease {})", lease);
}

// 6. The pipeline reports progress through a caller-supplied sink. function_ref
//    accepts ANY callable with this signature without templating provision() and
//    without a std::function allocation - the callable simply outlives the call.
using reporter = util::function_ref<void(std::string_view)>;

// A device's mutable provisioning record, kept in the fleet registry.
struct device_record {
  device_id id;
  slot_index active_slot;
  capabilities caps;
  kilobytes image_size;
};

// The core operation. It validates a request, leases hardware, edits the
// registry, and either commits or rolls everything back - all without throwing.
//
// 7. `registry` is non_null: a provisioner with no registry is nonsense, so we
//    encode "must be present" in the type. Passing nullptr will not compile, and
//    the body needs no defensive null check.
auto provision(
  util::non_null<std::vector<device_record>*> registry,
  device_record const& request,
  reporter report
) -> result {
  report("validating request");

  // The secure-boot capability is mandatory for any device that can be wiped
  // remotely: shipping remote_wipe without secure_boot is a security hole.
  if (request.caps.has(capability::remote_wipe) && !request.caps.has(capability::secure_boot)) {
    return std::unexpected{provision_status::insecure_capabilities};
  }

  // A flash slot is one byte on the wire; the firmware image size is measured in
  // 8 KB pages. We refuse images that overflow the 8-bit page count.
  // 8. narrow_cast performs the conversion AND asserts (debug) that the value
  //    actually fits - unlike static_cast, which would silently wrap a huge
  //    image down to a small, plausible-looking page count.
  constexpr kilobytes page{8};
  auto const pages_wide{request.image_size / page};  // dimensionless ratio (quantity / quantity)
  if (pages_wide > 0xFFU) {
    return std::unexpected{provision_status::image_too_large};
  }
  auto const pages{util::narrow_cast<std::uint8_t>(pages_wide)};
  report(std::format("image occupies {} flash page(s)", pages));

  // The requested slot must be free. Comparing slot_index to slot_index is fine;
  // comparing it to a device_id would not compile (different tags).
  for (device_record const& existing : *registry) {
    if (existing.active_slot == request.active_slot) {
      return std::unexpected{provision_status::slot_busy};
    }
  }

  // 4. Lease the scarce hardware handle. unique_resource binds the handle to its
  //    releaser and runs it exactly once at scope exit - on success, on early
  //    return, or (if this could throw) on an exception. -1 is the failure
  //    sentinel: a failed lease would leave the resource un-owned and skip the
  //    releaser entirely.
  auto programmer{util::make_unique_resource_checked(
    lease_programmer(request.id), -1, [](int const lease) noexcept { release_programmer(lease); }
  )};
  if (!programmer.owns()) {
    return std::unexpected{provision_status::slot_busy};  // treat as transient
  }

  // 5. Tentatively append the record, then arm a rollback. If anything below
  //    fails we must NOT leave a half-written entry in the registry. scope_guard
  //    runs its lambda on every exit path until we dismiss it - so the commit is
  //    "the absence of a rollback", which is impossible to forget on a new
  //    early-return branch.
  auto const mark{registry->size()};
  registry->push_back(request);
  auto rollback{util::scope_guard{[registry, mark] {
    registry->resize(mark);
    std::println("  [registry] rolled back to {} record(s)", mark);
  }}};

  report("writing firmware image");  // (pretend the flash write happens here)

  rollback.dismiss();  // success: keep the appended record
  report("committed");
  // `programmer`'s releaser fires here, exactly once, as the scope unwinds.
  return {};
}

// 9. The fleet receives heterogeneous commands. A std::variant models the
//    closed set, and overloaded turns a pile of lambdas into one visitor so each
//    alternative is handled by the right typed branch (and a missing branch is a
//    compile error, not a silent default).
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
  // The reporter the whole run shares: a plain lambda. Because function_ref only
  // *views* it, this local must outlive every call below - and it does, living
  // for the whole of main.
  auto const log{[](std::string_view msg) { std::println("    - {}", msg); }};

  std::vector<device_record> fleet;

  // A batch of commands the provisioner must process in order. Note how each
  // request mixes the branded ids, the capability mask, and a quantity - none of
  // which can be accidentally swapped.
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
    // &fleet converts implicitly to non_null; passing nullptr here would not
    // compile, which is the point.
    if (result const r{run_command(&fleet, batch[i], log)}; !r) {
      // 10. The error is an enum value; enum_to_string reflects its name with no
      //     hand-written switch, so diagnostics never drift from the enum.
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
