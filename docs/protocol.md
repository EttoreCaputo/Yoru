# Protocol v2 (Yoru)

Yoru speaks DualEye's protocol v2, copied from [DualEye's protocol](../../esp32s3-dualeye-pcmonitor/docs/protocol.md): the same frames, channels, JSON-RPC methods, audio streams and WiFi pairing. This page lists only what is different on Yoru; read DualEye's for the rest. When the two diverge, fix both until the shared parts move to a repository of their own.

Implementations: `main/link.c`, `main/link_frame.c`, `main/link_crypto.c`, `main/net.c`, `main/rpc.c` (board); `tools/yoru_link.py` (test scripts); `host/yoru-core` from M4.

## Identity

`hello` and `ready` answer:

```json
{"protocol":2,"firmware":"0.3.0","idf":"v6.1","board":"yoru-amoled-1.75c","id":"7c0c5f4233e4","transport":"usb","max_payload":4096,"channels":["ctrl","metrics","log","audio_up","audio_down"],"capabilities":["tools","gaze","wifi","voice","speaker"]}
```

- `board` is `yoru-amoled-1.75c`; a host tells Yoru from DualEye by it.
- There is no `media` or `music` capability: Yoru has no image or music face, so `media/...` and `music/...` answer `-32601`.
- `eyes/gaze` turns Yoru's eyes towards the host's pointer for 6 s after the last one, unless a finger is on the screen, a conversation is on or it is asleep.
- `voice/state` drives the eyes: `listening` (wide, looking at you), `thinking` (dots), `speaking` (warm, swelling with the speaker's level), `error` (sad). The wake word wakes Yoru up if it was asleep.

## WiFi

The same pairing and sealing, with Yoru's own names, so a DualEye host can't talk to Yoru on WiFi by mistake:

| | DualEye | Yoru |
|---|---|---|
| mDNS service | `_dualeye._tcp` | `_yoru._tcp` |
| Host name | `dualeye-<6 hex>.local` | `yoru-<6 hex>.local` |
| Proof labels | `dualeye host`, `dualeye board` | `yoru host`, `yoru board` |
| Session key labels | `dualeye h2b`, `dualeye b2h` | `yoru h2b`, `yoru b2h` |
| NVS namespace | `dualeye_net` | `yoru_net` |

The TCP port is the same, 7340.

## Metrics

The same snapshot. Yoru reads it for the pet's mood only (temperatures, music, Claude Code, the battery, the time and the host's idle time) and shows none of it. A timer's `screen` is ignored.

## Board tools

Settings are kept in NVS and survive a reboot.

| Tool | Arguments | Effect |
|------|-----------|--------|
| `set_brightness` | `percent`: 5–100 | How bright the eyes glow while awake. Asleep they dim by themselves (setting `auto_dim`) |
| `set_mic` | `muted`, `wake_sound`: booleans, at least one | As on DualEye. Muted, the mic isn't read: no wake word, nothing heard in the room |
| `set_wake_word` | `word`: one of `voice.wake_words` (`alexa`, `hiesp`) | As on DualEye. Yoru's own word comes in M6 |
| `set_volume` | `percent`: 0–100 | Speaker volume, with the `volume` sound at the new level |
| `react` | `name`: `happy` · `excited` · `wink` · `proud` · `relieved` · `charged` · `dance` · `love` · `curious` · `thinking` · `focus` · `angry` · `hot` · `sleepy` · `yawn` · `tired` · `sigh` · `sad` · `surprised` · `scared` · `bored` · `look_around` · `blink` | The eyes react for a few seconds, with the pet's sound for it. DualEye's `play_eyes` names, those these eyes can show. Refused during a conversation |
| `play_sound` | `name` | As on DualEye: the same sounds |
| `set_pet` | `sounds`, `react`, `hear`: booleans, at least one | As on DualEye |
| `get_pet` | none | As on DualEye, without `idle_scenes` |
| `get_settings` | none | Every setting as `{"value":n,"min":n,"max":n}` under its name, plus `wake_word`: `brightness`, `night_bright`, `night_from`, `night_to`, `auto_dim`, `rotation`, `volume`, `pet_sounds`, `mic_muted`, `sleepy_after`, `asleep_after`, `focus`, `touch_react`, `motion_react`, `pet_react`, `pet_hear`, `eye_color`, `eye_style`, `wake_on`, `language`, `wake_sound`. The menu on the board (M2) shows the same |
| `set_setting` | `name`, `value`: integer | Change one setting within its range (switches are 0 or 1); it applies at once |
| `get_state` | none | Firmware, uptime, metrics state, `eyes` (the eyes' mood now), `brightness`, voice, the pet, audio, UI load, memory, link counters and the WiFi |

DualEye's `set_face`, `set_rotation`, `show_text` and `set_eyes` are not there: one screen, no faces. Showing a message comes back in M4.
