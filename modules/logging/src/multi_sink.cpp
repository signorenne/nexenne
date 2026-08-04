#include <cstddef>
#include <memory>
#include <utility>

#include <nexenne/logging/multi_sink.hpp>
#include <nexenne/logging/record.hpp>

namespace nexenne::logging {

auto multi_sink::add(std::unique_ptr<sink> child) -> void {
  if (child != nullptr) {
    m_children.push_back(std::move(child));
  }
}

auto multi_sink::child_count() const noexcept -> std::size_t {
  return m_children.size();
}

auto multi_sink::write_out(record const& r) noexcept -> void {
  for (auto const& child : m_children) {
    child->write(r);
  }
}

auto multi_sink::flush_out() noexcept -> void {
  for (auto const& child : m_children) {
    child->flush();
  }
}

}  // namespace nexenne::logging
