#pragma once

/**
 * @file
 * @brief Logging sink that forwards records to ESP-IDF's \c esp_log.
 *
 * Lets the nexenne logging front end (levels, \c std::format messages, async
 * backend thread) share ESP-IDF's log stream: the already-formatted message is
 * emitted through \c esp_log_write at the mapped level with the logger's name as
 * the ESP-IDF tag, so it interleaves with component logs and obeys the IDF
 * per-tag level controls. Note that \c esp_log_write emits the raw message; the
 * "I (123) tag:" prefix and ANSI colour the \c ESP_LOGx macros add live in those
 * macros, not in \c esp_log_write, so they are not applied here.
 *
 * This header requires ESP-IDF and is intentionally excluded from the umbrella
 * header and the host test build; include it explicitly on an ESP-IDF target:
 * @code
 * manager.add_sink(std::make_shared<nexenne::logging::esp_log_sink>());
 * @endcode
 */

#if !__has_include(<esp_log.h>)
#  error                                                                                           \
    "nexenne/logging/esp_log_sink.hpp requires ESP-IDF (esp_log.h); include it only on an ESP-IDF target, not on host builds."
#endif

#include <esp_log.h>
#include <nexenne/logging/level.hpp>
#include <nexenne/logging/record.hpp>
#include <nexenne/logging/sink.hpp>

namespace nexenne::logging {

/**
 * @brief Sink that writes each record's message to ESP-IDF's \c esp_log_write.
 *
 * @pre None.
 * @post None.
 */
class esp_log_sink final : public sink {
protected:
  auto write_out(record const& r) noexcept -> void override {
    // r.logger_name is an interned view (null-terminated storage); fall back to
    // a fixed tag when empty so esp_log_write never sees a null pointer.
    auto const* const tag{r.logger_name.empty() ? "log" : r.logger_name.data()};
    esp_log_write(map_level(r.severity), tag, "%s\n", r.message.c_str());
  }

  auto flush_out() noexcept -> void override {}

private:
  [[nodiscard]] static auto map_level(level const l) noexcept -> esp_log_level_t {
    switch (l) {
      case level::trace:
        return ESP_LOG_VERBOSE;
      case level::debug:
        return ESP_LOG_DEBUG;
      case level::info:
        return ESP_LOG_INFO;
      case level::warn:
        return ESP_LOG_WARN;
      case level::error:
        return ESP_LOG_ERROR;
      case level::critical:
        return ESP_LOG_ERROR;
      case level::off:
        return ESP_LOG_NONE;
    }
    return ESP_LOG_INFO;
  }
};

}  // namespace nexenne::logging
