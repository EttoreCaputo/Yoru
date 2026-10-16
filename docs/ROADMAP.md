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
| Voice on the board | `voice.c`, `board_audio.c`, `playback.c`, `sounds.c`, `ears.c`, `audio_selftest.c` | Copied in 0.3.0; `ui_voice.c` hands the voice state to `behaviour.c` |
| Link | `link*.c`, `net.c`, `rpc.c`, `board_tools.c` | Copied in 0.3.0, with Yoru's names on WiFi and `hello`, and tools for one screen |
| Settings | `board_settings.c` | Copied in 0.2.0, rewritten as a table for the round menu |
| Mood | `pet.c`, `ears.c`, `metrics_model.c` | Copied in 0.2.0: `pet.c` hands its scenes to `behaviour.c` instead of playing them on `ui_eyes.c`; `metrics_model` without the faces |
| PC data | `metrics_*` | Copied in 0.3.0; it drives moods, not faces |
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

### M2: settings menu and senses (in since 0.5.0)

In 0.5.0: the menu (`menu.c`), motion (`motion.c`), the PMU (`board_pmu.c`), the sound's direction (`ears_dir.c`), and new eye moods and gestures for them (dizzy, scared, peek; hold, shoo, flail; knock and lean). Left for later: the Look items (name, colour, eye shape: M5, when there is more than one), pairing a computer from the menu (the host does it over USB), deep sleep with wake-on-motion on battery, and checking on the board which way the IMU's x and y and the two mics lie (`SCREEN_X`/`SCREEN_Y` in `motion.c`, `MIC_A_SIDE` in `ears_dir.c`).


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
  - Look: name, colour, eye shape and finish (more unlock later, M5).
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
- **Storage and sync.** Settings are kept in NVS by `board_settings.c` (in since 0.2.0, as a table the menu walks: name, range, default). Once the link exists (M3/M4), the app shows and changes the same values, and the two stay in sync both ways.
- **Language.** Labels in Italian and English, picked in the menu and from the app.

**Mood (in since 0.2.0)**
- `pet.c` from DualEye: energy, happiness and affection, kept in NVS. It picks the resting eyes, the whims and how often they come; pokes and strokes move it. `behaviour.c` turns its scenes into eye moods.
- New eye moods for it: sad, loving (with hearts), surprised, bored.

**Motion (QMI8658)**

The accelerometer and gyroscope turn the whole case into an input: Yoru lives in a round badge that is picked up, turned and put down, so how it moves says a lot. A `motion.c` reads the IMU (0x6B on the shared I2C, INT1 on GPIO 21), recognises a few gestures and hands them to `pet.c` as events, the way `ears.c` does for sounds. Like touch, each gesture moves the mood a little and plays a reaction; the "touch and motion reactions" setting turns them off.
- **Gravity for the gaze.** Tilted, the eyes slide towards the low side, as if they had weight; level again, they drift back. Small tilts only nudge the gaze, so the desk's slope doesn't matter.
- **Lifted and put down.** Picking it up wakes it (curious, looking at you); putting it back down settles it with a blink. Lifting is also the gentlest way to wake it, without touching the screen.
- **Shaken.** Spiral, dizzy eyes, then grumpy if it goes on. A new `dizzy` eye mood.
- **Taps on the case.** A knock on the case or on the desk next to it is a poke without fingerprints. Two knocks call it, like the claps `ears.c` hears.
- **Rocked.** A slow rocking back and forth makes it drowsy, a lullaby.
- **Face down.** Turned over on the desk, it goes to sleep and mutes the mic: a privacy gesture that needs no menu. Turned back, it wakes.
- **Dropped.** Free fall: scared eyes and a squeak, then relief.
- **Spun.** Turning it fast on the desk (gyroscope): dizzy.
- **Orientation.** The screen's rotation can follow how the badge is held or stood, as an "auto" choice next to the four fixed ones in the menu.
- **Power (later).** The IMU's wake-on-motion interrupt lets the board sleep deeply on battery and wake when it is picked up.
- **Later.** Gestures are sent to the host over the link (M3), so the app and MCP tools can use them; in M6 the gesture thresholds become data like the moods.

**Senses**
- Sound direction from the two mics, so the gaze turns to the noise (`ears_dir.c`, from the raw mics before the AFE).
- `ears.c` for bangs, claps and chatter.
- AXP2101: battery, charging, PWR key, power-off; sleep with the panel and touch in standby.

### M3: voice and link (in since 0.3.0)
- ESP-SR front end with AEC, the wake word ("Alexa" or "Hi ESP" until M6), VAD, the utterance stream and playback, from DualEye. The TDM slots are checked on the board: slot 0 mic, slot 1 the speaker loopback (AEC reference), slot 2 the second mic, slot 3 empty.
- The link over USB and WiFi, `rpc` and `hello` with `board: "yoru-amoled-1.75c"`; the board tools reworked for one screen and the settings table. What differs from DualEye's protocol: `docs/protocol.md`.
- Listening, thinking and speaking in the eye engine, with the voice's level; the wake word wakes Yoru.
- The pet's sounds (`sounds.c`) on touch, reactions and whims.
- Still to do: tune the mic gain (the mic next to the speaker nearly clips at 60% volume), use the second mic (two-mic AFE, and the sound direction in M2), check internal RAM with the WiFi on (0.3.1: 135 KB free, 99 KB at the lowest, without it; it was 48 and 12 KB in 0.3.0).

### M4: host and app
- `yoru-core`, `yoru-cli` and `yoru-app` from DualEye's host. One screen, no faces: the app shows Yoru live, plus mood, settings and the voice set-up.
- PC data and Claude Code usage drive moods.
- MCP tools: react, say, show a message.

### M5: character
- Behaviour scheduler: boredom, sighs, stretches, sleep at night, greeting on return, late-night notice.
- Non-verbal sound language (chirps, sighs, trills) for the pet.
- The sphere look (see `docs/CONCEPT.md`): in since 0.4.0 (`look.c`, `eyes.c`), a matte sphere about 40% of the disc (`LOOK_SCALE`), shaded once into PSRAM; small dark eyes that slide over its surface; two floating hands that pop out for gestures (wave, point, cover the eyes, clap); state tint, halo and status badge; sleep as drifting away into the dark.
- Progression: experience from real activity unlocks colours, eye shapes, finishes and rare animations.
- Official mascot in Rive, driven by the same parameters as the eye engine. Started: `docs/mascot/yoru.riv` holds Yoru with a state machine whose inputs mirror the eye engine.
- Music: bob to the beat.

### M6: hackability and integrations
- Moods and looks as data (JSON) that can be loaded and shared from the app.
- Skills for devices in the house, run by the host's agent and confirmed by the user.
- Custom wake word "Yoru" (microWakeWord).
- Simulator: the eye engine compiled for the desktop (SDL), so animations can be made without flashing.
