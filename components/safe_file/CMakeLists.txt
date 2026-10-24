# safe_file — replace a file on flash without ever leaving it half-written.
#
# Pure C++ over stdio: no IDF headers, no filesystem of its own, so every consumer can take
# it (audio_dsp, modesp_net, modesp_services, modules/*) without pulling a dependency in with
# it, and tests/host compiles it unchanged.
idf_component_register(
    SRCS "src/safe_file.cpp"
    INCLUDE_DIRS "include"
)
