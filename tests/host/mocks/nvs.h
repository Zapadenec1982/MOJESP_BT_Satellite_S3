/**
 * @file nvs.h
 * @brief HOST BUILD: NVS as an in-memory store, so that the firmware's own nvs_helper.cpp
 *        and persist_service.cpp run on the host unchanged.
 *
 * It keeps the rules of IDF v5.5.5 (components/nvs_flash/src) that PersistService can see:
 *   - a read-only open of a namespace never written fails with ESP_ERR_NVS_NOT_FOUND; a
 *     read-write open creates it, which is itself a write to flash;
 *   - a get of the wrong type fails with ESP_ERR_NVS_TYPE_MISMATCH (Page::findItem), which
 *     is how a stale positional key of another type goes unread;
 *   - a set replaces a value of any type, and setting the value already stored writes
 *     nothing (Storage::writeItem, its cmpItem / cmpMultiPageBlob shortcut);
 *   - erase and find match a key whatever its type; a key is at most 15 characters.
 *
 * host_nvs:: is the test's side: the store, a count of the writes that reached flash, and
 * power_cut_after(n) — the next n writes land and every later one fails, which is what a
 * power cut during boot looks like from the flash's side.
 */
#pragma once

#include "esp_err.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

typedef uint32_t nvs_handle_t;

typedef enum {
    NVS_READONLY,
    NVS_READWRITE,
} nvs_open_mode_t;

typedef enum {
    NVS_TYPE_U8   = 0x01,
    NVS_TYPE_I8   = 0x11,
    NVS_TYPE_U16  = 0x02,
    NVS_TYPE_I16  = 0x12,
    NVS_TYPE_U32  = 0x04,
    NVS_TYPE_I32  = 0x14,
    NVS_TYPE_U64  = 0x08,
    NVS_TYPE_I64  = 0x18,
    NVS_TYPE_STR  = 0x21,
    NVS_TYPE_BLOB = 0x42,
    NVS_TYPE_ANY  = 0xff,
} nvs_type_t;

// Same values as IDF's nvs.h.
#define ESP_ERR_NVS_BASE                0x1100
#define ESP_ERR_NVS_NOT_INITIALIZED     (ESP_ERR_NVS_BASE + 0x01)
#define ESP_ERR_NVS_NOT_FOUND           (ESP_ERR_NVS_BASE + 0x02)
#define ESP_ERR_NVS_TYPE_MISMATCH       (ESP_ERR_NVS_BASE + 0x03)
#define ESP_ERR_NVS_READ_ONLY           (ESP_ERR_NVS_BASE + 0x04)
#define ESP_ERR_NVS_NOT_ENOUGH_SPACE    (ESP_ERR_NVS_BASE + 0x05)
#define ESP_ERR_NVS_INVALID_NAME        (ESP_ERR_NVS_BASE + 0x06)
#define ESP_ERR_NVS_INVALID_HANDLE      (ESP_ERR_NVS_BASE + 0x07)
#define ESP_ERR_NVS_KEY_TOO_LONG        (ESP_ERR_NVS_BASE + 0x09)
#define ESP_ERR_NVS_INVALID_LENGTH      (ESP_ERR_NVS_BASE + 0x0c)
#define ESP_ERR_NVS_NO_FREE_PAGES       (ESP_ERR_NVS_BASE + 0x0d)
#define ESP_ERR_NVS_NEW_VERSION_FOUND   (ESP_ERR_NVS_BASE + 0x10)

#define NVS_KEY_NAME_MAX_SIZE 16

namespace host_nvs {

struct Entry {
    nvs_type_t           type;
    std::vector<uint8_t> bytes;
    bool operator==(const Entry& o) const { return type == o.type && bytes == o.bytes; }
};
using Namespace = std::map<std::string, Entry>;

struct Store {
    std::map<std::string, Namespace> spaces;
    std::map<nvs_handle_t, std::pair<std::string, bool>> handles;   ///< namespace, read-only
    nvs_handle_t next_handle = 1;
    long writes    = 0;    ///< operations that changed what the flash holds
    long cut_after = -1;   ///< writes left before the power goes; -1: it does not
};

inline Store& store() { static Store s; return s; }

/// An erased partition: no namespaces, no open handles, power on.
inline void reset() { store() = Store{}; }

inline long writes() { return store().writes; }

/// The next `n` writes reach flash and every one after them fails; -1 restores power.
inline void power_cut_after(long n) { store().cut_after = n; }

/// A copy of one namespace — empty if it was never created.
inline Namespace snapshot(const char* ns) {
    auto it = store().spaces.find(ns);
    return it == store().spaces.end() ? Namespace{} : it->second;
}

/// Spends one write. False once the power is gone.
inline bool power_for_write() {
    auto& s = store();
    if (s.cut_after == 0) return false;
    if (s.cut_after > 0) s.cut_after--;
    s.writes++;
    return true;
}

inline esp_err_t check_key(const char* key) {
    if (!key || !*key) return ESP_ERR_NVS_INVALID_NAME;
    if (strlen(key) > NVS_KEY_NAME_MAX_SIZE - 1) return ESP_ERR_NVS_KEY_TOO_LONG;
    return ESP_OK;
}

inline Namespace* space_of(nvs_handle_t h, bool for_write, esp_err_t& err) {
    auto& s = store();
    auto it = s.handles.find(h);
    if (it == s.handles.end()) { err = ESP_ERR_NVS_INVALID_HANDLE; return nullptr; }
    if (for_write && it->second.second) { err = ESP_ERR_NVS_READ_ONLY; return nullptr; }
    err = ESP_OK;
    return &s.spaces[it->second.first];
}

inline bool variable_length(nvs_type_t t) { return t == NVS_TYPE_STR || t == NVS_TYPE_BLOB; }

inline esp_err_t get(nvs_handle_t h, const char* key, nvs_type_t type, void* out, size_t* len) {
    esp_err_t err;
    Namespace* ns = space_of(h, false, err);
    if (!ns) return err;
    if ((err = check_key(key)) != ESP_OK) return err;
    auto it = ns->find(key);
    if (it == ns->end()) return ESP_ERR_NVS_NOT_FOUND;
    if (it->second.type != type) return ESP_ERR_NVS_TYPE_MISMATCH;
    const auto& b = it->second.bytes;
    if (variable_length(type)) {
        if (!len) return ESP_ERR_NVS_INVALID_LENGTH;
        if (!out) { *len = b.size(); return ESP_OK; }
        if (*len < b.size()) { *len = b.size(); return ESP_ERR_NVS_INVALID_LENGTH; }
        *len = b.size();
    }
    if (out && !b.empty()) memcpy(out, b.data(), b.size());
    return ESP_OK;
}

inline esp_err_t set(nvs_handle_t h, const char* key, nvs_type_t type, const void* data, size_t len) {
    esp_err_t err;
    Namespace* ns = space_of(h, true, err);
    if (!ns) return err;
    if ((err = check_key(key)) != ESP_OK) return err;
    const auto* p = static_cast<const uint8_t*>(data);
    Entry e{type, std::vector<uint8_t>(p, p + len)};
    auto it = ns->find(key);
    if (it != ns->end() && it->second == e) return ESP_OK;   // unchanged: nothing is written
    if (!power_for_write()) return ESP_FAIL;
    (*ns)[key] = std::move(e);
    return ESP_OK;
}

} // namespace host_nvs

inline esp_err_t nvs_open(const char* name, nvs_open_mode_t mode, nvs_handle_t* out) {
    auto& s = host_nvs::store();
    if (!name || !*name || strlen(name) > NVS_KEY_NAME_MAX_SIZE - 1) return ESP_ERR_NVS_INVALID_NAME;
    if (!s.spaces.count(name)) {
        if (mode == NVS_READONLY) return ESP_ERR_NVS_NOT_FOUND;
        if (!host_nvs::power_for_write()) return ESP_FAIL;   // the namespace entry is a write
        s.spaces[name];
    }
    const nvs_handle_t h = s.next_handle++;
    s.handles[h] = {name, mode == NVS_READONLY};
    *out = h;
    return ESP_OK;
}

inline esp_err_t nvs_commit(nvs_handle_t h) {
    return host_nvs::store().handles.count(h) ? ESP_OK : ESP_ERR_NVS_INVALID_HANDLE;
}

inline void nvs_close(nvs_handle_t h) { host_nvs::store().handles.erase(h); }

inline esp_err_t nvs_get_u8(nvs_handle_t h, const char* key, uint8_t* out) {
    return host_nvs::get(h, key, NVS_TYPE_U8, out, nullptr);
}
inline esp_err_t nvs_set_u8(nvs_handle_t h, const char* key, uint8_t value) {
    return host_nvs::set(h, key, NVS_TYPE_U8, &value, sizeof(value));
}
inline esp_err_t nvs_get_i32(nvs_handle_t h, const char* key, int32_t* out) {
    return host_nvs::get(h, key, NVS_TYPE_I32, out, nullptr);
}
inline esp_err_t nvs_set_i32(nvs_handle_t h, const char* key, int32_t value) {
    return host_nvs::set(h, key, NVS_TYPE_I32, &value, sizeof(value));
}
inline esp_err_t nvs_get_str(nvs_handle_t h, const char* key, char* out, size_t* len) {
    return host_nvs::get(h, key, NVS_TYPE_STR, out, len);
}
inline esp_err_t nvs_set_str(nvs_handle_t h, const char* key, const char* value) {
    return host_nvs::set(h, key, NVS_TYPE_STR, value, strlen(value) + 1);
}
inline esp_err_t nvs_get_blob(nvs_handle_t h, const char* key, void* out, size_t* len) {
    return host_nvs::get(h, key, NVS_TYPE_BLOB, out, len);
}
inline esp_err_t nvs_set_blob(nvs_handle_t h, const char* key, const void* value, size_t len) {
    return host_nvs::set(h, key, NVS_TYPE_BLOB, value, len);
}

inline esp_err_t nvs_find_key(nvs_handle_t h, const char* key, nvs_type_t* out_type) {
    esp_err_t err;
    auto* ns = host_nvs::space_of(h, false, err);
    if (!ns) return err;
    if ((err = host_nvs::check_key(key)) != ESP_OK) return err;
    auto it = ns->find(key);
    if (it == ns->end()) return ESP_ERR_NVS_NOT_FOUND;
    if (out_type) *out_type = it->second.type;
    return ESP_OK;
}

inline esp_err_t nvs_erase_key(nvs_handle_t h, const char* key) {
    esp_err_t err;
    auto* ns = host_nvs::space_of(h, true, err);
    if (!ns) return err;
    if ((err = host_nvs::check_key(key)) != ESP_OK) return err;
    auto it = ns->find(key);
    if (it == ns->end()) return ESP_ERR_NVS_NOT_FOUND;
    if (!host_nvs::power_for_write()) return ESP_FAIL;
    ns->erase(it);
    return ESP_OK;
}
