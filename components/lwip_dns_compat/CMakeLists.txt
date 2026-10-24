# lwip_dns_compat — dns_gethostbyname() for one-address callers (lwIP's SNTP) when
# CONFIG_LWIP_DNS_MAX_HOST_IP makes a cache hit copy several. lwip_dns_compat.c says why.
#
# WHOLE_ARCHIVE: the only reference to __wrap_dns_gethostbyname comes from liblwip.a, which
# the linker may reach after this archive, and an archive member nobody has asked for yet is
# not pulled in. Same cure as components/esp_dlna and components/hosted_mem.
idf_component_register(
    SRCS "lwip_dns_compat.c"
    PRIV_REQUIRES lwip
    WHOLE_ARCHIVE
)

# With one address per name, dns_lookup() copies one and there is nothing to fix; the
# source compiles to nothing then, and the wrap must not be asked for.
if(CONFIG_LWIP_DNS_MAX_HOST_IP GREATER 1)
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=dns_gethostbyname")
endif()
