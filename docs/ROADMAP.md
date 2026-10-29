# Yoru — roadmap

Yoru reuses what the [DualEye PC Monitor](../../esp32s3-dualeye-pcmonitor) already has working on the board: the voice front end and the encrypted USB/WiFi link. What is new is the character, the UI and the hardware. The computer side is Yoru Hub (`../yoru-hub`), Yoru's personal assistant and its only host; its plan is `docs/assistant.md` there. Fixes to the shared firmware parts go to DualEye too.

## Where it stands

Firmware 0.11.9 (2026-10-29).

**In**
- The look: the matte sphere with its eyes and hands, depth in perspective, state tint and halo, sleep as drifting away, 24-bit colour as an option (M5).
- Life on the desk, reduced since 0.8.0: Yoru floats near the middle, turns to sudden sounds, reacts to touch and to the case, gets sleepy and naps when nobody is around, and comes to the glass when something urgent waits for you. The needs and the acts they chose (fireflies, play, exploring) are out for now (M5).
- Senses: touch, motion (`motion.c`: lifted, put down, shaken, knocked, rocked, face down, dropped, spun, tilt), the sound's direction (`ears_dir.c`), the PMU (M2).
- The menu as a watch's: Notifications, Timers, Habits, Games, Do not disturb and Settings around Yoru; every setting on the board, in Italian and English (M2).
- Voice and link: wake word (Alexa, Hi ESP, Jarvis, Computer), push-to-talk on PWR, listening on after an answer, the host judging the end of a sentence (Smart Turn), speech on the speaker, USB and WiFi (M3).
- The assistant's body (M4): the inbox (`show_notification`) and the Allow and Deny card, both used by Yoru Hub; BOOT opens the inbox, the screen goes dark after a minute asleep. The Timers and Habits apps and the errands are built too, but Yoru Hub doesn't drive them yet.
- Games: Tilt ball, Slingshot, Constellations (since 0.11.9).

**Left**
- Yoru Hub's side of 0.11.0: send `set_timers`, `set_habits` and `show_fetch`, and handle `timer_action` and `habit_check` (habits don't exist in Yoru Hub at all). Until then Timers shows only the snapshot's timer, Habits is empty and Yoru never goes on an errand.
- Voice: the custom wake word "Yoru" (M6); the mic gain (M3); the second mic, with a two-mic AFE (M3).
- The pet's return: needs, moods and acts, with the late-night notice and the sounds' language (M5).
- Power: deep sleep on battery with wake-on-motion (M2).
- The menu: the Look items (name, colour, eye shape: the host can set `eye_color` and `eye_style`, the menu can't yet) and pairing a computer from the board (M2).
- Checks on the board: which way the IMU's x and y and the two mics lie (M2); internal RAM with WiFi and ESP-SR on together (M3), which also decides Bluetooth.
- Gestures sent to the host (M2), growing up (M5), bobbing to music (M5), moods and looks as data, a desktop simulator (M6), Bluetooth (M4).

## Changes of direction

The plan changed three times in a few days; the latest wins.

- **2026-10-21: only Yoru.** Yoru has a story of its own (`docs/CONCEPT.md`, Who Yoru is) and is the only character: no coding agents on the board. Firmware 0.11.5 removed the agents view, the remote, Claude Code's requests and questions, `shortcut` and the `agents` and `remote` capabilities (M4b, history). PWR held talks to Yoru; BOOT opens the inbox.
- **2026-10-19: a personal assistant with a body.** Yoru lives on the computer as an assistant connected to your services (MCP connectors, our own client with OAuth), tells you what is worth knowing and answers when you ask; the board is its body. Its host is Yoru Hub (Companion Hub renamed, Yoru only). The Yoru app was retired the same day (History, below).
- **2026-10-18: the agents' companion.** Yoru stopped being a pet and became the physical side of the coding agents, with a fork of coucou as its app. Firmware 0.8.0 took the needs and moods out; they haven't come back yet.

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
| Voice on the board | `voice.c`, `board_audio.c`, `playback.c`, `sounds.c`, `audio_selftest.c` | Copied in 0.3.0; `ui_voice.c` hands the voice state to `behaviour.c` |
| Link | `link*.c`, `net.c`, `rpc.c`, `board_tools.c` | Copied in 0.3.0, with Yoru's names on WiFi and `hello`, and tools for one screen |
| Settings | `board_settings.c` | Copied in 0.2.0, rewritten as a table for the round menu |
| Mood | `pet.c`, `ears.c` | Copied in 0.2.0, removed in 0.8.0 with the needs |
| PC data | `metrics_*` | Copied in 0.3.0; since 0.8.0 only the host's time and idle time are read |
| Host | DualEye's `host/` | Became Companion Hub, now Yoru Hub (`../yoru-hub`), Yoru only |
| New | display/touch/IMU/PMU drivers, the eye engine, behaviours, the menu and its apps, the games | Written here |

## Milestones

### M1: bring-up (done)
- ESP-IDF 6.1 project: CO5300 over QSPI, LVGL 9.3 with even-aligned flushes, CST9217 touch as an LVGL input.
- The eye engine (`eyes.c`): parameters, eased moods, blink, saccades, squash, overlays.
- A demo loop: it looks around and blinks; a tap makes it happy, three taps make it grumpy, a long press puts it to sleep.
- Audio codecs up on the shared I2C bus, with a boot chirp.

To check on the hardware: rendering time per frame (`!lvgl stats`), and the flash size (`esptool flash_id`).

### M2: settings menu and senses (in since 0.5.0)

In 0.5.0: the menu (`menu.c`), motion (`motion.c`), the PMU (`board_pmu.c`), the sound's direction (`ears_dir.c`), and new eye moods and gestures for them (dizzy, scared, peek; hold, shoo, flail; knock and lean). Left: the Look items (name, colour, eye shape: M5, when there is more than one; the host already sets `eye_color` and `eye_style`), pairing a computer from the menu (the host does it over USB), deep sleep with wake-on-motion on battery, sending the gestures to the host, and checking on the board which way the IMU's x and y and the two mics lie (`SCREEN_X`/`SCREEN_Y` in `motion.c`, `MIC_A_SIDE` in `ears_dir.c`).

**The menu as a watch's (since 0.11.0).** The ring of icons became a honeycomb of round apps around Yoru; since 0.11.1 Yoru is one of the bubbles (top left), the bubbles are smaller, the honeycomb pans with a fisheye towards the rim, and only the bubbles that change are redrawn: Notifications, Timers, Habits, Settings, Do not disturb and, since 0.11.9, Games (Agents left in 0.11.5), with badges and rings that say what is new. Since 0.11.8 an app opens as a ring of its colour rippling out past the edge. The items below are all under Settings, each category with its round icon. Timers and Habits come from the host (`set_timers`, `set_habits`); a host's connector call sends Yoru on an errand through a portal (`show_fetch`, `errand.c`). Yoru Hub sends none of the three yet.

**Settings menu on the board.** It is a round menu, made for the circle, that opens without the app. This is the plan it was built from; what is in today: Display (brightness, night brightness and hours, dim when asleep, rotation with auto, 24-bit colour), Sound (volume, Yoru's sounds, mic off, listening chime), Behaviour (sleepy and asleep after, do not disturb, touch and motion reactions, turns to sounds, the BOOT button), Voice (wake word on or off, which one, language), Connection, Battery and About (with the 80 MHz bus test). Look and pairing are still to do.
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

**Mood (0.2.0 to 0.7.x, removed in 0.8.0)**
- `pet.c` from DualEye: energy, happiness and affection, kept in NVS. It picked the resting eyes, the whims and how often they came; pokes and strokes moved it. It went with the needs in 0.8.0 (M5); the eye moods it used (sad, loving with hearts, surprised, bored) stay as reactions.

**Motion (QMI8658)**

The accelerometer and gyroscope turn the whole case into an input: Yoru lives in a round badge that is picked up, turned and put down, so how it moves says a lot. A `motion.c` reads the IMU (0x6B on the shared I2C, INT1 on GPIO 21), recognises a few gestures and hands them to `behaviour.c` as events. Like touch, each gesture plays a reaction (it moved the mood too, until 0.8.0); the "touch and motion reactions" setting turns them off.
- **Gravity for the gaze.** Tilted, the eyes slide towards the low side, as if they had weight; level again, they drift back. Small tilts only nudge the gaze, so the desk's slope doesn't matter.
- **Lifted and put down.** Picking it up wakes it (curious, looking at you); putting it back down settles it with a blink. Lifting is also the gentlest way to wake it, without touching the screen.
- **Shaken.** Spiral, dizzy eyes, then grumpy if it goes on. A new `dizzy` eye mood.
- **Taps on the case.** A knock on the case or on the desk next to it is a poke without fingerprints. Two knocks call it.
- **Rocked.** A slow rocking back and forth makes it drowsy, a lullaby.
- **Face down.** Turned over on the desk, it goes to sleep and nothing shows: a privacy gesture that needs no menu, always on. Turned back, it wakes.
- **Dropped.** Free fall: scared eyes and a squeak, then relief.
- **Spun.** Turning it fast on the desk (gyroscope): dizzy.
- **Orientation.** In: the screen's rotation can follow how the badge stands, as an "auto" choice (rotation 4) next to the four fixed ones.
- **Power (to do).** The IMU's wake-on-motion interrupt lets the board sleep deeply on battery and wake when it is picked up.
- **To do.** Gestures are sent to the host over the link (M3), so the app and MCP tools can use them; in M6 the gesture thresholds become data like the moods.

**Senses**
- Sound direction from the two mics, so the gaze turns to the noise (`ears_dir.c`, from the raw mics before the AFE).
- `ears.c` for bangs, claps and chatter: removed in 0.8.0 with the pet; only the sound's direction is left.
- AXP2101: battery, charging, PWR key, power-off; sleep with the panel and touch in standby, and since 0.9.0 the dark screen after a minute asleep.

### M3: voice and link (in since 0.3.0)
- ESP-SR front end with AEC, the wake word (Alexa, Hi ESP, Jarvis or Computer until M6), VAD, the utterance stream and playback, from DualEye. The TDM slots are checked on the board: slot 0 mic, slot 1 the speaker loopback (AEC reference), slot 2 the second mic, slot 3 empty.
- The link over USB and WiFi, `rpc` and `hello` with `board: "yoru-amoled-1.75c"`; the board tools reworked for one screen and the settings table. What differs from DualEye's protocol: `docs/protocol.md`.
- Listening, thinking and speaking in the eye engine, with the voice's level; the wake word wakes Yoru.
- The pet's sounds (`sounds.c`) on touch and reactions.
- Since 0.9.0 push-to-talk on PWR; since 0.11.6 listening on after an answer, with a tick and a ring running out; since 0.11.7 the host may judge the end of a sentence (`utterance_pause`, `voice/pause`, Smart Turn in Yoru Hub).
- Still to do: tune the mic gain (`AUDIO_MIC_GAIN_DB`, 37.5 dB) (the mic next to the speaker nearly clips at 60% volume), use the second mic (two-mic AFE, and the sound direction in M2), check internal RAM with the WiFi on (0.3.1: 135 KB free, 99 KB at the lowest, without it; it was 48 and 12 KB in 0.3.0).

### M4: host and app (in)
- The host is Yoru Hub (`../yoru-hub`): the assistant with connectors, voice, watchers and notifications, timers, habits, the flasher and WiFi pairing, and an MCP server for other clients (react, speak, notify, timers). Its plan and status: `docs/assistant.md` there.
- On the board, for it: the inbox (0.10.0), the Timers and Habits apps and the errands (0.11.0), the Allow and Deny card for its own confirmations (0.11.5). Yoru Hub uses the inbox and the card; timers, habits and errands wait for its side (Where it stands).
- Earlier hosts: Companion Hub with DualEye (to 2026-10-18), then the Yoru app (2026-10-18 to 2026-10-19, History).
- Left: Anthropic's Hardware Buddy BLE protocol, so Yoru also works with Claude Desktop without the hub (Bluetooth next to WiFi and ESP-SR needs its internal RAM measured first).

### M4b: the agents' companion (0.7.0 to 0.11.4, removed in 0.11.5)

Yoru is the physical side of the coding agents on the computer, the way the coucou notch app is on the screen: it shows what they are doing, lets them be answered without going back to the terminal, and gets you to the right terminal. Nothing else (no chat, no service pills): the host does the agents' bookkeeping, Yoru shows it.
- On the board: a dot per session on the rim (`agents_ui.c`), coloured by its state; the step ticker over the bottom rim and the host's toasts; the agents view (swipe up from the bottom edge, BOOT, the menu): a little Yoru per session in its agent's colour (Claude orange, Gemini yellow, Codex blue, Cursor green...), animated by its state, tap for its terminal, hold to interrupt, quiet for an hour, and the plan's limits.
- Cards: permission requests with Always, refusing blind allows of a request cut to fit (`approval.c`); questions with their choices (`question.c`); both on `card.c`.
- Yoru reacts: blue while an agent works, a nod when it finishes, a wince when it fails, a knock on the glass while one waits for you (`life_call`).
- The host side (Companion Hub): hooks for Claude Code, Codex, Gemini CLI, Cursor and OpenCode; `set_agents`, `show_question`, the `shortcut` notification. The protocol is in `docs/protocol.md` (Agents, Questions, Shortcuts).

### M5: character
- Behaviour scheduler: in 0.6.0 and 0.7.x (`needs.c`, `life.c`), five needs (rest, food, fun, company, curiosity) and acts chosen by how well they meet them: floating about, exploring, hunting fireflies, asking for you, three kinds of play, daydreaming, sulking, dancing, naps and waking up rested. 0.8.0 took the needs and those acts out; `life.c` now only lounges, naps, sleeps and comes to the glass (`call`, `come`). To do: bring the pet back under the new story (`docs/CONCEPT.md`), with the late-night notice, and acts made from data (M6).
- Depth: in since 0.6.0. Yoru moves through a space in perspective (`eyes_move`, `eyes_follow`), the sphere rendered at any size on demand (`look_ball`).
- Non-verbal sound language (chirps, sighs, trills) for the pet: to do; today's sounds are reactions and earcons.
- The sphere look (see `docs/CONCEPT.md`): in since 0.4.0 (`look.c`, `eyes.c`), a matte sphere about 40% of the disc (`LOOK_SCALE`), shaded once into PSRAM; small dark eyes that slide over its surface; two floating hands that pop out for gestures (wave, point, cover the eyes, clap); state tint, halo and status badge; sleep as drifting away into the dark.
- Progression (to do): experience from real activity unlocks colours, eye shapes, finishes and rare animations.
- Official mascot in Rive, driven by the same parameters as the eye engine. Started: `docs/mascot/yoru.riv` holds Yoru with a state machine whose inputs mirror the eye engine up to 0.5.0 (`docs/mascot/STATES.md`); not used by any app yet.
- 24-bit colour (0.11.2, setting `color_24bit`): smoother gradients and shadows than 16-bit.
- Games (0.11.9): Tilt ball, Slingshot and Constellations, in the menu's Games app (`games_ui.c`, `game_*.c`); each keeps the level reached.
- Music: bob to the beat (to do).

### M6: hackability and integrations
- Moods and looks as data (JSON) that can be loaded and shared from the app.
- Skills for devices in the house, run by the host's agent and confirmed by the user.
- Custom wake word "Yoru" (microWakeWord). Until then: Alexa, Hi ESP, Jarvis, Computer.
- Simulator: the eye engine compiled for the desktop (SDL), so animations can be made without flashing.

All four are still to do.

## History: the Yoru app (2026-10-18 to 2026-10-19)

For a day Yoru's host was the Yoru app, a fork of [coucou](https://github.com/louis-cfm/coucou) in `../yoru-app` ([EttoreCaputo/yoru-app](https://github.com/EttoreCaputo/yoru-app), branch `yoru`), with the link, the agents, the voice and the flasher written in Swift (`NotchBuddy/Sources/Yoru/`). It was retired on 2026-10-19, once Yoru Hub's assistant worked, and its folder removed; firmware 0.8.0 to 0.11.4 served it (M4b). The full plan (phases F0 to F8) is in this file's history before 2026-10-29.

