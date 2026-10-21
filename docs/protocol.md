# Protocol v2 (Yoru)

Yoru speaks DualEye's protocol v2, copied from [DualEye's protocol](../../esp32s3-dualeye-pcmonitor/docs/protocol.md): the same frames, channels, JSON-RPC methods, audio streams and WiFi pairing. This page lists only what is different on Yoru; read DualEye's for the rest. When the two diverge, fix both until the shared parts move to a repository of their own.

Implementations: `main/link.c`, `main/link_frame.c`, `main/link_crypto.c`, `main/net.c`, `main/rpc.c` (board); `tools/yoru_link.py` (test scripts); `NotchBuddy/Sources/Yoru/` in the [Yoru app](https://github.com/EttoreCaputo/yoru-app) (host, since 2026-10-18); `core/src/link.rs` and `wifi.rs` in [Companion Hub](https://github.com/EttoreCaputo/companion-hub) (the earlier host). The host is moving back to Companion Hub, now Yoru Hub (`../yoru-hub`, `core/src/`), which becomes Yoru's personal assistant: see its `docs/assistant.md`. The inbox below is its firmware part (phase A4).

## Identity

`hello` and `ready` answer:

```json
{"protocol":2,"firmware":"0.3.0","idf":"v6.1","board":"yoru-amoled-1.75c","id":"7c0c5f4233e4","transport":"usb","max_payload":4096,"channels":["ctrl","metrics","log","audio_up","audio_down"],"capabilities":["tools","gaze","wifi","voice","speaker","inbox","timers","habits","fetch"]}
```

- `board` is `yoru-amoled-1.75c`; a host tells Yoru from DualEye by it.
- `agents` and `remote` (0.7.0 to 0.11.4): the coding agents' tools and notifications and the remote for them. 0.11.5 removed them, with the agents' questions, drafts, chats and `shortcut` (`show_approval` stays, for Yoru's own confirmations): a host must not send those tools to a board without the capabilities.
- `inbox` (0.10.0 and later): the inbox (see Inbox): the `show_notification`, `hide_notification` and `clear_notifications` tools and the `notification_action` notification.
- `timers` (0.11.0 and later): the menu's Timers app (see Timers): the `set_timers` tool and the `timer_action` notification.
- `habits` (0.11.0 and later): the menu's Habits app (see Habits): the `set_habits` tool and the `habit_check` notification.
- `fetch` (0.11.0 and later): Yoru's errands (see Fetch): the `show_fetch` tool.
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

The same snapshot. Since 0.10.0 a snapshot needs only `pet` (the host's time and idle time): `cpu` and `gpu` are optional, and one with none of the three is still ignored. Since 0.8.0 Yoru reads only the host's time and idle time (night brightness, and the sleep times count from your last input on the computer); it shows none of the rest. A timer's `screen` is ignored.

## Board tools

Settings are kept in NVS and survive a reboot.

| Tool | Arguments | Effect |
|------|-----------|--------|
| `set_brightness` | `percent`: 5–100 | How bright the eyes glow while awake. Asleep they dim by themselves (setting `auto_dim`) |
| `set_mic` | `muted`, `wake_sound`: booleans, at least one | As on DualEye. Muted, the mic isn't read: no wake word, nothing heard in the room |
| `set_wake_word` | `word`: one of `voice.wake_words` (`alexa`, `hiesp`) | As on DualEye. Yoru's own word comes in M6 |
| `set_volume` | `percent`: 0–100 | Speaker volume, with the `volume` sound at the new level |
| `react` | `name`: `happy` · `excited` · `wink` · `proud` · `relieved` · `charged` · `dance` · `love` · `curious` · `thinking` · `focus` · `angry` · `hot` · `sleepy` · `yawn` · `tired` · `sigh` · `sad` · `surprised` · `scared` · `bored` · `look_around` · `blink` · `wave` · `point` · `shy` · `clap` · `stretch` · `dizzy` · `annoyed` · `falling` · `drowsy` · `lifted` | The eyes react for a few seconds, with Yoru's sound for it. DualEye's `play_eyes` names, those these eyes can show. Refused during a conversation |
| `play_sound` | `name` | As on DualEye: the same sounds |
| `get_settings` | none | Every setting as `{"value":n,"min":n,"max":n}` under its name, plus `wake_word`: `brightness`, `night_bright`, `night_from`, `night_to`, `auto_dim`, `rotation`, `volume`, `pet_sounds`, `mic_muted`, `sleepy_after`, `asleep_after`, `focus`, `touch_react`, `motion_react`, `pet_hear` (turns to sudden sounds), `eye_color`, `eye_style`, `wake_on`, `language`, `wake_sound`, `color_24bit` (24-bit colour instead of 16; Yoru restarts to apply it). `rotation` is 0–3 quarter turns or 4 to follow how the badge stands. `sleepy_after` and `asleep_after` count from the last sign of someone around (a touch, a voice, input on the host), 300 and 600 s by default since 0.6.0. The menu on the board shows the same |
| `set_setting` | `name`, `value`: integer | Change one setting within its range (switches are 0 or 1); it applies at once |
| `get_state` | none | Firmware, uptime, metrics state, `eyes` (the eyes' mood now), `brightness`, voice, `activity` (what Yoru does: `lounge`, `call`, `nap`, `sleep`, `come`; it replaced `pet` in 0.8.0), audio, UI load, memory, link counters, the WiFi, `battery` (`present`, `percent`, `mv`, `usb`, `charging`) and `motion` (gravity `g` and turn rate `dps` on the screen's axes, `lean_x`/`lean_y`, `upright`: the quarter turn it stands at, or -1 lying flat) and, since 0.10.0, `inbox` (`count`, `unread`, `view`); since 0.11.0 `timers` (how many), `habits` (`count`, `done`) and `errand` (Yoru is on one) |
| `show_menu` | `open`: boolean, `page`: `home` · `inbox` · `timers` · `habits` · `settings` · `display` · `sound` · `behaviour` · `voice` · `connection` · `battery` · `about` | Open the menu on its home (the apps around Yoru), on an app (`inbox` opens the inbox view), on Settings or on one of its pages, or close it. Since 0.11.0; before, the menu was a ring and `inbox` one of its pages |
| `show_approval` | `id`: string, `tool`: string, `text`: string, `project`?: string, `seconds`: integer | Yoru asks before it acts for you, over the eyes, with Allow and Deny (see Confirmations). A second call replaces it |
| `hide_approval` | `id`: string | Take that card away: it was answered in the app, or gave up. An `id` that isn't shown is fine |
| `show_toast` | `text`: string, `seconds`: integer, `tone`?: `info` · `done` · `wait` · `error` | Since 0.7.0. One line above the bottom rim for a few seconds. A second call replaces it |
| `show_notification` | `id`, `source`?, `title`, `text`?, `urgency`?, `actions`? | Since 0.10.0. Put a notification in the inbox (see Inbox) |
| `hide_notification` | `id`: string | Since 0.10.0. Take it out of the inbox. An `id` that isn't there is fine |
| `clear_notifications` | none | Since 0.10.0. Empty the inbox |
| `set_timers` | `timers`: array | Since 0.11.0. Every timer and reminder (see Timers) |
| `set_habits` | `habits`: array | Since 0.11.0. Today's habits (see Habits) |
| `show_fetch` | `state`, `source`?, `id`? | Since 0.11.0. A connector call starts or ends: Yoru's errand (see Fetch) |

The host's tools (`show_approval`, `hide_approval`, `show_toast`, since 0.10.0 `show_notification`, `hide_notification`, `clear_notifications`, and since 0.11.0 `set_timers`, `set_habits`, `show_fetch`) are for the host only: Yoru Hub keeps them out of its MCP server.

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

## PWR

### Push-to-talk

Holding PWR for more than 0.4 s streams an utterance until it is let go: no wake word, and a pause doesn't end it. A shorter press does what it always did (sleep, wake, close). `utterance_start` then has `trigger` `button`, and the words go to Yoru as after the wake word. 0.9.0 to 0.11.4 also sent `to`, a coding agent's session or project; 0.11.5 sends none.

The stream lasts only while PWR is held: it ends when PWR is let go (`utterance_end` `reason` `released`) or after 60 s (`max_length`). `speech` is the VAD's opinion; a host transcribes a `button` utterance of 0.3 s or more anyway, and the board shows `thinking` after one. The wake word's chime plays when the board starts listening. The AXP2101's power-off on a long press of PWR is turned off, so a long press can't switch the board off mid-sentence; Battery → Power off in the menu does.

### The dark screen

After a minute asleep the screen goes dark: brightness 0, touch off. The board stays awake for the host: notifications, timers and the voice all keep working, out of sight. Only PWR lights it again: a tap, or holding it to talk. While dark, BOOT does nothing.

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

Since 0.11.0, with the `timers` capability. The host keeps the timers, the pomodoro and the reminders (Yoru Hub's `core/src/timers.rs`) and sends the whole list whenever it changes (one is set, paused, resumed, cancelled, rings, is dismissed) and on connect. The board counts each one down from when the list came; the menu's Timers app shows them, the one that ends first first, and the ringing ones before all.

| Tool | Arguments | Effect |
|------|-----------|--------|
| `set_timers` | `timers`: array of at most 8 `{id, kind?, state, left_s, total_s, label?, at?}` | Replace the list. `id`: the host's number, or a string of at most 23 bytes; `kind`: `timer` (default) · `work` · `break` (a pomodoro's) · `reminder`; `state`: `run` · `pause` · `ring`; `left_s`: seconds left now (0 while ringing); `total_s`: its length, above 0; `label`: what it's for, at most 27 characters shown; `at`: a reminder's time, `"HH:MM"`. An empty array clears it |

```json
{"timers":[{"id":3,"kind":"timer","state":"run","left_s":272,"total_s":600,"label":"Pasta"},{"id":4,"kind":"reminder","state":"run","left_s":2520,"total_s":3600,"label":"Call Anna","at":"17:30"}]}
```

A timer that goes to `ring` (and wasn't ringing in the last list) makes Yoru jump like an alarm clock, with the alarm earcon and a line over the bottom rim with its label; the host still says it aloud. A host that never sends `set_timers` still has its snapshot's `timer` (Metrics): the Timers app shows that one alone, without its actions, and it rings the same way.

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

## Habits

Since 0.11.0, with the `habits` capability. The host keeps the user's habits, how many times each is done today and the streaks, starts each day afresh at midnight, and sends today's list whenever it changes and on connect.

| Tool | Arguments | Effect |
|------|-----------|--------|
| `set_habits` | `habits`: array of at most 8 `{id, name, done?, goal?, streak?, color?}` | Replace the list. `id`: at most 23 bytes; `name`: at most 24 characters shown; `done`: today's count (0 by default, kept within 0..`goal`); `goal`: times a day, 1 to 99 (1 by default); `streak`: days in a row it was done, today counting once it is; `color`: `"#RRGGBB"`, otherwise one of the board's in turn |

```json
{"habits":[{"id":"water","name":"Water","done":3,"goal":8,"streak":12,"color":"#40C8E0"},{"id":"walk","name":"Walk","done":0,"goal":1,"streak":4}]}
```

The menu's Habits app shows the first three as rings around how many are done today, and a row for each. A tap adds one (up to `goal`), a long press takes one back; finishing one adds today to its streak on the board, undoing it takes it away. Each change comes back as `habit_check`, with the new count:

```json
{"jsonrpc":"2.0","method":"habit_check","params":{"id":"water","done":4}}
```

## Fetch

Since 0.11.0, with the `fetch` capability. While the host's assistant calls one of its connectors (an MCP tool: Gmail, Calendar, GitHub...), Yoru runs an errand: a portal in the connector's colour opens on the rim, Yoru dives in, and comes back with a parcel when the call is done.

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
