/**
 * @file error_service.h
 * @brief Централізована обробка помилок + Safe Mode
 *
 * Найважливіший сервіс. Збирає помилки від модулів, зберігає
 * історію останніх 16 помилок, вмикає Safe Mode при FATAL.
 *
 * Safe Mode:
 *   - Публікує MsgSafeMode на шину
 *   - Актуатори повинні вимкнутись при отриманні MsgSafeMode
 *   - Сенсори та моніторинг продовжують працювати
 *   - Очікує ручне втручання або перезавантаження
 *
 * Threading. report() is callable from any task (not from an ISR): the record, the counter
 * and the safe-mode flag are updated under a spinlock, and the log line is written outside
 * it. What report() does NOT do is touch the message bus: ModuleManager::publish() runs
 * every module's on_message() synchronously in the caller's task, so a report from the audio
 * task would have run the whole control plane on the audio task's stack. Bus delivery is
 * queued instead and drained by on_update(), i.e. from the main loop, which is the only
 * place the bus is meant to run (see ModuleManager::publish).
 *
 * error.count is written outside the spinlock too (SharedState takes a mutex), by one
 * reporter at a time and with the count as it then stands: the key never goes back, and it
 * ends at the total.
 */

#pragma once

#include "modesp/base_module.h"
#include "modesp/service_messages.h"
#include "etl/circular_buffer.h"
#include "etl/queue.h"
#include "freertos/FreeRTOS.h"

namespace modesp {

#ifndef MODESP_MAX_ERROR_HISTORY
#define MODESP_MAX_ERROR_HISTORY 16
#endif

/// Records waiting for the main loop to put them on the bus. A burst deeper than this loses
/// bus delivery (never the record or the count) and is counted in dropped_messages().
#ifndef MODESP_ERROR_QUEUE_DEPTH
#define MODESP_ERROR_QUEUE_DEPTH 8
#endif

class ErrorService : public BaseModule {
public:
    ErrorService();

    // ── BaseModule lifecycle ──
    bool on_init() override;
    void on_update(uint32_t dt_ms) override;
    void on_message(const etl::imessage& msg) override;
    void on_stop() override;

    // ── API для модулів (thread-safe: any task, not an ISR) ──
    void report(const char* source, int32_t code,
                ErrorSeverity severity, const char* description);

    // ── Стан ──
    size_t error_count() const { return total_errors_; }
    bool is_safe_mode() const { return safe_mode_; }
    uint32_t dropped_messages() const { return dropped_; }

    // ── Історія (для діагностики; читати з main loop — без блокування) ──
    const etl::circular_buffer<ErrorRecord, MODESP_MAX_ERROR_HISTORY>& history() const {
        return history_;
    }

private:
    /// Deliver what report() queued — main loop only.
    void drain();

    portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;   // guards everything below

    etl::circular_buffer<ErrorRecord, MODESP_MAX_ERROR_HISTORY> history_;
    etl::queue<ErrorRecord, MODESP_ERROR_QUEUE_DEPTH> pending_;   // WARNING and above, for the bus
    etl::string<48> safe_mode_reason_;
    uint32_t total_errors_ = 0;
    uint32_t dropped_ = 0;
    bool safe_mode_ = false;
    bool safe_mode_pending_ = false;   // MsgSafeMode not yet on the bus

    uint32_t uptime_ms_ = 0;           // main loop only
};

} // namespace modesp
