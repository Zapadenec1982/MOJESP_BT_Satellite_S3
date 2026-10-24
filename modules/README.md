# modules/

Product modules (one ESP-IDF component per module, `manifest.json` + a C++ `BaseModule`).
The list of active modules is `project.json` → `modules`; `tools/generate_ui.py` turns it
into `generated/modules.cmake`, includes, instances and registration code.

Planned for this product (see `docs/CONCEPT.md` §9 and `docs/AGENT_PROMPT.md`):

- `ui_lvgl` — LVGL UI on the MIPI-DSI panel, touch + encoder (stage 2)
- audio core — I2S full-duplex, DSP, source switching (CONCEPT.md phase "аудіоядро")
- radio / slimproto — network sources (CONCEPT.md phase "мережа")
