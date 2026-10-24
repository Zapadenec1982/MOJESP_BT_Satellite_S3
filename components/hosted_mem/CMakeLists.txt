# hosted_mem — esp_hosted's per-frame copies out of internal DMA RAM (hosted_mem.c says why),
# and a count of the frames it drops when it cannot allocate one.
#
# WHOLE_ARCHIVE: the object holds the heap-failure hook's constructor, and nothing references
# a constructor. Without it the archive member is skipped and the counters stay at zero.
idf_component_register(
    SRCS "hosted_mem.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES heap
    WHOLE_ARCHIVE
)

if(CONFIG_HOSTED_MEM_FRAMES_IN_PSRAM)
    # The wrap in hosted_mem.c repeats esp_hosted's prototype, because esp_hosted keeps the
    # header off its public include path. If an esp_hosted bump changes that line, stop here
    # rather than link a wrap whose signature no longer matches the function it replaces.
    # A registry component is registered under its namespaced name (espressif__esp_hosted),
    # and idf_component_get_property does not resolve the short one, so find it by suffix.
    idf_build_get_property(_all_components BUILD_COMPONENTS)
    set(_eh_name "")
    foreach(_c ${_all_components})
        if(_c MATCHES "(^|__)esp_hosted$")
            set(_eh_name "${_c}")
        endif()
    endforeach()
    if(NOT _eh_name)
        message(FATAL_ERROR "hosted_mem: HOSTED_MEM_FRAMES_IN_PSRAM is on but esp_hosted is "
                            "not in this build.")
    endif()
    idf_component_get_property(_eh_dir ${_eh_name} COMPONENT_DIR)
    set(_eh_hdr "${_eh_dir}/port/os/include/eh_host_port_dma.h")
    if(NOT EXISTS "${_eh_hdr}")
        message(FATAL_ERROR
            "hosted_mem: ${_eh_hdr} is gone. esp_hosted was reorganised; re-check that "
            "eh_host_port_dma_alloc() is still what sdio_buffer_alloc() and "
            "eh_host_transport_tx() call before turning HOSTED_MEM_FRAMES_IN_PSRAM back on.")
    endif()
    file(READ "${_eh_hdr}" _eh_src)
    string(FIND "${_eh_src}" "void *eh_host_port_dma_alloc(size_t n);" _eh_pos)
    if(_eh_pos EQUAL -1)
        message(FATAL_ERROR
            "hosted_mem: eh_host_port_dma_alloc() no longer reads 'void *eh_host_port_dma_alloc"
            "(size_t n);' in ${_eh_hdr}. Update the prototype in hosted_mem.c to match, or "
            "turn HOSTED_MEM_FRAMES_IN_PSRAM off.")
    endif()
    # A link option, so it lives here and not in the source — the same shape as
    # components/esp_dlna/CMakeLists.txt.
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=eh_host_port_dma_alloc")
endif()
