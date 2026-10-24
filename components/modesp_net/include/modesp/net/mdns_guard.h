#pragma once

#include <cstdint>

namespace modesp {
// Serialize whole mDNS registration/removal operations with Wi-Fi's init/free.
// The ESP event task can rebuild mDNS while product modules run on the main loop.
// wait_ms bounds the wait for the guard (default: forever); check locked() either way.
class MdnsGuard {
public:
    static constexpr uint32_t WAIT_FOREVER = UINT32_MAX;
    explicit MdnsGuard(uint32_t wait_ms = WAIT_FOREVER);
    ~MdnsGuard();
    bool locked() const { return held_; }
    MdnsGuard(const MdnsGuard&) = delete;
    MdnsGuard& operator=(const MdnsGuard&) = delete;
private:
    bool held_ = false;
};
} // namespace modesp
