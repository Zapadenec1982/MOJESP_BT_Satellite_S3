# Панель: MIPI-DSI екран, тач, енкодер (етап 2)

Як екран JC4880P443C і органи керування заходять у ModESP — через ті самі механізми, що й
решта заліза: секції `board.json` → HAL (тільки конфіг) → драйвери (володіють залізом) →
модуль `ui_lvgl` через шов `components/modesp_hal/include/modesp/hal/lcd_port.h`.

## Шов `lcd_port.h`

- `ILcdPanel` — панель: `init()`, `geometry()`, esp_lcd `panel()`/`io()`, `set_backlight(%)`.
- `ITouchPort` — тач-контролер: `init()`, `handle()` для `lvgl_port_add_touch`.
- `ILcdPort` — те, що повертає фабрика драйвера категорії `lcd`; `as_panel()` / `as_touch()`
  кажуть, яку з двох ролей драйвер реалізує (один драйвер = одна роль = один hardware id).

Модуль не знає пінів, таймінгів, init-таблиць — лише отримує esp_lcd-хендли для
`esp_lvgl_port`. Це окремий шов від `display_port.h` (символьні OSD-дисплеї) і від
`panel::IPanelPort` (BLE LED-панель).

## `board.json` (boards/p4_player)

| Секція | Запис | Звідки цифри |
|---|---|---|
| `i2c_buses` | `i2c_0`: SDA 7 / SCL 8, 400 кГц (ES_I2C, спільна з кодеком і JP1 21/23) | CONCEPT §4 |
| `dsi_displays` | `lcd_0`: 2 лінії 500 Мбіт/с, DPI 34 МГц, 480×800, hsync 12/42/42, vsync 2/8/166, **RGB888** (з 24.09.2026; BSP мав RGB565 — сходинки на градієнтах), reset GPIO5, підсвітка GPIO23 LEDC 20 кГц, LDO ch3 2,5 В | CONCEPT §8б (BSP csvke) |
| `i2c_touch` | `touch_0`: GT911 @0x5D на `i2c_0`, INT/RST не розведені (−1) → опитування; `x_max` 800, `y_max` 480, `swap_xy`/`mirror_*` — усі `false` | BSP `bsp_touch.c` + замір 16.09.2026 |
| `encoders` | `enc_0`: A=GPIO32, B=GPIO33, pull-up | CONCEPT §4 |
| `gpio_inputs` | `enc_sw`: GPIO35, pull-up, invert (натиснуто = LOW) | CONCEPT §4 |
| `gpio_outputs` | `amp_enable`: GPIO34, active-high; HAL при старті тримає LOW = mute | CONCEPT §4 |

`bindings.json`: `lcd_0→dsi_st7701`, `touch_0→gt911`, `enc_0→rotary_encoder`,
`enc_sw→digital_input`, усе в модуль `ui_lvgl`.

## Орієнтація (альбомна, 800×480)

Дисплей і тач повертаються **незалежно** — це два різні налаштування в `board.json`.

**Орієнтація — `rotation: 270`** (з 25.09.2026; до того 90): мікрофон плати вгорі. Замовник
попросив перевернути картинку на 180° відносно 90, бо при 90 мікрофон опинявся знизу.

**Екран.** `dsi_displays[].rotation` = 0/90/180/270. DPI-панель не вміє
міняти осі сама (`esp_lcd_panel_swap_xy` на ній не підтримується), тому повертає
LVGL: `flags.sw_rotate` + `lv_display_set_rotation()`. При `CONFIG_LVGL_PORT_ENABLE_PPA=y`
`esp_lvgl_port` віддає кожен блок PPA, а не CPU — у лозі має бути
`LVGL: Setting PPA context for SW rotation`. Якщо цього рядка немає — опція не взялась:
`sdkconfig.board` засіває лише **свіжий** `sdkconfig`, тож старий рядок
`# CONFIG_... is not set` перемагає — його треба видалити з `sdkconfig`.

**Тач.** `esp_lvgl_port` його **не повертає**, але **LVGL повертає**: у 9.3
`indev_pointer_proc()` (`src/indev/lv_indev.c`) крутить точку кожного pointer-пристрою на
поворот його дисплея. Тому тач іде за `rotation` сам, і зміна 90 → 270 перевертає картинку
й дотик разом, без прапорців GT911. Стендовий `tap=` (`ui_screenshot.cpp`) спирається на
те саме і влучає. Прапорці GT911 (`esp_lcd_touch`: `mirror_x` → `mirror_y` → `swap_xy`)
потрібні лише тоді, коли тач не збігається з матрицею сам по собі.

Замір 16.09.2026 (чотири кути екрана, лог `UiLvgl: touch x,y`):

| кут екрана | GT911 |
|---|---|
| лівий верхній | 57, 63 |
| правий верхній | 726, 54 |
| правий нижній | 707, 433 |
| лівий нижній | 81, 412 |

Ця таблиця — з рядка `UiLvgl: touch x,y`, тобто вже **після** повороту LVGL, у координатах
UI (тоді `rotation: 90`). Висновок 16.09, що «GT911 віддає альбомні 800×480 нативно», читав
у ній сирі координати контролера; насправді прапорці GT911 не потрібні тому, що його
система збігається з матрицею, а поворот робить LVGL. `swap_xy` = `mirror_x` = `mirror_y`
= `false` лишаються.

Перевірити після будь-якої зміни орієнтації: натиснути чотири кути й подивитись
на `UiLvgl: touch x,y` — має вийти прямокутник близько (0,0)…(800,480).

## Драйвери

- `drivers/dsi_st7701` (`espressif/esp_lcd_st7701` 2.0.2, нативний MIPI-DSI): LDO PHY →
  DSI-шина → DBI → панель з init-таблицею вендора (BSP csvke, Apache-2.0) → reset → init →
  disp_on → LEDC-підсвітка (0 % до першого кадру). Два фреймбуфери у PSRAM (DPI-драйвер).
- `drivers/gt911` (`espressif/esp_lcd_touch_gt911` 1.2.1): пристрій на `i2c_0`; без чіпа
  `init()` повертає false, модуль працює без тача.
- `drivers/rotary_encoder`: PCNT, 2 канали (обидва фронти обох ліній → 4 імпульси на
  клацання EC11), glitch-фільтр 1 мкс, 32-бітний накопичувач у `update()`; `read()` = клацання.
  Налаштування binding: `steps_per_detent` (4), `invert`.

## Модуль `ui_lvgl`

- LVGL 9.3 + `esp_lvgl_port` 2.9: задача LVGL на ядрі 1, пріоритет 3, стек 16 КБ у PSRAM
  (`CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM`), heap LVGL у PSRAM (`CONFIG_LV_USE_CUSTOM_MALLOC`
  + `src/lv_mem_psram.c`), draw-буфери 2 × 480×100 RGB888 у PSRAM (`buff_dma` вимкнено: порт
  приймає DMA-буфери лише для RGB565), копія у фреймбуфер DMA2D, `avoid_tearing = false`
  (як у BSP на bring-up). `LV_COLOR_DEPTH` = 24, як і `color_bits` дисплея.
- Шрифти: Roboto (кирилиця) і Material Symbols Rounded (контурні + заповнені), генеруються
  `tools/fonts/build_fonts.py` у `src/fonts/` і комітяться; ролі — `src/ui_m3.h`.
- `GET /api/screenshot` — BMP того, що на склі, прямо з кадрового буфера DPI (`src/ui_screenshot.cpp`),
  за логіном веб-інтерфейсу; `CONFIG_UI_SCREENSHOT_OPEN` знімає логін на панелі розробки.
  Параметри: `screen=<id>` спершу показує ту сторінку; `peek=drawer|standby` кладе накладку
  на час знімка, `peek=pair` — діалог спарювання на вигаданому запиті, `peek=bt18` — сторінку
  Bluetooth на вигаданому сателіті 1.8 (разом зі `screen=bt`); нічого з цього не йде на лінк;
  `tap=x,y` / `hold=x,y` (лише з `UI_SCREENSHOT_OPEN`) натискають у точці
  знімка через власний pointer-пристрій LVGL — той самий шлях, що й палець, — і знімають
  результат. Так оболонку проходять зі стенду без руки на панелі.
- `on_update()` (100 Гц) не блокує: читає енкодер/кнопку/годинник у snapshot під spinlock,
  пише `ui.*` у SharedState, застосовує `ui.backlight`. LVGL-таймер (100 мс) переносить
  snapshot у label-и; події тача пишуть координати у snapshot.
- Стан: `ui.screen`, `ui.backlight` (rw, persist, MQTT), `ui.encoder_pos`, `ui.encoder_sw`,
  `ui.touch_down`, `ui.touch_x/y`. Картка «Екран» у WebUI. Ключі `ui.*`, не `ui_lvgl.*` —
  генератор попереджає про префікс, це свідомо (API/MQTT-шлях `ui/…`).

## Головний екран (ui_lvgl 0.2, фаза 4 — перший зріз)

Портрет 480×800: зверху годинник/дата і Wi-Fi (SSID, IP, RSSI коли є); по центру назва
джерела (OFF / TEST TONE / AUX / BLUETOOTH) і рядок стану аудіоядра; гучність — число, смуга і
позначка MUTE; індикатори рівня L/R (−60…0 дБFS); знизу клавіші OFF · TONE · AUX · BT (тач).
Натиснута клавіша → `audio.source` (запит із LVGL-задачі застосовується в головному циклі —
SharedState пише лише один потік). Енкодер → `audio.volume` (2 % на клацання), кнопка → `audio.mute`.
**З 18.10.2026 ручка на кожній сторінці робить одне** (`ui_shell_encoder`,
docs/UX_RESEARCH_2026-10-18.md): поворот — гучність (і знімає вимкнений звук), натиск — пауза й
відтворення того, що грає, утримання — меню; вибір на склі — лише дотиком. Без інтерфейсу кнопка
так само вимикає звук.
Стан аудіо/Wi-Fi дзеркалиться у snapshot 5 разів на секунду. Без модуля `audio` екран показує
«no audio core» і ховає клавіші.

## Перевірка на стенді (DoD етапу 2)

1. Лог: `DsiSt7701: [lcd_main] 480x800 RGB16 …`, `Gt911: [touch_main] GT911 @0x5D …`,
   `RotaryEnc: [encoder_main] PCNT on GPIO 32/33`, `UiLvgl: Initialized (480x800, touch yes)`.
2. Екран: годинник (після SNTP) + дата, SSID/IP, джерело, гучність, клавіші джерел (v0.2).
3. Енкодер: гучність на екрані і `audio.volume` у `/api/state` (лічильник — у рядку `enc N` внизу);
   кнопка → MUTE на екрані, `audio.mute` перемикається.
4. Тач: клавіша OFF/TONE/AUX/BT підсвічується, лог `UiLvgl: source key -> N`, `audio.source`
   у `/api/state`; дотик поза клавішами → `ui.touch_x/y`.
5. Пам'ять: рядки `UiLvgl: heap before lvgl_port_init` / `after LVGL screen` — `DMA` не менший,
   ніж `Heap after init` етапу 1 (≈391 КБ); LVGL і фреймбуфери мають сидіти в PSRAM.

## Відкрите

- GT911 RST/INT: BSP каже «не розведені»; польові нотатки згадують RST = GPIO3 або 22 — звірити
  зі схемою, за потреби вписати `rst_gpio` у `board.json` (драйвер уже вміє).
- Стек LVGL у PSRAM: якщо на стенді впаде під час OTA/NVS-запису — перенести в internal
  (`task_stack_caps` у `ui_lvgl_module.cpp`).
- Експандер I2C для BT-модуля і FAULT/CLIP_OTW: заліза ще немає; коли з'явиться — MCP23017
  (16 ліній, 2 регістрові банки), окремий драйвер за зразком `pcf8574_input`.
