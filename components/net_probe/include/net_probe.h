/**
 * @file net_probe.h
 * @brief A bench-only UDP receiver shaped like an AirPlay 1 stream, for measuring the
 *        P4 <-> C6 link without writing the receiver first. tools/net_probe.py drives it.
 *
 * Compiled in only with CONFIG_NET_PROBE_ENABLE (the MODESP_NETTEST overlay). The port is
 * unauthenticated and can switch where esp_hosted allocates, which is fine on a bench and
 * wrong anywhere else, so a shipping image does not contain it at all.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/// Start the probe task. Call once the network stack is up; a no-op when the probe is not
/// compiled in, so callers need no #if.
void net_probe_start(void);

#ifdef __cplusplus
}
#endif
