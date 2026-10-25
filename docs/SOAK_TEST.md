# Соук-тест SDIO (ESP-Hosted P4 ↔ C6)

`tools/sdio_soak/` — окремий IDF-проєкт для P4 без ModESP. Він підключається до Wi-Fi через C6,
годинами тягне один HTTP(S)-потік у PSRAM-кільце, яке «декодер» спорожнює з заданою швидкістю, і
рахує розриви, зависання та вільну пам'ять. Блок ESP-Hosted у `tools/sdio_soak/sdkconfig.defaults`
(SDIO-піни, 40 МГц, reset, ревізія P4) той самий, що в `boards/p4_player/sdkconfig.board`, і має
таким лишатися. Відрізняється решта мережевого стека: тут типове TCP-вікно lwIP, немає
`components/hosted_mem` (копії кадрів esp_hosted ідуть у внутрішню DMA-RAM) і mbedTLS алокує з
внутрішньої RAM.

## Що друкує

Кожні `SOAK_REPORT_SEC` (30 с) — один рядок для `grep SOAK`:

```
SOAK t=01:23:45 rx=123.4MB kbps=318 ring=61% reconn=2 restart=0 stall=0 http_err=2 wifi_drop=0 rssi=0 dma=380.1K dma_min=372.9K dma_big=335.9K int=401.2K int_min=398.0K psram=31.5M
```

| Поле | Значення |
|---|---|
| `t` | час від старту |
| `rx`, `kbps` | усього прийнято; середня швидкість за останній період |
| `ring` | заповнення PSRAM-кільця (`SOAK_RING_KB`, 512 КБ), яке спорожнюється зі швидкістю `SOAK_DRAIN_KBPS`; спорожнення починається з 25 % заповнення |
| `reconn` | повторні відкриття потоку після помилки, таймауту або відповіді не 2xx |
| `restart` | потік закінчився без помилки з відповіддю 2xx (скінченний файл дійшов до кінця, LAN-прогін) і відкритий знову — не помилка |
| `stall` | `SOAK_STALL_SEC` (5 с) без жодного байта — головний показник «лінк завис» |
| `http_err` | ті з `reconn`, де `esp_http_client_perform()` повернув помилку (з'єднання, TLS, таймаут сокета `SOAK_HTTP_TIMEOUT_MS`); відповідь не 2xx сюди не входить |
| `wifi_drop` | події `STA_DISCONNECTED` |
| `rssi` | з esp_hosted 3.0.7 завжди 0: відповідь `sta_get_ap_info` не містить rssi, а `esp_wifi_sta_get_rssi()` хост не вміє скласти (`pack_req_payload failed msg_id=341`); так само `wifi.rssi` у плеєрі |
| `dma`, `dma_min`, `dma_big` | вільна DMA-SRAM зараз / мінімум / найбільший вільний блок |
| `int`, `int_min`, `psram` | вільна внутрішня SRAM зараз / мінімум; вільна PSRAM |

`dma_min` і `int_min` — мінімум від старту, а у фазовому прогоні — від початку поточної фази.

Розрив потоку логується окремо: `stream broke: <помилка>, HTTP <код>, after <T> ms — reconnect #N in
M ms`. Перепідключення з бекофом 1 → 30 с, без обмеження спроб; кінець скінченного файлу відкривається
знову одразу. `STALL #N: no data for 5 s (ring P%)` — на початку зависання, `data resumed` — коли дані
пішли знову. Через `SOAK_DURATION_MIN` (типово 120 хв; 0 — без підсумку) один раз друкується
підсумок:

```
SOAK DONE after 120 min: rx=… reconn=… restart=… stall=… http_err=… wifi_drop=… ring_underruns=… ring_overflow=…B dma_min=… int_min=…
```

`ring_underruns` — скільки разів кільце спорожніло і «декодер» чекав нового заповнення до 25 %,
`ring_overflow` — байти, що не влізли в кільце (спорожнення повільніше за прийом). Після підсумку тест
працює далі.

## Налаштування (`menuconfig` → «SDIO soak test»)

| Опція | Типово | Що задає |
|---|---|---|
| `SOAK_URL` | `https://icecast.radiofrance.fr/fip-midfi.mp3` | потік, `http://` або `https://` |
| `SOAK_DURATION_MIN` | 120 | коли друкувати `SOAK DONE`; 0 — ніколи |
| `SOAK_REPORT_SEC` | 30 | період рядка `SOAK` |
| `SOAK_RING_KB` | 512 | розмір PSRAM-кільця |
| `SOAK_DRAIN_KBPS` | 128 | темп «декодера», кбіт/с; 0 — забирати все одразу (чиста пропускна здатність) |
| `SOAK_STALL_SEC` | 5 | поріг `stall` |
| `SOAK_HTTP_TIMEOUT_MS` | 10000 | таймаут сокета HTTP |
| `SOAK_PHASE_MIN` | 0 | довжина фази, хв; 0 — без фаз |
| `SOAK_BALLAST_TARGETS` | `0` | цілі вільної DMA-SRAM по фазах, КБ, через кому |
| `SOAK_WIFI_SSID`, `SOAK_WIFI_PASS` | порожньо | мережа; порожньо — з NVS плеєра |

## Прогони

1. **Контроль, без інтернету і TLS:** великий файл (FLAC) на HTTP-сервері в локальній мережі,
   наприклад `python -m http.server 8000` у теці з файлом →
   `SOAK_URL = http://<ip ПК>:8000/track.flac`, `SOAK_DRAIN_KBPS = 1000` або 0. Файл відкривається
   знову на EOF (`restart`).
2. **Реальний шлях, HTTPS:** публічна MP3-станція, типово `SOAK_URL` вище (128 кбіт/с),
   `SOAK_DRAIN_KBPS` = бітрейт станції. Більший drain спорожнює кільце (`ring_underruns`) — це не
   помилка лінку.
3. **Керований бітрейт:** `tools/sdio_soak/rate_server.py` на ПК віддає нескінченне HTTP-тіло
   (chunked, псевдовипадковий нестискуваний блок) зі швидкістю, яку можна змінювати, не розриваючи
   з'єднання:

   ```
   python tools/sdio_soak/rate_server.py --port 8000 --rate-file rate.txt   # --rate <кбіт/с> — початкова, якщо файла ще немає
   echo 320 > rate.txt                                                       # перечитується щосекунди; 0 = стеля
   ```

   `SOAK_URL = http://<ip ПК>:8000/`. Вміст на панелі не перевіряється — тест рахує байти; втрати
   пакетів міряються окремо ICMP-пінгом панелі з того самого ПК, паралельно з потоком.

Кожен прогін — щонайменше 2 години, краще ніч.

## Фазовий прогін: тиск на DMA-SRAM

На плеєрі внутрішню DMA-SRAM, з якої бере буфери ESP-Hosted, ділять із ним DMA I2S і решта
прошивки, тож лінк, виміряний із сотнями вільних кілобайтів, нічого не каже про плеєр. З
`SOAK_PHASE_MIN > 0` тест по колу проходить `SOAK_BALLAST_TARGETS` (до 8 цілей): перед кожною фазою
займає DMA-SRAM блоками по 4 КБ (до 2 МБ), доки вільної не лишиться не більше цілі (0 — без баласту),
і тримає так `SOAK_PHASE_MIN` хвилин. Цілі — від найбільшої вільної до найменшої: тісні фази можуть
обірвати прогін. Фактичну вільну DMA-SRAM плеєра видно в його рядку
`main: Heap …: … DMA <N> (largest <M>) …`.

На початку і в кінці кожної фази:

```
SOAK PHASE <i>/<n> target=<ціль>K ballast=<зайнято>K dma_free=<…>K dma_big=<…>K
SOAK PHASE END <i>/<n> target=<ціль>K ballast=<зайнято>K kbps_avg=<…> rx=<…>MB reconn=<…> stall=<…> http_err=<…> wifi_drop=<…> dma_min=<…>K int_min=<…>K
```

`ballast` — скільки зайнято; `kbps_avg`, `rx`, лічильники й мінімуми — лише за цю фазу. Бітрейт
змінюється з боку ПК (`rate_server.py`), тож сітку «пам'ять × бітрейт» можна пройти за один прогін
без перепрошивки.

## TLS: вхідний буфер 16 КБ

`CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=16384` (OUT — 4096). Станція `SOAK_URL` ігнорує
`max_fragment_length` і шле записи по 16 КБ; запис, що не влазить у вхідний буфер, не розшифровується,
і з 4096 кожне читання падає з `esp-tls-mbedtls: read error :-0x7200` приблизно через секунду. Плеєр
має те саме значення (`boards/p4_player/sdkconfig.board`). У плеєрі mbedTLS алокує з PSRAM
(`CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC`); у соук-тесті цієї опції немає, тож TLS-сесія займає внутрішню
RAM і це видно в `int` / `dma`.

## Запуск

Передумови: C6 уже на CP 3.0.7 ([C6_UPDATE.md](C6_UPDATE.md)); плеєр хоча б раз підключався до Wi-Fi — тест
читає мережу з того самого NVS (простір `wifi`, ключі `ssid`, `pass`). Якщо NVS вимагає стирання, тест
відмовляється працювати, щоб не втратити облікові дані плеєра.

```
cd tools/sdio_soak
idf.py set-target esp32p4
idf.py menuconfig          # «SDIO soak test»
idf.py build
idf.py -p <порт панелі> flash
idf.py -p <порт панелі> monitor | tee soak.log
```

- Windows: перед збіркою `$env:IDF_COMPONENT_CACHE_PATH = "<коротка тека>"` (як для плеєра);
  лог — `| Tee-Object -FilePath soak.log`. Без `idf.py`: `python -m esp_idf_monitor -p <порт>
  build/sdio_soak.elf`.
- `idf.py monitor` при підключенні скидає P4 (DTR — лінія скидання USB-Serial/JTAG), тест починається
  заново. Підключатися на початку прогону і не перепідключатися.
- Таблиця розділів (`tools/sdio_soak/partitions.csv`) рядок у рядок — `partitions_p4_16mb.csv`. Тест
  стає в `ota_0` замість плеєра; NVS, `data` і `userdata` не чіпаються.
- Повернути плеєр: з кореня репозиторію `idf.py -p <порт панелі> flash`.

## Як читати результат

- `stall` > 0, після якого `reconn` не рятує і `rx` не росте, — лінк завис. Це негативний результат.
- `stall` > 0, що закінчується `data resumed` або перепідключенням за секунди, — пауза, не
  зависання; дивитися, як часто.
- `reconn` кілька за ніч з миттєвим відновленням і `stall = 0` — норма для інтернет-радіо (сервери
  самі рвуть з'єднання); тоді дивитися на `wifi_drop` і `http_err`.
- `ring_underruns` при `stall = 0` — drain вищий за реальний бітрейт джерела, не проблема лінку.
- `dma_min` — найнижча вільна DMA-SRAM поруч із потоком; у фазовому прогоні — порівнювати по фазах
  `SOAK PHASE END`: фаза, де ростуть `stall` / `reconn` або падає `kbps_avg`, показує поріг пам'яті,
  нижче якого лінк деградує.
