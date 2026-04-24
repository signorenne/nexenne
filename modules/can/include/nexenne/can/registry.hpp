#pragma once

/**
 * @file
 * @brief A receive-side index over a database: lookup, filters, and frame decoding.
 *
 * Where a \c database.hpp is the data, a \c registry is the runtime view
 * used while receiving frames. It builds a sorted identifier index for a
 * logarithmic lookup, holds an optional set of \c filter.hpp objects so a
 * node can subscribe to only the messages it cares about, and decodes every
 * signal of a matched frame through a callback so no result container is
 * allocated. The registry does not own the database; the database must outlive
 * it.
 */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>

#include <nexenne/can/codec.hpp>
#include <nexenne/can/database.hpp>
#include <nexenne/can/filter.hpp>
#include <nexenne/can/frame.hpp>
#include <nexenne/can/id.hpp>
#include <nexenne/can/message.hpp>
#include <nexenne/container/small_vector.hpp>

namespace nexenne::can {

/**
 * @brief A sorted identifier index and filter set over a database.
 */
class registry {
public:
  using value_type = message;

private:
  struct index_entry {
    std::uint32_t key;
    std::uint32_t message_index;
  };

  database const* m_database{nullptr};
  container::small_vector<index_entry, 8> m_index{};
  container::small_vector<filter, 4> m_filters{};

  // The lookup key ignores the remote and error flags, so a data frame and the
  // matching remote request resolve to the same message. It masks the identifier
  // to the width the extended-frame flag selects, matching can_id::identifier(),
  // so a standard id carrying junk in bits 11..28 keys the same way database::find
  // would look it up.
  [[nodiscard]] static constexpr auto key_of(can_id const id) noexcept -> std::uint32_t {
    auto const width_mask{(id.raw() & extended_flag) != 0U ? extended_id_mask : standard_id_mask};
    return id.raw() & (extended_flag | width_mask);
  }

public:
  /**
   * @brief Builds a registry indexing the messages of a database.
   *
   * @param db Database to index; it must outlive the registry.
   *
   * @pre \p db outlives the registry.
   * @post \c find resolves any identifier present in \p db and no filters are set.
   */
  explicit registry(database const& db) : m_database{&db} {
    auto const messages{db.messages()};
    for (std::size_t i{0}; i < messages.size(); ++i) {
      m_index.push_back(index_entry{key_of(messages[i].id()), static_cast<std::uint32_t>(i)});
    }
    std::ranges::stable_sort(m_index, {}, &index_entry::key);
  }

  /**
   * @brief Deleted: a registry never binds a temporary database.
   *
   * The registry stores a pointer into the database, so a temporary would dangle
   * the moment the constructing expression ends. Rejecting an rvalue at compile
   * time turns \c registry{database_builder{}.build()} into an error instead of a
   * silent use-after-free.
   */
  registry(database&&) = delete;

  /**
   * @brief Adds an identifier filter.
   *
   * @param f Filter to add.
   *
   * @return Reference to \c *this for chaining.
   *
   * @pre None.
   * @post \c accepts now requires a frame to pass at least one filter.
   */
  auto add_filter(filter const f) -> registry& {
    m_filters.push_back(f);
    return *this;
  }

  /**
   * @brief Reports whether an identifier passes the filter set.
   *
   * @param id Identifier to test.
   *
   * @return \c true when no filters are set, or when \p id matches at least one.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto accepts(can_id const id) const noexcept -> bool {
    if (m_filters.empty()) {
      return true;
    }
    return std::ranges::any_of(m_filters, [id](filter const f) { return f.matches(id); });
  }

  /**
   * @brief The number of indexed messages.
   *
   * @return The count of messages the registry can resolve.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto message_count() const noexcept -> std::size_t {
    return m_index.size();
  }

  /**
   * @brief The number of installed identifier filters.
   *
   * @return The filter count; zero means every identifier is accepted.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto filter_count() const noexcept -> std::size_t {
    return m_filters.size();
  }

  /**
   * @brief Finds the message for an identifier through the index.
   *
   * Ignores filters; this is the raw lookup. Matches the identifier and the
   * extended-frame flag, ignoring the remote and error flags.
   *
   * @param id Identifier to look up.
   *
   * @return A pointer to the matching message, or \c nullptr when none matches.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto find(can_id const id) const noexcept -> message const* {
    auto const key{key_of(id)};
    auto const it{std::ranges::lower_bound(m_index, key, {}, &index_entry::key)};
    if (it == m_index.end() || it->key != key) {
      return nullptr;
    }
    return &m_database->messages()[it->message_index];
  }

  /**
   * @brief Finds the message for a frame, after applying the filters.
   *
   * @param f Frame whose identifier is matched.
   *
   * @return A pointer to the message, or \c nullptr when the frame is filtered
   *         out or no message matches.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto match(frame const& f) const noexcept -> message const* {
    if (!accepts(f.id())) {
      return nullptr;
    }
    return find(f.id());
  }

  /**
   * @brief Decodes every signal of a frame's message, invoking \p fn on each.
   *
   * Looks up the frame's message (honouring filters), then calls
   * \c fn(signal const&, double) once per signal that fits the frame. Signals
   * that run past the frame's data length are skipped, which suits short or
   * multiplexed frames. Nothing is allocated; the callback receives each value.
   *
   * @tparam F Callable invocable as \c fn(signal const&, double).
   * @param f Frame to decode.
   * @param fn Callback receiving each signal and its physical value.
   *
   * @return The number of signals decoded and passed to \p fn.
   *
   * @pre None.
   * @post \p fn has been invoked once for each signal of the matched message
   *       that fits \p f.
   */
  template <typename F>
  auto decode_signals(frame const& f, F&& fn) const -> std::size_t {
    message const* const msg{match(f)};
    if (msg == nullptr) {
      return 0;
    }

    // Read the multiplexor selector first, so multiplexed signals from the wrong
    // group are skipped. A message without a selector decodes everything.
    std::optional<std::uint64_t> selector;
    for (signal_entry const& entry : msg->signals()) {
      if (entry.definition.mux_role() == multiplex_role::selector) {
        if (auto const raw{read_bits(entry.plan, f)}) {
          selector = *raw;
        }
        break;
      }
    }

    std::size_t decoded{0};
    for (signal_entry const& entry : msg->signals()) {
      if (entry.definition.mux_role() == multiplex_role::multiplexed
          && (!selector || *selector != entry.definition.mux_value())) {
        continue;
      }
      // decode_value honours the signal's invalid-value policy, so a field that
      // reads as "not available" yields no value and is skipped.
      if (auto const value{nexenne::can::decode_value(entry.definition, entry.plan, f)};
          value && value->has_value()) {
        fn(entry.definition, **value);
        ++decoded;
      }
    }
    return decoded;
  }

  /**
   * @brief The database the registry indexes.
   *
   * @return Reference to the indexed database.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto source() const noexcept -> database const& {
    return *m_database;
  }
};

}  // namespace nexenne::can
