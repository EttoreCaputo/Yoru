# Yoru — roadmap

Yoru reuses what the [DualEye PC Monitor](../../esp32s3-dualeye-pcmonitor) already has working:
- on the board: the voice front end, the encrypted USB/WiFi link and the pet's mood;
- on the computer: the Rust host with local or API models, the flasher and the app shell.

What is new is the character, the UI and the hardware. The parts the two projects share will later move to a repository of their own. Until then they are copied, and fixes go to both.

## Hardware

Waveshare ESP32-S3-Touch-AMOLED-1.75C only, for now. DualEye support may follow.

| Part | Detail |
|------|--------|
| SoC | ESP32-S3R8, 8 MB octal PSRAM, native USB (303A:1001) |
| Flash | 32 MB (GigaDevice, read on the first unit; the schematic says 16 MB). The layout still uses 16 MB |
| Display | CO5300 AMOLED 466 × 466, QSPI: CS 12, CLK 38, D0–D3 4/5/6/7, RST 1, TE 13. Column offset 6. Areas must be even-aligned. Brightness via DCS `0x51` |
| Touch | CST9217 at I2C 0x5A, INT 11, RST 2 |
| I2C (shared) | SDA 15, SCL 14: codecs, touch, PMU (AXP2101 0x34), IMU (QMI8658 0x6B) |
| Audio | ES8311 (0x18) speaker with NS4150B PA on GPIO 46; ES7210 (0x40) with 2 mics and a speaker loopback on MIC3. I2S: MCLK 16, BCLK 9, WS 45, DOUT 8, DIN 10. TDM slots: MIC1, MIC3 (reference), MIC2, MIC4 |
| Buttons | BOOT on GPIO 0, PWR via AXP2101 (also readable on GPIO 3) |

Sources: Waveshare's BSP `waveshare/esp32_s3_touch_amoled_1_75c`, xiaozhi-esp32, the schematic, and Meta's muse-gadget-sdk board file (Apache-2.0).

## Reuse from DualEye

| Area | Modules | How |
|------|---------|-----|
| Voice on the board | `voice.c`, `board_audio.c`, `playback.c`, `sounds.c`, `ears.c`, `audio_selftest.c` | Copy; change the pins and share the I2C bus |
| Link | `link*.c`, `net.c`, `rpc.c`, `board_settings.c` | Copy; new protocol name and `hello` |
| Mood | `pet.c` | Copy; feed the eye engine instead of `ui_eyes.c` |
| PC data | `metrics_*` | Copy; it drives moods, not faces |
| Host | `dualeye-core` (link, flasher, firmware, wifi, stt, tts, llm, agent, voice, models, mcp, claude, timers, intents, diary, quips, sensors, snapshot) | Copy as `yoru-core` |
| App | settings, onboarding, i18n, updates, title bar | Copy; replace the board view with Yoru's eyes |
| New | display/touch/IMU/PMU drivers, the eye engine, behaviours | Write |

## Milestones

### M1: bring-up (this commit)
- ESP-IDF 6.1 project: CO5300 over QSPI, LVGL 9.3 with even-aligned flushes, CST9217 touch as an LVGL input.
- The eye engine (`eyes.c`): parameters, eased moods, blink, saccades, squash, overlays.
- A demo loop: it looks around and blinks; a tap makes it happy, three taps make it grumpy, a long press puts it to sleep.
- Audio codecs up on the shared I2C bus, with a boot chirp.

To check on the hardware: rendering time per frame (`!lvgl stats`), and the flash size (`esptool flash_id`).

### M2: settings menu and senses

**Settings menu on the board.** It is a round menu, made for the circle, that opens without the app.
- **Opening and closing.** Swipe down from the top edge to open it, swipe up or wait a few seconds to close it. Yoru peeks at the menu while it is open, eyes small and up at the edge. The gesture must not clash with poking and stroking the eyes.
- **Layout.** Items sit around the rim or in a vertical round list. Each one has an icon and a big arc or slider that the finger drags along the circle. Changes apply straight away.
- **Items:**
  - Display:
    - brightness;
    - night brightness and hours;
    - auto-dim when idle;
    - rotation.
  - Sound:
    - volume;
    - mute the pet's sounds;
    - mute the microphone, a privacy switch that Yoru shows with closed "ears".
  - Behaviour:
    - how soon it gets sleepy and falls asleep;
    - do not disturb or focus mode (no sounds, no whims);
    - touch and motion reactions on or off.
  - Look: eye colour and eye style (more styles unlock later, M5).
  - Voice: wake word on or off; which wake word (M3).
  - Connection:
    - USB or WiFi status;
    - WiFi network;
    - pair a computer (M3).
  - Battery: level, charging, power off (from the AXP2101).
  - About:
    - firmware version;
    - free memory and flash;
    - LVGL load;
    - restart;
    - factory reset, confirmed with a long press.
- **Storage and sync.** Settings are kept in NVS, as `board_settings.c` does in DualEye. Once the link exists (M3/M4), the app shows and changes the same values, and the two stay in sync both ways.
- **Language.** Labels in Italian and English, picked in the menu and from the app.

**Senses**
- IMU (QMI8658): tilt makes the gaze compensate, shaking makes it dizzy, lifting wakes it.
- Sound direction from the two mics, so the gaze turns to the noise.
- `ears.c` for bangs, claps and chatter.
- AXP2101: battery, charging, PWR key, power-off; sleep with the panel and touch in standby.

### M3: voice and link
- Port the ESP-SR front end with AEC (reference on slot 1), the wake word and playback.
- Port the link (USB and WiFi), `rpc`, and `hello` with `board: "yoru-amoled-1.75c"`.
- Listening, thinking and speaking states in the eye engine.

### M4: host and app
- `yoru-core`, `yoru-cli` and `yoru-app` from DualEye's host. One screen, no faces: the app shows Yoru's eyes live, plus mood, settings and the voice set-up.
- PC data and Claude Code usage drive moods.
- MCP tools: react, say, show a message.

### M5: character
- Behaviour scheduler: boredom, sighs, stretches, sleep at night, greeting on return, late-night notice.
- Non-verbal sound language (chirps, sighs, trills) for the pet.
- Progression: experience from real activity unlocks eye styles and rare animations.
- Music: bob to the beat.

### M6: hackability and integrations
- Moods and eye styles as data (JSON) that can be loaded from the app.
- Skills for devices in the house, run by the host's agent and confirmed by the user.
- Custom wake word "Yoru" (microWakeWord).
- Simulator: the eye engine compiled for the desktop (SDL), so animations can be made without flashing.
