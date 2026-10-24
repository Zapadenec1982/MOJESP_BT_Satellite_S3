# Оновлення прошивки ESP32-C6 (ESP-Hosted co-processor)

Wi-Fi на панелі Guition JC4880P443C живе на ESP32-C6, з яким ESP32-P4 говорить по SDIO через
ESP-Hosted. Хост-бібліотека (`espressif/esp_hosted` **3.0.7**, `esp_wifi_remote` 1.6.4 — прибиті в
`components/modesp_net/idf_component.yml`) і прошивка C6 (co-processor, CP) мають бути **однієї
версії**: CP не старіший за хост. З заводу на C6 стоїть стара slave-прошивка (у польових нотатках
для цієї панелі — 2.3.0), тому перший крок етапу 1 — оновити C6.

Симптоми розбіжності версій у логу P4: `Version on Host is NEWER than version on co-processor`,
`rpc_core: Response not received`, підключення до Wi-Fi «є», але кожен запит повертає помилку,
`Dropping packet(s) from stream` / `Association Leave`.

## 1. Прочитати поточну версію CP

- **З логу плеєра** при старті: ESP-Hosted друкує версію хоста і CP (`Host firmware version`,
  `Slave/CP firmware version`) і попередження, якщо вони не збігаються.
- **З логу updater-а** (розділ 3): `Host firmware version: 3.0.7` / `Slave firmware version: a.b.c`.

## 2. Зібрати CP-прошивку для C6

Джерело — та сама версія, що прибита в `idf_component.yml`: репозиторій `espressif/esp-hosted-mcu`,
тег `v3.0.7`. **Каталог клону має називатися `esp_hosted`** — приклади підключають компонент через
`override_path` на корінь репо, а ім'я IDF-компонента = ім'я каталогу (з іншою назвою збірка падає з
`Failed to resolve component 'esp_hosted'`).

```powershell
git clone --depth 1 --recurse-submodules --shallow-submodules -b v3.0.7 https://github.com/espressif/esp-hosted-mcu.git D:\P4_DSP\esp_hosted
. C:\Espressif\tools\Microsoft.v5.5.5.PowerShell_profile.ps1
cd D:\P4_DSP\esp_hosted\examples\ota\coprocessor_ota\cp
idf.py set-target esp32c6
idf.py menuconfig      # лише перевірити: Component config → ESP-Hosted → Configure coprocessor → CP transport = SDIO (default)
idf.py build           # → build\eh_cp_ota_coprocessor_ota.bin (~1,1 МБ; розділ slave_fw — 2 МБ)
```

- Піни SDIO на боці C6 фіксовані апаратно — нічого не налаштовувати.
- Таблиця розділів CP за замовчуванням — `partitions_eh_cp_ota_4m.csv` (4 МБ flash). Якщо на
  панелі C6 з 2 МБ flash (перевірити `esptool --chip esp32c6 -p COMy flash_id` через UART C6, розділ 5) —
  у `sdkconfig.defaults` поставити `partitions_eh_cp_ota_2m.csv`.
- `examples/ota/coprocessor_ota/cp` і `examples/wifi/sta/cp` мають ідентичний `sdkconfig.defaults`
  (RPC v2, Wi-Fi, System/OTA) — образ універсальний.
- Windows: `eh.py`/`install.sh` з репо не потрібні — приклад є звичайним IDF-проєктом.

## 3. Прошити C6 через P4 (updater, штатний шлях)

`tools/c6_updater/` — окремий IDF-проєкт для P4 (копія `examples/ota/coprocessor_ota/mcu_host`
з тієї ж версії esp_hosted, метод «Partition OTA»). Він читає CP-образ з розділу `slave_fw` у
flash P4 і передає його на C6 по SDIO (`esp_hosted_slave_ota_begin/write/end/activate`), C6
перезавантажується в нову прошивку, P4 перезапускається. Таблиця розділів updater-а — копія
`partitions_p4_16mb.csv`, тому після прошивки плеєра розділ `slave_fw` лишається на місці.

```powershell
copy D:\P4_DSP\esp_hosted\examples\ota\coprocessor_ota\cp\build\eh_cp_ota_coprocessor_ota.bin `
     D:\P4_DSP\modesp-player\tools\c6_updater\components\ota_partition\slave_fw_bin\
cd D:\P4_DSP\modesp-player\tools\c6_updater
$env:IDF_COMPONENT_CACHE_PATH = "D:\P4_DSP\.idf_cache"   # Windows: пакет esp_hosted 3.0.7 не розпаковується у стандартний кеш (шляхи > 260 символів)
idf.py set-target esp32p4
idf.py build
idf.py -p COMx flash monitor   # COMx = USB-Serial-JTAG панелі (FS Type-C)
```

`idf.py flash` пише updater і **найновіший `.bin` із `slave_fw_bin/`** у розділ `slave_fw`
(0x630000). Якщо `idf.py flash` спотикається (наприклад, esptool сканує чужі COM-порти або bootloader
відмовляє через ревізію кристала) — прошити напряму: `cd build; python -m esptool --chip esp32p4 -p COMx -b 460800 write_flash "@flash_args"`
(розділ `slave_fw` тоді окремо: `python -m esptool --chip esp32p4 -p COMx write_flash 0x630000 ..\components\ota_partition\slave_fw_bin\eh_cp_ota_coprocessor_ota.bin`).
У моніторі очікувано:

1. `eh_init_evt: esp-hosted fw versions: host=3.0.7 coprocessor=2.3.2` → `major version mismatch — OTA coprocessor from host`
   → `Starting OTA via Partition` → `Firmware verified - Size: 1118624 bytes, Version: v3.0.7`.
2. ~8 с запису: `Partition OTA completed successfully`, `OTA completed successfully!`. На заводській 2.3.x
   `Activate API not supported (requires v2.6.0+)` — це нормально: C6 сам перезавантажується в новий образ,
   а хост робить `Restarting host to resync with slave...`.
3. після перезапуску: `esp-hosted fw versions: host=3.0.7 coprocessor=3.0.7 (match)`, `RPC version negotiated: V2`,
   `Current slave firmware version (3.0.7) is the same as new version (v3.0.7). Skipping OTA.` →
   `OTA not required - slave firmware is up to date`. (Рядки `Host firmware version: 2.12.6` у логу — застарілі
   compat-константи прикладу, не версія хоста.)

Перевірено на панелі 15.09.2026: заводський CP був 2.3.2, оновлення пройшло з першої спроби (~8 с).

Якщо CP не відповідає взагалі (`Response not received`, таймаути `eh_host_connect_to_slave`) —
заводська прошивка занадто стара для RPC-каналу; йти в розділ 5.

## 3а. Перевірка образу — двічі, до того як він потрапить на C6 (пункт 11.3)

Wi-Fi на панелі живе лише на C6, а прошити C6 може лише P4. Зіпсований образ, який встигли
активувати, залишає панель без мережі й без будь-якого шляху виправити це дистанційно —
лишається розділ 5 (USB-TTL на JP1). Тому образ перевіряється двічі.

**На ПК, до запису в `slave_fw_bin/`:**

```powershell
python tools/c6_image.py verify D:\P4_DSP\esp_hosted\examples\ota\coprocessor_ota\cp\build\eh_cp_ota_coprocessor_ota.bin
python tools/c6_image.py record <той самий .bin> --tag v3.0.7   # запам'ятати як відомий-добрий
python tools/c6_image.py check  <будь-яка копія>                # чи це він
```

`record` пише `tools/c6_updater/known_good.json` — sha256, довжина, версія. Сам образ (~1,1 МБ)
лежить **поруч із репо**, не в ньому; у git — лише відбиток, щоб питання «це той файл?» мало
відповідь, яка не залежить від пам'яті.

**На пристрої, перед передачею:** `ota_partition.c` перевіряє власні контрольні поля образу
(байт-checksum і доданий SHA-256) **до** `esp_hosted_slave_ota_begin()`. Не збіглось — жоден байт
не йде на C6, і `activate_and_restart()` не викликається взагалі:

```
E (...) ota_partition: the image's own SHA-256 does not match its contents
E (...) ota_partition: REFUSING to flash the C6: the image in 'slave_fw' did not verify
E (...) ota_partition: Nothing was sent to the co-processor; it is still running what it had.
```

**Знайдене при цьому:** колишній `parse_image_header()` рахував довжину образу на 16 байтів
більше за справжню всюди, де дані сегментів не закінчуються на межі 16 байт (він вирівнював ДО
checksum-байта замість того, щоб той байт був останнім у блоці, і додавав ще 16 байт перед хешем,
яких esptool не пише). Це ніколи не вилізло назовні: `esp_ota_write()` на C6 приймав зайві байти,
а `esp_ota_end()` бере довжину з заголовка. Але з нею жодна перевірка SHA-256 не зійшлася б ніколи.
Розкладку й арифметику зафіксовано в `tools/c6_image.py` і в `tools/tests/test_c6_image.py`
(вісім різних вирівнювань).

---

## 4. Прошити плеєр

```powershell
cd D:\P4_DSP\modesp-player
$env:IDF_COMPONENT_CACHE_PATH = "D:\P4_DSP\.idf_cache"
idf.py set-target esp32p4
idf.py build
idf.py -p COMx flash monitor
```

У логу плеєра не має бути попереджень про версії ESP-Hosted; далі — DoD етапу 1
(`GET /api/state`, MQTT `player/v1/…/status`).

## 5. Відновлення: прошивка C6 напряму через UART (JP1)

Коли updater не може домовитись із C6 (занадто стара прошивка, зіпсований образ). На гребінці JP1
виведений UART0 C6: піни **18 / 20 / 22 / 24 = U0RXD, U0TXD, IO9 (BOOT), CHIP_PU (reset)**
(docs/CONCEPT.md §8 «Крок нуль»). Рівні 3,3 В.

1. **Спершу зупинити P4** — він тримає GPIO54 (reset C6) і скине C6 посеред прошивки. Найпростіше —
   ввести P4 у download-режим (BOOT + reset на панелі) або прошити на P4 updater/порожній образ, який
   не ініціалізує ESP-Hosted; і не запускати монітор P4 під час операції.
2. USB-TTL 3,3 В: TX адаптера → U0RXD (18), RX адаптера → U0TXD (20), GND ↔ GND.
3. Download-режим C6: IO9 (22) на GND, короткий імпульс CHIP_PU (24) на GND, відпустити IO9.
4. Прошити образи з каталогу збірки CP — зміщення беруться з його `build/flash_args`, не вгадувати
   (для збірки 3.0.7 з `partitions_eh_cp_ota_4m.csv`: `0x0 bootloader.bin`, `0x8000 partition-table.bin`,
   `0xd000 ota_data_initial.bin`, `0x10000 eh_cp_ota_coprocessor_ota.bin`, `--flash_size 4MB`):

   ```powershell
   cd D:\P4_DSP\esp_hosted\examples\ota\coprocessor_ota\cp
   idf.py -p COMy flash            # або: cd build; python -m esptool --chip esp32c6 -p COMy write_flash "@flash_args"
   ```

5. Зняти IO9 з GND, імпульс CHIP_PU — C6 стартує з новою прошивкою. Перезапустити P4.

## 6. Дві пастки, знайдені на стенді

- **Полярність reset C6.** GPIO54 іде прямо на CHIP_PU: LOW = скидання, HIGH = робота. У термінах
  ESP-Hosted 3.x це `CONFIG_ESP_HOSTED_SDIO_RESET_ACTIVE_LOW` (default). Польові нотатки й 2.x кажуть
  «active-high» — це та сама послідовність «idle high, pulse low» у старому іменуванні. З `ACTIVE_HIGH`
  у 3.x пін паркується LOW, C6 сидить у скиданні, і хост бачить лише `sdmmc_init_ocr … 0x107`.
- **Ревізія кристала.** На панелі ESP32-P4 **v1.3** (engineering sample). IDF 5.5 за замовчуванням збирає
  для v3.1+, і esptool/bootloader відмовляють (`requires chip revision in range [v3.1 - v3.99]`). Тому в
  обох проєктах `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y` + `CONFIG_ESP32P4_REV_MIN_100=y`.

## 7. Правило на майбутнє

Версія `esp_hosted` змінюється лише в `components/modesp_net/idf_component.yml` (і в
`tools/c6_updater/main/idf_component.yml` — тримати однаковими). Після кожного bump-у:
перезібрати CP з того ж тега → покласти `.bin` у `slave_fw_bin/` → прогнати updater → прошити плеєр.
