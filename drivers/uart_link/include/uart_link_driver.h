/**
 * @file uart_link_driver.h
 * @brief Byte link to a co-processor over UART (link seam: IByteLink).
 *
 * One board `uart_buses` entry -> one driver instance. HAL owns the installed port (pins,
 * baud, RX ring, event queue); this driver owns what has to be done to that port for a
 * framed protocol to survive on it at 460800 baud, and nothing about the protocol itself.
 */

#pragma once

#include "modesp/hal/byte_link.h"
#include "modesp/hal/hal_types.h"

#include <atomic>

class UartLinkDriver : public modesp::link::IByteLink {
public:
    void configure(modesp::UartBusResource* bus);

    bool init() override;
    bool can_wait() const override { return events_ != nullptr; }
    modesp::link::Rx wait_rx(uint32_t timeout_ms) override;
    int  read(uint8_t* out, size_t max) override;
    int  write(const uint8_t* data, size_t len) override;
    void flush_rx() override;
    modesp::link::LinkStats stats() const override;
    modesp::link::RxPin probe_rx() override;
    const char* describe() const override { return desc_; }
    const char* name() const override { return "uart_link"; }
    bool is_healthy() const override { return ready_; }

    /// RX-FIFO threshold, in bytes, at which the port raises its receive interrupt.
    ///
    /// IDF defaults to 120 in a 128-byte P4 FIFO: only ~174 us of headroom at 460800 8N1.
    /// Servicing bursts earlier is what leaves room while audio and display interrupts run,
    /// and uart.h explicitly recommends a lower threshold against RX overwrite.
    static constexpr int RX_FULL_THRESHOLD = 32;

private:
    modesp::UartBusResource* bus_ = nullptr;
    /// The port's event queue (QueueHandle_t), held as void* like the resource holds it.
    /// Null when HAL installed the port without one — then nothing can see an overflow.
    void* events_ = nullptr;
    bool  ready_  = false;
    /// Read and written from two tasks (the reader's and the protocol's). Monotonic, so a
    /// torn read would cost one stale log line — atomics are here to say that out loud, and
    /// because relaxed ones cost nothing on this core.
    std::atomic<uint32_t> rx_bytes_{0};
    std::atomic<uint32_t> tx_bytes_{0};
    std::atomic<uint32_t> overruns_{0};
    char  desc_[40] = "";
};
