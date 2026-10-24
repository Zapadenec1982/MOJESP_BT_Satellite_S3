# net_probe — bench-only UDP receiver for tools/net_probe.py (net_probe.h says what and why).
# Empty unless CONFIG_NET_PROBE_ENABLE, which only the MODESP_NETTEST overlay sets.
idf_component_register(
    SRCS "net_probe.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES hosted_mem lwip esp_timer heap freertos log
)
