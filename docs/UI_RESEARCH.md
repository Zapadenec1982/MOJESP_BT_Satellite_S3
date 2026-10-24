# Огляд LVGL-проєктів з сучасним дизайном для аудіо (плеєри, стрімери, EQ/DSP)

Дата: 15.09.2026. Мета — що взяти для фази 4 «UI» плеєра (панель 480×800 портрет, LVGL 9.3,
esp_lvgl_port 2.9, тач GT911, енкодер). Оцінка «що брати» — моя; рішення про напрям дизайну —
замовника.

## 1. Плеєри / стрімери на ESP32 з LVGL

| Проєкт | Платформа | Що в дизайні | Ліцензія | Брати |
|---|---|---|---|---|
| [SonosESP](https://github.com/OpenSurface/SonosESP) — контролер Sonos | ESP32-P4, LVGL 9.5, 800×480 і 1024×600 MIPI-DSI, апаратний JPEG | Три теми плеєра (**Amber** — плоска тепла панель з одним золотим акцентом, обкладинка edge-to-edge зліва, усі контроли завжди видимі; **SonosESP** — розмита обкладинка як фон; **Immersive** — full-bleed колір, великий анімований рядок лірики). П'ять «облич» standby-годинника (Amber/Horizon/Orbit/Monolith/StandBy — цифри тоновані домінантним кольором обкладинки). Домінантний колір з обкладинки → тема, `shade()` з насиченістю/яскравістю. Реєстр тем — один масив `THEMES[]` з builder-функцією на тему; масштабування під два розміри через `ui_scale.h`. Latin-1/Latin Ext-A у шрифтах | MIT | **Архітектуру**: реєстр тем + builder, standby-годинник, домінантний колір з JPEG. Стиль Amber — найближчий до «hi-fi без зайвого». Код Arduino — не вбудовувати, лише як довідник (CONCEPT §8б) |
| [cloudspores/tab5](https://github.com/cloudspores/tab5) — інтернет-радіо на M5Stack Tab5 | ESP32-P4 + C6, IDF 6.1, LVGL 9, 1280×720, PPA-поворот | «Приладова панель»: пронумеровані модулі 01 STATION / 02 CONTROL / 03 SIGNAL (dBFS-точковий метр) / 04 TUNE / 05 PRESETS, один помаранчевий акцент, спільний верхній бар, шрифти Familjen Grotesk + JetBrains Mono (OFL). ICY-метадані, esp_audio_codec 2.5, перепідключення | (див. репо) | **Мова дизайну** для інженерного плеєра: модульна сітка, моно-шрифт для чисел, один акцент. Приклад PPA-повороту на P4 (CONCEPT §8а: питання портрет/ландшафт) |
| [Yoradio_lvgl](https://github.com/Witaliy76/Yoradio_lvgl) — touch-first інтернет-радіо | ESP32-S3 (профіль P4 є), LVGL 9.5, 480×480 ST7701S + GT911 | Кільце з 6 екранів свайпом (Info / Main / Visual / Station / Weather / Settings); **Visual** — «цифрова реконструкція класичного стрілочного індикатора», два канали −20…+5 дБ; пресети свайпом вниз; теми Dark/Light/Custom (текстовий файл палітри); TTF-шрифти, іконки Tabler; RU/EN/PL/SK | GPL-3.0 | **Ідеї**: VU-екран, ring-навігація свайпом, пресети «як в автомагнітолі» (CONCEPT §8 радіо), Tabler-іконки. Код не копіювати (GPL) |
| [esp32p4-dashboard](https://github.com/lobuche67/esp32p4-dashboard) — ESPHome/LVGL | Guition JC-ESP32P4 1024×600 | Сторінка «Music»: велика обкладинка, title/artist/album одним кеглем 22 px, транспорт + смуга гучності, автопробудження екрана на play; обкладинка через власний `remote_image` (штатний `online_image` глючить з LVGL) | не вказана | Довідник по граблях обкладинок на P4 (RGB565 LE, буфер 200 КБ) |
| [HA_MediaController ESP32-4848S040](https://github.com/VahaC/HA_MediaController_ESP324848S040_EspHome_LVGL) ([стаття](https://vahac.com/blogs/esp32-4848s040-media-controller-with-esphome-and-lvgl/)) | ESP32-S3, 480×480 | Обкладинка на весь екран як фон, **дуга прогресу** навколо, автоскрол довгих назв, 7 кольорів + 4 прозорості налаштовуються без перепрошивки, Roboto з кирилицею | open source | Дуга прогресу/гучності (`lv_arc`), налаштовувана палітра зі стану |
| [Album art + dynamic colors from Music Assistant](https://community.home-assistant.io/t/esp32-display-with-album-art-and-dynamic-colors-from-music-assistant/1010884) | ESP32-S3, ESPHome | Уся палітра UI (фон, кнопки, текст) з домінантного кольору обкладинки; яскравість тексту підлаштовується для читабельності | — | Правило контрасту тексту при динамічній темі |
| [LVGL Music Player demo](https://github.com/lvgl/lvgl/tree/master/demos/music) ([демо](https://lvgl.io/demos)) | будь-яка, 480×272 / 272×480 | «Смартфонний» плеєр: обкладинка в колі, **спектр навколо обкладинки** (4 смуги, custom draw), список треків, анімації | MIT | Готовий приклад custom-draw спектра і анімацій під LVGL 9; портретний варіант близький до наших пропорцій |
| [gcore_lvgl_music_player](https://github.com/danjulio/gcore_lvgl_music_player), [ESP32-bluetooth-music-player-lvgl](https://github.com/drabaioli/ESP32-bluetooth-music-player-lvgl), [ESP32_Display_LVGL_MP3_Player](https://github.com/qq71680264/ESP32_Display_LVGL_MP3_Player) | ESP32/S3 | Порти демо / прості плеєри (енкодер KY-040 як indev у другому) | різні | Лише як приклади коду енкодер→LVGL |

Squeezelite-ESP32 ([sle118](https://github.com/sle118/squeezelite-esp32)) — дисплеї SPI/I2C моно/кольорові без LVGL; для дизайну не референс.

## 2. EQ / DSP з екраном

| Проєкт | Що є | Ліцензія | Брати |
|---|---|---|---|
| [fusiblemcu — Parametric EQ & Crossover на Guition JC4827W543C](https://github.com/fusiblemcu/ESP32-S3-Parametric-Equaliser-and-Crossover-based-on-Guition-JC4827W543C) ([diyAudio](https://www.diyaudio.com/community/threads/esp32-s3-touchscreen-parametric-eq-2-way-crossover-project-guition-jc4827w543c.439631/), [відео](https://www.youtube.com/watch?v=OtHIzkW23tQ)) | **Найближчий аналог нашого тракту**: ESP32-S3 + PCM1808 + 2×PCM5102A, full-duplex I2S 48 кГц, до 120 біквадів/SVF, 2-way кросовер, лімітер, тест-тон, 4 пресети. Тач-UI на LVGL (Arduino): тайли, слайдери + **крива АЧХ**, екран кросовера, спектроаналізатор, «whirlpool» візуалізатор. Автор виправляв розбіжність кривої й фільтра до 12 дБ на НЧ/високих Q — урок: **малювати криву з тих самих коефіцієнтів, що й DSP** (у нас `design_band()` → `magnitude_db()` уже є в host-тестах) | MIT | Ідея екрана EQ (слайдери + крива), кросовер/пресети; Arduino-код — довідник |
| [weq8](https://github.com/teropa/weq8) (Web Audio) | Еталон інтерфейсу PEQ «як Ableton EQ Eight»: одна крива АЧХ, **8 перетягуваних ручок** (X = частота, Y = gain, колесо/жест = Q), типи фільтрів, 12/24 дБ | ISC | Концепція взаємодії для тач-екрана 480 px: ручки на кривій замість 30 слайдерів |
| [dspcrossover](https://github.com/lvdopqt/dspcrossover), [Elektor_AudioDSP](https://github.com/ClemensAtElektor/Elektor_AudioDSP), [SigmaDSP](https://github.com/MCUdude/SigmaDSP) | ADAU1701 + енкодер/OLED — інший клас UI | — | ні |
| [esp32s3_audio_dsp](https://github.com/Kristian8606/esp32s3_audio_dsp) | esp-dsp IIR EQ + FIR-кросовер на S3, конфіг через menuconfig, без UI | GPL-3.0 | приклад esp-dsp FIR, не UI |
| [USB-DAC-with-interactive-Parametric-EQ](https://github.com/sh1ura/USB-DAC-with-interactive-Parametric-EQ) | Pico, PEQ 4 кнопками + LCD, без LVGL | — | ні |

Спектроаналізатори з FFT на ESP32 ([ESP32_FFT_VU](https://github.com/s-marley/ESP32_FFT_VU), [ESP32-oled-spectrum](https://github.com/colonelwatch/ESP32-oled-spectrum), [ESP32_Music_Spectrum_Analyzer](https://github.com/jayagopalk11/ESP32_Music_Spectrum_Analyzer)) — LED-матриці/OLED, не LVGL; математику (Constant-Q, лог-смуги) можна взяти, вивід — свій.

## 3. LVGL 9: що корисно саме для аудіо-UI

- **Метри**: `lv_meter` з 8.x видалено; у 9.x — `lv_scale` (дуги/лінійки з секціями), `lv_bar`, `lv_arc`; лог-шкала — мапити дБ → 0…1 у коді і годувати лінійний віджет, плавність — ramp у задачі даних ([LVGL forum](https://forum.lvgl.io/t/logarithmic-scale-meter-vu-sound-meter-using-lvgl-on-esp32-s3/17687)).
- **Крива EQ**: `lv_chart` (line series, до 480 точок) + власні draggable-об'єкти поверх; або custom draw через `LV_EVENT_DRAW_MAIN` як у music-demo спектрі.
- **Апаратне прискорення P4**: LVGL 9.4 додала PPA-прискорення для ESP32-P4 (≈30 % швидше рендер, −30 % CPU) ([changelog](https://docs.lvgl.io/9.4/CHANGELOG.html)); SonosESP і tab5 уже на 9.4/9.5 — аргумент підняти нашу `~9.3.0` після перевірки Kconfig-змін.
- **Шрифти/іконки з відкритою ліцензією**, які використовують ці проєкти: Familjen Grotesk, JetBrains Mono (OFL), Roboto (з кирилицею), Montserrat (у LVGL), іконки Tabler (MIT).
- Редактори: SquareLine Studio (співпраця з LVGL завершена), EEZ Studio, LVGL Pro — для нашого маніфестного підходу код-first у C++ простіший, ніж генерований YAML/C.

## 4. Висновок для нашого плеєра (пропозиція)

Мова дизайну — «інженерний hi-fi» (як tab5/Amber): темний плоский фон, один акцент, великі цифри
моно-шрифтом, обкладинка/логотип станції без розмиття, нічого декоративного. Екрани:

1. **Now Playing** (є перший зріз): джерело, назва/станція, гучність, L/R-пікметри; для радіо —
   обкладинка/логотип і ICY-рядок; домінантний колір — опційно (SonosESP-підхід).
2. **Джерела/пресети**: клавіші джерел + 6 пресетів радіо «як в автомагнітолі» (коротке — грати,
   довге — записати; енкодер перебирає) — з CONCEPT §8.
3. **EQ**: крива АЧХ з 10 ручками (weq8-концепція), під нею панель обраної смуги (f / gain / Q,
   тип), крива рахується з тих самих RBJ-коефіцієнтів, що й DSP (`design_band()`); пресети;
   перемикач EQ on/bypass; лімітер-індикатор (gain reduction) з `audio.*`.
4. **Налаштування**: підсвітка, Wi-Fi, час, тема, версія/OTA.
5. **Standby-годинник** через N хвилин без дій (велике «години над хвилинами», тон з обкладинки).

Енкодер — гучність скрізь; довге натискання — mute; свайпи між екранами (ring, як Yoradio) або
клавіші зверху. Орієнтація — портрет нативно; ландшафт через PPA (рецепт Allsky, LVGL 9.4) —
окремий експеримент.

Що не копіювати як код: SonosESP/Yoradio/fusiblemcu — Arduino або GPL; беремо лише ідеї,
дизайн-токени (палітри, відступи, шрифти) і алгоритми (домінантний колір, ramp метрів).
