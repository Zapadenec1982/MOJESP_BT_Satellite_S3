# DSP: дослідження «хрипу» на басу — 30.09.2026

Скарга: хрип/шорсткість на басу й на великій гучності, зникає з вимкненим EQ («DSP»).
Шлях: `decode → pcm_to_f32 → resampler → ring → DspChain::process` (trim → 10 смуг PEQ →
width/mono-bass → volume → limiter + soft_clip) `→ f32_to_i32 → I2S → PCM5102A → TPA3255`.
«DSP off» обходить лише смуги EQ. Шість дослідницьких напрямів + хост-стенд; код репо не
змінювався. Проміжні файли — у scratchpad сесії (`dsp_research/*`, `bench/`).

## 1. Коротко

- **Головна причина — ядро біквада DF2 у float32.** Його стан на смузі 40 Гц у 10⁴–10⁶ разів
  більший за вхід, похибка округлення — білий шум, що йде за рівнем басу. З EQ замовника на
  96 кГц: шум −36…−41 dB відносно сигналу, і вище 1 кГц так само гучний (стенд, §3.1). Вимкнення
  EQ прибирає його повністю.
- **Друга причина — лімітер.** EQ піднімає бас на +2.8 dB, піки йдуть у лімітер без lookahead,
  той модулює підсилення з частотою басу й віддає 9–17 % семплів у soft_clip:
  THD+N −49 dB на 50 Гц при +3 dB над порогом (≈ стеля для EQ замовника на 100 %), −44…−41 dB
  при +6…+9 dB (§3.4). Діє лише біля верху гучності (≳90 %).
- **Третій кандидат — аналог.** TPA3255 при PVDD ≤ ~36 В обрізає раніше за 0 dBFS [33]; буст EQ
  цього вистачає, і «EQ off» теж його прибирає. Не виміряно — потрібні дані стенду.
- **Література:** DF2/DF1/TDF2 у single precision на низькій f0 і високій fs — погано; практики
  переходять на double (недоступно на P4) або на трапецієвидний SVF (Simper), який зберігає
  точність у float [1][2][4][8][15].
- **Що першим:** (1) лог `limiter -X dB` + гучність % + PVDD — розвести три причини;
  (2) ядро SVF замість DF2 (та сама АЧХ RBJ); (3) автоматичний запас під EQ; (4) утримання
  (hold) у лімітері.

## 2. Що каже література

**Структури IIR у float.**
- DF1 — для фіксованої коми (один широкий акумулятор); TDF2 — «для float» [1][13][16]. Усі
  прямі форми гіршають на низькій f0 через коефіцієнти зворотного зв'язку, біля 192 кГц у ~4 рази
  гірше [1][2]. Власний код EarLevel тримає стани в double [1].
- Dattorro: помилку усічення підсилюють полюси, до ~90 dB на низьких f0; лікує ширина мантиси,
  не «плаваюча кома» як така; error feedback — техніка фіксованої коми [3].
- Simper, float32 проти 100-бітного еталону, 48 кГц: дзвін 20 Гц — SVF −111, DF1 −59, DF2T −61 dB;
  нотч 50 Гц — SVF −105, DF2T −33 [4]. Звичайний (нетранспонований) DF2 у порівнянні відсутній.
- ADI: single precision не для вмісту нижче fs/10 і не для високої Q [15]. CMSIS має 64-бітний
  стан саме для полюсів біля одиничного кола [14].
- Трапецієвидний SVF реалізує будь-яку біквадну АЧХ; коефіцієнти петлі (a1..a3) обмежені [0,2]; ~на одну
  операцію дорожчий [5][8]. Формули дзвону/полиць звірені з двома незалежними реалізаціями [6][7].
- Серед оглянутих програвачів нетранспонований DF2 у float є лише в esp-dsp і в нас; PipeWire —
  TDF2 з коефіцієнтами в double [24], CamillaDSP — TDF2 у f64 [25], Chromium — DF1 у double [26].

**Зміна коефіцієнтів.**
- Прямі форми погано переносять модуляцію; SVF BIBO-стійкий при довільній зміні в часі [10].
- Але в SVF підсилення дзвону/полиці сидить у вихідному міксі, тож жорстке перемикання клацає;
  TDF2 при стрибках gain/Q у MUSHRA кращий за SVF [10][11]. Практики згладжують параметри SVF
  ~15 мс [9].

**Денормали.** Після тиші стан IIR сповзає в субнормальні числа і з округленням до найближчого
може там лишатися; лікувати треба петлю зворотного зв'язку [12]. FPU P4 обробляє субнормальні
числа, біта FTZ/DAZ у `fxcr` немає [17]; їх вартість у тактах не задокументована.

**Проєктування коефіцієнтів.** RBJ у float страждає від `1+a1+a2 = O(w0²)`; JUCE/Signalsmith
проєктують у double і округлюють раз [42][43]. Біля Найквіста RBJ «стискає» АЧХ; matched-дизайни
Vicanek це прибирають [21][22]; практичний випадок — [23]. Полиця з S>1 (Q>0.707) дає горб і
провал [16].

**Запас і лімітер.**
- Стандарт індустрії: preamp = −max сумарної АЧХ EQ (−0.1…−0.2 dB) [27][28]; Android LVM
  обмежує гучність на −headroom замість сталого preamp [29]; RME тримає 24 dB внутрішнього запасу,
  гучність після EQ [30]; Roon радить стартувати з −3 dB [31].
- Міжсемплові піки до +3 dB; більшість інтерполяторів ЦАП їх обрізають [32]; PCM5102A про це
  мовчить [34].
- Лімітер без перевищення: ковзний мінімум потрібного підсилення + коробкове згладжування +
  затримка аудіо на ту ж довжину [36]; x42 — lookahead ~1.2 мс і довше утримання для НЧ, щоб
  не «йти за хвилею» [35]. Розгалужений детектор атаки/відпуску спотворює [37].

## 3. Заміри на ПК

Реальний `dsp_chain.cpp` (MSVC `/O2 /fp:precise`), без змін; FMA емульовано в порядку, який GCC
14.2 `-O2` видає для P4 (перевірено objdump). Похибка ядра — проти double-прогону тієї ж
структури з тими ж float-коефіцієнтами. **[код]** = реальний код репо, **[сим]** = симуляція
окремих ядер.

### 3.1. Шум ядра, dB відносно сигналу (гірший з 20 сигналів), 44.1/48/96/192 кГц

| ядро | EQ замовника | EQ замовника, >1 кГц | усі 19 пресетів + крайні |
|---|---|---|---|
| DF2 (зараз) [код] | −51/−49/**−36**/−23 | −51/−49/**−36**/−23 | −39/−37/−24/**−11** |
| TDF2 [сим] | −76/−74/−66/−56 | −120/−119/−110/−99 | −65/−63/−51/−47 |
| DF1 [сим] | −70/−73/−58/−56 | −118/−117/−105/−99 | −60/−62/−55/−47 |
| DF1 + error feedback 2 [сим] | −72/−76/−66/−57 | −117/−117/−106/−100 | −61/−65/−56/−47 |
| **SVF (Simper)** [сим] | **−130/−127/−125/−114** | −138/−137/−136/−134 | **−112/−111/−111/−96** |
| DF2, стан у double [сим] | −139/−138/−138/−138 | — | −132/−132/−130/−130 |

- Шум DF2 не залежить від рівня у відносних dB (40 Гц: −1 і −12 dBFS → ~−38 dB), тобто «їде» на басу.
- Окремий агент, абсолютно: 40 Гц −6 dBFS, 96 кГц — DF2 −45 dBFS, TDF2 −76, SVF −139 [сим].
- 10 смуг по 0 dB: DF2 −50/−38/−27 dB (48/96/192k) — навіть «плоский» увімкнений EQ шумить. SVF
  з FMA тут бітово точний, TDF2 з FMA — −67 dB.
- Ранній результат «TDF2 −154 dB» був артефактом (0 dB смуга → b ≡ a бітово).

### 3.2. Похибка АЧХ від коефіцієнтів, dB (сітка 20 Гц–20 кГц) [код для RBJ-float]

| смуга | fs | RBJ у float (зараз) | RBJ double→float | SVF у float |
|---|---|---|---|---|
| LS 40 Гц +2.6 (замовник) | 96k | 0.34 | 0.02 | 0.00002 |
| LS 40 Гц +2.6 | 192k | 0.82 | 0.10 | 0.0001 |
| усі пресети, гірше | 192k | 0.82 | 0.46 | 0.0003 |
| PK 20 Гц +12 Q8 (край UI) | 192k | 6.09 | 6.09 | 0.005 |

Це лінійна помилка тембру, не хрип. Проєктування в double допомагає лише частково; межа —
зберігання `a1≈−2`, `a2≈1` у float будь-якою прямою формою.

### 3.3. Ціна на P4 (riscv32-esp-elf-g++ 14.2 `-O2`, інструкцій на семпл у циклі)

| ядро | fp-contract=fast | 48k | 96k | 192k |
|---|---|---|---|---|
| DF2 (зараз, 5545 такт/блок виміряно на панелі) | 12 | 6.2 % | 12.3 % | 24.6 % |
| TDF2 | 10–11 | 5.6 % | 11.3 % | 22.6 % |
| SVF | 15 (дзвін 12–13) | 7.7 % | 15.4 % | 30.8 % |
| SVF стерео в одному циклі | 13 / канал | ~4 % | 7–8 % | 14–17 % |
| DF2 з double-станом | 130 + 11 soft-float викликів | — | — | — |

Відсотки — екстраполяція з 1.93 такт/інстр. DF2; стерео-рядок — модель in-order ядра з латентністю
FP ≈4 (оцінка, TRM латентностей не дає [17]). **Треба міряти на панелі.** Агенти розходяться
в оцінці моно-SVF: від +10 % до ×1.7 від нинішньої смуги — залежить від латентності FMA.

### 3.4. Лімітер, 50 Гц стаціонарно, THD+N dB (k = семплів/с у коліні soft_clip) [код для «наш»]

| перевищення порогу −1 dBFS | +1 | +3 | +6 | +9 |
|---|---|---|---|---|
| наш | −58 k8300 | −49 k11700 | −44 k14500 | −41 k16400 |
| lookahead 1.5 мс, release 250 мс | −61 k0 | −52 | −48 | −46 k0 |
| lookahead 1.5 мс + hold 20 мс | −151 | −149 | −150 | −149 k0 |

- Наш на сплесках +6/+9 dB притискає 344–534 семпли/с до 1.0 (жорсткий кліп).
- За частотою при +6 dB (Python-транскрипція нашого циклу): 20 Гц −36, 50 Гц −44, 1 кГц −64 dB.
  Hold 40 мс без lookahead знімає стаціонарне спотворення, атака лишається [сим].
- Auto pre-gain = −max бусту повертає до 0/+1 dB над порогом: −150/−58 dB навіть із нашим лімітером.

### 3.5. Інше

- Денормали: 30 с тиші після 50 Гц, 96 кГц — усі 4 структури тримають субнормальний стан у
  94–100 % семплів; скидання на кінці блоку дає 0 за 0.18–0.27 с [сим].
- Перехід коефіцієнтів (дзвін 62 Гц +1.8→+6 dB, 48k, пік відхилення): 1 блок — DF2 −41, SVF −41,
  TDF2 −22 dBFS; 8 блоків — SVF −55 [сим]. Інший агент при **жорсткому** перемиканні без фейду:
  SVF −43, TDF2 −80 dBFS. Висновок: SVF — лише з кросфейдом/рампою (у нас кросфейд є).
- Переліт полиці над заданим gain (аналоговий прототип): +12 dB Q2 → +4.6, Q8 → +15.6 dB [сим];
  графік UI обрізаний ±12 dB — користувач цього не бачить.

## 4. Що можна реалізувати в проєкті

| P | пункт | що | навіщо нам | де в коді | CPU на P4 | зусилля | ризик |
|---|---|---|---|---|---|---|---|
| P0 | Діагностика на стенді | лог `limiter -X dB`, % гучності, PVDD, CLIP LED; +2 лічильники (soft_clip, clamp у `f32_to_i32`) і мінімум `g_lim` за вікно (зараз лог — миттєве значення, §6.1) | розвести DF2 / лімітер / підсилювач | `audio_module.cpp:572,1859,154`; `dsp_chain.cpp:376-385` | ~0 | S | немає |
| P0 | SVF замість DF2 | ядро Simper, та сама АЧХ RBJ; також `side_hp_` | прибирає шум −36→−125 dB і похибку АЧХ | `dsp_chain.cpp:34-101,156-178,282-326`; `dsp_chain.h:70-78,165` | моно +30 % (модель іншого агента — до ×1.7); зі стерео-циклом ≤ нинішнього — виміряти | M | тест esp-dsp-еквівалентності ламається навмисно |
| P0 | Автозапас під EQ | `B = max(0, max|H_run|)` → стеля гучності або pre-gain | тримає EQ-буст поза лімітером | `set_bands/set_eq_enabled/set_width/set_sample_rate`, цикл гучності `:346-355` | 0/семпл; ~35k тактів на правку | M | ~4–6 dB менше максимуму гучності — рішення замовника |
| P0 | Hold у лімітері | утримання цілі 40 мс перед one-pole | знімає модуляцію басу, нуль затримки | `dsp_chain.cpp:372-386` | +4–6 інстр/кадр | S | довше «присідання» після піку (450 проти 300 мс) |
| P1 | TDF2 як проміжний A/B | тіло `biquad_f32` → TDF2 | швидко підтвердити діагноз на панелі | `dsp_chain.cpp:34-46` | ≤ нинішнього | S | гірші переходи на повзунку; не кінцеве |
| P1 | Скидання денормалів | `|s|<1e-15 → 0` в кінці блоку | ядро не працює на субнормалах у тиші | `run_eq`, `run_stereo:318` | ~0.02 інстр/семпл | S | немає |
| P1 | Кросфейд у часі | 20–40 мс замість 240 кадрів | 240 кадрів = 1.25 мс на 192k | `dsp_chain.cpp:246-270`; `dsp_chain.h:162-168` | лише під час фейду | S | правило «правка всередині фейду» |
| P1 | Стерео в одному циклі, in place | L і R одного біквада в одному циклі, без memcpy | оплачує SVF на in-order ядрі | `run_eq :231-275` | −40…50 % тактів (модель) | S | виміряти |
| P1 | Хост-тести | шум проти double на 44.1–192k, SVF≡RBJ, денормали | нинішні тести цього не бачать | `tests/host/test_dsp_chain.cpp:100-125` | — | S | MSVC без FMA → `std::fmaf` |
| P1 | Q полиці ≤ 1 (або S ≤ 1) | обмежити Q/показати S | прихований буст до +15.6 dB | `screen_eq.cpp:507-508,364`; `eq_config.cpp` | 0 | S | міграція старих `eq.json` |
| P1 | Lookahead-лімітер 1.5 мс | max → hold → release → 2 коробки, затримка аудіо | вихід ніколи > порогу, soft_clip інертний | `dsp_chain.cpp:357-389` | ~78 інстр/кадр активно, ~10 у простої | M | +1.5 мс затримки |
| P1 | Затримка лімітера в моделі конвеєра | `+L` у `set_consumer`, повтор у `retune()` | чесна латентність для AirPlay | `audio_module.cpp:331,714-723` | 0 | S | низький |
| P1 | Стеля аналогу з `board.json` | `out_ceiling_db` у множнику `f32_to_i32` | лімітер захищає реальну межу підсилювача | `audio_module.cpp:142,154-161` | 0 | S | повна прошивка + бекап user data |
| P2 | Крива UI зі SVF | замкнена форма |H| з {g,k,m}; дизайн смуги раз на прохід (зараз 97 точок × 10 = 970 викликів `design_band`) | крива = те, що звучить | `dsp_chain.cpp:107-128` | лише UI | S | немає |
| P2 | CLIP_OTW TPA3255 | лічильник PCNT або на сателіті | фіксувати кліп підсилювача | CONCEPT §4, §5 | 0 | M | пін — рішення замовника |
| P2 | Двоконтурний hold | НЧ-контур 40 мс + швидкий 2 мс | коротке «присідання» на ВЧ-піках | блок лімітера | +10–12 інстр/кадр | M | більше параметрів |
| P2 | Тонкомпенсація за гучністю | 2 полиці, gain ∝ (ref − vol) | замість статичного «Тонкомп» | `set_volume_percent`, `stage_band` | +2 смуги | M | потрібна калібровка ref |
| P2 | Matched-дизайн <88.2k | Vicanek для дзвону й полиць S=1 | EQ не звучить інакше на 44.1 і 96k | `dsp_chain.cpp:58-82`; `fit_presets.py` | 0/семпл | M | перепідгонка пресетів |
| P2 | soft_clip: окреме коліно | `SOFT_CLIP_KNEE` ≠ `LIMIT_THR` | поріг лімітера можна рухати | `dsp_chain.h:35,50-58` | 0 | S | немає |
| P3 | Пропуск смуг 0 dB | `gain==0` → off | −шум, −CPU | `set_bands :184` | економія | S | немає |
| P3 | Повільніша рампа гучності на великих стрибках | 5–20 мс для mute/старту | клац на старті −57→−71 dB [сим] | `dsp_chain.h:101` | 0 | S | лінива ручка |
| P3 | Коментар про esp-dsp | виправити «happens to keep» | не повертатись на DF2 esp-dsp | `dsp_chain.cpp:14-33` | 0 | S | немає |
| P3 | Тест міжсемплових overs PCM5102A | fs/4 під 45°, 0 і −3.5 dBFS | чи треба ще 1–3 dB запасу | стенд | 0 | S | немає |

### 4.1. P0 — ядро SVF (Simper [5][6])

Дизайн (float достатньо, §3.2), `A = 10^(dB/40)`, `g = tanf(pi*f/fs)`, `k = 1/Q`:

```
PEAK:       k = 1/(Q*A);  m = (1, k*(A*A-1), 0)
LOW_SHELF:  g /= sqrtf(A); m = (1, k*(A-1), A*A-1)
HIGH_SHELF: g *= sqrtf(A); m = (A*A, k*(1-A)*A, 1-A*A)
LOW_PASS:   m = (0, 0, 1)   HIGH_PASS: m = (1, -k, -1)   NOTCH: m = (1, -k, 0)
a1 = 1/(1 + g*(g+k)); a2 = g*a1; a3 = g*a2
coef[6] = {a1, a2, a3, m0, m1, m2}      // замість esp-dsp {b0 b1 b2 a1 a2}
```

На семпл, стан `{ic1, ic2}` (ті ж `w[2]`):

```
v3 = x - ic2;  v1 = a1*ic1 + a2*v3;  v2 = ic2 + a2*ic1 + a3*v3;
ic1 = 2*v1 - ic1;  ic2 = 2*v2 - ic2;  y = m0*x + m1*v1 + m2*v2;
```

- **Не** згортати `ic2 + a3*(x-ic2)` у `(1-a3)*ic2 + a3*x`: на низькій f0 `a3≈1e-5`, і `1-a3` у
  float знищує точність.
- У DspChain: `Biquad::coef[5]→[6]`, `memcmp/memcpy` у `stage_band` і `old_coef_[..][6]`;
  `reset()`, старт із тиші й кросфейд (стара схема на копії стану) — без змін. `side_hp_` — те саме
  з `m = (1,-k,-1)`, `k = 1/0.7071`.
- Крива UI (P2, але робити разом): `|H|²` з `s = sin(w/2)`, `c = cos(w/2)`,
  `dr = g²c² − s²`, `di = g·k·s·c`, `nr = m0·dr + m2·g²c²`, `ni = m0·di + m1·g·s·c`.
- Хост-тести: реальна DspChain з EQ замовника, синус 40 Гц −6 dBFS, лімітер поза дією; залишок
  проти double-прогону (тон видалити МНК). Пороги: **≤ −110 dB** відн. на 44.1/48/96k,
  **≤ −105 dB** на 192k; усі пресети **≤ −90 dB** на кожній частоті (виміряно −114 і −96, §3.1).
  SVF≡RBJ у double, випадковий вхід, 6 типів: `max|diff| < 1e-9`. Похибка АЧХ < 0.02 dB (виміряно до
  0.009 dB на PK 20 Гц +12 Q8, 96k — поріг 0.01 був би впритул).
  Ядро писати з `std::fmaf`, щоб хост і P4 округлювали однаково.
- На 192k моно-SVF — ~31 % ядра лише на EQ за стендом (до ~40 % за іншою моделлю), тож SVF
  робити разом зі стерео-циклом (P1) і міряти такти до злиття.
- Розбіжність агентів: один поставив P0 саме TDF2 (дешевше, ВЧ-шум −110 dB, тобто хрип зникає), а
  SVF — P2. Тут P0 — SVF, бо TDF2 лишає −66/−56 dB НЧ-шуму на 96/192k і ту саму похибку АЧХ
  (§3.1–3.2); TDF2 — перший крок для A/B, не альтернатива.
- Майбутній кросовер (CONCEPT §7а: LR24 = два баттерворти, саб у 2.1 з LP ~80 Гц) має ту саму
  проблему низької f0 — він має йти на тому ж ядрі SVF (LP/HP з однієї структури).
- Проміжний варіант для швидкого A/B на панелі (P1), шар esp-dsp `{b0 b1 b2 a1 a2}`, стан `{s1,s2}`:

```
y  = b0*x + s1;
s1 = (b1*x + s2) - a1*y;
s2 = b2*x - a2*y;
```

### 4.2. P0 — автозапас під EQ

`B = max(0, trim_db + M + 20·log10(max(1, width)))`, де `M` — максимум |H| **робочих**
коефіцієнтів на ~48 лог-точках 10 Гц…0.49·fs + f0 кожної смуги + DC/Найквіст у замкненій формі.
Причина: висока полиця замовника доходить до +4.2 dB лише на Найквісті; сітка UI 20 Гц–20 кГц
дає +3.59 (96k) і +3.40 (192k) — занижує. Запас `margin = 1.0–1.5 dB`: навіть із
preamp = −max|H| піки семплів +0.3…+1.4 dB, true peak до +2.5 dB на «цеглинних» мастерах [сим].
Два способи — **рішення замовника**:
- стеля гучності (як Android LVM [29]): `vol = min(vol_target, 10^(−(B+margin)/20))`, окремий
  one-pole ~20 мс; нижче стелі нічого не міняється, A/B EQ on/off без зсуву рівня;
- сталий pre-gain (AutoEq/APO/PipeWire [24][27]): EQ-on завжди на ~4 dB тихіший.
Для замовника: ~5.7 dB зверху шкали. Тест: `B ≥ max(щільна сітка) − 0.05 dB` для 19 пресетів на
4 частотах; плоский EQ бітово точний.

### 4.3. P0 — hold у лімітері (без затримки)

```
if (t <= ht)            { ht = t; cnt = H; }
else if (t <= ht*TOL)   { cnt = H; }          // TOL = 10^(0.5/20)
else if (cnt > 0)       { --cnt; }
else                    { ht = t; }
// далі g_lim іде за ht, як зараз (ATK 1.5 мс, REL 250 мс)
```

`H = 0.040·fs` у `set_sample_rate`, `ht = 1, cnt = 0` у `reset`. Без `TOL` утримання не
оновлювалось на піках, що відрізняються на ulp (−58 dB замість «підлоги»). Атаки лишаються —
їх закриває lookahead (P1). Тест: 20/50/100 Гц при +6 dB над порогом, стаціонарно (після 0.5 с),
THD+N ≤ −90 dB.

### 4.4. P1 — нотатки

- **Lookahead-лімітер:** `L = round(1.5e-3·fs)` (66/72/144/288), ковзний max по `L+1` (монотонна
  дека) → hold 40 мс (дециматорний мінімум по чанках ~0.33 мс) → release як «зазор» `e = held − r`
  → дві коробки `0.5822:0.4178` [36] → `y = x[n−L]·g`. Гарантія `|y| ≤ T`. У float32: суми коробок
  перераховувати точно раз на блок (без цього дрейф 0.65 dB за 30 хв [сим]); release через зазор,
  інакше one-pole «застрягає» не дійшовши до 1. Тест: 0 семплів > T за 30 хв на 96k.
- **Денормали:** після кожної смуги `if (fabsf(s0)<1e-15f && fabsf(s1)<1e-15f) s0=s1=0;` —
  не DC-зсув (його знімають HP), не шум (ламає бітовий нуль) [12].
- **Кросфейд у часі:** лічильник на смугу `L = 0.02–0.04·fs`; нова правка під час фейду —
  фейд від того, що зараз чути. Альтернатива без другого фільтра — лінійна рампа всіх 6 параметрів
  SVF на семпл + one-pole «ковзання» f/dB/Q ~15 мс [9]; за замірами на ~5–10 dB гірша за
  двофільтровий фейд, але дешевша й без `old_out_`.

## 5. Що не підходить і чому

| ідея | чому ні |
|---|---|
| Біквади в double (EarLevel, ADI, CMSIS 32×64, CamillaDSP f64) | FPU P4 — лише single [17][18]; double — soft-float, DF2 у double ≈130 інстр. + 11 викликів/семпл (§3.3) |
| esp-dsp `dsps_biquad_f32/sf32`, генератори `dsps_biquad_gen_*` | той самий DF2; P4-asm на `esp.lp.setup` порушує правила v1.3 [20]; генератори у float, notch із gain дає половину dB [19] |
| esp_audio_effects `esp_ae_eq`/DRC, ESP-ADF equalizer | ≥1.4.0 вимагає P4 rev 3.0 (у нас v1.3) [41]; equalizer — до 48 кГц, закритий [S6] |
| Error feedback Dattorro у float | у float нема «безкоштовного» молодшого слова; виміряно ≤5 dB виграшу при ×2 ціні (§3.1) |
| Фіксована кома DF1 з 64-біт акумулятором | на RV32IM ~30 інстр./семпл, і все одно треба «double»-варіант для низьких f0 |
| TDF2/DF1 як кінцеве рішення | на 96/192k −56…−66 dB шуму і та сама похибка АЧХ; лише як проміжний A/B |
| Chamberlin SVF, решітка Regalia–Mitra, coupled form | не білінійні / гірші за SVF у замірах [4] і [сим] |
| Дитер на виході | шум PCM5102A ≈ −112 dBFS [34], LSB int32 −186 dBFS; наша похибка — у DF2, дитер її не торкається [25] |
| True-peak 4× у лімітері (BS.1770) | ~288 MAC/кадр ≈ 3× весь EQ; на 96–192k не потрібен [38]; стеля −1 dBFS тримала < 0 dBTP [сим] |
| Довгий lookahead 5–20 мс | 20 мс на 192k ≈ 45 КБ внутрішньої RAM + 20 мс затримки; 1.0–1.5 мс дали ті ж результати |
| Кубічний soft clip CamillaDSP | нелінійний на всіх рівнях [25]: −40 dB THD уже на −6 dBFS (розрахунок) |
| Мультирейт / оверсемплінг EQ | затримка й ×2–4 CPU заради того, що SVF робить на повній частоті |
| Копіювати код fil4, CamillaDSP, LSP, Rockbox | GPL/LGPL/MPL несумісні з PolyForm Noncommercial; реалізувати з формул. MIT/BSD/Apache (Signalsmith, PipeWire, Chromium, CMSIS) — з атрибуцією |

## 6. Відкриті питання / що перевірити на стенді

1. При якій гучності (%) і з якого джерела чути хрип? Чи є в лозі `limiter -X dB` у цей момент?
   Нижче ~90 % лімітер з поточним EQ не діє → причина в DF2 або в аналозі. Увага: рядок — миттєвий
   `limiter_gain()` у кінці 0.5-с вікна (`audio_module.cpp:1861-1862`), не мінімум за вікно; короткий
   пік за 250 мс release встигає відпуститись нижче порогу 0.2 dB, а soft_clip не логується зовсім.
   Відсутність рядків доводить лише відсутність сталого лімітування — тому P0 і додає лічильники.
2. PVDD плати TPA3255, чи є вхідний каскад / регулятор / CLIP LED? При ≤36 В підсилювач обрізає
   раніше за 0 dBFS [33]. Не вгадувати (CLAUDE.md, правило 2).
3. Чи ввімкнено mono-bass (`audio.mono_bass_hz`)? Його DF2-HP — найшумніший окремий фільтр:
   THD+N −36 dB на 96k [сим].
4. Такти на панелі (метод 25.09, найкращий із 200 × 240 кадрів): SVF моно/стерео, TDF2, новий
   лімітер; 240 `fmadd.s` із субнормальними операндами проти нормальних.
5. Чи звичні 192k-джерела (DLNA hi-res)? Там DF2 дає −23 dB — терміновість SVF ще вища.
6. Автозапас: стеля гучності чи сталий pre-gain; margin 1.0 чи 1.5 dB; сканувати до Найквіста.
7. Полиці: обмежити Q ≤ 1 чи показати S; що робити зі збереженими Q>1.
8. Чи має EQ звучати однаково на 44.1 і 96k (matched-дизайни, перепідгонка пресетів)?
   Зараз ВЧ-крива EQ замовника різниться ~0.9 dB (44.1 проти 96k), «Більше верхів» — ~1.6 dB
   (48 проти 192k) [сим].
9. Міжсемплові overs PCM5102A: fs/4 під 45° на 0 і −3.5 dBFS, FFT на виході ЦАП.
10. Сабсонік ~35–40 Гц при бусті басу? Denon SC-G101 заявлено від 68 Гц [S5, не перевірено].
11. Розбіжність агентів про SVF при зміні параметрів: із кросфейдом SVF найкращий, при жорсткому
    перемиканні — гірший за TDF2 (§3.5). Кросфейд лишаємо; перевірити на слух тягання повзунка.

## 7. Джерела

Прочитані агентами (крім позначених «S» — лише пошуковий фрагмент).

1. EarLevel, Biquads — https://www.earlevel.com/main/2003/02/28/biquads/ ; Biquad C++ source — https://www.earlevel.com/main/2012/11/26/biquad-c-source-code/
2. EarLevel, Floating point caveats — https://www.earlevel.com/main/2019/04/19/floating-point-caveats/
3. J. Dattorro, The Implementation of Recursive Digital Filters for High-Fidelity Audio, JAES 1988 — https://ccrma.stanford.edu/~dattorro/HiFi.pdf
4. A. Simper, SVF vs DF1 with 32-bit float — https://cytomic.com/files/dsp/SVF-vs-DF1.pdf
5. A. Simper, SvfLinearTrapOptimised2 — https://cytomic.com/files/dsp/SvfLinearTrapOptimised2.pdf ; v1 — https://www.cytomic.com/files/dsp/SvfLinearTrapOptimised.pdf
6. hollance, Cytomic SVF in C++ — https://gist.github.com/hollance/2891d89c57adc71d9560bcf0e1e55c4b
7. chowdsp_utils StateVariableFilter — https://github.com/Chowdhury-DSP/chowdsp_utils
8. KVR, Biquad vs SVF — https://www.kvraudio.com/forum/viewtopic.php?t=600410 ; double precision — https://www.kvraudio.com/forum/viewtopic.php?p=4836443
9. KVR, SVF modulation clicking — https://www.kvraudio.com/forum/viewtopic.php?t=511314 ; interpolating coefficients — https://www.kvraudio.com/forum/viewtopic.php?t=557579
10. A. Wishnick, Time-Varying Filters for Musical Applications, DAFx-14 — https://www.dafx14.fau.de/papers/dafx14_aaron_wishnick_time_varying_filters_for_.pdf
11. V. Zavalishin, The Art of VA Filter Design 2.1.2 — https://www.discodsp.net/VAFilterDesign_2.1.2.pdf
12. L. de Soras, Denormal numbers in floating point signal processing — http://ldesoras.free.fr/doc/articles/denormal-en.pdf
13. CMSIS-DSP BiquadCascadeDF2T — https://arm-software.github.io/CMSIS-DSP/latest/group__BiquadCascadeDF2T.html
14. CMSIS-DSP DF1 32x64 — https://arm-software.github.io/CMSIS-DSP/latest/group__BiquadCascadeDF1__32x64.html
15. Analog Devices wiki, SigmaStudio Filters — https://wiki.analog.com/resources/tools-software/sigmastudio/toolbox/filters
16. RBJ Audio EQ Cookbook (W3C) — https://www.w3.org/TR/audio-eq-cookbook/
17. ESP32-P4 Technical Reference Manual — https://www.espressif.com/sites/default/files/documentation/esp32-p4_technical_reference_manual_en.pdf
18. Espressif, Floating-Point Units on Espressif SoCs — https://developer.espressif.com/blog/2025/10/cores_with_fpu/
19. esp-dsp 1.8.2, modules/iir/biquad — https://github.com/espressif/esp-dsp/tree/master/modules/iir/biquad
20. esp-idf#19025 (P4 HWLOOP) — https://github.com/espressif/esp-idf/issues/19025
21. M. Vicanek, Matched Second Order Digital Filters — https://vicanek.de/articles/BiquadFits.pdf
22. M. Vicanek, Matched Two-Pole Digital Shelving Filters — https://vicanek.de/articles/2poleShelvingFits.pdf
23. dusk-audio-plugins #289 — https://github.com/dusk-audio/dusk-audio-plugins/issues/289
24. PipeWire filter-graph — https://gitlab.freedesktop.org/pipewire/pipewire/-/tree/master/spa/plugins/filter-graph
25. CamillaDSP README і src — https://github.com/HEnquist/camilladsp
26. Chromium biquad.cc — https://raw.githubusercontent.com/chromium/chromium/main/third_party/blink/renderer/platform/audio/biquad.cc
27. AutoEq — https://github.com/jaakkopasanen/AutoEq/tree/master/autoeq
28. EasyEffects #1592 — https://github.com/wwmm/easyeffects/issues/1592
29. AOSP LVM_Control.cpp — https://android.googlesource.com/platform/frameworks/av/+/refs/heads/main/media/libeffects/lvm/lib/Bundle/src/LVM_Control.cpp
30. RME ADI-2 DAC User's Guide — https://www.rme-audio.de/downloads/adi2dac_e.pdf
31. Roon KB, DSP Engine Headroom Management — https://help.roonlabs.com/portal/en/kb/articles/dsp-engine-headroom-management
32. Benchmark, Intersample Overs in CD Recordings — https://benchmarkmedia.com/blogs/application_notes/intersample-overs-in-cd-recordings
33. TI TPA3255 datasheet — https://www.ti.com/lit/ds/symlink/tpa3255.pdf
34. TI PCM5102A datasheet — https://www.ti.com/lit/ds/symlink/pcm5102a.pdf
35. x42 dpl.lv2 — https://github.com/x42/dpl.lv2
36. Signalsmith, Designing a straightforward limiter — https://signalsmith-audio.co.uk/writing/2022/limiter/ ; basics/limiter.h — https://github.com/Signalsmith-Audio/basics
37. Giannoulis, Massberg, Reiss, Digital Dynamic Range Compressor Design, JAES 2012 — https://web.archive.org/web/2019id_/http://www.eecs.qmul.ac.uk/~josh/documents/2012/GiannoulisMassbergReiss-dynamicrangecompression-JAES2012.pdf
38. ITU-R BS.1770-5 — https://www.itu.int/dms_pubrec/itu-r/rec/bs/R-REC-BS.1770-5-202311-I!!PDF-E.pdf
39. ISO 226:2003 таблиці — https://gist.github.com/chummersone/8499aa1dfe6955bd437c3a475cd17763
40. Rockbox lib/rbcodec/dsp — https://github.com/Rockbox/rockbox/tree/master/lib/rbcodec/dsp
41. esp_audio_effects (esp-adf-libs) — https://github.com/espressif/esp-adf-libs/tree/master/esp_audio_effects
42. JUCE juce_IIRFilter.cpp — https://raw.githubusercontent.com/juce-framework/JUCE/master/modules/juce_audio_basics/utilities/juce_IIRFilter.cpp
43. Signalsmith dsp filters.h — https://github.com/Signalsmith-Audio/dsp/blob/main/filters.h

Лише пошукові фрагменти:

- S1. ADI EngineerZone, single vs double precision filters — https://ez.analog.com/dsp/sigmadsp/w/documents/5196/when-should-i-use-single-precision-or-double-precision-filters
- S2. Kauraniemi et al., Delta operator realizations, IEEE TCAS-II 1998 — https://www.semanticscholar.org/paper/Delta-operator-realizations-of-direct-form-IIR-Kauraniemi-Laakso/2ac625830cbd5da7f17287680a39213227535cd5
- S3. S. Orfanidis, Digital Parametric Equalizer Design with Prescribed Nyquist-Frequency Gain, JAES 1997 — https://www.aes.org/e-lib/download.cfm?ID=7854 (реалізовано за переказом у [21])
- S4. U. Zölzer, Roundoff Error Analysis of Digital Filters, JAES 1994 — https://www.aes.org/e-lib/browse.cfm?elib=5574
- S5. Denon SC-G101 specifications — https://www.hifi-guide.com/loudspeaker/denon-sc-g101/
- S6. ESP-ADF equalizer (сторінка 404; дані з фрагментів і коду squeezelite-esp32) — https://docs.espressif.com/projects/esp-adf/en/latest/api-reference/audio-processing/equalizer.html
