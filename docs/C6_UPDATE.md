# Оновлення прошивки ESP32-C6 (ESP-Hosted co-processor)

Wi-Fi панелі — це ESP32-C6, з яким ESP32-P4 говорить по SDIO через ESP-Hosted. Хост-бібліотека
(`espressif/esp_hosted` **3.0.7**, `espressif/esp_wifi_remote` 1.6.4, прибиті в
`components/modesp_net/idf_component.yml`) і прошивка C6 (co-processor, CP) мають бути з **одного
релізу** esp_hosted; CP не старіший за хост. Заводський CP на C6 — 2.3.x, старіший за хост, тож
нова панель спершу проходить розділи 2–4. Прошити C6 можна лише через P4 (updater, розділ 3) або
через UART C6 на гребінці JP1 (розділ 5).

Розбіжність версій у лозі плеєра (рівень WARN і вище, видно на штатній збірці):

```
E (...) eh_init_evt: major version mismatch — OTA coprocessor from host
W (...) eh_init_evt: minor version mismatch — OTA coprocessor from host
```

Різниця лише в patch-версії — INFO `patch version differs (compatible)`, працює.

## 1. Поточна версія CP

Рядок `eh_init_evt: esp-hosted fw versions: host=3.0.7 coprocessor=a.b.c` (з ` (match)`, коли
збігаються). Він INFO і друкується, коли C6 після свого старту надсилає хосту подію ініціалізації:

- у лозі updater-а (розділ 3) — завжди: його збірка лишає типовий рівень IDF (INFO);
- у лозі плеєра — лише на збірці з `CONFIG_LOG_DEFAULT_LEVEL_INFO` у локальному `sdkconfig`:
  штатна збірка на WARN, а `/api/loglevel` відкриває тег уже після того, як рядок пройшов.

Рядок «Сервіс» у налаштуваннях панелі показує `C6 3.0.7` — це константа в
`modules/ui_lvgl/src/screen_set.cpp`, а не версія, прочитана з C6.

## 2. Зібрати CP-прошивку для C6

Джерело — та сама версія, що прибита в `idf_component.yml`: репозиторій `espressif/esp-hosted-mcu`,
тег `v3.0.7`. **Каталог клону має називатися `esp_hosted`**: приклади підключають компонент через
`override_path` на корінь репозиторію, а ім'я IDF-компонента = ім'я каталогу (з іншою назвою збірка
падає з `Failed to resolve component 'esp_hosted'`).

```
git clone --depth 1 --recurse-submodules --shallow-submodules -b v3.0.7 https://github.com/espressif/esp-hosted-mcu.git esp_hosted
cd esp_hosted/examples/ota/coprocessor_ota/cp
idf.py set-target esp32c6
idf.py build          # → build/eh_cp_ota_coprocessor_ota.bin, ~1,1 МБ
```

- Транспорт: `menuconfig` → Configure coprocessor → CP transport → «Communication bus» = SDIO
  (типовий для C6). Піни SDIO на боці C6 фіксовані апаратно.
- Таблиця розділів CP. Приклад збирає `partitions_eh_cp_ota_4m.csv` і 4 МБ flash. Таблицю задають
  і `sdkconfig.defaults`, і `sdkconfig.defaults.esp32c6`; другий застосовується після першого і
  перекриває його. Для C6 з 2 МБ flash (розмір — `esptool --chip esp32c6 -p <порт> flash_id` через
  UART C6, розділ 5) у `sdkconfig.defaults.esp32c6` поставити
  `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions_eh_cp_ota_2m.csv"` і
  `CONFIG_ESPTOOLPY_FLASHSIZE_2MB=y`, видалити `sdkconfig` і `build/` (defaults засівають лише новий
  `sdkconfig`), після збірки перевірити `--flash_size 2MB` у `build/flash_args`. Слот 2-МБ таблиці —
  960 КБ (0xF0000), а образ 3.0.7 з типовою конфігурацією прикладу займає ~1,1 МБ; IDF звіряє розмір
  образу з app-розділом під час збірки.
- `eh.py` / `install.sh` з репозиторію не потрібні: приклад — звичайний IDF-проєкт.

## 3. Прошити C6 через P4 (updater, штатний шлях)

`tools/c6_updater/` — окремий IDF-проєкт для P4: приклад `examples/ota/coprocessor_ota/mcu_host` з
esp_hosted 3.0.7, метод «Partition OTA» (`CONFIG_OTA_METHOD_PARTITION`). Він:

1. читає CP-образ із розділу `slave_fw` flash P4 (0x630000, 2 МБ);
2. перевіряє образ (розділ 3а);
3. порівнює версію образу з версією, що працює на C6 (префікс `v` відкидається), і пропускає OTA,
   якщо вони однакові;
4. передає образ на C6 по SDIO (`esp_hosted_slave_ota_begin/write/end`), на CP ≥ 2.6.0 активує його
   (`eh_host_cp_ota_activate()`) і перезапускає P4.

Таблиця розділів updater-а (`tools/c6_updater/partitions.csv`) рядок у рядок повторює
`partitions_p4_16mb.csv`: `slave_fw` лежить там, де його чекає плеєр, а таблиця, яку пише updater,
не забирає `userdata`. Updater ставиться в `ota_0` замість плеєра; `data`, `userdata` і NVS не
чіпаються (образів для них у проєкті немає).

```
cp esp_hosted/examples/ota/coprocessor_ota/cp/build/eh_cp_ota_coprocessor_ota.bin tools/c6_updater/components/ota_partition/slave_fw_bin/
cd tools/c6_updater
idf.py set-target esp32p4
idf.py build
idf.py -p <порт панелі> flash monitor
```

- Windows: перед збіркою `$env:IDF_COMPONENT_CACHE_PATH = "<коротка тека>"` — пакет esp_hosted 3.0.7
  не розпаковується у стандартний кеш (шляхи довші за 260 символів).
- `slave_fw_bin/*.bin` не в git. У `slave_fw` іде **найновіший за часом файлу** `.bin` із цієї теки.
- Крок, що пише `slave_fw`, викликає esptool сам і бере порт з `ESPPORT` (його ставить `idf.py -p`)
  або `ESPTOOL_PORT`; без них esptool перебирає всі послідовні порти машини.
- Якщо `idf.py flash` не проходить, прошити напряму з `build/`:

  ```
  python -m esptool --chip esp32p4 -p <порт> -b 460800 write_flash "@flash_args"
  python -m esptool --chip esp32p4 -p <порт> write_flash 0x630000 ../components/ota_partition/slave_fw_bin/eh_cp_ota_coprocessor_ota.bin
  ```

Лог updater-а (приклад — заводський CP 2.3.2):

1. `eh_init_evt: esp-hosted fw versions: host=3.0.7 coprocessor=2.3.2` →
   `eh_init_evt: major version mismatch — OTA coprocessor from host`.
2. `host_performs_slave_ota: Starting OTA via Partition` →
   `ota_partition: image: <N> bytes, <k> segment(s), version v3.0.7, hash appended` →
   `Firmware verified - Size: <N> bytes, Version: v3.0.7` →
   `Version differs - proceeding with OTA from 2.3.2 to v3.0.7`.
   `<N>` — довжина образу, яку друкує `tools/c6_image.py verify`; для `.bin` від esptool вона дорівнює
   довжині файлу.
3. `Partition OTA completed successfully - Sent <N> bytes`, `OTA completed successfully!`. Далі на
   CP < 2.6.0 — `Activate API not supported (requires v2.6.0+)` (C6 сам завантажує новий образ), на
   CP ≥ 2.6.0 — `New firmware activated - slave will reboot`; потім
   `Restarting host to resync with slave...`.
4. Після перезапуску: `esp-hosted fw versions: host=3.0.7 coprocessor=3.0.7 (match)`,
   `RPC version negotiated: V2`,
   `Current slave firmware version (3.0.7) is the same as new version (v3.0.7). Skipping OTA.` →
   `OTA not required - slave firmware is up to date`. Так updater відповідає на кожному старті, доки
   не прошито плеєр (розділ 4).

Перевірка прикладу «host ↔ CP» (`CONFIG_OTA_VERSION_CHECK_HOST_SLAVE`) вимкнена: вона порівнює CP з
compat-константами прикладу (2.12.x), а не з версією хоста. Ворота — порівняння версії образу з
версією CP (`CONFIG_OTA_VERSION_CHECK_SLAVEFW_SLAVE`).

Якщо updater не з'єднується з C6 (`eh_host_connect_to_slave()` повертає помилку, `ESP_ERROR_CHECK`
перезапускає P4) — розділ 5.

## 3а. Перевірка образу — двічі, до того як він потрапить на C6

Wi-Fi панелі є лише на C6, а прошити C6 по повітрю може лише P4: активований зіпсований образ лишає
панель без мережі, і далі — тільки розділ 5. Тому образ перевіряється двічі, тими самими трьома
перевірками: магія заголовка `0xE9` і таблиця сегментів, що вміщується у файл; байт-checksum esptool
(`0xEF` xor усіх байтів даних сегментів); доданий SHA-256 (коли в заголовку `hash_appended`).
Довжина образу рахується так, як її пише esptool: checksum — останній байт 16-байтового блоку, перед
хешем вирівнювання немає (`tools/tests/test_c6_image.py`, вісім вирівнювань).

**На ПК, до копіювання в `slave_fw_bin/`:**

```
python tools/c6_image.py verify <образ.bin>
python tools/c6_image.py record <образ.bin> --tag v3.0.7   # записати як відомий-добрий
python tools/c6_image.py check  <будь-яка копія>           # чи це той самий образ
```

`record` пише `tools/c6_updater/known_good.json`: sha256, довжину, версію, проєкт, тег і версію
esp_hosted (`--hosted`, типово 3.0.7). Образ у git не входить; відбиток призначений для git, але в
репозиторії його немає, тож `check` відповідає `no fingerprint yet — run: python tools/c6_image.py
record <image.bin>`.

**На пристрої, перед передачею:** `tools/c6_updater/components/ota_partition/ota_partition.c` перевіряє образ у
`slave_fw` **до** `esp_hosted_slave_ota_begin()`. Не зійшлося — жоден байт не йде на C6 і
активація не викликається:

```
E (...) ota_partition: the image's own SHA-256 does not match its contents
E (...) ota_partition: REFUSING to flash the C6: the image in 'slave_fw' did not verify (ESP_ERR_INVALID_CRC)
E (...) ota_partition: Nothing was sent to the co-processor; it is still running what it had.
```

Образ без доданого SHA-256 проходить з WARN
`no SHA-256 in this image - only the 1-byte checksum could be verified`.

## 4. Прошити плеєр

Updater займає `ota_0`, тож після нього — повна прошивка плеєра з кореня репозиторію:

```
idf.py set-target esp32p4
idf.py build
idf.py -p <порт панелі> flash
```

Повна прошивка переписує `data` з `data.bin`; `userdata` і `slave_fw` не чіпає (образів для них
збірка плеєра не робить). Після старту в лозі не має бути рядків `eh_init_evt: … version mismatch`.

## 5. Відновлення: прошивка C6 напряму через UART (JP1)

Коли updater не може домовитися з C6 (застара прошивка, зіпсований образ). На гребінці JP1 виведено
UART0 C6: піни **18 / 20 / 22 / 24 = U0RXD, U0TXD, IO9 (BOOT), CHIP_PU (reset)** (розпіновка JP1 —
[CONCEPT.md](CONCEPT.md), §4). Рівні 3,3 В.

1. **Спершу зупинити P4**: він керує GPIO54 (reset C6) і скине C6 посеред прошивки. Ввести P4 у
   download-режим (утримуючи BOOT — GPIO35, кнопка SW1 на платі, паралельно з кнопкою енкодера —
   скинути P4) або прошити на P4 образ, що не ініціалізує ESP-Hosted. Монітор P4 на цей час не
   відкривати.
2. USB-TTL 3,3 В: TX адаптера → U0RXD (18), RX адаптера → U0TXD (20), GND ↔ GND.
3. Download-режим C6: IO9 (22) на GND, короткий імпульс CHIP_PU (24) на GND, відпустити IO9.
4. Прошити образи з каталогу збірки CP; зміщення — з його `build/flash_args`, не вгадувати (для обох
   таблиць прикладу: `0x0 bootloader.bin`, `0x8000 partition-table.bin`,
   `0xd000 ota_data_initial.bin`, `0x10000 eh_cp_ota_coprocessor_ota.bin`; `--flash_size` — за
   конфігурацією, розділ 2):

   ```
   cd esp_hosted/examples/ota/coprocessor_ota/cp
   idf.py -p <порт USB-TTL> flash      # або: cd build; python -m esptool --chip esp32c6 -p <порт> write_flash "@flash_args"
   ```

5. Зняти IO9 з GND, імпульс CHIP_PU — C6 стартує з новою прошивкою. Перезапустити P4.

## 6. Конфігурація хоста, від якої залежить зв'язок із C6

- **Полярність reset C6.** GPIO54 іде прямо на CHIP_PU: LOW — скидання, HIGH — робота. У
  ESP-Hosted 3.x це `CONFIG_ESP_HOSTED_SDIO_RESET_ACTIVE_LOW=y`. З `ACTIVE_HIGH` пін стоїть у LOW,
  C6 тримається в скиданні, і хост бачить лише `sdmmc_init_ocr … 0x107`.
- **Ревізія кристала.** На панелі ESP32-P4 **v1.3**. IDF 5.5 типово збирає для v3.1+, і
  bootloader/esptool відмовляють (`requires chip revision in range [v3.1 - v3.99]`). Тому у трьох
  проєктах — плеєр (`boards/p4_player/sdkconfig.board`), `tools/c6_updater/sdkconfig.defaults`,
  `tools/sdio_soak/sdkconfig.defaults` — стоять `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y` і
  `CONFIG_ESP32P4_REV_MIN_100=y`.
- **Блок SDIO** у цих трьох файлах однаковий і має таким лишатися: слот 1, 4 біти, 40 МГц,
  CLK 18, CMD 19, D0–D3 14–17, reset GPIO54, `CONFIG_ESP_HOSTED_HOST_SDIO_RX_STREAMING_MODE=y`.

## 7. Зміна версії esp_hosted

Версія записана в кількох місцях, і всі мають змінюватися разом:

| Де | Що |
|---|---|
| `components/modesp_net/idf_component.yml` | `espressif/esp_hosted`, `espressif/esp_wifi_remote` — плеєр |
| `tools/c6_updater/main/idf_component.yml` | ті самі два пакети — updater |
| `tools/sdio_soak/main/idf_component.yml` | ті самі два пакети — соук-тест |
| `modules/ui_lvgl/src/screen_set.cpp` | текст `C6 3.0.7` у рядку «Сервіс» |
| `tools/c6_image.py` | типове значення `record --hosted` |

`components/hosted_mem` (`CONFIG_HOSTED_MEM_FRAMES_IN_PSRAM`, типово `y`) обгортає внутрішню функцію
esp_hosted `eh_host_port_dma_alloc()` і повторює її прототип. CMake зупиняє збірку, якщо
`port/os/include/eh_host_port_dma.h` зник або прототип у ньому змінився: обгортку треба звірити з
новою версією.

Після зміни: зібрати CP з того самого тега (розділ 2) → покласти `.bin` у `slave_fw_bin/` → прогнати
updater (розділ 3) → прошити плеєр (розділ 4).
