#include <cassert>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

#include <nexenne/logging/async_sink.hpp>
#include <nexenne/logging/record.hpp>
#include <nexenne/logging/sink.hpp>

namespace nexenne::logging {

async_sink::async_sink(std::unique_ptr<sink> inner, config const cfg)
    : m_inner{std::move(inner)}, m_cfg{cfg} {
  assert(m_inner != nullptr && "async_sink requires a non-null inner sink");
  assert(m_cfg.queue_size_limit >= 1 && "async_sink queue_size_limit must be at least 1");
  m_worker = std::thread{[this] { run(); }};
}

async_sink::~async_sink() noexcept {
  {
    auto const lk{std::scoped_lock{m_mu}};
    m_stop = true;
  }
  // Wake the worker (it may be waiting for records), any producer parked on
  // the block policy, and any thread parked in flush_out. The flusher matters
  // as much as the producer: the worker's graceful-shutdown exit returns
  // without touching m_drained, so without this a parked flusher waits
  // forever and then has the condition variable destroyed underneath it.
  m_not_empty.notify_all();
  m_not_full.notify_all();
  m_drained.notify_all();
  if (m_worker.joinable()) {
    m_worker.join();
  }
  if (m_inner) {
    m_inner->flush();
  }
}

auto async_sink::write_out(record const& r) noexcept -> void {
  auto lk{std::unique_lock{m_mu}};
  if (m_queue.size() >= m_cfg.queue_size_limit) {
    switch (m_cfg.on_overflow) {
      case overflow_action::block:
        m_not_full.wait(lk, [this] { return m_queue.size() < m_cfg.queue_size_limit || m_stop; });
        if (m_stop) {
          return;
        }
        break;
      case overflow_action::drop_oldest:
        m_queue.pop();
        break;
      case overflow_action::drop_newest:
        return;
    }
  }
  if (m_stop) {
    return;
  }
  m_queue.push(r);
  lk.unlock();
  m_not_empty.notify_one();
}

auto async_sink::flush_out() noexcept -> void {
  {
    auto lk{std::unique_lock{m_mu}};
    m_drained.wait(lk, [this] { return (m_queue.empty() && !m_processing) || m_stop; });
  }
  if (m_inner) {
    m_inner->flush();
  }
}

auto async_sink::run() noexcept -> void {
  while (true) {
    auto r{record{}};
    {
      auto lk{std::unique_lock{m_mu}};
      m_not_empty.wait(lk, [this] { return m_stop || !m_queue.empty(); });
      if (m_queue.empty()) {
        // The queue is empty; the predicate only also lets us through on a
        // stop request, so this is the graceful-shutdown exit.
        return;
      }
      r = std::move(m_queue.front());
      m_queue.pop();
      m_processing = true;  // a record is now in flight, not yet written
    }
    // A slot just freed up; release a producer parked on the block policy.
    m_not_full.notify_one();
    if (m_inner) {
      m_inner->write(r);
    }
    {
      // The write is complete. Only now, with nothing dequeued and nothing
      // left, is the sink truly drained, so wake flush waiters here rather
      // than right after the pop, which would let flush return with this
      // record still unwritten.
      auto lk{std::unique_lock{m_mu}};
      m_processing = false;
      if (m_queue.empty()) {
        m_drained.notify_all();
      }
    }
  }
}

}  // namespace nexenne::logging
