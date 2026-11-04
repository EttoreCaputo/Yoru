# Protocol v2 (Yoru)

Yoru speaks DualEye's protocol v2, copied from [DualEye's protocol](../../esp32s3-dualeye-pcmonitor/docs/protocol.md): the same frames, channels, JSON-RPC methods, audio streams and WiFi pairing. This page lists only what is different on Yoru; read DualEye's for the rest. When the two diverge, fix both until the shared parts move to a repository of their own.

Implementations: `main/link.c`, `main/link_frame.c`, `main/link_crypto.c`, `main/net.c`, `main/rpc.c` (board); `tools/yoru_link.py` (test scripts); `core/src/protocol.rs`, `link.rs`, `wifi.rs` and `voice.rs` in Yoru Hub (`../yoru-hub`), the host, Yoru's personal assistant (its `docs/assistant.md`). The Yoru app (`NotchBuddy/Sources/Yoru/` in the coucou fork) was the host from 2026-10-18 to 2026-10-19. A change to the protocol is made here and in Yoru Hub's `core/src/` together.

## Identity

`hello` and `ready` answer:

```json
{"protocol":2,"firmware":"0.11.9","idf":"v6.1","board":"yoru-amoled-1.75c","id":"7c0c5f4233e4","transport":"usb","max_payload":4096,"channels":["ctrl","metrics","log","audio_up","audio_down"],"capabilities":["tools","gaze","wifi","voice","speaker","inbox","timers","tasks","fetch"]}
```

- `board` is `yoru-amoled-1.75c`; a host tells Yoru from DualEye by it.
- `agents` and `remote` (0.7.0 to 0.11.4): the coding agents' tools and notifications and the remote for them. 0.11.5 removed them, with the agents' questions, drafts, chats and `shortcut` (`show_approval` stays, for Yoru's own confirmations): a host must not send those tools to a board without the capabilities.
- `inbox` (0.10.0 and later): the inbox (see Inbox): the `show_notification`, `hide_notification` and `clear_notifications` tools and the `notification_action` notification.
- `timers` (0.11.0 and later): the menu's Timers app (see Timers): the `set_timers` tool and the `timer_action` notification.
- `tasks` (0.16.0 and later): the menu's Tasks app (see Tasks): the `set_tasks` tool and the `task_run` notification. `habits` (0.11.0 to 0.15.1) was the Habits app, with `set_habits` and `habit_check`.
- `fetch` (0.11.0 and later): Yoru's errands (see Fetch): the `show_fetch` tool.
- `cloud` (0.14.0 and later): talking without the host (see Cloud voice): the `cloud/set` and `cloud/get` methods.
- `ota` (0.19.0 and later, once flashed over USB with the two-slot partition table): updates over the link (see OTA).
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

## OTA

Since 0.19.0 the flash has two app slots, `ota_0` (where `factory` was, at 0x10000) and `ota_1` (0x820000), with `otadata` at 0x810000; `nvs` and `model` stay where they were, so the one flash over USB that brings the new table keeps the WiFi, the pairing and the models. From then on a host updates the board over its link, USB or WiFi (sealed with the pairing key, so only a paired host can):

| Method | Params | Result |
|---|---|---|
| `ota/begin` | `size` (bytes), `sha256` (hex), `version` (for the log) | `{"slot","chunk"}`: the slot it writes, the most raw bytes per write (2048) |
| `ota/write` | `offset` (where the board stands), `data` (base64) | `{"written"}` |
| `ota/end` | none | `{"restarting":true}`: checked and set to boot; it restarts 0.8 s later |
| `ota/abort` | none | `{}` |
| `ota/status` | none | `running` (slot), `available`, `probation`, `active` (an update under way), `written` |

- The image is the app's alone (`build/yoru.bin`, or the app partition's part of the merged image), not the merged one.
- The board refuses, with `-32602`: an image that isn't Yoru's (the app descriptor's `project_name`, checked before anything is written), a write at another offset than the one reached (after a lost reply, `ota/status` says where it stands), a SHA-256 that doesn't match at the end. `ota/end` also has ESP-IDF check the image itself. A new `ota/begin` drops one left half done. A board flashed before 0.19.0 answers `-32601`: it needs the USB flash once.
- **Probation.** The new firmware boots on probation (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`). It's kept once it gets an address on the WiFi or a host says `hello`: then it can be updated again. If neither comes within 5 minutes it restarts into the firmware before; so does a restart before then (a crash, or a host opening the USB port, which resets the chip).
- On the board a toast says "Updating…", then "Updated: restarting" or "Update failed". Yoru carries on meanwhile.
- About a minute for 3 MB, over USB or WiFi.

## Metrics

The same snapshot. Since 0.10.0 a snapshot needs only `pet` (the host's time and idle time): `cpu` and `gpu` are optional, and one with none of the three is still ignored. Since 0.8.0 Yoru reads only the host's time and idle time (night brightness, and the sleep times count from your last input on the computer); it shows none of the rest. A timer's `screen` is ignored.

**The board's clock (0.13.0).** The board keeps the time of day itself (`clock.c`): from the internet (SNTP, `pool.ntp.org`) once its WiFi is up, and from each snapshot: `ts` (UTC seconds) sets it when it is more than 2 s off, and `pet.tz`, the host's time zone as POSIX writes it (`"CET-1CEST,M3.5.0,M10.5.0/3"`, at most 47 characters), sets the zone, kept in NVS. A host without `tz` gives its offset from `pet.min` against `ts`, to a quarter of an hour. Until a host has said a zone, the board uses Central European Time. Night brightness, the tasks' times and "what time is it" use this clock, with or without a host. A host whose snapshots stopped more than 3 s ago is taken as gone: its idle time no longer counts, the board rings its timers and hears spoken commands itself (Offline commands).

```json
{"v":2,"ts":1793574000,"pet":{"min":915,"idle":4,"tz":"CET-1CEST,M3.5.0,M10.5.0/3"}}
```

## Board tools

Settings are kept in NVS and survive a reboot.

| Tool | Arguments | Effect |
|------|-----------|--------|
| `set_brightness` | `percent`: 5–100 | How bright the eyes glow while awake. Asleep they dim by themselves (setting `auto_dim`) |
| `set_mic` | `muted`, `wake_sound`: booleans, at least one | As on DualEye. Muted, the mic isn't read: no wake word, nothing heard in the room |
| `set_wake_word` | `word`: one of `voice.wake_words` (`alexa`, `hiesp`, `jarvis`, `computer`) | As on DualEye. Yoru's own word comes in M6 |
| `set_volume` | `percent`: 0–100 | Speaker volume, with the `volume` sound at the new level |
| `react` | `name`: `happy` · `excited` · `wink` · `proud` · `relieved` · `charged` · `plugged` (0.12.1) · `dance` · `love` · `curious` · `thinking` · `focus` · `angry` · `hot` · `sleepy` · `yawn` · `tired` · `sigh` · `sad` · `surprised` · `scared` · `bored` · `look_around` · `blink` · `wave` · `point` · `shy` · `clap` · `stretch` · `dizzy` · `annoyed` · `falling` · `drowsy` · `lifted` | The eyes react for a few seconds, with Yoru's sound for it. DualEye's `play_eyes` names, those these eyes can show. Refused during a conversation |
| `play_sound` | `name` | As on DualEye: the same sounds, plus Yoru's own: `unplug` and, an earcon played with pet sounds off too, `battery_low` (0.12.1) |
| `get_settings` | none | Every setting as `{"value":n,"min":n,"max":n}` under its name, plus `wake_word`: `brightness`, `night_bright`, `night_from`, `night_to`, `auto_dim`, `rotation`, `volume`, `pet_sounds`, `mic_muted`, `sleepy_after`, `asleep_after`, `focus`, `touch_react`, `motion_react`, `pet_hear` (turns to sudden sounds), `eye_color`, `eye_style`, `wake_on`, `language`, `wake_sound`, `lcd_80mhz` (the screen's bus at 80 MHz instead of 40, a test; three restarts in a row within 20 s each turn it off), `boot_key` (BOOT opens the inbox; on by default), `color_24bit` (24-bit colour instead of 16; Yoru restarts to apply it), `power_saver` (on battery, the Saver profile instead of Balanced; 0.12.0, see Power), `hibernate` (on battery, minutes of dark screen before hibernating, 0 never; 0.12.0). `rotation` is 0–3 quarter turns or 4 to follow how the badge stands. `sleepy_after` and `asleep_after` count from the last sign of someone around (a touch, a voice, input on the host), 300 and 600 s by default since 0.6.0. The menu on the board shows the same |
| `set_setting` | `name`, `value`: integer | Change one setting within its range (switches are 0 or 1); it applies at once |
| `get_state` | none | Firmware, uptime, metrics state, `eyes` (the eyes' mood now), `brightness`, voice, `activity` (what Yoru does: `lounge`, `call`, `nap`, `sleep`, `come`; it replaced `pet` in 0.8.0; since 0.15.0 also its state, `watch`, `explore`, `play`, `dream`, `bored`, `seek`, `sulk`, `drowsy`, and `greet`), `cheer` (0..1), audio, UI load, memory, link counters, the WiFi, `battery` (`present`, `percent`, `mv`, `usb`, `charging` and, since 0.12.0, `profile`: `desk`, `balanced` or `saver`) and `motion` (gravity `g` and turn rate `dps` on the screen's axes, `lean_x`/`lean_y`, `upright`: the quarter turn it stands at, or -1 lying flat) and, since 0.10.0, `inbox` (`count`, `unread`, `view`); since 0.11.0 `timers` (how many), `habits` (`count`, `done`; replaced in 0.16.0 by `tasks`: `count`, `failed`) and `errand` (Yoru is on one); since 0.13.0 `minute`, the board's clock (local minutes since midnight, -1 while it doesn't know the time); since 0.15.0 `life`: `activity`, `state`, `act`, `since_s`, `left_s` and `emotions` (`happy`, `energy`, `social`, `stim`, `trust`, -1..1), see `docs/life.md`. `eyes` may also be `delighted`, `smitten`, `starry` or `dreamy` since 0.15.0; since 0.18.0 `friends`, the other Yorus in range (`docs/FRIENDS.md`): `[{id, rssi, seen_s, awake, firmware}]` |
| `show_menu` | `open`: boolean, `page`: `home` · `inbox` · `timers` · `tasks` (0.16.0 and later; `habits` before) · `games` (0.11.8 and later) · `settings` · `display` · `sound` · `behaviour` · `voice` · `connection` · `battery` · `about` | Open the menu on its home (the apps around Yoru), on an app (`inbox` opens the inbox view), on Settings or on one of its pages, or close it. Since 0.11.0; before, the menu was a ring and `inbox` one of its pages |
| `show_approval` | `id`: string, `tool`: string, `text`: string, `project`?: string, `seconds`: integer | Yoru asks before it acts for you, over the eyes, with Allow and Deny (see Confirmations). A second call replaces it |
| `hide_approval` | `id`: string | Take that card away: it was answered in the app, or gave up. An `id` that isn't shown is fine |
| `show_toast` | `text`: string, `seconds`: integer, `tone`?: `info` · `done` · `wait` · `error` | Since 0.7.0. One line above the bottom rim for a few seconds. A second call replaces it. Yoru Hub no longer sends it (since 2026-10-21) |
| `show_notification` | `id`, `source`?, `title`, `text`?, `urgency`?, `actions`? | Since 0.10.0. Put a notification in the inbox (see Inbox) |
| `hide_notification` | `id`: string | Since 0.10.0. Take it out of the inbox. An `id` that isn't there is fine |
| `clear_notifications` | none | Since 0.10.0. Empty the inbox |
| `set_timers` | `timers`: array | Since 0.11.0. Every timer and reminder (see Timers) |
| `set_tasks` | `tasks`: array | Since 0.16.0. The tasks the user gave the host (see Tasks); `set_habits` (0.11.0 to 0.15.1) is gone |
| `show_fetch` | `state`, `source`?, `id`? | Since 0.11.0. A connector call starts or ends: Yoru's errand (see Fetch) |

The host's tools (`show_approval`, `hide_approval`, `show_toast`, since 0.10.0 `show_notification`, `hide_notification`, `clear_notifications`, and since 0.11.0 `set_timers`, `show_fetch`, and since 0.16.0 `set_tasks`) are for the host only: Yoru Hub keeps them out of its MCP server.

## Confirmations

Before Yoru Hub's assistant does something with one of your services that changes it (sends a mail, creates an event, deletes something), it asks. Up to 0.11.4 the same card answered Claude Code's permission requests; since 0.11.5 it is only Yoru's. The host sends it to every connected Yoru with `show_approval`:

```json
{"id":"c3","tool":"Gmail: send","text":"To Marco: the meeting moves to 4","project":"Yoru","seconds":60}
```

- `text` is what it would do, at most 72 characters, UTF-8. `project` is where it comes from, shown with the seconds left.
- `seconds` is how long the host waits; the board shows it running down, and drops the card by itself once it's over.
- `always` (default false) adds Always besides Allow and Deny; `full` (default true) false means `text` was cut to fit, and the board then shows Deny and "Answer on the computer" instead of Allow.
- A tap sends the `approval` notification and closes the card:

```json
{"jsonrpc":"2.0","method":"approval","params":{"id":"c3","decision":"allow","allow":true}}
```

- `decision` is `allow`, `deny` or `always`; `allow` is true for allow and always.
- The first answer wins, from the app or the board; the host sends `hide_approval` to every board once it's over.
- On the board: the tool and `text` above Yoru, Deny and Allow below it, and a ring along the rim running the time down. The fonts are ASCII. An `id` is at most 47 bytes. While a card is up the screen takes no pokes, `show_menu` is refused and PWR doesn't put Yoru to sleep; Yoru wakes up, comes close and glances between the request and the buttons. `get_state` has `approval`, true while one is up.

## Listening after an answer

`voice/listen` with `"follow_up": true` (the host's "Keep listening after an answer") takes two more numbers since 0.11.6, both optional:

```json
{"jsonrpc":"2.0","id":7,"method":"voice/listen","params":{"follow_up":true,"wait_ms":8000,"pause_ms":700}}
```

- `wait_ms`: how long the board waits for speech before giving up (`utterance_end` `reason` `no_speech`), 1500 to 15000; 4000 without it.
- `pause_ms`: how long a pause ends what's being said, 200 to 3000 (2000 in 0.11.6); 400 without it (the wake word's). The VAD's own 500 ms come on top.
- Without the wake word only speech near the board counts: 350 ms of it, each frame at -50 dBFS or louder, gaps under 300 ms. Quieter or shorter speech leaves `speech` false.
- The board plays a soft tick (`listen`) and shows a ring around Yoru running out over `wait_ms`; the ring goes once speech starts. Giving up without speech plays a soft falling note (`listen_end`). Both are earcons, played with pet sounds off too.
- Older boards ignore the two numbers and wait 4 s.

## Finished sentences

Since 0.11.7 a host can judge for itself when a sentence is finished (Yoru Hub does with Smart Turn), instead of the board's short pause:

```json
{"jsonrpc":"2.0","method":"utterance_pause","params":{"id":12,"ms":2350}}
{"jsonrpc":"2.0","id":8,"method":"voice/pause","params":{"id":12,"pause_ms":1500}}
```

- `utterance_pause` comes at each pause once speech has started (wake word, host or follow-up, not push-to-talk): when the VAD's 500 ms hangover after the last speech is over. `ms` is how much audio was sent so far. It comes again after the next stretch of speech.
- `voice/pause` sets how long a pause ends utterance `id` (200 to 3000 ms after the hangover), if it's the one being heard; an older id is ignored. A host that judges pauses sends it right after `utterance_start` with a long one, then `voice/stop` as soon as it hears the sentence is finished; the board's own pause only ends it if the host says nothing.
- An older board answers `voice/pause` with `-32601` and never sends `utterance_pause`: its own pause ends every utterance.

## PWR

### Push-to-talk

Holding PWR for more than 0.4 s streams an utterance until it is let go: no wake word, and a pause doesn't end it. A shorter press does what it always did (sleep, wake, close). `utterance_start` then has `trigger` `button`, and the words go to Yoru as after the wake word. 0.9.0 to 0.11.4 also sent `to`, a coding agent's session or project; 0.11.5 sends none.

The stream lasts only while PWR is held: it ends when PWR is let go (`utterance_end` `reason` `released`) or after 60 s (`max_length`). `speech` is the VAD's opinion; a host transcribes a `button` utterance of 0.3 s or more anyway, and the board shows `thinking` after one. The wake word's chime plays when the board starts listening. The AXP2101's power-off on a long press of PWR is turned off, so a long press can't switch the board off mid-sentence; Battery → Power off in the menu does.

### The dark screen

After a minute asleep the screen goes dark: brightness 0, touch off and, since 0.12.0, the panel asleep. The board stays awake for the host: notifications, timers and the voice all keep working, out of sight. Only PWR lights it again: a tap, or holding it to talk. While dark, BOOT does nothing.

### Power (0.12.0)

On USB (or without a battery) the board runs as before: the `desk` profile. On battery it's `balanced`, or `saver` with the setting `power_saver`; `get_state`'s `battery.profile` says which.

- On battery the WiFi uses modem sleep (`WIFI_PS_MIN_MODEM`): a message from the host may take up to a beacon interval (about 100 ms) longer to arrive. During a conversation (voice not `idle`, or PWR held) it's `WIFI_PS_NONE`, as on USB. In `saver` with the screen dark it's `WIFI_PS_MAX_MODEM`, listening to every third beacon (about 300 ms).
- The screen's brightness is capped: 60% in `balanced`, 40% in `saver`. `brightness` keeps the value set.
- In `saver`, while Yoru sleeps the mics are off: no wake word, no `utterance_start` from it. PWR held and `voice/listen` turn them on again.
- **Hibernate:** on battery, after `hibernate` minutes of dark screen (0 never; 30 by default) with no timer running, no errand and no card up, the board stops the WiFi and light-sleeps. The host's connection drops and nothing reaches the board until it wakes: moved (the IMU's wake on motion), PWR or BOOT pressed, or USB plugged in. It then joins the WiFi again and lights the screen; a host reconnects as after any drop.

## Inbox

Since 0.10.0, with the `inbox` capability. The host watches the user's services (mail, calendar, issues...) and tells Yoru what is worth knowing. The board keeps an inbox of the latest 30, newest first; the host stays the source of truth (there is no `get_notifications`): it sends what is new, takes away what is done, and can send its whole inbox again on connect.

| Tool | Arguments | Effect |
|------|-----------|--------|
| `show_notification` | `id`: string (1 to 47 bytes), `source`?: string (at most 24 characters: "Gmail", "Calendar", "GitHub", "Yoru"), `title`: string (1 to 60), `text`?: string (at most 200, may be empty), `urgency`?: `low` · `normal` · `high` · `urgent` (default `normal`), `actions`?: array of at most 2 labels (at most 16 characters each: `["Open","Reply"]`) | Add it on top of the inbox, replacing the one with the same `id`. The oldest goes past 30. Sent again exactly as it is (same `source`, `title`, `text`, `urgency`, `actions`), it changes nothing: it stays where it was, read or not, and doesn't alert again |
| `hide_notification` | `id`: string | Take it out: it was dealt with elsewhere. An `id` that isn't there is fine |
| `clear_notifications` | none | Empty the inbox |

```json
{"id":"gmail:18c2f","source":"Gmail","title":"Anna: dinner on Friday?","text":"Are you free on Friday at 8? We could try the new place.","urgency":"normal","actions":["Reply","Snooze"]}
```

Texts are UTF-8 and the fonts ASCII, as on the cards: accented letters lose their accents, other characters show as `?`, and longer texts are cut with "...". An `id` over 47 bytes, an empty `title`, an unknown `urgency` or more than 2 `actions` is refused (`isError`).

How it arrives depends on `urgency`:

| `urgency` | On the board |
|---|---|
| `low` | Only in the inbox, unread: a dot on the rim at the top right |
| `normal` | Also a banner over the bottom rim for 5 s (its source and title; a tap opens it) with a soft chirp (one of Yoru's sounds, so silent with them off) |
| `high` | Also wakes a dimmed screen and Yoru comes to the glass and knocks once; a touch on Yoru then opens the inbox. The banner stays 8 s and the dot pulses |
| `urgent` | Like `high`, with the alarm earcon twice (it plays with Yoru's sounds off) and the banner for 12 s; it also lights the dark screen (PWR, The dark screen) |

Do not disturb on the board (setting `focus`) makes every notification but `urgent` arrive as `low`; the host's quiet hours are the host's to apply (it sends `low`). While the screen is dark only `urgent` shows; the rest wait in the inbox. Turned face down, nothing shows. A banner doesn't come over the menu or the views.

The inbox view opens from the banner, from the menu (Inbox), and from BOOT. It lists every notification with its source, title and age on the board ("now", "5 min", "2 h"), the unread ones marked. A tap opens one: the whole text, the `actions`, Dismiss and Open. Swipe right or down, or tap the title, to go back.

What the user does comes back as `notification_action`:

```json
{"jsonrpc":"2.0","method":"notification_action","params":{"id":"gmail:18c2f","action":"read"}}
```

| `action` | When | On the board |
|---|---|---|
| `read` | The notification was opened, the first time | It is read |
| `dismiss` | Dismiss | It leaves the inbox |
| `open` | Open: show it on the computer (the mail in the browser, the issue on GitHub) | It stays, read; the view closes |
| `"0"`, `"1"` | One of `actions`, by its index, as a string | It stays, read |

The host does what it means and may then `hide_notification` it.

## Timers

Since 0.11.0, with the `timers` capability. The host keeps the timers, the pomodoro and the reminders and sends the whole list whenever it changes (one is set, paused, resumed, cancelled, rings, is dismissed) and on connect. The board counts each one down from when the list came; the menu's Timers app shows them, the one that ends first first, and the ringing ones before all.

| Tool | Arguments | Effect |
|------|-----------|--------|
| `set_timers` | `timers`: array of at most 8 `{id, kind?, state, left_s, total_s, label?, at?}` | Replace the list. `id`: the host's number, or a string of at most 23 bytes; `kind`: `timer` (default) · `work` · `break` (a pomodoro's) · `reminder`; `state`: `run` · `pause` · `ring`; `left_s`: seconds left now (0 while ringing); `total_s`: its length, above 0; `label`: what it's for, at most 27 characters shown; `at`: a reminder's time, `"HH:MM"`. An empty array clears it |

```json
{"timers":[{"id":3,"kind":"timer","state":"run","left_s":272,"total_s":600,"label":"Pasta"},{"id":4,"kind":"reminder","state":"run","left_s":2520,"total_s":3600,"label":"Call Anna","at":"17:30"}]}
```

A timer that goes to `ring` (and wasn't ringing in the last list) makes Yoru jump like an alarm clock, with the alarm earcon and a line over the bottom rim with its label; the host still says it aloud. A host that never sends `set_timers` still has its snapshot's `timer` (Metrics): the Timers app shows that one alone, without its actions, and it rings the same way. Yoru Hub keeps its timers in `core/src/timers.rs` and sends the list from each board's session (`core/src/bridge/session.rs`), the snapshot's `timer` too for older firmware.

What the user does comes back as `timer_action`:

```json
{"jsonrpc":"2.0","method":"timer_action","params":{"id":"3","action":"pause"}}
```

| `action` | When |
|---|---|
| `pause` · `resume` | A tap on a running or paused timer (not on a reminder) |
| `dismiss` | A tap on a ringing one |
| `cancel` | A long press on one that isn't ringing |

`id` comes back as a string. The board shows the change at once; the host's next `set_timers` has the last word.

**The board's own timers (0.13.0).** The Timers app has round buttons for a new timer (1, 3, 5, 10, 15, 25, 30 or 60 minutes, or a pomodoro of 25 + 5 minutes four times), and spoken commands start them too without a host (Offline commands). The board keeps them (ids starting with `b`), rings them itself and, while it's ringing, a pomodoro goes on to its next phase. Without a host the board also rings the host's timers when they're up, and drops a ringing one after a minute. A host that is there is offered each of the board's own, once per connection and again after a pause, a resume or a new phase:

```json
{"jsonrpc":"2.0","method":"timer_new","params":{"id":"b3f09a1c2","kind":"work","state":"run","left_s":1412.5,"total_s":1500,"language":"it","pomodoro":{"work_s":1500,"break_s":300,"round":1,"rounds":4}}}
```

`label` comes when it has one, `pomodoro` only for a `work` or `break` phase, `language` is the board's (`it` or `en`), for what the host says when it rings. A host that takes it over puts it in its list with `was` set to the board's id, and the board drops its own copy then; offered again, the host updates the one it has instead of adding another. A host that doesn't know `timer_new` ignores it, and the timer stays the board's. Yoru Hub: `Timers::adopt` in `core/src/timers.rs`. Hibernation waits while any timer is there; the board's own don't survive a restart.

## Tasks

Since 0.16.0, with the `tasks` capability; it replaced the Habits app (0.11.0 to 0.15.1: `set_habits`, `habit_check`, the `habits` capability, gone). Yoru Hub keeps them in `core/src/assistant/tasks.rs`: jobs the user gave its assistant, in words, once or on a schedule, which the host does on its own when their time comes. The host sends the whole list whenever one changes (a run starting or ending included), every ten minutes, and on connect; the board only shows them.

| Tool | Arguments | Effect |
|------|-----------|--------|
| `set_tasks` | `tasks`: array of at most 8 `{id, name, what?, repeat?, at?, date?, days?, day?, every?, acting?, enabled?, next?, last?, result?, failed?, running?}` | Replace the list. `id`: at most 39 bytes; `name`: at most 32 characters shown; `what`: the job and `result`: what was done last time, 100 characters each; `repeat`: `once` (`date` `YYYY-MM-DD` and `at` `HH:MM`), `daily` (`at`, `days` 1 Monday to 7 Sunday, none for every day), `monthly` (`day` and `at`) or `hourly` (`every` hours); `acting`: `never` (reads and tells), `ask` (asks before acting, the default) or `free` (acts on its own); `next` and `last`: Unix seconds, 0 for none; `failed`: the last run went wrong; `running`: it is being done now |

```json
{"tasks":[{"id":"weekly-issues","name":"Weekly issues","what":"Sum up my open GitHub issues","repeat":"daily","at":"09:00","days":[1],"acting":"never","enabled":true,"next":1794300000,"last":1793700000,"result":"6 open, 2 stuck.","failed":false,"running":false}]}
```

The menu's Tasks app has a row for each, with a dot (green: went well, red: failed, amber: running, grey: off) and when it runs next; its bubble's badge counts the ones that failed. A tap opens one: when it runs, what to do, how far it may go, what was done last time, and "Do it now", which asks the host with `task_run` (only while a host is there):

```json
{"jsonrpc":"2.0","method":"task_run","params":{"id":"weekly-issues"}}
```

The row shows it as running until the host's next list. A run that ends well since the last list cheers Yoru up (`task` in `docs/life.md`). The board keeps the last list in NVS, so it shows after a restart and without a host; the Cloud voice's prompt lists them, to be done once the computer is back.

## Offline commands

Since 0.13.0. When no host has sent a snapshot for 3 s, what is said after the wake word (or with PWR held) is heard on the board, by ESP-SR's MultiNet 7 (`mn7_en`, English only: there is no Italian model), and nothing goes on `audio_up`. It listens for up to 6 s, the ring showing the voice's level, and does one of these (`commands.c`), with a line over the bottom rim saying what it did:

| Phrase | What it does |
|---|---|
| "set a timer for five minutes", "timer for five minutes", "five minute timer" | A timer on the board: 1–10, 12, 15, 20, 25, 30, 40 or 45 minutes, one hour, an hour and a half or two hours |
| "start a pomodoro", "start pomodoro", "focus time" | A pomodoro on the board |
| "stop the timer", "cancel the timer", "stop the alarm", "stop" | Stops the one ringing, or else cancels the one that ends first |
| "pause the timer"; "resume the timer", "continue the timer" | Pauses the first running timer; resumes the first paused one |
| "how much time is left", "how long is left" | Shows what is left on the first timer |
| "what time is it", "what is the time" | Shows the time on the board's clock |
| "go to sleep", "good night" | To sleep, as a tap of PWR |
| "lets play", "play a game"; "show my timers", "open timers"; "show my tasks", "open tasks"; "show my notifications", "open notifications" | Opens Games, Timers, Tasks or Notifications |
| "volume up", "louder"; "volume down", "quieter" | The volume by 15 |
| "hello there", "how are you" | Yoru waves |

Since 0.14.0 these phrases are only the fallback: with the services of the Cloud voice set and the WiFi up, what is said goes to them instead. Anything else gets a puzzled "hm?" and a line saying that without the Mac it knows only a few words. MultiNet takes about 13 KB of internal RAM and stays unloaded if less than 64 KB would be left. With a host there, everything goes to it as before.

## Cloud voice

Since 0.14.0, with the `cloud` capability (`main/cloud.c`; Yoru Hub: `core/src/bridge/cloud.rs`). When no host has sent a snapshot for 3 s and the WiFi is up, the board talks to online services itself, with the keys a host handed over: what is said after the wake word (or with PWR held) is recorded in PSRAM, 15 s at most (the ring along the rim runs it down while it listens; past it the utterance ends with `max_length`, whatever started it), and sent to a speech-to-text service; the text goes with Yoru's character, what it knows of the user and what the board knows now (date and time, timers, tasks, battery) to the brain; the answer is said by a voice, streamed to the speaker as it arrives. Then it listens for a follow-up without the wake word, as with a host. Since 0.14.1 a WiFi on its way back counts as up: woken from hibernating, what is said is recorded at once and sent when the WiFi has joined, waiting up to 10 s (then the error line says there's no internet). Only a network that isn't there or a wrong password leave it to the offline phrases. The last eight messages are kept for the next question, for ten minutes.

`cloud/set` is a method of its own, not a tool, so a model calling the board's tools never sees it. A host sends it on every handshake and whenever the services change; the board keeps them in NVS (namespace `cloud`) and writes the flash only when they differ. `{"clear": true}` forgets them. The answer is `cloud/get`'s.

```json
{"name":"Yoru","character":"You are friendly and to the point...","memory":"# What Yoru knows about you ...",
 "stt":{"url":"https://api.groq.com/openai/v1/audio/transcriptions","key":"gsk_...","model":"whisper-large-v3-turbo","language":"it"},
 "llm":{"api":"openai","url":"https://api.openai.com/v1/chat/completions","key":"sk-...","model":"gpt-6-luna","params":{"reasoning_effort":"none"}},
 "tts":{"api":"elevenlabs","url":"https://api.elevenlabs.io","key":"...","model":"eleven_v4_turbo","voice":"<voice_id>"},
 "search":{"url":"https://api.tavily.com/search","key":"tvly-..."}}
```

| Field | What it is |
|---|---|
| `stt` | An OpenAI-style `/audio/transcriptions` endpoint (Groq, OpenAI): the full `url`, `model`, `language` (optional) |
| `llm` | `api`: `openai` (Chat Completions at `url`, with `params` merged into each request) or `anthropic` (Messages at `url`, `max_tokens` 400) |
| `tts` | `api`: `elevenlabs` (`url` is the base; `voice` the voice id; PCM at 16 kHz) or `openai` (`url` the base, `/audio/speech`; `voice`; `format` `pcm` at `rate`, 24000 by default, or `wav`; `max_chars` per request, the text cut at sentences) |
| `search` | Optional (0.17.0 and later): web search through Tavily's `/search` (`url`, `key`). With it the brain may ask for a search (see below); without it the board says it can't search the web |
| `name`, `character`, `memory` | Optional: Yoru's name, a line on how it talks, what it knows of the user (a host sends 1,200 characters at most) |

The whole object must fit in 3,900 bytes. Every URL must be `https`; the certificates are ESP-IDF's bundle plus GlobalSign Root CA (`main/certs/extra_roots.pem`), which Groq's, Anthropic's and ElevenLabs' chains end in.

`cloud/get` (no params): `{"set":true,"stt":"whisper-large-v3-turbo","llm":"gpt-6-luna","tts":"eleven_v4_turbo","search":true,"ready":true}`, the models, whether a web search is set (0.17.0 and later) and whether it would answer now (services, WiFi); never the keys.

The brain's answer may carry tags, which are taken out before it is said and act on the board (`commands_tag()`): `[timer SECONDS]`, `[pomodoro]`, `[stop]`, `[pause]`, `[resume]`, `[volume up]`, `[volume down]`, `[open games|timers|tasks|inbox]` (`[habit NUMBER]` and `habits` until 0.15.1), `[sleep]` and `[face NAME]` (happy, excited, love, curious, proud, wink, surprised, sad, shy, sleepy, dance, wave, thinking).

- **Web search (0.17.0).** With `search` set, the prompt tells the brain it may reply with only `[search QUERY]` for anything recent or unknown. The board then calls Tavily (`include_answer`, three results), and asks the brain again with the results, as data, in the same turn; the brain's answer is said. The results are not kept in the history (only the question and the answer), a second search in the same turn is ignored, and the search connection is closed once it has answered, to spare internal RAM. A failed search is told to the brain, which says so kindly.
- **Speed.** Each service keeps one connection open between requests, and its TLS session to resume a new one; all three are opened (a `HEAD`) while the user is still talking. A fresh connection takes the board 1 to 4 s; a kept one a few milliseconds.
- **Failures** show in a line over the bottom rim (no internet, a key refused, too many requests) with the red ring; nothing is said.
- **Not while a host is there:** with one, everything goes to it as before. Without the services or the WiFi, the offline phrases above.
- `debug/cloud` `{"text": "..."}` answers a text as if it had been heard, for tests.

## Debugging Yoru's life

Since 0.15.0 (`docs/life.md`). `debug/life` takes any of:

- `state`: one of `lounge`, `watch`, `explore`, `play`, `dream`, `bored`, `seek`, `sulk`, `drowsy`, entered now, its cooldown ignored;
- `act`: an act's name (`firefly`, `dance`, `gaze`, `rest`...), played next;
- `feel`: an event's name (`stroke`, `drop`, `ignored`...), applied to the emotions;
- `speed`: how much faster the life clock runs (1 to 300; dwell times, cooldowns and the emotions' fading; the animations stay at their pace). A restart sets it back to 1.

It answers with `get_state`'s `life`, or `-32602` for an unknown name.

## Friends' hello

The ESP-NOW hello to other Yorus (`docs/FRIENDS.md`) is on at boot (off at boot in 0.19.0 and 0.19.1). Since 0.19.1 `debug/friends` `{"on"?: bool}` turns it on or off until the next restart, and answers `{"on", "friends"}` (`friends` as in `get_state`). It only takes effect while the WiFi has an address.

## Fetch

Since 0.11.0, with the `fetch` capability (Yoru Hub: `core/src/bridge/errands.rs`). While the host's assistant calls one of its connectors (an MCP tool: Gmail, Calendar, GitHub...), Yoru runs an errand: a portal in the connector's colour opens on the rim, Yoru dives in, and comes back with a parcel when the call is done.

| Tool | Arguments | Effect |
|------|-----------|--------|
| `show_fetch` | `state`: `start` · `done` · `failed`, `source`?: the connector's name (at most 24 bytes), `id`?: at most 23 bytes | `start`: Yoru goes; `done` or `failed` with the same `id`: that call ended. Without an `id` an end ends the oldest call out |

```json
{"state":"start","id":"t1","source":"Gmail"}
```

- A start shows only while Yoru is awake on the screen (no menu, view or card, not dark or face down, no finger on it) and idle or thinking in a conversation; the answer's text says `Yoru is on the errand` or `not shown`. Ends are always taken.
- Calls that start while Yoru is out join the same errand; it comes back once all of them ended, after a moment inside at least, or after 20 s.
- `source` picks the portal's colour and what is drawn in it (mail, calendar, drive, GitHub, Slack, Spotify, web and others by name; anything else its initial) and where on the upper rim it opens.
- `done` for at least one call: Yoru comes back with a parcel and opens it in sparkles. Only `failed`: empty-handed and sad. Nothing after 20 s: it just comes back.
- Opening the menu, a view or a card, or talking to it, calls Yoru back at once.
