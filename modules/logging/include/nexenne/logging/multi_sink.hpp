#pragma once

/**
 * @file
 * @brief Composite sink that fans every record out to several child sinks.
 *
 * Attach a console sink, a file sink, and an in-memory ring sink to one
 * \c multi_sink and connect it to a logger: every record then reaches all
 * three from a single log call. Children are owned via \c std::unique_ptr and
 * dispatched in insertion order. Because each child applies its own
 * minimum-level filter (through the base \c sink::write), a record may reach
 * some children and be dropped by others.
 */

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include <nexenne/logging/record.hpp>
#include <nexenne/logging/sink.hpp>

namespace nexenne::logging {

/**
 * @brief Composite sink that fans every record out to multiple child sinks.
 *
 * Each child is owned via \c std::unique_ptr; ownership transfers on \c add.
 * Records are dispatched in insertion order, and each child applies its own
 * per-sink level filter. Null children are rejected by \c add, so the fan-out
 * never has to guard against them.
 *
 * @pre None.
 * @post A default-constructed \c multi_sink has no children.
 */
class multi_sink final : public sink {
public:
  /**
   * @brief Constructs a multi_sink with no children.
   *
   * @pre None.
   * @post \c child_count() is zero.
   */
  multi_sink() noexcept = default;

  /**
   * @brief Adds a child sink, taking ownership of it.
   *
   * Null pointers are ignored. Not \c noexcept: appending to the child vector
   * may reallocate, and \c std::bad_alloc is allowed to propagate per the
   * module's error policy.
   *
   * @param child Sink to adopt; a null pointer is a no-op.
   *
   * @pre None.
   * @post \c child_count() has grown by one when \p child is non-null.
   * @throws std::bad_alloc if the child vector cannot grow.
   *
   * @complexity Amortised \c O(1).
   */
  auto add(std::unique_ptr<sink> child) -> void {
    if (child != nullptr) {
      m_children.push_back(std::move(child));
    }
  }

  /**
   * @brief Number of child sinks.
   *
   * @return The current child count.
   *
   * @pre None.
   * @post None.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] auto child_count() const noexcept -> std::size_t {
    return m_children.size();
  }

protected:
  /**
   * @brief Fans \p r out to every child, in insertion order.
   *
   * Each child applies its own level filter through \c sink::write, so the base
   * \c multi_sink filter is the only gate the composite itself imposes.
   *
   * @param r Record to dispatch.
   *
   * @pre None.
   * @post Every child has been offered \p r.
   *
   * @complexity \c O(child_count()).
   */
  auto write_out(record const& r) noexcept -> void override {
    for (auto const& child : m_children) {
      child->write(r);
    }
  }

  /**
   * @brief Flushes every child, in insertion order.
   *
   * @pre None.
   * @post Every child has been flushed.
   *
   * @complexity \c O(child_count()).
   */
  auto flush_out() noexcept -> void override {
    for (auto const& child : m_children) {
      child->flush();
    }
  }

private:
  std::vector<std::unique_ptr<sink>> m_children{};  ///< Owned children, in insertion order.
};

}  // namespace nexenne::logging
