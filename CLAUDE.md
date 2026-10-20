# Yoru

Desk companion on the Waveshare ESP32-S3-Touch-AMOLED-1.75C. Character: `docs/CONCEPT.md`. Plan and pins: `docs/ROADMAP.md`. Sister project: `../esp32s3-dualeye-pcmonitor` (DualEye), where the voice, link and mood code come from. When copying from there, adapt the code; don't carry over DualEye's two-screen assumptions.

Host program: `../yoru-hub` (formerly Companion Hub), Yoru's personal assistant (Yoru only, no DualEye); the plan is its `docs/assistant.md`. It is the only host and ships the firmware: the Yoru app (`../yoru-app`, a fork of coucou) was retired on 2026-10-19. A protocol change is made here (`docs/protocol.md`) and in Yoru Hub's `core/src/`.

## Rule: every firmware change bumps the version and gets built

1. Bump `version.txt` (patch by default). Check `git log` first: if the current version is committed, it must go up. Bump once per piece of work.
2. Add a `## <version>` section at the top of `FIRMWARE_CHANGELOG.md`, written for the person using the board.
3. Build: `source ~/.espressif/tools/activate_idf_v6.1.sh && idf.py build merge-bin`. If it fails, fix it or report it clearly.
4. Flash the board once the build passes: `idf.py -p <port> flash` (the port is `/dev/cu.usbmodem*`). If no board is connected or flashing fails, say so.
5. Copy the build into Yoru Hub, which ships it: `../yoru-hub/tools/fetch_firmware.sh`.

Report the new version, the build result, the flash result and the copy into the app when done.

## Rule: the user makes the commits

Never run `git commit`, `git push` or anything else that writes history. Leave the changes in the working tree; the user commits them.

## Rule: everything in the code is in English

Identifiers, comments, log messages, strings, file names and docs in the repo are written in English, even when the conversation is in another language. The only exception is text the user sees in another language on purpose (the Italian menu labels).

## Conventions

- ESP-IDF 6.1, LVGL 9.3.0, C. Match DualEye's style: short functions, comments that say why.
- The eye engine (`main/eyes.c`) only draws; moods and behaviours set its targets.
- Every area sent to the CO5300 must be even-aligned (the LVGL rounder in `lvgl_port.c`).
- Internal RAM is scarce once WiFi and ESP-SR run: big buffers go to PSRAM.
