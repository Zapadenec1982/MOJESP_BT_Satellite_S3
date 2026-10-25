#include "dlna_browser.h"
#include "upnp_xml.h"
#include "cover.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

// View::title (the servers' list) and View::status are messages of the web's catalog ("@<key>",
// arguments behind MSG_ARG): the panel's DLNA page and
// GET /api/dlna/browser show them in the panel's and the browser's language.
#define MSG_ARG "\x1f"

namespace dlna_client {
namespace {
constexpr size_t XML_CAP = 65536, ART_CAP = 512 * 1024;
constexpr int SERVERS = 8, DEPTH = 16;
struct Server {
    char name[96]{}, location[257]{}, control[257]{}, type[96]{};
};
struct Entry {
    char id[257]{};
    dlna_core::Track track{};
    bool folder = false, supported = false;
};
struct Frame {
    char id[257]{}, name[96]{};
    uint32_t offset = 0;
};
struct State {
    View view{};
    Server servers[SERVERS]{};
    int server_count = 0, selected = -1;
    Entry entries[PAGE_SIZE]{};
    Frame path[DEPTH]{};
    int depth = 0;
    uint32_t total = 0, returned = 0;
    dlna_core::Track selected_track{};
    bool track_ready = false, queue_next = false;
    char art_uri[257]{};
    uint32_t art_generation = 0;
    std::atomic<uint32_t> art_serial{0}, serial{0};
    std::atomic<bool> run{true};
    char *http = nullptr;
    char *xml = nullptr;
    QueueHandle_t commands = nullptr;
    SemaphoreHandle_t guard = nullptr;
};
State *s = nullptr;
struct Request {
    Command command;
    uint8_t row;
    uint16_t revision;
    uint32_t serial;
};
struct Lock {
    Lock() {
        xSemaphoreTake(s->guard, portMAX_DELAY);
    }
    ~Lock() {
        xSemaphoreGive(s->guard);
    }
};
struct Fetch {
    char *data;
    size_t cap, used = 0;
    uint32_t serial;
    bool art = false, failed = false;
    int64_t deadline;
};
bool cancelled(const Fetch &f) {
    return !s->run.load() || (f.art ? s->art_serial.load() : s->serial.load()) != f.serial;
}
bool fetch(const char *url, char *out, size_t cap, uint32_t serial, const char *body = nullptr,
           const char *soap = nullptr, bool art = false, size_t *length = nullptr) {
    if (!dlna_core::http_url(url))
        return false;
    Fetch f{out, cap, 0, serial, art, false, esp_timer_get_time() + 12000000};
    out[0] = 0;
    esp_http_client_config_t cfg{};
    cfg.url = url;
    cfg.timeout_ms = 4000;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.disable_auto_redirect = true;
    cfg.buffer_size = 2048;
    cfg.buffer_size_tx = 2048;
    auto h = esp_http_client_init(&cfg);
    if (!h)
        return false;
    esp_http_client_set_header(h, "Connection", "close");
    if (body) {
        esp_http_client_set_method(h, HTTP_METHOD_POST);
        esp_http_client_set_header(h, "Content-Type", "text/xml; charset=\"utf-8\"");
        esp_http_client_set_header(h, "SOAPAction", soap);
    }
    bool ok = false;
    for (int redirects = 0; redirects <= 3 && !cancelled(f) && esp_timer_get_time() < f.deadline;
         ++redirects) {
        size_t body_len = body ? strlen(body) : 0;
        if (esp_http_client_open(h, body_len) != ESP_OK)
            break;
        if (body_len && esp_http_client_write(h, body, body_len) != (int)body_len)
            break;
        int64_t declared = esp_http_client_fetch_headers(h);
        if (declared < 0)
            break;
        int code = esp_http_client_get_status_code(h);
        if (code >= 300 && code < 400) {
            if (esp_http_client_set_redirection(h) != ESP_OK)
                break;
            esp_http_client_close(h);
            continue;
        }
        if (code != 200 || declared >= (int64_t)cap)
            break;
        while (!cancelled(f) && esp_timer_get_time() < f.deadline && f.used < cap - 1) {
            int r = esp_http_client_read(h, out + f.used, cap - f.used - 1);
            if (r < 0)
                break;
            f.used += r;
            out[f.used] = 0;
            if (esp_http_client_is_complete_data_received(h)) {
                ok = true;
                break;
            }
            if (!r)
                break;
        }
        break;
    }
    if (length)
        *length = f.used;
    esp_http_client_cleanup(h);
    return ok && !cancelled(f);
}

void status(const char *message, bool busy = false) {
    Lock lock;
    snprintf(s->view.status, sizeof(s->view.status), "%s", message);
    s->view.busy = busy;
}
void publish(bool servers) {
    Lock lock;
    auto &v = s->view;
    ++v.revision;
    if (!v.revision)
        ++v.revision;
    v.servers = servers;
    v.busy = false;
    v.can_back = !servers;
    v.has_prev = !servers && s->path[s->depth].offset > 0;
    v.has_next = !servers && s->returned > 0 && s->path[s->depth].offset + s->returned < s->total;
    v.count = servers ? s->server_count : (s->returned > PAGE_SIZE ? PAGE_SIZE : s->returned);
    snprintf(v.title, sizeof(v.title), "%s", servers ? "@dlna.servers" : s->path[s->depth].name);
    for (int i = 0; i < PAGE_SIZE; ++i) {
        v.rows[i] = Row{};
        if (i >= v.count)
            continue;
        auto &row = v.rows[i];
        if (servers) {
            snprintf(row.title, sizeof(row.title), "%s", s->servers[i].name);
            row.folder = true;
        } else {
            auto &e = s->entries[i];
            snprintf(row.title, sizeof(row.title), "%s", e.track.title);
            snprintf(row.subtitle, sizeof(row.subtitle), "%s", e.track.artist);
            row.folder = e.folder;
            row.playable = e.supported;
        }
    }
    if (servers) snprintf(v.status, sizeof(v.status), "@dlna.st_servers" MSG_ARG "n=%d", v.count);
    else         snprintf(v.status, sizeof(v.status), "@dlna.st_page" MSG_ARG "n=%d", v.count);
}
bool describe(Server &server, uint32_t serial) {
    if (!fetch(server.location, s->http, XML_CAP, serial))
        return false;
    std::string_view doc(s->http);
    char base[257]{};
    if (!dlna_core::field(doc, "URLBase", base, sizeof(base)) || !dlna_core::http_url(base))
        snprintf(base, sizeof(base), "%s", server.location);
    dlna_core::field(doc, "friendlyName", server.name, sizeof(server.name));
    size_t p = 0;
    dlna_core::Element e;
    while (dlna_core::element(doc, "service", p, e)) {
        char type[96]{}, control[257]{};
        dlna_core::field(e.body, "serviceType", type, sizeof(type));
        if (strncmp(type, "urn:schemas-upnp-org:service:ContentDirectory:", 46))
            continue;
        if (!dlna_core::field(e.body, "controlURL", control, sizeof(control)))
            continue;
        if (!dlna_core::resolve_url(base, control, server.control, sizeof(server.control)))
            continue;
        snprintf(server.type, sizeof(server.type), "%s", type);
        return true;
    }
    return false;
}
void discover(uint32_t serial) {
    {
        Lock lock;
        // A refresh invalidates every frame and the selected server, including when
        // discovery is subsequently cancelled or fails before publish().
        s->server_count = 0;
        s->selected = -1;
        s->depth = 0;
        s->total = s->returned = 0;
        s->path[0] = Frame{};
        s->view.servers = true;
        s->view.can_back = false;
        s->view.has_prev = s->view.has_next = false;
        s->view.count = 0;
        snprintf(s->view.title, sizeof(s->view.title), "%s", "@dlna.servers");
        ++s->view.revision;
    }
    status("@dlna.searching", true);
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) {
        status("@dlna.st_no_socket");
        return;
    }
    timeval wait{0, 200000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &wait, sizeof(wait));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(1900);
    inet_pton(AF_INET, "239.255.255.250", &address.sin_addr);
    const char query[] =
        "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: "
        "2\r\nST: urn:schemas-upnp-org:device:MediaServer:1\r\n\r\n";
    sendto(fd, query, sizeof(query) - 1, 0, (sockaddr *)&address, sizeof(address));
    int64_t end = esp_timer_get_time() + 2500000;
    while (s->run && s->serial.load() == serial && esp_timer_get_time() < end &&
           s->server_count < SERVERS) {
        char packet[2048];
        int n = recv(fd, packet, sizeof(packet) - 1, 0);
        if (n <= 0)
            continue;
        packet[n] = 0;
        const char *p = packet;
        char location[257]{};
        while (*p) {
            const char *nl = strstr(p, "\r\n");
            if (!nl)
                break;
            if (!strncasecmp(p, "location:", 9)) {
                const char *first = p + 9;
                while (first < nl && *first == ' ')
                    ++first;
                size_t size = nl - first;
                if (size < sizeof(location)) {
                    memcpy(location, first, size);
                    location[size] = 0;
                }
            }
            p = nl + 2;
        }
        if (!dlna_core::http_url(location))
            continue;
        bool duplicate = false;
        for (int i = 0; i < s->server_count; ++i)
            duplicate |= !strcmp(location, s->servers[i].location);
        if (duplicate)
            continue;
        auto &server = s->servers[s->server_count++];
        server = Server{};
        snprintf(server.location, sizeof(server.location), "%s", location);
    }
    close(fd);
    for (int i = 0; i < s->server_count;) {
        if (s->serial.load() != serial)
            return;
        if (describe(s->servers[i], serial))
            ++i;
        else {
            for (int j = i + 1; j < s->server_count; ++j)
                s->servers[j - 1] = s->servers[j];
            --s->server_count;
        }
    }
    if (s->serial.load() == serial)
        publish(true);
}
bool browse_target_valid() {
    return s->selected >= 0 && s->selected < s->server_count &&
           s->depth >= 0 && s->depth < DEPTH;
}
bool browse(uint32_t serial) {
    if (!browse_target_valid()) {
        publish(true);
        return false;
    }
    {
        Lock lock;
        s->view.count = 0;
        ++s->view.revision;
    }
    status("@app.loading", true);
    auto &server = s->servers[s->selected];
    auto &frame = s->path[s->depth];
    char object[1600], soap[128], body[2300];
    if (!dlna_core::xml_reply(frame.id, object, sizeof(object))) {
        status("@dlna.st_bad_id");
        return false;
    }
    snprintf(soap, sizeof(soap), "\"%s#Browse\"", server.type);
    snprintf(
        body, sizeof(body),
        "<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><u:Browse "
        "xmlns:u=\"%s\"><ObjectID>%s</ObjectID><BrowseFlag>BrowseDirectChildren</"
        "BrowseFlag><Filter>*</Filter><StartingIndex>%lu</StartingIndex><RequestedCount>%d</"
        "RequestedCount><SortCriteria></SortCriteria></u:Browse></s:Body></s:Envelope>",
        server.type, object, (unsigned long)frame.offset, PAGE_SIZE);
    if (!fetch(server.control, s->http, XML_CAP, serial, body, soap)) {
        status("@dlna.st_unreachable");
        return false;
    }
    char count[20]{};
    if (!dlna_core::field(s->http, "NumberReturned", count, sizeof(count))) {
        status("@dlna.st_garbled");
        return false;
    }
    uint32_t returned = strtoul(count, nullptr, 10);
    if (returned > PAGE_SIZE) {
        status("@dlna.st_page_size");
        return false;
    }
    if (!dlna_core::field(s->http, "TotalMatches", count, sizeof(count))) {
        status("@dlna.st_no_total");
        return false;
    }
    uint32_t total = strtoul(count, nullptr, 10);
    if (!dlna_core::field(s->http, "Result", s->xml, XML_CAP)) {
        status("@dlna.st_bad_list");
        return false;
    }
    std::string_view doc(s->xml);
    size_t cursor = 0;
    uint32_t actual = 0;
    while (actual < returned) {
        dlna_core::Element item, folder;
        size_t ip = cursor, fp = cursor;
        bool hi = dlna_core::element(doc, "item", ip, item),
             hf = dlna_core::element(doc, "container", fp, folder);
        if (!hi && !hf)
            break;
        bool is_folder = hf && (!hi || folder.whole.data() < item.whole.data());
        auto &e = is_folder ? folder : item;
        cursor = is_folder ? fp : ip;
        auto &entry = s->entries[actual++];
        entry = Entry{};
        entry.folder = is_folder;
        dlna_core::attribute(e.attributes, "id", entry.id, sizeof(entry.id));
        dlna_core::field(e.body, "title", entry.track.title, sizeof(entry.track.title));
        dlna_core::field(e.body, "artist", entry.track.artist, sizeof(entry.track.artist));
        dlna_core::field(e.body, "album", entry.track.album, sizeof(entry.track.album));
        char relative[257]{};
        if (dlna_core::field(e.body, "albumArtURI", relative, sizeof(relative)))
            dlna_core::resolve_url(server.location, relative, entry.track.art,
                                   sizeof(entry.track.art));
        if (is_folder) {
            entry.supported = entry.id[0];
            continue;
        }
        size_t rp = 0;
        dlna_core::Element res;
        char chosen_protocol[256]{};
        while (dlna_core::element(e.body, "res", rp, res)) {
            char protocol[256]{};
            if (!dlna_core::attribute(res.attributes, "protocolInfo", protocol, sizeof(protocol)) ||
                !dlna_core::playable(protocol))
                continue;
            if (!dlna_core::decode_text(res.body, relative, sizeof(relative)))
                continue;
            if (dlna_core::resolve_url(server.location, relative, entry.track.uri,
                                       sizeof(entry.track.uri))) {
                snprintf(chosen_protocol, sizeof(chosen_protocol), "%s", protocol);
                entry.supported = true;
                break;
            }
        }
        // Rebuild with known namespace bindings: a server may use arbitrary prefixes
        // declared on its outer DIDL root, which would be lost by copying one item.
        if (entry.supported && !dlna_core::resource_metadata(entry.track, chosen_protocol))
            entry.supported = false;
    }
    if (actual != returned) {
        status("@dlna.st_broken_list");
        return false;
    }
    s->returned = returned;
    s->total = total;
    if (s->serial.load() != serial)
        return false;
    publish(false);
    return true;
}
void process(Request r) {
    if (r.command == Command::Discover) {
        discover(r.serial);
        return;
    }
    if (r.command == Command::Cancel) {
        status("@dlna.st_cancelled");
        return;
    }
    bool root;
    {
        Lock lock;
        if (r.revision != s->view.revision) {
            s->view.busy = false;
            return;
        }
        root = s->view.servers;
    }
    if (r.command == Command::Back) {
        if (root)
            return;
        if (!s->depth) {
            publish(true);
            return;
        }
        --s->depth;
        browse(r.serial);
        return;
    }
    if (r.command == Command::NextPage || r.command == Command::PrevPage) {
        if (root)
            return;
        auto &offset = s->path[s->depth].offset;
        if (r.command == Command::NextPage) {
            if (!s->returned || offset + s->returned >= s->total)
                return;
            offset += s->returned;
        } else
            offset = offset > PAGE_SIZE ? offset - PAGE_SIZE : 0;
        browse(r.serial);
        return;
    }
    if (r.row >= (root ? s->server_count : s->returned))
        return;
    if (root) {
        s->selected = r.row;
        s->depth = 0;
        s->path[0] = Frame{};
        snprintf(s->path[0].id, sizeof(s->path[0].id), "0");
        snprintf(s->path[0].name, sizeof(s->path[0].name), "%s", s->servers[r.row].name);
        browse(r.serial);
        return;
    }
    auto &entry = s->entries[r.row];
    if (entry.folder) {
        if (!entry.id[0] || s->depth + 1 == DEPTH) {
            status("@dlna.st_too_deep");
            return;
        }
        ++s->depth;
        auto &f = s->path[s->depth];
        f = Frame{};
        memmove(f.id, entry.id, sizeof(f.id));
        memmove(f.name, entry.track.title, sizeof(f.name));
        browse(r.serial);
        return;
    }
    if (!entry.supported) {
        status("@dlna.unsupported");
        return;
    }
    {
        Lock lock;
        s->selected_track = entry.track;
        s->queue_next = r.command == Command::Queue;
        s->track_ready = true;
        s->view.busy = false;
    }
    // The title cut to 70 bytes on a character boundary, the message key ahead of it.
    char title[71];
    size_t len = strlen(entry.track.title), cut = len < 70 ? len : 70;
    while (cut > 0 && cut < len && ((unsigned char)entry.track.title[cut] & 0xC0) == 0x80) --cut;
    memcpy(title, entry.track.title, cut);
    title[cut] = '\0';
    char message[96];
    if (r.command == Command::Queue) snprintf(message, sizeof(message), "@dlna.queued" MSG_ARG "title=%s", title);
    else                             snprintf(message, sizeof(message), "@dlna.st_playing" MSG_ARG "title=%s", title);
    status(message);
}
void worker(void *) {
    uint32_t art_seen = 0;
    while (s->run) {
        Request r;
        if (xQueueReceive(s->commands, &r, pdMS_TO_TICKS(100)) == pdTRUE) {
            process(r);
            continue;
        }
        auto serial = s->art_serial.load();
        if (serial == art_seen)
            continue;
        art_seen = serial;
        char uri[257];
        uint32_t gen;
        {
            Lock lock;
            snprintf(uri, sizeof(uri), "%s", s->art_uri);
            gen = s->art_generation;
        }
        if (!uri[0])
            continue;
        char *bytes = (char *)heap_caps_malloc(ART_CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!bytes)
            continue;
        size_t size = 0;
        if (fetch(uri, bytes, ART_CAP, serial, nullptr, nullptr, true, &size) &&
            s->art_serial.load() == serial)
            player_core::cover_set_jpeg((uint8_t *)bytes, size, player_core::CoverSource::Network,
                                        gen);
        heap_caps_free(bytes);
    }
    vTaskDeleteWithCaps(nullptr);
}
} // namespace
int32_t encode(Command command, int row, uint16_t revision) {
    return (int32_t)((uint32_t)revision << 12 | (uint32_t)(row & 255) << 4 | (uint8_t)command);
}
bool init() {
    if (s)
        return true;
    auto memory = heap_caps_calloc(1, sizeof(State), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!memory)
        return false;
    s = new (memory) State;
    s->guard = xSemaphoreCreateMutex();
    s->commands = xQueueCreate(1, sizeof(Request));
    s->http = (char *)heap_caps_malloc(XML_CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s->xml = (char *)heap_caps_malloc(XML_CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    auto failed = []() {
        if (s->guard)
            vSemaphoreDelete(s->guard);
        if (s->commands)
            vQueueDelete(s->commands);
        heap_caps_free(s->http);
        heap_caps_free(s->xml);
        s->~State();
        heap_caps_free(s);
        s = nullptr;
        return false;
    };
    if (!s->guard || !s->commands || !s->http || !s->xml)
        return failed();
    snprintf(s->view.title, sizeof(s->view.title), "%s", "@dlna.servers");
    snprintf(s->view.status, sizeof(s->view.status), "%s", "@dlna.st_not_searched");
    // Keep the XML/TLS call stack out of the SRAM used by ESP-Hosted DMA.
    if (xTaskCreatePinnedToCoreWithCaps(worker, "dlna_client", 12288, nullptr, 1, nullptr, 0,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS)
        return failed();
    return true;
}
bool request(int32_t code) {
    if (!s)
        return false;
    Request r{(Command)(code & 15), (uint8_t)((code >> 4) & 255), (uint16_t)((uint32_t)code >> 12),
              0};
    if ((int)r.command < 1 || (int)r.command > 7)
        return false;
    r.serial = ++s->serial;
    return xQueueOverwrite(s->commands, &r) == pdTRUE;
}
bool snapshot(View &view) {
    if (!s || !s->guard)
        return false;
    Lock lock;
    view = s->view;
    return true;
}
bool take_track(dlna_core::Track &track, bool &next) {
    if (!s)
        return false;
    Lock lock;
    if (!s->track_ready)
        return false;
    track = s->selected_track;
    next = s->queue_next;
    s->track_ready = false;
    return true;
}
void artwork(const char *uri) {
    if (!s)
        return;
    Lock lock;
    player_core::cover_clear(player_core::CoverSource::Network);
    s->art_generation = player_core::cover_view(player_core::CoverSource::Network).gen;
    snprintf(s->art_uri, sizeof(s->art_uri), "%s", uri);
    ++s->art_serial;
}
void stop() {
    if (s) {
        s->run = false;
        ++s->serial;
        ++s->art_serial;
    }
}
} // namespace dlna_client
