/**
 * @file shared_state.cpp
 * @brief Реалізація thread-safe key-value сховища
 */

#include "modesp/shared_state.h"
#include "modesp/utf8.h"
#include <cstring>
#include "esp_log.h"

static const char* TAG = "SharedState";

namespace modesp {

SharedState::SharedState() {
    mutex_ = xSemaphoreCreateMutex();
    configASSERT(mutex_ != nullptr);
}

SharedState::~SharedState() {
    if (mutex_) {
        vSemaphoreDelete(mutex_);
    }
}

SharedState::LongValue* SharedState::find_long(const StateKey& key) {
    for (auto& e : long_values_) if (e.key == key) return &e;
    return nullptr;
}

const SharedState::LongValue* SharedState::find_long(const StateKey& key) const {
    for (const auto& e : long_values_) if (e.key == key) return &e;
    return nullptr;
}

bool SharedState::set(const StateKey& key, const StateValue& value, bool track_change) {
    // A caller that reaches the generic setter with a string for a long key has already
    // clipped it to the inline width — nothing can be recovered here. What CAN be kept is
    // the invariant that the side table and the map never disagree about a key's value,
    // which is what get_string() and change detection both rest on.
    const auto* sp = etl::get_if<StringValue>(&value);
    if (sp && gen::is_long_state_key(key.c_str()))
        return set_impl(key, value, sp->c_str(), sp->size(), track_change);
    return set_impl(key, value, nullptr, 0, track_change);
}

bool SharedState::set_impl(const StateKey& key, const StateValue& value, const char* long_text,
                           size_t long_len, bool track_change) {
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        set_failures_++;  // BUG-018
        ESP_LOGE(TAG, "Mutex timeout on set('%s') [failures=%lu]",
                 key.c_str(), (unsigned long)set_failures_);
        return false;
    }

    if (map_.full() && !map_.count(key)) {
        set_failures_++;
        // Знімаємо дані до release mutex, логуємо ПІСЛЯ — інакше ризик deadlock
        // (ESP_LOG бере свій внутрішній lock; якщо інший core тримає log lock і чекає
        // state mutex — класичний ABBA deadlock на двоядерному ESP32)
        int sz = (int)map_.size();
        unsigned long failures = (unsigned long)set_failures_;
        xSemaphoreGive(mutex_);
        ESP_LOGE(TAG, "STATE MAP FULL (%d/%d) — cannot add '%s' [failures=%lu]",
                 sz, MODESP_MAX_STATE_ENTRIES, key.c_str(), failures);
        return false;
    }

    // Перевірка чи значення змінилось (для persist callback)
    bool changed = true;
    if (long_text) {
        // Compare the SIDE TABLE, never the inline prefix. Two URLs that differ only in a
        // query string share their first 64 characters, and comparing prefixes would call
        // that "unchanged" — no WS delta, no persist callback, a stale screen.
        LongValue* slot = find_long(key);
        if (!slot) {
            if (long_values_.full()) {
                // Cannot happen with a generated capacity: it is the count of `long`
                // keys in the manifests. If it ever does, say so and refuse the write
                // rather than store a value get_string() would hand back half of.
                set_failures_++;
                int n = (int)long_values_.size();
                unsigned long failures = (unsigned long)set_failures_;
                xSemaphoreGive(mutex_);
                ESP_LOGE(TAG, "LONG TABLE FULL (%d) — '%s' kept as a prefix only [failures=%lu]",
                         n, key.c_str(), failures);
                return false;
            }
            long_values_.push_back(LongValue{key, LongStringValue()});
            slot = &long_values_.back();
        } else {
            changed = slot->text.size() != long_len ||
                      memcmp(slot->text.data(), long_text, long_len) != 0;
        }
        slot->text.assign(long_text, long_len);
    }

    auto it = map_.find(key);
    if (it != map_.end() && !long_text) {
        changed = !(it->second == value);
    }

    map_[key] = value;

    if (changed) {
        // Інкрементуємо version тільки при реальній зміні (BUG-017 fix)
        if (track_change) {
            version_++;
        }

        // Delta tracking: додаємо ключ до changed_keys_ (без дублікатів)
        if (track_change) {
            bool found = false;
            for (const auto& k : changed_keys_) {
                if (k == key) { found = true; break; }
            }
            if (!found) {
                if (!changed_keys_.full()) {
                    changed_keys_.push_back(key);
                } else {
                    force_full_ = true;  // Переповнення — fallback на full broadcast
                }
            }
        }
    }

    // Зберігаємо callback локально перед звільненням mutex
    auto cb = persist_cb_;
    auto ud = persist_user_data_;
    xSemaphoreGive(mutex_);

    // Persist callback ПОЗА mutex — незалежно від track_change
    if (changed && cb) {
        cb(key, value, ud);
    }

    return true;
}

etl::optional<StateValue> SharedState::get(const StateKey& key) const {
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        return etl::nullopt;
    }

    auto it = map_.find(key);
    etl::optional<StateValue> result;
    if (it != map_.end()) {
        result = it->second;
    }
    xSemaphoreGive(mutex_);
    return result;
}

bool SharedState::has(const StateKey& key) const {
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    bool result = map_.count(key) > 0;
    xSemaphoreGive(mutex_);
    return result;
}

bool SharedState::remove(const StateKey& key) {
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    size_t removed = map_.erase(key);
    // The side table IS the value, not a cache of it: an entry left behind would come back
    // as the answer the next time that key is read.
    for (auto it = long_values_.begin(); it != long_values_.end(); ++it) {
        if (it->key == key) { long_values_.erase(it); break; }
    }
    xSemaphoreGive(mutex_);
    return removed > 0;
}

size_t SharedState::size() const {
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return 0;
    size_t s = map_.size();
    xSemaphoreGive(mutex_);
    return s;
}

void SharedState::clear() {
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return;
    map_.clear();
    long_values_.clear();
    xSemaphoreGive(mutex_);
}

size_t SharedState::long_count() const {
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return 0;
    size_t n = long_values_.size();
    xSemaphoreGive(mutex_);
    return n;
}

// ── Зручні обгортки ──

bool SharedState::set(const char* key, int32_t value, bool track_change) {
    return set(StateKey(key), StateValue(value), track_change);
}

bool SharedState::set(const char* key, float value, bool track_change) {
    return set(StateKey(key), StateValue(value), track_change);
}

bool SharedState::set(const char* key, bool value, bool track_change) {
    return set(StateKey(key), StateValue(value), track_change);
}

bool SharedState::set(const char* key, const char* value, bool track_change) {
    if (!value) value = "";
    // This overload is the only one that still has the whole text in its hands, so it is
    // where a long key has to be recognised. Every write path arrives here: the module
    // helper, the HTTP writer and the MQTT writer all pass a const char*.
    //
    // It is also the one place every string passes on its way in, so it is where the cut is
    // made on a character boundary. etl::string would stop at its capacity mid-character, and
    // a value some source already cut on a byte count (a 63-byte title buffer, say) would
    // arrive here ending in half a letter; utf8_fit() takes care of both. The WebSocket
    // writer turns that half into U+FFFD, which is how it was seen: "…Слаане�".
    const size_t len = strlen(value);
    if (gen::is_long_state_key(key)) {
        const size_t n = utf8_fit(value, len, MODESP_MAX_LONG_STRING_LENGTH);
        if (len > MODESP_MAX_LONG_STRING_LENGTH) {
            ESP_LOGW(TAG, "'%s': %u bytes cut to %u", key, (unsigned)len, (unsigned)n);
        }
        // The map keeps the inline prefix: it is the type tag ("this key holds a string"),
        // it keeps has()/size()/for_each() working unchanged, and it is a sane fallback for
        // a reader that has not been taught about the side table. The truth is in the table.
        const StringValue prefix(value, utf8_fit(value, n, MODESP_MAX_STRING_VALUE_LENGTH));
        return set_impl(StateKey(key), StateValue(prefix), value, n, track_change);
    }
    // etl::string truncates to capacity without a word, which turns "this value is too
    // long" into "this value is wrong" somewhere far away — a path that cannot be opened,
    // a title that ends mid-word. Cheap to notice here, expensive to find from the symptom.
    const size_t n = utf8_fit(value, len, MODESP_MAX_STRING_VALUE_LENGTH);
    if (len > MODESP_MAX_STRING_VALUE_LENGTH) {
        ESP_LOGW(TAG, "'%s': %u bytes cut to %u; mark it `long` in the manifest if "
                      "that matters", key, (unsigned)len, (unsigned)n);
    }
    return set_impl(StateKey(key), StateValue(StringValue(value, n)), nullptr, 0, track_change);
}

etl::optional<StateValue> SharedState::get(const char* key) const {
    return get(StateKey(key));
}

bool SharedState::get_string(const StateKey& key, char* out, size_t out_sz) const {
    if (!out || out_sz == 0) return false;
    out[0] = '\0';
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return false;

    const char* src = nullptr;
    if (const LongValue* lv = find_long(key)) {
        src = lv->text.c_str();
    } else {
        auto it = map_.find(key);
        if (it != map_.end()) {
            if (const auto* sp = etl::get_if<StringValue>(&it->second)) src = sp->c_str();
        }
    }
    bool ok = false;
    if (src) {
        // A buffer narrower than the value gets as many WHOLE characters as fit: the screen
        // reads 256-byte keys into 96- and 128-byte fields.
        utf8_copy(out, out_sz, src, strlen(src));
        ok = true;
    }
    xSemaphoreGive(mutex_);
    return ok;
}

bool SharedState::get_string(const char* key, char* out, size_t out_sz) const {
    return get_string(StateKey(key), out, out_sz);
}

// ── Version ──

uint32_t SharedState::version() const {
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return 0;
    uint32_t v = version_;
    xSemaphoreGive(mutex_);
    return v;
}

// ── Delta tracking ──

bool SharedState::has_changes() const {
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    bool result = !changed_keys_.empty() || force_full_;
    xSemaphoreGive(mutex_);
    return result;
}

bool SharedState::needs_full_broadcast() const {
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    bool result = force_full_;
    xSemaphoreGive(mutex_);
    return result;
}

const char* SharedState::text_of(const StateKey& key, const StateValue& value) const {
    const auto* sp = etl::get_if<StringValue>(&value);
    if (!sp) return nullptr;                    // not a string — nothing to hand over
    const LongValue* lv = find_long(key);
    return lv ? lv->text.c_str() : sp->c_str();
}

bool SharedState::for_each_changed_and_clear(IterCallback cb, void* user_data) {
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return false;

    bool had_changes = false;

    if (force_full_) {
        // Переповнення changed_keys_ — серіалізуємо ВСІ ключі
        for (auto& pair : map_) {
            cb(pair.first, pair.second, text_of(pair.first, pair.second), user_data);
        }
        had_changes = true;
    } else {
        // Серіалізуємо тільки змінені ключі
        for (const auto& key : changed_keys_) {
            auto it = map_.find(key);
            if (it != map_.end()) {
                cb(it->first, it->second, text_of(it->first, it->second), user_data);
                had_changes = true;
            }
        }
    }

    changed_keys_.clear();
    force_full_ = false;

    xSemaphoreGive(mutex_);
    return had_changes;
}

// ── Persist callback ──

void SharedState::set_persist_callback(PersistCallback cb, void* user_data) {
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return;
    persist_cb_ = cb;
    persist_user_data_ = user_data;
    xSemaphoreGive(mutex_);
}

// ── Ітерація ──

void SharedState::for_each(IterCallback cb, void* user_data) const {
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return;
    for (auto& pair : map_) {
        cb(pair.first, pair.second, text_of(pair.first, pair.second), user_data);
    }
    xSemaphoreGive(mutex_);
}

} // namespace modesp
