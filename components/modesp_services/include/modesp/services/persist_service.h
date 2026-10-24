/**
 * @file persist_service.h
 * @brief Auto-persist readwrite settings to NVS
 *
 * Зберігає readwrite state keys з persist:true в NVS.
 * При boot — відновлює збережені значення з NVS в SharedState.
 * При зміні — записує в NVS з debounce (5 секунд).
 *
 * NVS key strategy (BUG-012 fix): стабільний хеш від імені ключа
 * (djb2 → "s" + 7 hex chars). The positional keys "p<i>" of the format before it are read
 * by one pass in a device's life, and only on a device that has no hashed key yet — see
 * "Legacy keys, one pass" in persist_service.cpp.
 *
 * NVS namespace: "persist".
 */

#pragma once

#include "modesp/base_module.h"
#include "modesp/shared_state.h"
#include "nvs.h"

namespace modesp {

class PersistService : public BaseModule {
public:
    PersistService();

    bool on_init() override;
    void on_update(uint32_t dt_ms) override;

    /// Ін'єкція залежності — SharedState
    void set_state(SharedState* state) { ext_state_ = state; }

    /// BUG-014: примусовий запис dirty keys в NVS (перед restart)
    void flush_now();

private:
    static constexpr const char* NVS_NAMESPACE = "persist";
    /// Quiet time a value must hold before it is written.
    static constexpr uint32_t DEBOUNCE_MS = 5000;
    /// Ceiling on how long the window may keep being restarted. Without it, a key that
    /// changes faster than DEBOUNCE_MS forever would never be written at all — that is
    /// BUG-025, and restarting the window unconditionally is how it happened.
    static constexpr uint32_t MAX_DEFER_MS = 60000;
    static constexpr size_t MAX_PERSIST_KEYS = gen::STATE_META_COUNT;

    /// Where this namespace stands with the positional keys of the pre-BUG-012 format; i32.
    /// Absent: this code has not run on the device yet. See "Legacy keys, one pass".
    static constexpr const char* LEGACY_MARK_KEY = "legacy_keys";
    static constexpr int32_t LEGACY_MIGRATING = 1;  ///< a migration started: resume it
    static constexpr int32_t LEGACY_RETIRED   = 2;  ///< positional keys are never read again
    /// Positional keys are erased up to STATE_META_COUNT plus this many: they index the table
    /// of whatever firmware wrote them, and that table may have been the longer one.
    static constexpr size_t LEGACY_KEY_MARGIN = 64;

    SharedState* ext_state_ = nullptr;

    // Dirty flags для кожного persist key в STATE_META
    bool dirty_[MAX_PERSIST_KEYS] = {};
    uint32_t debounce_timer_ = 0;
    uint32_t defer_ms_ = 0;        ///< how long the pending write has been held back
    volatile bool changed_ = false;  ///< a persist key moved since the last tick

    /// Відновити збережені значення з NVS → SharedState
    void restore_from_nvs();

    /// Записати dirty keys в NVS
    void flush_to_nvs();

    /// BUG-012: стабільний NVS ключ на основі djb2 хешу імені state key
    static void make_nvs_key(const char* state_key, char* out, size_t out_size);

    /// Legacy: позиційний ключ "p0", "p1"... (для міграції)
    static void make_legacy_nvs_key(size_t index, char* out, size_t out_size);

    /// Is any persisted state stored under its hashed key? Then the device has run the
    /// hashed format before, and positional keys left in it are leftovers.
    static bool has_hashed_key(nvs_handle_t h);

    /// Erase "p0" .. "p<STATE_META_COUNT + LEGACY_KEY_MARGIN - 1>"; returns how many existed.
    static int erase_legacy_keys(nvs_handle_t h);

    /// Persist callback — викликається з SharedState::set()
    static void on_state_changed(const StateKey& key, const StateValue& value, void* user_data);
};

} // namespace modesp
