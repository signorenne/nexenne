#pragma once

/**
 * @file
 * @brief Debug printing and formatting for the signal module's handle types.
 *
 * Three layers, like the rest of the library's format headers: \c to_string(x)
 * builds a readable string, \c operator<<(std::ostream&, x) streams it, and a
 * \c std::formatter specialization makes \c std::format("{}", x) work. The output
 * is for diagnostics, not serialisation, and is not stable across versions.
 *
 * Every public type is covered. The handles print their slot id and state:
 * \c connection and \c scoped_connection show whether the owning signal is
 * still alive, \c static_connection and \c static_scoped_connection whether
 * they name a slot. The stateful types print a one-line summary of what their
 * accessors expose: \c signal and \c static_signal their alive slot count and
 * blocked flag, \c sink and \c static_sink the count of the signal they bind,
 * \c slot and \c static_slot their tracked count and capacity, and
 * \c emit_blocker whether it still holds a signal to restore. Callables are
 * never printed.
 *
 * The standard \c format header is heavy. The umbrella \c signal.hpp includes
 * this header (last, so its \c signal and \c sink are complete here); include
 * \c connection.hpp, \c slot.hpp or \c static_signal.hpp directly to leave it
 * out.
 */

#include <cstddef>
#include <format>
#include <ostream>
#include <string>

#include <nexenne/signal/connection.hpp>
#include <nexenne/signal/emit_blocker.hpp>
#include <nexenne/signal/signal.hpp>
#include <nexenne/signal/slot.hpp>
#include <nexenne/signal/static_signal.hpp>

namespace nexenne::signal {

/**
 * @brief Debug string for a \c connection.
 *
 * @param c Connection to describe.
 *
 * @return A string of the form \c "connection(id=N, valid=B)".
 *
 * @pre None.
 * @post \p c is unchanged.
 */
[[nodiscard]] inline auto to_string(connection const& c) -> std::string {
  return std::format("connection(id={}, valid={})", c.slot_id(), c.valid());
}

/**
 * @brief Streams a \c connection via \c to_string.
 *
 * @param os Output stream.
 * @param c Connection to stream.
 *
 * @return \p os, for chaining.
 *
 * @pre None.
 * @post \p c is unchanged; \p os has the debug string appended.
 */
inline auto operator<<(std::ostream& os, connection const& c) -> std::ostream& {
  return os << to_string(c);
}

/**
 * @brief Debug string for a \c static_connection.
 *
 * @param c Connection to describe.
 *
 * @return A string of the form \c "static_connection(id=N, has_target=B)".
 *
 * @pre None.
 * @post \p c is unchanged.
 */
[[nodiscard]] inline auto to_string(static_connection const& c) -> std::string {
  return std::format("static_connection(id={}, has_target={})", c.id(), c.has_target());
}

/**
 * @brief Streams a \c static_connection via \c to_string.
 *
 * @param os Output stream.
 * @param c Connection to stream.
 *
 * @return \p os, for chaining.
 *
 * @pre None.
 * @post \p c is unchanged; \p os has the debug string appended.
 */
inline auto operator<<(std::ostream& os, static_connection const& c) -> std::ostream& {
  return os << to_string(c);
}

/**
 * @brief Debug string for a \c scoped_connection, through its owned handle.
 *
 * @param c Scoped connection to describe.
 *
 * @return A string of the form \c "scoped_connection(id=N, valid=B)", the
 *         slot id and liveness of the owned \c connection.
 *
 * @pre None.
 * @post \p c is unchanged.
 */
[[nodiscard]] inline auto to_string(scoped_connection const& c) -> std::string {
  return std::format("scoped_connection(id={}, valid={})", c.get().slot_id(), c.valid());
}

/**
 * @brief Streams a \c scoped_connection via \c to_string.
 *
 * @param os Output stream.
 * @param c Scoped connection to stream.
 *
 * @return \p os, for chaining.
 *
 * @pre None.
 * @post \p c is unchanged; \p os has the debug string appended.
 */
inline auto operator<<(std::ostream& os, scoped_connection const& c) -> std::ostream& {
  return os << to_string(c);
}

/**
 * @brief Debug string for a \c static_scoped_connection, through its owned handle.
 *
 * @param c Scoped connection to describe.
 *
 * @return A string of the form
 *         \c "static_scoped_connection(id=N, has_target=B)", the slot id and
 *         target state of the owned \c static_connection.
 *
 * @pre None.
 * @post \p c is unchanged.
 */
[[nodiscard]] inline auto to_string(static_scoped_connection const& c) -> std::string {
  return std::format(
    "static_scoped_connection(id={}, has_target={})", c.get().id(), c.get().has_target()
  );
}

/**
 * @brief Streams a \c static_scoped_connection via \c to_string.
 *
 * @param os Output stream.
 * @param c Scoped connection to stream.
 *
 * @return \p os, for chaining.
 *
 * @pre None.
 * @post \p c is unchanged; \p os has the debug string appended.
 */
inline auto operator<<(std::ostream& os, static_scoped_connection const& c) -> std::ostream& {
  return os << to_string(c);
}

/**
 * @brief Debug string for a \c slot: its tracked count and capacity.
 *
 * @tparam Capacity Maximum number of connections the slot tracks.
 * @param s Slot to describe.
 *
 * @return A string of the form \c "slot(size=N, capacity=C)".
 *
 * @pre None.
 * @post \p s is unchanged.
 */
template <std::size_t Capacity>
[[nodiscard]] auto to_string(slot<Capacity> const& s) -> std::string {
  return std::format("slot(size={}, capacity={})", s.size(), s.capacity());
}

/**
 * @brief Streams a \c slot via \c to_string.
 *
 * @tparam Capacity Maximum number of connections the slot tracks.
 * @param os Output stream.
 * @param s Slot to stream.
 *
 * @return \p os, for chaining.
 *
 * @pre None.
 * @post \p s is unchanged; \p os has the debug string appended.
 */
template <std::size_t Capacity>
auto operator<<(std::ostream& os, slot<Capacity> const& s) -> std::ostream& {
  return os << to_string(s);
}

/**
 * @brief Debug string for a \c static_slot: its tracked count and capacity.
 *
 * @tparam Capacity Maximum number of connections the slot tracks.
 * @param s Slot to describe.
 *
 * @return A string of the form \c "static_slot(size=N, capacity=C)".
 *
 * @pre None.
 * @post \p s is unchanged.
 */
template <std::size_t Capacity>
[[nodiscard]] auto to_string(static_slot<Capacity> const& s) -> std::string {
  return std::format("static_slot(size={}, capacity={})", s.size(), s.capacity());
}

/**
 * @brief Streams a \c static_slot via \c to_string.
 *
 * @tparam Capacity Maximum number of connections the slot tracks.
 * @param os Output stream.
 * @param s Slot to stream.
 *
 * @return \p os, for chaining.
 *
 * @pre None.
 * @post \p s is unchanged; \p os has the debug string appended.
 */
template <std::size_t Capacity>
auto operator<<(std::ostream& os, static_slot<Capacity> const& s) -> std::ostream& {
  return os << to_string(s);
}

/**
 * @brief Debug string for a \c signal: its alive slot count and blocked flag.
 *
 * @tparam R Slot return type.
 * @tparam Args Slot argument types.
 * @tparam SlotCapacity Inline byte capacity of each slot callable.
 * @param sig Signal to describe.
 *
 * @return A string of the form \c "signal(size=N, blocked=B)".
 *
 * @pre None.
 * @post \p sig is unchanged.
 *
 * @complexity \c O(n) in the slot count, as \c signal::size is.
 */
template <typename R, typename... Args, std::size_t SlotCapacity>
[[nodiscard]] auto to_string(signal<R(Args...), SlotCapacity> const& sig) -> std::string {
  return std::format("signal(size={}, blocked={})", sig.size(), sig.is_blocked());
}

/**
 * @brief Streams a \c signal via \c to_string.
 *
 * @tparam R Slot return type.
 * @tparam Args Slot argument types.
 * @tparam SlotCapacity Inline byte capacity of each slot callable.
 * @param os Output stream.
 * @param sig Signal to stream.
 *
 * @return \p os, for chaining.
 *
 * @pre None.
 * @post \p sig is unchanged; \p os has the debug string appended.
 */
template <typename R, typename... Args, std::size_t SlotCapacity>
auto operator<<(std::ostream& os, signal<R(Args...), SlotCapacity> const& sig) -> std::ostream& {
  return os << to_string(sig);
}

/**
 * @brief Debug string for a \c sink: the alive slot count of its signal.
 *
 * @tparam R Slot return type.
 * @tparam Args Slot argument types.
 * @tparam SlotCapacity Inline byte capacity of each slot callable.
 * @param s Sink to describe.
 *
 * @return A string of the form \c "sink(size=N)".
 *
 * @pre The signal \p s binds is alive.
 * @post \p s is unchanged.
 */
template <typename R, typename... Args, std::size_t SlotCapacity>
[[nodiscard]] auto to_string(sink<R(Args...), SlotCapacity> const& s) -> std::string {
  return std::format("sink(size={})", s.size());
}

/**
 * @brief Streams a \c sink via \c to_string.
 *
 * @tparam R Slot return type.
 * @tparam Args Slot argument types.
 * @tparam SlotCapacity Inline byte capacity of each slot callable.
 * @param os Output stream.
 * @param s Sink to stream.
 *
 * @return \p os, for chaining.
 *
 * @pre The signal \p s binds is alive.
 * @post \p s is unchanged; \p os has the debug string appended.
 */
template <typename R, typename... Args, std::size_t SlotCapacity>
auto operator<<(std::ostream& os, sink<R(Args...), SlotCapacity> const& s) -> std::ostream& {
  return os << to_string(s);
}

/**
 * @brief Debug string for a \c static_signal: its alive slots, capacity and blocked flag.
 *
 * @tparam R Slot return type.
 * @tparam Args Slot argument types.
 * @tparam MaxSlots Inline slot capacity.
 * @tparam SlotCapacity Inline byte capacity of each slot callable.
 * @param sig Signal to describe.
 *
 * @return A string of the form \c "static_signal(size=N, capacity=M, blocked=B)".
 *
 * @pre None.
 * @post \p sig is unchanged.
 */
template <typename R, typename... Args, std::size_t MaxSlots, std::size_t SlotCapacity>
[[nodiscard]] auto to_string(static_signal<R(Args...), MaxSlots, SlotCapacity> const& sig)
  -> std::string {
  return std::format(
    "static_signal(size={}, capacity={}, blocked={})", sig.size(), sig.capacity(), sig.is_blocked()
  );
}

/**
 * @brief Streams a \c static_signal via \c to_string.
 *
 * @tparam R Slot return type.
 * @tparam Args Slot argument types.
 * @tparam MaxSlots Inline slot capacity.
 * @tparam SlotCapacity Inline byte capacity of each slot callable.
 * @param os Output stream.
 * @param sig Signal to stream.
 *
 * @return \p os, for chaining.
 *
 * @pre None.
 * @post \p sig is unchanged; \p os has the debug string appended.
 */
template <typename R, typename... Args, std::size_t MaxSlots, std::size_t SlotCapacity>
auto operator<<(std::ostream& os, static_signal<R(Args...), MaxSlots, SlotCapacity> const& sig)
  -> std::ostream& {
  return os << to_string(sig);
}

/**
 * @brief Debug string for a \c static_sink: the alive slot count of its signal.
 *
 * @tparam R Slot return type.
 * @tparam Args Slot argument types.
 * @tparam MaxSlots Inline slot capacity.
 * @tparam SlotCapacity Inline byte capacity of each slot callable.
 * @param s Sink to describe.
 *
 * @return A string of the form \c "static_sink(size=N)".
 *
 * @pre The signal \p s binds is alive.
 * @post \p s is unchanged.
 */
template <typename R, typename... Args, std::size_t MaxSlots, std::size_t SlotCapacity>
[[nodiscard]] auto to_string(static_sink<R(Args...), MaxSlots, SlotCapacity> const& s)
  -> std::string {
  return std::format("static_sink(size={})", s.size());
}

/**
 * @brief Streams a \c static_sink via \c to_string.
 *
 * @tparam R Slot return type.
 * @tparam Args Slot argument types.
 * @tparam MaxSlots Inline slot capacity.
 * @tparam SlotCapacity Inline byte capacity of each slot callable.
 * @param os Output stream.
 * @param s Sink to stream.
 *
 * @return \p os, for chaining.
 *
 * @pre The signal \p s binds is alive.
 * @post \p s is unchanged; \p os has the debug string appended.
 */
template <typename R, typename... Args, std::size_t MaxSlots, std::size_t SlotCapacity>
auto operator<<(std::ostream& os, static_sink<R(Args...), MaxSlots, SlotCapacity> const& s)
  -> std::ostream& {
  return os << to_string(s);
}

/**
 * @brief Debug string for an \c emit_blocker: whether it still holds a signal.
 *
 * @tparam Signal The guarded \c blockable type.
 * @param b Blocker to describe.
 *
 * @return \c "emit_blocker(active=true)" while the guard will restore its
 *         signal, \c "emit_blocker(active=false)" once it is inert.
 *
 * @pre None.
 * @post \p b is unchanged.
 */
template <blockable Signal>
[[nodiscard]] auto to_string(emit_blocker<Signal> const& b) -> std::string {
  return std::format("emit_blocker(active={})", b.is_active());
}

/**
 * @brief Streams an \c emit_blocker via \c to_string.
 *
 * @tparam Signal The guarded \c blockable type.
 * @param os Output stream.
 * @param b Blocker to stream.
 *
 * @return \p os, for chaining.
 *
 * @pre None.
 * @post \p b is unchanged; \p os has the debug string appended.
 */
template <blockable Signal>
auto operator<<(std::ostream& os, emit_blocker<Signal> const& b) -> std::ostream& {
  return os << to_string(b);
}

}  // namespace nexenne::signal

/**
 * @brief Formats a \c connection via \c nexenne::signal::to_string.
 */
template <>
struct std::formatter<nexenne::signal::connection> {
  /**
   * @brief Accepts an empty format spec.
   *
   * @param ctx Parse context positioned at the format spec.
   *
   * @return Iterator to the end of the parsed spec.
   *
   * @pre The spec between the braces is empty.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the connection's debug string to the output context.
   *
   * @tparam FormatContext Deduced output context type.
   * @param c Connection to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post \p c is unchanged; its debug string has been written to \p ctx.
   */
  template <typename FormatContext>
  static auto format(nexenne::signal::connection const& c, FormatContext& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::signal::to_string(c));
  }
};

/**
 * @brief Formats a \c static_connection via \c nexenne::signal::to_string.
 */
template <>
struct std::formatter<nexenne::signal::static_connection> {
  /**
   * @brief Accepts an empty format spec.
   *
   * @param ctx Parse context positioned at the format spec.
   *
   * @return Iterator to the end of the parsed spec.
   *
   * @pre The spec between the braces is empty.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the connection's debug string to the output context.
   *
   * @tparam FormatContext Deduced output context type.
   * @param c Connection to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post \p c is unchanged; its debug string has been written to \p ctx.
   */
  template <typename FormatContext>
  static auto format(nexenne::signal::static_connection const& c, FormatContext& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::signal::to_string(c));
  }
};

/**
 * @brief Formats a \c scoped_connection via \c nexenne::signal::to_string.
 *
 * @pre None.
 * @post None.
 */
template <>
struct std::formatter<nexenne::signal::scoped_connection> {
  /**
   * @brief Accepts an empty format spec.
   *
   * @param ctx Parse context positioned at the format spec.
   *
   * @return Iterator to the end of the parsed spec.
   *
   * @pre The spec between the braces is empty.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the scoped connection's debug string to the output context.
   *
   * @tparam FormatContext Deduced output context type.
   * @param c Scoped connection to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post \p c is unchanged; its debug string has been written to \p ctx.
   */
  template <typename FormatContext>
  static auto format(nexenne::signal::scoped_connection const& c, FormatContext& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::signal::to_string(c));
  }
};

/**
 * @brief Formats a \c static_scoped_connection via \c nexenne::signal::to_string.
 *
 * @pre None.
 * @post None.
 */
template <>
struct std::formatter<nexenne::signal::static_scoped_connection> {
  /**
   * @brief Accepts an empty format spec.
   *
   * @param ctx Parse context positioned at the format spec.
   *
   * @return Iterator to the end of the parsed spec.
   *
   * @pre The spec between the braces is empty.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the scoped connection's debug string to the output context.
   *
   * @tparam FormatContext Deduced output context type.
   * @param c Scoped connection to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post \p c is unchanged; its debug string has been written to \p ctx.
   */
  template <typename FormatContext>
  static auto format(nexenne::signal::static_scoped_connection const& c, FormatContext& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::signal::to_string(c));
  }
};

/**
 * @brief Formats a \c slot via \c nexenne::signal::to_string.
 *
 * @tparam Capacity Maximum number of connections the slot tracks.
 *
 * @pre None.
 * @post None.
 */
template <std::size_t Capacity>
struct std::formatter<nexenne::signal::slot<Capacity>> {
  /**
   * @brief Accepts an empty format spec.
   *
   * @param ctx Parse context positioned at the format spec.
   *
   * @return Iterator to the end of the parsed spec.
   *
   * @pre The spec between the braces is empty.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the slot's debug string to the output context.
   *
   * @tparam FormatContext Deduced output context type.
   * @param s Slot to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post \p s is unchanged; its debug string has been written to \p ctx.
   */
  template <typename FormatContext>
  static auto format(nexenne::signal::slot<Capacity> const& s, FormatContext& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::signal::to_string(s));
  }
};

/**
 * @brief Formats a \c static_slot via \c nexenne::signal::to_string.
 *
 * @tparam Capacity Maximum number of connections the slot tracks.
 *
 * @pre None.
 * @post None.
 */
template <std::size_t Capacity>
struct std::formatter<nexenne::signal::static_slot<Capacity>> {
  /**
   * @brief Accepts an empty format spec.
   *
   * @param ctx Parse context positioned at the format spec.
   *
   * @return Iterator to the end of the parsed spec.
   *
   * @pre The spec between the braces is empty.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the slot's debug string to the output context.
   *
   * @tparam FormatContext Deduced output context type.
   * @param s Slot to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post \p s is unchanged; its debug string has been written to \p ctx.
   */
  template <typename FormatContext>
  static auto format(nexenne::signal::static_slot<Capacity> const& s, FormatContext& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::signal::to_string(s));
  }
};

/**
 * @brief Formats a \c signal via \c nexenne::signal::to_string.
 *
 * @tparam R Slot return type.
 * @tparam Args Slot argument types.
 * @tparam SlotCapacity Inline byte capacity of each slot callable.
 *
 * @pre None.
 * @post None.
 */
template <typename R, typename... Args, std::size_t SlotCapacity>
struct std::formatter<nexenne::signal::signal<R(Args...), SlotCapacity>> {
  /**
   * @brief Accepts an empty format spec.
   *
   * @param ctx Parse context positioned at the format spec.
   *
   * @return Iterator to the end of the parsed spec.
   *
   * @pre The spec between the braces is empty.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the signal's debug string to the output context.
   *
   * @tparam FormatContext Deduced output context type.
   * @param sig Signal to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post \p sig is unchanged; its debug string has been written to \p ctx.
   */
  template <typename FormatContext>
  static auto
  format(nexenne::signal::signal<R(Args...), SlotCapacity> const& sig, FormatContext& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::signal::to_string(sig));
  }
};

/**
 * @brief Formats a \c sink via \c nexenne::signal::to_string.
 *
 * @tparam R Slot return type.
 * @tparam Args Slot argument types.
 * @tparam SlotCapacity Inline byte capacity of each slot callable.
 *
 * @pre None.
 * @post None.
 */
template <typename R, typename... Args, std::size_t SlotCapacity>
struct std::formatter<nexenne::signal::sink<R(Args...), SlotCapacity>> {
  /**
   * @brief Accepts an empty format spec.
   *
   * @param ctx Parse context positioned at the format spec.
   *
   * @return Iterator to the end of the parsed spec.
   *
   * @pre The spec between the braces is empty.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the sink's debug string to the output context.
   *
   * @tparam FormatContext Deduced output context type.
   * @param s Sink to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre The signal \p s binds is alive.
   * @post \p s is unchanged; its debug string has been written to \p ctx.
   */
  template <typename FormatContext>
  static auto format(nexenne::signal::sink<R(Args...), SlotCapacity> const& s, FormatContext& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::signal::to_string(s));
  }
};

/**
 * @brief Formats a \c static_signal via \c nexenne::signal::to_string.
 *
 * @tparam R Slot return type.
 * @tparam Args Slot argument types.
 * @tparam MaxSlots Inline slot capacity.
 * @tparam SlotCapacity Inline byte capacity of each slot callable.
 *
 * @pre None.
 * @post None.
 */
template <typename R, typename... Args, std::size_t MaxSlots, std::size_t SlotCapacity>
struct std::formatter<nexenne::signal::static_signal<R(Args...), MaxSlots, SlotCapacity>> {
  /**
   * @brief Accepts an empty format spec.
   *
   * @param ctx Parse context positioned at the format spec.
   *
   * @return Iterator to the end of the parsed spec.
   *
   * @pre The spec between the braces is empty.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the signal's debug string to the output context.
   *
   * @tparam FormatContext Deduced output context type.
   * @param sig Signal to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post \p sig is unchanged; its debug string has been written to \p ctx.
   */
  template <typename FormatContext>
  static auto format(
    nexenne::signal::static_signal<R(Args...), MaxSlots, SlotCapacity> const& sig,
    FormatContext& ctx
  ) {
    return std::format_to(ctx.out(), "{}", nexenne::signal::to_string(sig));
  }
};

/**
 * @brief Formats a \c static_sink via \c nexenne::signal::to_string.
 *
 * @tparam R Slot return type.
 * @tparam Args Slot argument types.
 * @tparam MaxSlots Inline slot capacity.
 * @tparam SlotCapacity Inline byte capacity of each slot callable.
 *
 * @pre None.
 * @post None.
 */
template <typename R, typename... Args, std::size_t MaxSlots, std::size_t SlotCapacity>
struct std::formatter<nexenne::signal::static_sink<R(Args...), MaxSlots, SlotCapacity>> {
  /**
   * @brief Accepts an empty format spec.
   *
   * @param ctx Parse context positioned at the format spec.
   *
   * @return Iterator to the end of the parsed spec.
   *
   * @pre The spec between the braces is empty.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the sink's debug string to the output context.
   *
   * @tparam FormatContext Deduced output context type.
   * @param s Sink to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre The signal \p s binds is alive.
   * @post \p s is unchanged; its debug string has been written to \p ctx.
   */
  template <typename FormatContext>
  static auto format(
    nexenne::signal::static_sink<R(Args...), MaxSlots, SlotCapacity> const& s, FormatContext& ctx
  ) {
    return std::format_to(ctx.out(), "{}", nexenne::signal::to_string(s));
  }
};

/**
 * @brief Formats an \c emit_blocker via \c nexenne::signal::to_string.
 *
 * @tparam Signal The guarded \c blockable type.
 *
 * @pre None.
 * @post None.
 */
template <nexenne::signal::blockable Signal>
struct std::formatter<nexenne::signal::emit_blocker<Signal>> {
  /**
   * @brief Accepts an empty format spec.
   *
   * @param ctx Parse context positioned at the format spec.
   *
   * @return Iterator to the end of the parsed spec.
   *
   * @pre The spec between the braces is empty.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the blocker's debug string to the output context.
   *
   * @tparam FormatContext Deduced output context type.
   * @param b Blocker to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post \p b is unchanged; its debug string has been written to \p ctx.
   */
  template <typename FormatContext>
  static auto format(nexenne::signal::emit_blocker<Signal> const& b, FormatContext& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::signal::to_string(b));
  }
};
