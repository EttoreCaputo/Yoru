# Yoru — roadmap

Yoru reuses what the [DualEye PC Monitor](../../esp32s3-dualeye-pcmonitor) already has working:
- on the board: the voice front end, the encrypted USB/WiFi link and the pet's mood;
- on the computer: the Rust host with local or API models, the flasher and the app shell.

What is new is the character, the UI and the hardware. The computer side is not copied: it is [Companion Hub](https://github.com/EttoreCaputo/companion-hub) (`../companion-hub`), the host program DualEye's `host/` became, shared by both boards and able to drive several at once. The firmware parts are still copied, and fixes go to both.

> **Change of direction (2026-10-18).** Yoru stops being a pet for now and becomes the physical side of the coding agents, like [coucou](https://github.com/louis-cfm/coucou) is on the screen. Its computer side becomes the **Yoru app**, a fork of coucou, instead of Companion Hub, which Yoru no longer uses at all. The plan is in [The Yoru app](#the-yoru-app-a-fork-of-coucou) below. Where the rest of this page says something different (Companion Hub, the needs, the moods, M4 and the pet parts of M5), this plan wins. The pet comes back later.

## The Yoru app: a fork of coucou

### Why and what

Yoru does three things:
- it shows what the agents are doing;
- it answers their permission requests and questions;
- it is a microphone that sends a prompt to an agent.

coucou already does all of this on the computer: the agents' hooks (Claude Code, Codex, Cursor, Gemini CLI, OpenCode and others), approvals with Always, questions, jumping to the right terminal, the plan limits, speech recognition (Parakeet offline, or the Mac's own) and a voice assistant. The fork keeps all of that and adds Yoru as a device the app drives. It replaces Companion Hub for Yoru. DualEye keeps its own `host/` and stays as it was.

### Constraints that shape the plan

- **Licence.** coucou's code is MIT. The names "Coucou" and "Mochi", the Mochi character, the icons, the sounds and the media are © Louis Raillé (`LICENSE-ASSETS.md`). A published fork needs its own name, icon, character and sounds. Keep the MIT notice.
- **coucou changes fast.** It had 17 releases in three weeks, and some files are huge (`IslandViewContent.swift` is 218 KB, `HookServer.swift` 180 KB). To keep merging from upstream:
  - Yoru's code lives in new files (`NotchBuddy/Sources/Yoru/`, and `windows/src-tauri/src/yoru/`);
  - upstream files only get small hooks into that code;
  - internal names (`NotchBuddy`, `pillId`) stay as they are;
  - features Yoru doesn't need are hidden, not deleted.
- **There is no device link on the Mac.** coucou's phone link (TLS and newline-delimited JSON, `docs/ANDROID_LINK.md`) exists only in the Windows/Linux app (`phone_link/`). The Mac app talks to the iPhone through iCloud. So the Mac side of the device link has to be written anyway.
- **Mac first.** The Mac app is Swift 6 / SwiftUI, needs macOS 15, and is generated with XcodeGen (`NotchBuddy/project.yml`). The Windows/Linux app is Tauri 2 with Rust, the same stack as Companion Hub.
- **Sandbox.** The App Store sandbox limits USB serial. Yoru ships with the GitHub build, the way coucou ships plan usage.

### Decisions (proposed)

- **The link keeps Yoru's protocol v2** (USB and WiFi, sealing, JSON-RPC, the audio streams; `docs/protocol.md`), not coucou's phone link.
  - The firmware's link already works.
  - USB powers the board and needs no set-up.
  - The audio streams are needed for the voice.
  - On the Mac it was ported to Swift from Companion Hub's `link.rs`, `wifi.rs`, `serial.rs` and `protocol.rs`, about 1,350 lines of Rust. On Windows/Linux it goes into the fork's own Tauri app; Companion Hub's Rust is only a reference to port from.
- **Yoru replaces Mochi in the app.** The sphere with its eyes is drawn in the island, the greeting and on the desktop, so the screen and the board show the same character. coucou draws Mochi in `BotCanvasView.swift`, `GreetingCanvasView.swift` and `DesktopMochi.swift`. The outfits are switched off.
- **Kept but hidden by default:** portfolio, Stripe/n8n/Resend/Notion/Cal.com, the music pills, the Notetaker, the file drop, WhatsApp. **Left alone for now:** the iPhone and Android apps. Their iCloud and phone-link switches stay off.
- **Companion Hub features coucou lacks:**
  - the flasher, WiFi pairing and Yoru's settings are needed and get ported;
  - timers, the MCP server, the diary and the quips are dropped for now.

### Phases

**F0. Fork and build.** Done (2026-10-18): [EttoreCaputo/yoru-app](https://github.com/EttoreCaputo/yoru-app), branch `yoru`, in `../yoru-app`.
- Fork `louis-cfm/coucou` on GitHub. Clone it to `../yoru-app`, add `upstream`, and work on a `yoru` branch.
- Build the stock Mac app to check the toolchain (Xcode, XcodeGen).
- Done when: coucou runs as it is, built from the fork.

**F1. Rebrand.** Done for the Mac app (2026-10-18). How it works and what is left: `YORU.md` in the fork.
- In `project.yml`: name, bundle id, URL scheme (`yoru://`), Keychain service, app group, iCloud off.
- The update feed points at the fork's releases.
- The icon and the menu bar icon.
- The 29 sounds: replaced, reusing Yoru's `sounds.c` where it fits.
- Mochi → Yoru (see the decisions).
- "Coucou" and "Mochi" in `Localizable.xcstrings` (10 languages), with a script.
- README and licence.
- Done when: no asset of coucou's is shipped and the app says Yoru everywhere.
- Also done (2026-10-18): Yoru shares nothing on disk with an installed Coucou (its own support and log folders, `yoru.sock`, `yoru-hook`, the agents' plugins), so the two run side by side. It is signed by the bytebusters team (`79AC9KV62T`) as `it.bytebusters.yoru`, with the iCloud container `iCloud.it.bytebusters.yoru`. The Debug build is signed; DebugCloud and Release need the App ID, the container, a Developer ID certificate and its profile created once in that account (`YORU.md`, "Signing").

**F2. Firmware: drop the pet.** Done in 0.8.0.
- Remove `needs.c`, `life.c`, the fireflies, the needs-driven parts of `pet.c` and `behaviour.c`, the moods from metrics, the Yoru page of the menu, and `feed` and `get_pet`.
- Keep the eye engine and its expressions (they act out the agents' states), the look, motion, the voice, the link, the agents view and the cards.
- Version 0.8.0.
- Done when: Yoru only rests, reacts to touch and motion, and reacts to the agents.

**F3. Yoru link in Swift.** Done (2026-10-18), in the fork's `NotchBuddy/Sources/Yoru/`. Checked on the board over USB, including a reboot. WiFi is checked only against a fake board, because the board's WiFi wasn't set up.
- `Sources/Yoru/`: USB serial, WiFi (mDNS `_yoru._tcp`, the sealing from `link_crypto.c`), framing, JSON-RPC, `hello` and its capabilities.
- A Yoru page in the settings: connection, WiFi pairing, the board's settings table, the firmware version.
- Check against `tools/yoru_link.py`.
- Done when: the app finds Yoru over USB and WiFi and changes a setting both ways.

**F4. Agents on Yoru.** Built (2026-10-18), in the fork's `Sources/Yoru/YoruAgents.swift`. Checked on the board: sessions, cards shown and taken down, toasts, Allow and a question answered on the touchscreen, BOOT short (focus) and long (quiet). Feed the board from coucou's own model (`HookServer`, `PillSessionRegistry`, `ApprovalQueue`, `AskQuestion`):
- `set_agents`, using coucou's 11 states (`idle working thinking searching approval question error finished ratelimit sleeping dizzy`);
- `show_approval`, with the decision going back through coucou's fingerprint check;
- `show_question` and `answer`;
- the step ticker and toasts;
- jump to terminal, interrupt, quiet, the plan limits, the BOOT shortcut.

The firmware side mostly exists (0.7.x); it is adapted to coucou's states and pills. Done when: an approval, a question and a jump to terminal work from Yoru with Claude Code and Codex.

**F5. Voice.** First part built (2026-10-18), in the fork's `Sources/Yoru/YoruVoice.swift`. Checked on the board: "Alexa", then a sentence, is transcribed (the Mac's speech recognition, Italian) and sent with `claude -p --resume` to a test session. Prompt to an agent session is done. The voice assistant, dictation and spoken answers are still to do.
- What it does now:
  - assembles the `audio_up` frames (a dropped frame becomes silence);
  - transcribes with Parakeet when its models are installed, else the Mac's speech recognition in the first two Automatic dictation languages, keeping the most confident result;
  - sends the text to the session waiting for you, else the one active last, among those a prompt can reach. In its terminal it is pasted, then Return is sent (this needs Accessibility). Otherwise it goes in the background with `claude -p --resume` in its folder;
  - shows thinking, then a toast.
- Claude Desktop's own sessions can't be reached, so they are skipped.
- Yoru's wake word or a touch opens the mic. `audio_up` goes into coucou's speech recognition (Parakeet or the Mac's own).
- The text goes to one of three places:
  1. **a prompt to an agent session**: coucou's "send the next instruction", which resumes the session in its own folder;
  2. the voice assistant;
  3. dictation where you type.
- Answers are spoken through Yoru's speaker (`audio_down`).
- Done when: "Hey Yoru, run the tests" reaches the chosen Claude Code session.

**F6. Firmware in the app.**
- The firmware image ships inside the app and is flashed through a bundled esptool, as Companion Hub does.
- This replaces Companion Hub's `tools/fetch_firmware.sh` in this repository's CLAUDE.md rule.

**F7. Windows and Linux.** The same Yoru module in the fork's Tauri app (`windows/src-tauri/src/yoru/`): the link, the agents and the voice, written for that app. Companion Hub is not used or shared.

**F8. Retire Companion Hub.** Yoru no longer depends on it (2026-10-18).
- Archive its GitHub repository (archive, not delete) and remove the local copy, once nothing else on this Mac uses it (Claude Code's status line still runs `companion-hub --claude-statusline`).
- DualEye is not touched: it has its own `host/`.
- Update this repository's CLAUDE.md, `docs/protocol.md` and this roadmap.

**Ongoing: keep up with coucou.** About once a week, `scripts/yoru_sync_upstream.sh` in the fork lists coucou's new commits and releases, merges them into `yoru` without committing, rebrands them, renders Yoru's sounds and icons again, checks that no coucou name or shared path came back, and builds. Each new feature is then hidden or adapted to Yoru before the merge is committed (`YORU.md`, "Merging upstream").

### Risks

- **Merge conflicts** with a fast upstream; the rule of keeping Yoru's code in its own files is what keeps them small.
- **Swift.** The Mac work is Swift, not Rust.
- **Lost Hub features.** Timers, MCP, the diary and the quips are gone until they are ported.
- **RAM.** The board's internal RAM is unchanged by this plan. F5 adds no new firmware load, because the voice and the audio streams already exist.

### Open questions

- Should the unused features be hidden or deleted?
- Are Windows/Linux (F7) wanted, and when?
- Should the iPhone/Android apps be kept, renamed or dropped?

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
| Host | `companion-core` in Companion Hub (link, flasher, firmware, wifi, stt, tts, llm, agent, voice, models, mcp, claude, timers, intents, diary, quips, sensors, snapshot) | Shared: Yoru is a board profile there (`docs/multiboard.md` in that repository) |
| App | Companion Hub's app: settings, onboarding, i18n, updates, title bar | Shared; it adds a Yoru view next to DualEye's |
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
- In [Companion Hub](https://github.com/EttoreCaputo/companion-hub), which drives DualEye and Yoru together: a Yoru profile in its core, CLI and MCP, and a Yoru view in its app. One screen, no faces: the app shows Yoru live, plus mood, settings and the voice set-up. The steps are in that repository's `docs/multiboard.md`.
- PC data and Claude Code usage drive moods.
- MCP tools: react, say, show a message.

### M4b: the agents' companion (in since 0.7.0)

Yoru is the physical side of the coding agents on the computer, the way the coucou notch app is on the screen: it shows what they are doing, lets them be answered without going back to the terminal, and gets you to the right terminal. Nothing else (no chat, no service pills): the host does the agents' bookkeeping, Yoru shows it.
- On the board: a dot per session on the rim (`agents_ui.c`), coloured by its state; the step ticker over the bottom rim and the host's toasts; the agents view (swipe up from the bottom edge, BOOT, the menu): a little Yoru per session in its agent's colour (Claude orange, Gemini yellow, Codex blue, Cursor green...), animated by its state, tap for its terminal, hold to interrupt, quiet for an hour, and the plan's limits.
- Cards: permission requests with Always, refusing blind allows of a request cut to fit (`approval.c`); questions with their choices (`question.c`); both on `card.c`.
- Yoru reacts: blue while an agent works, a nod when it finishes, a wince when it fails, a knock on the glass while one waits for you (`life_call`).
- The host side (Companion Hub): hooks for Claude Code, Codex, Gemini CLI, Cursor and OpenCode; `set_agents`, `show_question`, the `shortcut` notification. The protocol is in `docs/protocol.md` (Agents, Questions, Shortcuts).
- Next: Anthropic's Hardware Buddy BLE protocol, so Yoru also works with Claude Desktop and Cowork without the hub (Bluetooth next to WiFi and ESP-SR needs its internal RAM measured first).

### M5: character
- Behaviour scheduler: in since 0.6.0 (`needs.c`, `life.c`). Five needs (rest, food, fun, company, curiosity) and acts chosen by how well they meet them: floating about, exploring, hunting fireflies, asking for you, three kinds of play, daydreaming, sulking, dancing, naps and waking up rested. Still to do: the late-night notice, and acts made from data (M6).
- Depth: in since 0.6.0. Yoru moves through a space in perspective (`eyes_move`, `eyes_follow`), the sphere rendered at any size on demand (`look_ball`).
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
