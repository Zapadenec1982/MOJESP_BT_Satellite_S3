# Соук-тест SDIO (ESP-Hosted P4 ↔ C6)

`tools/sdio_soak/` — окремий голий IDF-проєкт для P4 (без ModESP), який годинами тягне один
HTTP(S)-потік через C6 у PSRAM-кільце і рахує розриви. Це «фаза 0» з `docs/CONCEPT.md` §10:
відповідь на питання, чи йде мережева частина (автономне радіо, slimproto) на цю панель узагалі.
Результат — рішення замовника; інструмент лише дає цифри.

## Що вимірює

Кожні `SOAK_REPORT_SEC` (30 с) один рядок, зручний для `grep SOAK`:

```
SOAK t=01:23:45 rx=123.4MB kbps=318 ring=61% reconn=2 restart=0 stall=0 http_err=2 wifi_drop=0 rssi=-57 dma=380.1K dma_min=372.9K dma_big=335.9K int=401.2K int_min=398.0K psram=31.5M
```

| Поле | Значення |
|---|---|
| `rx`, `kbps` | усього прийнято; середня швидкість за останній період |
| `ring` | заповнення PSRAM-кільця, яке «декодер» спорожнює зі швидкістю `SOAK_DRAIN_KBPS` |
| `reconn` | повторні відкриття потоку після помилки/таймауту (TCP/TLS/HTTP) |
| `restart` | скінченний файл дійшов до кінця і відкритий знову (LAN-тест) — не помилка |
| `stall` | `SOAK_STALL_SEC` (5 с) без жодного байта — головний показник «лінк завис» |
| `wifi_drop` | події `STA_DISCONNECTED` |
| `rssi` | на стенді **0**: у esp_hosted 3.0.7 відповідь `sta_get_ap_info` не містить rssi (host glue копіює лише ssid/bssid/channel/authmode), а `esp_wifi_sta_get_rssi()` host-шар не вміє скласти (`pack_req_payload failed msg_id=341`). Те саме, що `wifi.rssi = 0` у плеєрі; чекати bump esp_hosted |
| `dma`, `dma_min`, `dma_big` | вільна DMA-SRAM зараз / мінімум з початку / найбільший блок |
| `int`, `int_min`, `psram` | внутрішня SRAM зараз / мінімум; PSRAM |

Розриви потоку логуються окремо (`stream broke: … reconnect #N in M ms`), перепідключення з
бекофом 1 → 30 с без обмеження спроб. Наприкінці `SOAK_DURATION_MIN` — підсумковий рядок
`SOAK DONE …`, після чого тест продовжує працювати (нічний прогін просто довший).

## Два прогони (CONCEPT §10)

1. **Контроль, без інтернету і TLS:** великий файл (FLAC) на HTTP-сервері в локальній мережі,
   наприклад `python -m http.server 8000` у теці з файлом → `SOAK_URL = http://192.168.1.x:8000/track.flac`,
   `SOAK_DRAIN_KBPS = 1000` (або 0 — чиста пропускна здатність). Файл перевідкривається на EOF.
2. **Реальний шлях, HTTPS:** публічна MP3-станція. За замовчуванням `https://icecast.radiofrance.fr/fip-midfi.mp3`
   (128 кбіт/с), `SOAK_DRAIN_KBPS = 128` (= бітрейт станції; більше — кільце порожніє, це не помилка лінку).

   **Перевірено на стенді 15.09.2026:** з `CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=4096` (бюджет плеєра з
   CONCEPT §8а) ця станція рветься кожну секунду з `esp-tls-mbedtls: read error :-0x7200` — сервер
   ігнорує max_fragment_length і шле 16-КБ записи. Тому в `sdkconfig.defaults` тесту вхідний буфер
   TLS = 16384 (OUT лишається 4096). Для плеєра це означає: HTTPS-радіо коштує ~+12 КБ DMA-SRAM на
   сесію понад план — вхідний рядок для етапу «мережа».

По 2 години кожен, краще — ніч.

## Запуск

Передумови: C6 уже на CP 3.0.7 (`docs/C6_UPDATE.md`), плеєр хоча б раз підключався до Wi-Fi —
тест читає SSID/пароль із того самого NVS (`wifi/ssid`, `wifi/pass`), нічого вводити не треба.
(Альтернатива — `SOAK_WIFI_SSID/PASS` у menuconfig.)

```powershell
. C:\Espressif\tools\Microsoft.v5.5.5.PowerShell_profile.ps1
$env:IDF_COMPONENT_CACHE_PATH = "D:\P4_DSP\.idf_cache"
cd D:\P4_DSP\modesp-player\tools\sdio_soak
idf.py set-target esp32p4
idf.py menuconfig          # "SDIO soak test" → URL, тривалість, drain
idf.py build
cd build; python -m esptool --chip esp32p4 -p COM12 -b 460800 write_flash "@flash_args"; cd ..
idf.py -p COM12 monitor    # або будь-який термінал 115200; лог — у файл
```

Прошивка лягає в `ota_0` замість плеєра (таблиця розділів та сама, NVS і LittleFS не чіпаються).
Після тесту повернути плеєр: `cd D:\P4_DSP\modesp-player\build; python -m esptool --chip esp32p4 -p COM12 -b 460800 write_flash "@flash_args"`.

Лог у файл з PowerShell: `idf.py -p COM12 monitor | Tee-Object -FilePath soak_$(Get-Date -f yyyyMMdd_HHmm).log`
(Ctrl+] — вихід). Або `python -m esp_idf_monitor -p COM12 build\sdio_soak.elf`.

## Як читати результат

- `stall` > 0, після якого `reconn` не рятує і `rx` не росте — лінк завис («тихе вмирання»,
  esp-hosted #220, начебто виправлене у 3.0.7). Це і є негативний результат.
- `reconn` кілька за ніч з миттєвим відновленням і `stall = 0` — нормально для інтернет-радіо
  (станції рвуть з'єднання самі); дивитися на `wifi_drop`.
- `dma_min` — мінімум, з яким має жити аудіоядро поруч зі стрімом: I2S DMA-кільце, mbedTLS
  (~71 КБ при 16 КБ фрагментах), буфери SD. Якщо `dma_min` < ~150 КБ за 4 КБ TLS-фрагментів —
  бюджет DMA-SRAM треба переглядати до написання стрімера.
- `ring_underruns` у підсумку при `stall = 0` — drain задано вище за реальний бітрейт станції,
  не проблема лінку.

Негативний результат → план Б з CONCEPT §8: панель лишається UI + DSP + BT + AUX, мережевий потік
заходить у AUX з окремого стрімера.

## Прогін 16.09.2026 (HTTPS, 8 год 28 хв)

Лог стенду лежить у репозиторії: `docs/bench/2026-09-16-sdio-soak/soak_log.txt`, підсумок — у
README поруч. Коротко: 465 МБ при 127–128 кбіт/с, `stall=2` (обидва відновились самі),
`reconn=1` (сервер обірвав chunked-потік, перепідключення за ~2 с), `http_err=1`,
`wifi_drop=0`, мінімум DMA 400,7 КБ, мінімум internal 439,3 КБ. За правилами розділу
«Як читати результат» — «тихого вмирання» лінку немає.
