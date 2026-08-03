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
#error                                                                                           \
    "nexenne/logging/esp_log_sink.hpp requires ESP-IDF (esp_log.h); include it only on an ESP-IDF target, not on host builds."
#endif

#include <algorithm>
#include <array>
#include <cstring>
#include <string_view>

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
  /**
   * @brief Forwards \p r to ESP-IDF via \c esp_log_write at the mapped level.
   *
   * Copies the logger name into a bounded, null-terminated tag buffer (the
   * record only guarantees the name outlives the record, not that it is
   * terminated) and emits the already-formatted message at the IDF level that
   * \c map_level assigns to the severity.
   *
   * @param r Record to emit.
   *
   * @pre None.
   * @post The message has been handed to \c esp_log_write with the logger name
   *       as the tag.
   */
  auto write_out(record const& r) noexcept -> void override {
    // record only guarantees the name outlives the record, not that it is
    // null-terminated, so copy it into a bounded, explicitly terminated buffer
    // rather than handing esp_log_write a possibly-unterminated data() pointer
    // (a non-interned view built with substr would otherwise read past its end).
    auto tag{std::array<char, 32>{}};
    auto const name{r.logger_name.empty() ? std::string_view{"log"} : r.logger_name};
    auto const n{std::min(name.size(), tag.size() - 1)};
    std::memcpy(tag.data(), name.data(), n);
    tag[n] = '\0';
    esp_log_write(map_level(r.severity), tag.data(), "%s\n", r.message.c_str());
  }

  /**
   * @brief No-op flush; this sink does not buffer \c esp_log_write output.
   *
   * @pre None.
   * @post None.
   */
  auto flush_out() noexcept -> void override {}

private:
  /**
   * @brief Maps a nexenne severity to the matching ESP-IDF log level.
   *
   * \c level::critical folds onto \c ESP_LOG_ERROR, since ESP-IDF has no
   * distinct critical level.
   *
   * @param l Severity to map.
   *
   * @return The corresponding \c esp_log_level_t.
   *
   * @pre None.
   * @post None.
   *
   * @complexity \c O(1).
   */
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
