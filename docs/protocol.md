# Protocol v2 (Yoru)

Yoru speaks DualEye's protocol v2, copied from [DualEye's protocol](../../esp32s3-dualeye-pcmonitor/docs/protocol.md): the same frames, channels, JSON-RPC methods, audio streams and WiFi pairing. This page lists only what is different on Yoru; read DualEye's for the rest. When the two diverge, fix both until the shared parts move to a repository of their own.

Implementations: `main/link.c`, `main/link_frame.c`, `main/link_crypto.c`, `main/net.c`, `main/rpc.c` (board); `tools/yoru_link.py` (test scripts); `NotchBuddy/Sources/Yoru/` in the [Yoru app](https://github.com/EttoreCaputo/yoru-app) (host, since 2026-10-18); `core/src/link.rs` and `wifi.rs` in [Companion Hub](https://github.com/EttoreCaputo/companion-hub) (the earlier host). The host is moving back to Companion Hub, now Yoru Hub (`../yoru-hub`, `core/src/`), which becomes Yoru's personal assistant: see its `docs/assistant.md`. The inbox below is its firmware part (phase A4).

## Identity

`hello` and `ready` answer:

```json
{"protocol":2,"firmware":"0.3.0","idf":"v6.1","board":"yoru-amoled-1.75c","id":"7c0c5f4233e4","transport":"usb","max_payload":4096,"channels":["ctrl","metrics","log","audio_up","audio_down"],"capabilities":["tools","gaze","wifi","voice","speaker","agents","remote","inbox","timers","habits","fetch"]}
```

- `board` is `yoru-amoled-1.75c`; a host tells Yoru from DualEye by it.
- `agents` (0.7.0 and later): the board has the agent tools and notifications below (`set_agents`, `show_toast`, `show_question`, `hide_question`, `show_approval` with `always` and `full`, the `answer` and `shortcut` notifications). A host sends them only to boards that list it; older ones get approvals only.
- `remote` (0.9.0 and later): push-to-talk on PWR, drafts, chats and new sessions (see Remote): the `show_draft`, `hide_draft`, `set_chat` and `set_projects` tools, the `draft`, `chat` and `message` notifications, and `to` on `utterance_start`.
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

The same snapshot. Since 0.10.0 a snapshot needs only `pet` (the host's time and idle time): `cpu` and `gpu` are optional, and one with none of the three is still ignored. Since 0.8.0 Yoru reads only the host's time and idle time (night brightness, and the sleep times count from your last input on the computer) and the Claude plan's limits for the agents view; it shows none of the rest. A timer's `screen` is ignored.

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
| `get_state` | none | Firmware, uptime, metrics state, `eyes` (the eyes' mood now), `brightness`, voice, `activity` (what Yoru does: `lounge`, `call`, `nap`, `sleep`, `come`; it replaced `pet` in 0.8.0), audio, UI load, memory, link counters, the WiFi, `battery` (`present`, `percent`, `mv`, `usb`, `charging`) and `motion` (gravity `g` and turn rate `dps` on the screen's axes, `lean_x`/`lean_y`, `upright`: the quarter turn it stands at, or -1 lying flat), `agents` and, since 0.10.0, `inbox` (`count`, `unread`, `view`); since 0.11.0 `timers` (how many), `habits` (`count`, `done`) and `errand` (Yoru is on one) |
| `show_menu` | `open`: boolean, `page`: `home` · `inbox` · `timers` · `habits` · `settings` · `agents` · `display` · `sound` · `behaviour` · `voice` · `connection` · `battery` · `about` | Open the menu on its home (the apps around Yoru), on an app (`inbox` opens the inbox view), on Settings or on one of its pages (`agents` is the agents' settings), or close it. Since 0.11.0; before, the menu was a ring and `inbox` one of its pages |
| `show_approval` | `id`: string, `tool`: string, `text`: string, `project`?: string, `seconds`: integer | Show a Claude Code permission request over the eyes with Allow and Deny (see below). A second call replaces it |
| `hide_approval` | `id`: string | Take that request away: it was answered in the app or the terminal, or gave up. An `id` that isn't shown is fine |
| `set_agents` | `sessions`: array, `quiet_min`: integer, `focus`: boolean | Since 0.7.0. What every coding agent session on the computer is doing (see Agents) |
| `show_toast` | `text`: string, `seconds`: integer, `tone`?: `info` · `done` · `wait` · `error` | Since 0.7.0. One line above the bottom rim for a few seconds: an agent finished, needs you, the plan's limits. A second call replaces it |
| `show_question` | `id`, `project`?, `seconds`, `questions` | Since 0.7.0. An agent's question with its choices (see Questions). A second call replaces it |
| `hide_question` | `id`: string | Since 0.7.0. Take that question away. An `id` that isn't shown is fine |
| `show_notification` | `id`, `source`?, `title`, `text`?, `urgency`?, `actions`? | Since 0.10.0. Put a notification in the inbox (see Inbox) |
| `hide_notification` | `id`: string | Since 0.10.0. Take it out of the inbox. An `id` that isn't there is fine |
| `clear_notifications` | none | Since 0.10.0. Empty the inbox |
| `set_timers` | `timers`: array | Since 0.11.0. Every timer and reminder (see Timers) |
| `set_habits` | `habits`: array | Since 0.11.0. Today's habits (see Habits) |
| `show_fetch` | `state`, `source`?, `id`? | Since 0.11.0. A connector call starts or ends: Yoru's errand (see Fetch) |

The agent tools (`show_approval`, `hide_approval`, `set_agents`, `show_toast`, `show_question`, `hide_question`, since 0.9.0 `show_draft`, `hide_draft`, `set_chat`, `set_projects`, since 0.10.0 `show_notification`, `hide_notification`, `clear_notifications`, and since 0.11.0 `set_timers`, `set_habits`, `show_fetch`) are for the host only: Companion Hub keeps them out of its MCP server.

## Approvals

Companion Hub holds Claude Code's (and Codex's) permission requests (the `PermissionRequest` hook) while the user turns on "Answer permissions in the app and on Yoru". Each one goes to every connected Yoru with `show_approval`:

```json
{"id":"p3","tool":"Bash","text":"cargo test --workspace","project":"yoru-hub","seconds":120,"always":true,"full":true}
```

- `text` is what the tool would do: a command's first line, a file name, a pattern; at most 72 characters, UTF-8.
- `seconds` is how long the host waits before the agent asks in its own terminal; the board shows it running down, and drops the card by itself once it's over.
- `always` (0.7.0, default false): the agent offered rules to remember the choice (Claude Code's `permission_suggestions`). The card then has Always besides Allow and Deny.
- `full` (0.7.0, default true): `text` is the whole request. When it isn't (a long or multi-line command, cut to fit), the board doesn't let it be allowed blind: it shows Deny and "Answer on the computer" instead of Allow and Always.
- A tap sends the `approval` notification and closes the card:

```json
{"jsonrpc":"2.0","method":"approval","params":{"id":"p3","decision":"always","allow":true}}
```

- `decision` is `allow`, `deny` or `always` (0.7.0). `allow` is still sent for hosts that predate it: true for allow and always.
- The first answer wins, from the app, the board or the terminal; the host sends `hide_approval` to every board once the request is over, whoever answered.
- Firmware without the tools answers with an error (`-32602`, unknown tool); the host ignores it and the request is still answered in the app.
- On the board (0.5.4 and later): the tool's name and `text` above Yoru, Deny and Allow below it (Always under them since 0.7.0) with the project and the seconds left, and a ring along the rim running them down. The fonts are ASCII: accented letters lose their accents and other characters show as `?`. An `id` is at most 47 bytes. While a card is up the screen takes no pokes, `show_menu` is refused and PWR doesn't put Yoru to sleep; Yoru wakes up, shrinks and glances between the request and the buttons.

## Questions

Since 0.7.0. When an agent asks the user to choose (Claude Code's `AskUserQuestion`), the host holds it (a waiting `PreToolUse` hook) and sends it to every Yoru:

```json
{"id":"q4","project":"yoru","seconds":120,"questions":[
  {"header":"Auth","question":"Which login method should we use?","multi":false,"options":["OAuth","Magic link","Password"]},
  {"header":"Extras","question":"What else do you want?","multi":true,"options":["Remember me","2FA"]}]}
```

- 1 to 4 questions, each with 2 to 4 `options`. `header` is at most 12 characters, an option at most 40, a question at most 120. The host sends a question it can't fit (too many, too long) to the terminal instead of the board.
- The card shows one question at a time with its options; `multi` ones toggle and have Send. A small "Terminal" link gives up and lets the agent ask in its terminal.
- The answer is the indexes picked for each question, in order:

```json
{"jsonrpc":"2.0","method":"answer","params":{"id":"q4","answers":[[0],[0,1]]}}
{"jsonrpc":"2.0","method":"answer","params":{"id":"q4","terminal":true}}
```

- As with approvals, the first answer wins and the host sends `hide_question` once it's over. The card goes by itself when `seconds` run out.

## Agents

Since 0.7.0. The host follows every coding agent session it has hooks for (Claude Code, Codex, Gemini CLI, Cursor, OpenCode) and keeps Yoru up to date with `set_agents`: on every change (at most 4 times a second), on connect, and every 10 s even when nothing changed. Without one for 30 s the board forgets the sessions.

```json
{"quiet_min":0,"focus":true,"sessions":[
  {"id":"s1","agent":"claude","project":"yoru","state":"wait","task":"Add the agents view","since_s":312,
   "steps":[{"kind":"edit","text":"main/agents.c","add":42,"del":3,"ok":true},{"kind":"run","text":"idf.py build"}]},
  {"id":"s2","agent":"codex","project":"yoru-hub","state":"work","task":"Fix the hooks","since_s":40,"steps":[]}]}
```

- `sessions`: at most 6, the most urgent first (`wait`, `error`, `work`, `done`, `idle`), then the most recent.
  - `id`: the host's short id for the session, at most 15 bytes. The board sends it back in `shortcut`.
  - `agent`: `claude`, `codex`, `gemini`, `cursor`, `opencode`, or another short name.
  - `project`: the folder's name, at most 40 characters.
  - `state`: `work` (a turn is running), `wait` (it needs you: a permission or a question), `error` (the last turn failed), `done` (the turn ended), `idle` (started, nothing asked yet).
  - `task`: the prompt that started the turn, at most 72 characters.
  - `since_s`: seconds since the state began.
  - `steps`: the last 3, oldest first. `kind` is `read`, `edit`, `write`, `run`, `search`, `web`, `agent`, `ask` or `other`; `text` at most 48 characters; `add` and `del` the lines added and removed by an edit; `ok` false when the step failed.
- `quiet_min`: minutes left of "do not disturb agents", 0 when off. While quiet the board plays no agent sounds and doesn't knock; cards still come.
- `focus`: the host can bring a session's terminal to the front (and interrupt it).
- The whole call stays under the 4096-byte payload.

The board shows them on the rim (one segment per session, coloured by state), in a ticker over the bottom rim, and in the agents view (swipe up from the bottom edge, or the BOOT button), where each session is a little Yoru in its agent's colour acting out its state. The colour comes from `agent`: `claude` orange, `gemini` yellow, `codex`/`gpt`/`openai` blue, `cursor` green, `opencode` violet, `copilot` white, `windsurf` teal, `aider` pink, `qwen` indigo, `grok` grey; any other name is pearl.

### Shortcuts

The board asks the host for an action with the `shortcut` notification:

```json
{"jsonrpc":"2.0","method":"shortcut","params":{"name":"focus","session":"s1"}}
```

| `name` | `session` | The host |
|---|---|---|
| `focus` | optional | Brings that session's terminal to the front; without one, the session waiting the longest, else the latest |
| `interrupt` | required | Brings the terminal to the front and presses Esc in it, which stops the agent's turn |
| `quiet` | none | Turns "do not disturb agents" on for 60 minutes, or off when it's on; the next `set_agents` carries the new `quiet_min` |

The host answers nothing; what changes shows in the next `set_agents`. A host that can't do it (no terminal recorded, no permission to press keys) says so with `show_toast` (`tone` `error`).

## Remote

Since 0.9.0, with the `remote` capability. Yoru is a remote for the agents: hold PWR and talk to a session, open a session's chat on the board, or start a new one in a project.

### Push-to-talk

Holding PWR for more than 0.4 s streams an utterance until it is let go: no wake word, and a pause doesn't end it. A shorter press does what it always did (sleep, wake, close). `utterance_start` then has `trigger` `button` and, when the board knows where the words go, `to`:

```json
{"id":9,"trigger":"button","rate":16000,"format":"s16le","to":{"session":"s1"}}
{"id":10,"trigger":"button","rate":16000,"format":"s16le","to":{"project":"p2","agent":"claude"}}
```

- `{"session": id}`: for that session, an id from `set_agents`. The board sends it while that session's chat is open.
- `{"project": id, "agent": name}`: start a new session of that agent in that project, ids and names from `set_projects`.
- No `to`: the host picks by the words, as for the wake word, except that without a leading word it goes to the voice assistant, not to a waiting session.

The stream lasts only while PWR is held: it ends when PWR is let go (`utterance_end` `reason` `released`) or after 60 s (`max_length`). `speech` is the VAD's opinion; a host transcribes a `button` utterance of 0.3 s or more anyway, and the board shows `thinking` after one. The wake word's chime plays when the board starts listening. The AXP2101's power-off on a long press of PWR is turned off, so a long press can't switch the board off mid-sentence; Battery → Power off in the menu does.

### The dark screen

After a minute asleep the screen goes dark: brightness 0, touch off. The board stays awake for the host: `set_agents`, cards, drafts and the voice all keep working, out of sight. Only PWR lights it again: a tap, or holding it to talk. While dark, BOOT does nothing.

### Drafts

What is said to an agent (with PWR or the wake word) isn't sent until it has been read: the host transcribes it and shows it with `show_draft`.

| Tool | Arguments | Effect |
|------|-----------|--------|
| `show_draft` | `id`: string (at most 47 bytes), `text`: string (at most 400 characters), `to`: string (at most 40), `seconds`: integer | The words over the eyes with Send and Cancel, and where they go (`to`: "yoru", "New: yoru (claude)"). A second call replaces it; it goes by itself when `seconds` run out, which is a cancel |
| `hide_draft` | `id`: string | Take that draft away. An `id` that isn't shown is fine |

The board answers with `draft`:

```json
{"jsonrpc":"2.0","method":"draft","params":{"id":"d3","action":"send"}}
```

`action` is `send`, `cancel`, or `redo`: PWR was held over the draft to say it again. A `redo` is followed by an `utterance_start` without `to`: the host drops the old words and sends the new ones where the draft was going.

### Chats

In the agents view a tap on a session opens its chat: the conversation, your messages and the agent's, as bubbles that scroll, the newest at the bottom, and "hold PWR to talk" under it. There are no buttons: holding a session in the view still interrupts it. The board tells the host which chat is open:

```json
{"jsonrpc":"2.0","method":"chat","params":{"session":"s1","open":true}}
```

While one is open the host fills it with `set_chat`, at once and whenever the conversation changes:

```json
{"session":"s1","messages":[{"from":"you","text":"Add the agents view"},{"from":"agent","text":"Done: the view is in agents_ui.c."}]}
```

| Tool | Arguments | Effect |
|------|-----------|--------|
| `set_chat` | `session`: string, `messages`: array of `{"from": "you" \| "agent", "text"}`, the oldest first | The latest messages of that session's conversation: at most 12, an agent's last message at most 1200 characters and the others at most 400, about 3000 in all. Only what was said, as plain text: no tool calls or results, no Markdown marks, the line breaks kept. The host reads Claude Code's transcript; for other agents it has the prompts and replies its hooks saw |

Text the board has as it is, with no draft, comes as `message` (0.9.0 sent it for its quick replies; 0.9.2 sends none, and hosts still take it):

```json
{"jsonrpc":"2.0","method":"message","params":{"session":"s1","text":"Continue"}}
```

The host delivers a `message`, and a draft that was sent, as it delivers a spoken prompt: typed into the session's terminal, else `claude -p … --resume` in its folder. It says how it went with `show_toast`.

### New sessions

The host lists where a new session can start, on connect and when the list changes:

| Tool | Arguments | Effect |
|------|-----------|--------|
| `set_projects` | `agents`: array of names (at most 4), `projects`: array of `{"id","name"}` (at most 8, the most recent first) | The "New" page of the agents view: the agents whose command line is installed, and the projects. `id` at most 15 bytes, `name` at most 40 characters |

In the agents view "New" opens the list; pick an agent and a project, then hold PWR and say what to do. The draft's Send starts the agent in that folder, in a new terminal window, with the words as its first prompt.

DualEye's `set_face`, `set_rotation` (here the `rotation` setting), `show_text` and `set_eyes` are not there: one screen, no faces. Short messages go through `show_toast`.

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
| `low` | Only in the inbox, unread: a dot on the rim at the top right, away from the agents' dots |
| `normal` | Also a banner over the bottom rim for 5 s (its source and title; a tap opens it) with a soft chirp (one of Yoru's sounds, so silent with them off) |
| `high` | Also wakes a dimmed screen and Yoru comes to the glass and knocks once; a touch on Yoru then opens the inbox. The banner stays 8 s and the dot pulses |
| `urgent` | Like `high`, with the alarm earcon twice (it plays with Yoru's sounds off) and the banner for 12 s; it also lights the dark screen (Remote, The dark screen) |

Do not disturb on the board (setting `focus`) and the agents' quiet (`quiet_min` above 0) make every notification but `urgent` arrive as `low`. While the screen is dark only `urgent` shows; the rest wait in the inbox. Turned face down, nothing shows. A banner doesn't come over the menu, the views or the cards.

The inbox view opens from the banner, from the menu (Inbox), and from BOOT when something is unread and no agent waits for you (otherwise BOOT does what it did). It lists every notification with its source, title and age on the board ("now", "5 min", "2 h"), the unread ones marked. A tap opens one: the whole text, the `actions`, Dismiss and Open. Swipe right or down, or tap the title, to go back.

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
