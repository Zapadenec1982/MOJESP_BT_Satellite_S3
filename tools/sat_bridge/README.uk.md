[English](README.md) · **Українська**

# sat_bridge

UART-міст для плати ESP32-S3, чий Bluetooth-сателіт — голий ESP32-WROOM, напаяний на плату
(`boards/s3_player`, [S3_PORT.uk.md](../../docs/S3_PORT.uk.md), «Підключення»). Він працює на S3
замість плеєра, передає кожен байт між роз'ємом COM плати (UART0) і UART0 сателіта (GPIO11/12) на
115200 і за командою керує EN та IO0 сателіта (GPIO13, GPIO2), щоб esptool на ПК дістався до його
ROM-завантажувача. До першої команди обидва піни міст не чіпає.

| Команда (`sat_cmd.py`) | Сателіт |
|---|---|
| `#SAT-DL#` | перезапускається в ROM-завантажувач (IO0 низький під час скидання) |
| `#SAT-RUN#` | перезапускається у свою програму |

## Прошивка сателіта

У середовищі ESP-IDF v5.5.5; `<port>` — роз'єм COM.

1. Тут, у `tools/sat_bridge`: `idf.py set-target esp32s3 && idf.py -p <port> flash`. Міст
   зібраний із власною таблицею розділів плеєра (`partitions_s3_16mb.csv`) і його налаштуваннями
   flash, тож лягає в ota_0, як плеєр, і лишає налаштування, файли й дані користувача плеєра на
   місці.
2. `python sat_cmd.py <port> '#SAT-DL#'`: відповідь закінчується на `waiting for download`.
3. З теки збірки сателіта (modesp-bt-satellite, bare-збірка з `sdkconfig.bare`), на 115200 і без
   ліній скидання: міст не міняє швидкість, а лінії роз'єму COM скидають S3, а не сателіт.
   `python -m esptool --chip esp32 -p <port> -b 115200 --before no_reset --after no_reset write_flash @flash_args`
4. `python sat_cmd.py <port> '#SAT-RUN#'`
5. Плеєр назад, з кореня репозиторію:
   `idf.py -B build_s3 -D SDKCONFIG=build_s3/sdkconfig -p <port> flash`. Після цього його консоль
   пише `Bt: link up: sat ...`.

Сателіт, у чиї eFuse пропалено Secure Boot V2 і шифрування flash, приймає лише образи, підписані
його власним ключем, і записані з шифруванням: `encrypted_app-flash_args` збірки (`--encrypt`),
ніколи не завантажувач, ніколи не `--force`. Стирати такий чип esptool відмовляється; замість цього
записують 0xFF.
