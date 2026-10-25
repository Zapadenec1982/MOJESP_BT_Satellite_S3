# esp_dlna — the three UPnP services a DLNA renderer must answer, over Espressif's own
# UPnP stack (espressif/esp_media_protocols: SSDP, SOAP, GENA).
#
# Vendored from the ESP-ADF example rather than depended on, because ADF's copy sits inside
# an example tree with no registry package, and pulling ESP-ADF whole is exactly what was
# refused (docs/NETWORK.md «DLNA»). See README.md for the origin and the local changes.
#
# The XML is embedded as text files, and the list is longer than ADF's own CMakeLists.
#
# Two sets, and they are owned by different things:
#   • the five SCPD and event templates the .c files here reference — a control point GETs
#     the SCPD before it will talk to us;
#   • six more (rootDesc, rootDescLogo, rootDescService, devicelist, SoapResponseOk,
#     SoapResponseErr) that the PREBUILT esp_media_protocols library references and does not
#     carry. ADF got them from the same component that held the library source; the registry
#     package ships only the archive, so leaving them out links cleanly right up to the
#     final .elf and then fails on six `undefined reference to _binary_rootDesc_xml_start`.
idf_component_register(
    SRCS "esp_dlna.c" "esp_dlna_avt.c" "esp_dlna_cmr.c" "esp_dlna_rcs.c"
         "httpd_uri_compat.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server espressif__esp_media_protocols
    EMBED_TXTFILES "xml/AVTransport.xml"
                   "xml/ConnectionManager.xml"
                   "xml/RenderingControl.xml"
                   "xml/EventLastChange.xml"
                   "xml/EventProtocolCMR.xml"
                   "xml/rootDesc.xml"
                   "xml/rootDescLogo.xml"
                   "xml/rootDescService.xml"
                   "xml/devicelist.xml"
                   "xml/SoapResponseOk.xml"
                   "xml/SoapResponseErr.xml"
    # Those six are referenced by esp_media_protocols and by nothing in this component, and
    # the reference points the wrong way down the dependency graph: the linker reaches
    # libesp_dlna.a before it knows it needs them, and an archive member nobody has asked
    # for yet is not pulled in. Same shape as lv_mem_psram.c in modules/ui_lvgl, same cure.
    WHOLE_ARCHIVE
)

# The prebuilt UPnP library passes a sixteen-byte httpd_uri_t to an application whose
# httpd_uri_t is twenty-four, because this firmware has CONFIG_HTTPD_WS_SUPPORT on for the
# web UI. httpd then reads `supported_subprotocol` past the end of the library's object and
# strdup()s whatever follows it — a load access fault on every boot with the renderer on.
# httpd_uri_compat.c rebuilds the struct, but only while the library is registering; the
# wrap has to be a link option, so it lives here rather than in the source.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=httpd_register_uri_handler")
