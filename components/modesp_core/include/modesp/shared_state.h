/**
 * @file shared_state.h
 * @brief Thread-safe key-value сховище стану
 *
 * Централізоване сховище для обміну даними між модулями.
 * Використовує etl::unordered_map з фіксованим розміром.
 *
 * Характеристики:
 *   - Zero heap allocation
 *   - Thread-safe (FreeRTOS mutex)
 *   - O(1) average доступ
 *
 * SIZE. This object is static — it is internal .bss, on a board where internal .bss is what
 * the SDIO co-processor's receive pool and the I2S DMA descriptors are left to share. The
 * header used to claim "~6KB RAM at 96 entries"; the measured figure at MODESP_MAX_STATE_ENTRIES
 * = 189 with a 256-char inline string was 67140 bytes, because every slot pays for the widest
 * value any key might hold. Two decisions follow:
 *
 *   - the inline string is 64, which is the widest SHORT value in the manifests (radio.name);
 *   - the handful of keys that carry a PATH or a URI are marked `"long": true` in their
 *     manifest and keep their text in the side table below — STATE_LONG_KEYS in the
 *     generated state_meta.h is that list, so the manifests and this class cannot drift.
 *
 * A cut path is a file that cannot be opened and a cut URI is a track whose completion is
 * never recognised (dlna_module compares them with strcmp), which is why those keys get the
 * room and a label does not.
 */

#pragma once

#include "modesp/types.h"
#include "etl/unordered_map.h"
#include "etl/vector.h"
#include "etl/optional.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "state_meta.h"  // MODESP_MAX_STATE_ENTRIES (auto-generated)

namespace modesp {

/// Максимум змінених ключів між WS broadcast-ами.
/// При переповненні — fallback на повну серіалізацію.
static constexpr size_t MAX_CHANGED_KEYS = 32;

class SharedState {
public:
    SharedState();
    ~SharedState();

    // Не копіюється
    SharedState(const SharedState&) = delete;
    SharedState& operator=(const SharedState&) = delete;

    // ── Основний API ──
    /// @param track_change true = додати до changed_keys_ для WS broadcast.
    ///   false = тихий set (таймери, лічильники — не тригерять WS delta).
    bool set(const StateKey& key, const StateValue& value, bool track_change = true);
    etl::optional<StateValue> get(const StateKey& key) const;
    bool has(const StateKey& key) const;
    bool remove(const StateKey& key);
    size_t size() const;
    void clear();

    // ── Зручні обгортки ──
    bool set(const char* key, int32_t value, bool track_change = true);
    bool set(const char* key, float value, bool track_change = true);
    bool set(const char* key, bool value, bool track_change = true);
    /// Every string write comes through here. Text longer than the key holds is cut on a
    /// character boundary, and a character the text already ended halfway through is dropped
    /// (modesp/utf8.h): what is stored is never half a letter.
    bool set(const char* key, const char* value, bool track_change = true);

    etl::optional<StateValue> get(const char* key) const;

    /// The FULL text of a string value, copied into `out` (zero-heap, always
    /// NUL-terminated). For a key in STATE_LONG_KEYS this reads the side table; for every
    /// other key it reads the variant. False, and `out` left empty, when the key is missing
    /// or does not hold a string. An `out` too small for the value gets the whole characters
    /// that fit, never part of one.
    ///
    /// get() cannot do this: it returns a StateValue by value, and a StateValue's string is
    /// the inline one. Anything that needs a path or a URI back out of the store comes
    /// through here — BaseModule::read_string() does.
    bool get_string(const StateKey& key, char* out, size_t out_sz) const;
    bool get_string(const char* key, char* out, size_t out_sz) const;

    /// How many side-table slots are in use, for tests and diagnostics.
    size_t long_count() const;

    // ── Ітерація (для серіалізації, з блокуванням!) ──
    using Map = etl::unordered_map<StateKey, StateValue, MODESP_MAX_STATE_ENTRIES>;

    // Callback викликається під mutex — не робити довгих операцій!
    /// @param text  For a string value: the WHOLE of it — the side table's copy when the key
    ///              is a long one, the variant's own buffer otherwise. nullptr for the
    ///              numeric and boolean alternatives. Serialise from this, not from
    ///              etl::get<StringValue>(value), which for a long key holds only as much of
    ///              the text as fits inline.
    using IterCallback = void(*)(const StateKey& key, const StateValue& value,
                                 const char* text, void* user_data);

    /// Ітерація по ВСІХ ключах (для GET /api/state, initial WS)
    void for_each(IterCallback cb, void* user_data) const;

    /// Ітерація лише по змінених ключах + атомарне очищення.
    /// Повертає true якщо були зміни.
    bool for_each_changed_and_clear(IterCallback cb, void* user_data);

    /// true якщо є зміни для WS broadcast
    bool has_changes() const;

    /// true якщо changed_keys_ переповнився — потрібен full broadcast
    bool needs_full_broadcast() const;

    // Version counter — incremented on every tracked set().
    uint32_t version() const;

    // BUG-018: лічильник відмов set() для діагностики
    uint32_t set_failures() const { return set_failures_; }

    // ── Persist callback ──
    // Викликається ПІСЛЯ set() якщо значення змінилось (незалежно від track_change).
    // PersistService реєструє callback для збереження в NVS.
    using PersistCallback = void(*)(const StateKey& key, const StateValue& value, void* user_data);
    void set_persist_callback(PersistCallback cb, void* user_data = nullptr);

private:
    /// One entry of the side table. There is one of these per key in STATE_LONG_KEYS, not
    /// one per map slot, which is the whole saving.
    struct LongValue {
        StateKey        key;
        LongStringValue text;
    };

    /// Common path for every set(): `long_text` non-null means the value belongs in the side
    /// table — its first `long_len` bytes, already cut to fit — and then `value` carries only
    /// the inline prefix (kept so has(), size(), for_each() and the "this key is a string"
    /// type tag all still work).
    bool set_impl(const StateKey& key, const StateValue& value, const char* long_text,
                  size_t long_len, bool track_change);
    /// Side-table lookup. The caller must hold mutex_.
    LongValue*       find_long(const StateKey& key);
    const LongValue* find_long(const StateKey& key) const;
    /// What an IterCallback gets as `text`. The caller must hold mutex_.
    const char*      text_of(const StateKey& key, const StateValue& value) const;

    Map map_;
    /// Sized by the generator from the number of `long` keys in the manifests, so it cannot
    /// be too small for the keys that exist.
    etl::vector<LongValue, MODESP_LONG_STATE_ENTRIES> long_values_;
    mutable SemaphoreHandle_t mutex_;
    uint32_t version_ = 0;
    uint32_t set_failures_ = 0;  // BUG-018
    PersistCallback persist_cb_ = nullptr;
    void* persist_user_data_ = nullptr;

    // ── Delta tracking для WS broadcasts ──
    etl::vector<StateKey, MAX_CHANGED_KEYS> changed_keys_;
    bool force_full_ = false;  // true якщо changed_keys_ переповнився
};

} // namespace modesp
