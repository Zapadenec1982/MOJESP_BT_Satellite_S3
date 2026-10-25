# esp_dlna — вендорений

Три UPnP-сервіси, на які має відповідати DLNA MediaRenderer, — AVTransport, RenderingControl і
ConnectionManager — як таблиці сервісів поверх UPnP-стека Espressif.

## Походження

`espressif/esp-adf`, гілка `release/v2.x`, `examples/advanced_examples/dlna/components/esp_dlna`.
Ліцензія — ESPRESSIF MIT: безкоштовно для використання на кристалах Espressif (заголовки файлів).
Вендорений, а не залежність: в ADF компонент лежить у дереві прикладу і пакета в реєстрі не має.

UPnP-стек під ним не вендорений: `espressif/esp_media_protocols` 1.0.1 з реєстру (прибитий у
`idf_component.yml`; бібліотека зібрана заздалегідь, її архів для esp32p4 не містить інструкцій, яких
немає на P4 v1.3) робить SSDP, опис пристрою, розбір SOAP і події GENA й чіпляється до наявного
`httpd_handle_t`.

## Локальні зміни

Решта — як в ADF, без переформатування: diff проти ADF має лишатися читабельним. Коментар
`p4-player:` у `.c`-файлах стоїть біля змін 1–5; XML у пункті 5 і зміни 6–7 позначки не мають.

1. `esp_dlna.c` — `audio_mem.h` / `audio_calloc()` замінено на `<stdlib.h>` / `calloc()`: заголовок
   ADF тягнув би `audio_sal` заради одного виділення.
2. `esp_dlna.c` — рядки опису пристрою (`manufacturer` «ModESP», `model_name` «P4 Player»,
   `model_description`, URL) замість рядків прикладу ADF: їх показує телефон у списку пристроїв.
3. `esp_dlna.c` — `esp_dlna_start()` звільняє дескриптор, коли `esp_upnp_init()` не вдався.
4. `esp_dlna_avt.c` — `SetAVTransportURI` віддає рендереру й `CurrentURIMetaData` як
   `AVT_SET_TRACK_METADATA`, щоб рендерер знав назву треку.
5. `esp_dlna_cmr.c` і `xml/EventProtocolCMR.xml` — список sink-протоколів ConnectionManager називає
   лише те, що прошивка декодує: MP3 (`audio/mpeg`, `audio/mp3`, `audio/x-mpeg`, `audio/mpeg3`), FLAC
   (`audio/flac`, `audio/x-flac`), WAV (`audio/wav`, `audio/x-wav`, `audio/wave`) і плейлисти, які
   розбирає `modules/radio` (`audio/mpegurl`, `audio/x-mpegurl`, `audio/x-scpls`). Без
   `http-get:*:*:*`, без L16 і без AAC. Заявлений формат, якого декодер не бере, мовчки дає тишу; за
   цим списком Music Assistant вирішує, що перекодувати на сервері.
6. `esp_dlna_avt.c`, `include/esp_dlna.h` — черга наступного треку: дії `SetNextAVTransportURI` і
   `GetCurrentTransportActions`; `NextURI` / `NextURIMetaData` у `GetMediaInfo` і події
   `NextAVTransportURI`, `NextAVTransportURIMetaData`, `CurrentTransportActions`, `TransportStatus`
   беруться через callback, а не константами. Нові значення `renderer_request_type_t`
   (`AVT_SET_NEXT_URI`, `AVT_SET_NEXT_METADATA`, `AVT_GET_NEXT_URI`, `AVT_GET_NEXT_METADATA`,
   `AVT_GET_ACTIONS`) додані в кінець enum, щоб наявні значення не зсунулися.
7. `httpd_uri_compat.c`, `include/esp_dlna_compat.h` — нові файли:
   - ABI-прокладка. Бібліотека `esp_media_protocols` зібрана без `CONFIG_HTTPD_WS_SUPPORT` і передає
     16-байтний `httpd_uri_t`, а в прошивці він 24-байтний (WebSocket для веб-інтерфейсу); httpd читав
     би `supported_subprotocol` за межею об'єкта. `httpd_register_uri_handler` обгорнуто на етапі
     лінкування (`-Wl,--wrap`, `CMakeLists.txt`), і поки `esp_dlna_httpd_compat(true)` — лише на час
     `esp_dlna_start()` — запит перебудовується з чотирьох справжніх полів. Решта реєстрацій, зокрема
     WebSocket, іде без змін.
   - Заміна обробника AVTransport. `esp_dlna_avt_handler()` задає власний обробник; під час тієї самої
     реєстрації POST-маршрути `/AVTransport/…` бібліотеки отримують його замість її диспетчера.

Шість XML (`rootDesc`, `rootDescLogo`, `rootDescService`, `devicelist`, `SoapResponseOk`,
`SoapResponseErr`) потрібні зібраній бібліотеці, а не цьому компоненту; вони вбудовані тут, і
компонент лінкується `WHOLE_ARCHIVE` (`CMakeLists.txt`).

## Як використовується

Дескриптором володіє `modules/dlna`. Перед `esp_dlna_start()` модуль знімає wildcard-маршрут httpd,
реєструє `esp_dlna_avt_handler(avt_soap)` і вмикає `esp_dlna_httpd_compat(true)`; після — вимикає
прокладку й повертає wildcard.

- **AVTransport** (`POST /AVTransport/Control`) до callback бібліотеки не доходить: його розбирає
  `avt_soap()` модуля (`modules/dlna/src/dlna_soap.inc`), який сам формує відповіді й повертає
  справжні UPnP-помилки (`500` з `UPnPError`). Тому дії AVTransport із таблиці `esp_dlna_avt.c`
  (зміна 4 і дії зміни 6) на SOAP-запити не виконуються; з цієї таблиці діють події.
- **RenderingControl, ConnectionManager** і значення для подій GENA (зокрема AVTransport) ідуть через
  один callback `esp_dlna_start()` як `renderer_request_type_t`; модуль перекладає їх у ключі
  SharedState і назад.
- Події надсилаються `esp_dlna_notify()` / `esp_dlna_notify_avt_by_action()` — так повзунок гучності
  й кнопка відтворення в контрольній точці йдуть за панеллю.

DLNA-рендерер і браузер медіасерверів з боку користувача — [NETWORK.md](../../docs/NETWORK.md).
