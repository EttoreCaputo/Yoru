# Protocol v2 (Yoru)

Yoru speaks DualEye's protocol v2, copied from [DualEye's protocol](../../esp32s3-dualeye-pcmonitor/docs/protocol.md): the same frames, channels, JSON-RPC methods, audio streams and WiFi pairing. This page lists only what is different on Yoru; read DualEye's for the rest. When the two diverge, fix both until the shared parts move to a repository of their own.

Implementations: `main/link.c`, `main/link_frame.c`, `main/link_crypto.c`, `main/net.c`, `main/rpc.c` (board); `tools/yoru_link.py` (test scripts); `core/src/link.rs` and `wifi.rs` in [Companion Hub](https://github.com/EttoreCaputo/companion-hub) (host).

## Identity

`hello` and `ready` answer:

```json
{"protocol":2,"firmware":"0.3.0","idf":"v6.1","board":"yoru-amoled-1.75c","id":"7c0c5f4233e4","transport":"usb","max_payload":4096,"channels":["ctrl","metrics","log","audio_up","audio_down"],"capabilities":["tools","gaze","wifi","voice","speaker","agents"]}
```

- `board` is `yoru-amoled-1.75c`; a host tells Yoru from DualEye by it.
- `agents` (0.7.0 and later): the board has the agent tools and notifications below (`set_agents`, `show_toast`, `show_question`, `hide_question`, `show_approval` with `always` and `full`, the `answer` and `shortcut` notifications). A host sends them only to boards that list it; older ones get approvals only.
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
| `react` | `name`: `happy` · `excited` · `wink` · `proud` · `relieved` · `charged` · `dance` · `love` · `curious` · `thinking` · `focus` · `angry` · `hot` · `sleepy` · `yawn` · `tired` · `sigh` · `sad` · `surprised` · `scared` · `bored` · `look_around` · `blink` · `wave` · `point` · `shy` · `clap` · `stretch` · `dizzy` · `annoyed` · `falling` · `drowsy` · `lifted` | The eyes react for a few seconds, with the pet's sound for it. DualEye's `play_eyes` names, those these eyes can show. Refused during a conversation |
| `play_sound` | `name` | As on DualEye: the same sounds |
| `set_pet` | `sounds`, `react`, `hear`: booleans, at least one | As on DualEye |
| `get_pet` | none | As on DualEye, without `idle_scenes`. Since 0.6.0 also `needs` (`rest`, `food`, `fun`, `company`, `curiosity`, 0–1, 1 fully met), `activity` (what it is doing: `lounge`, `explore`, `forage`, `snack`, `ask`, `bounce`, `peekaboo`, `loop`, `daydream`, `sulk`, `dance`, `nap`, `sleep`, `wake`, `come`) and `alone_s` (seconds since the last sign of someone: a touch, a voice, input on the host). `mood` can also be `hungry` |
| `feed` | none | Since 0.6.0. A firefly drifts in from the rim and Yoru chases and eats it, or bats it away when it isn't hungry. An error while it sleeps, the menu or a request is up, or a conversation is on |
| `get_settings` | none | Every setting as `{"value":n,"min":n,"max":n}` under its name, plus `wake_word`: `brightness`, `night_bright`, `night_from`, `night_to`, `auto_dim`, `rotation`, `volume`, `pet_sounds`, `mic_muted`, `sleepy_after`, `asleep_after`, `focus`, `touch_react`, `motion_react`, `pet_react`, `pet_hear`, `eye_color`, `eye_style`, `wake_on`, `language`, `wake_sound`. `rotation` is 0–3 quarter turns or 4 to follow how the badge stands. `sleepy_after` and `asleep_after` count from the last sign of someone around (a touch, a voice, input on the host), 300 and 600 s by default since 0.6.0. The menu on the board shows the same |
| `set_setting` | `name`, `value`: integer | Change one setting within its range (switches are 0 or 1); it applies at once |
| `get_state` | none | Firmware, uptime, metrics state, `eyes` (the eyes' mood now), `brightness`, voice, the pet (with its `activity`), audio, UI load, memory, link counters, the WiFi, `battery` (`present`, `percent`, `mv`, `usb`, `charging`) and `motion` (gravity `g` and turn rate `dps` on the screen's axes, `lean_x`/`lean_y`, `upright`: the quarter turn it stands at, or -1 lying flat) |
| `show_menu` | `open`: boolean, `page`: `yoru` (its needs, since 0.6.0) · `display` · `sound` · `behaviour` · `voice` · `connection` · `battery` · `about` | Open the settings menu on the ring or on a page, or close it |
| `show_approval` | `id`: string, `tool`: string, `text`: string, `project`?: string, `seconds`: integer | Show a Claude Code permission request over the eyes with Allow and Deny (see below). A second call replaces it |
| `hide_approval` | `id`: string | Take that request away: it was answered in the app or the terminal, or gave up. An `id` that isn't shown is fine |
| `set_agents` | `sessions`: array, `quiet_min`: integer, `focus`: boolean | Since 0.7.0. What every coding agent session on the computer is doing (see Agents) |
| `show_toast` | `text`: string, `seconds`: integer, `tone`?: `info` · `done` · `wait` · `error` | Since 0.7.0. One line above the bottom rim for a few seconds: an agent finished, needs you, the plan's limits. A second call replaces it |
| `show_question` | `id`, `project`?, `seconds`, `questions` | Since 0.7.0. An agent's question with its choices (see Questions). A second call replaces it |
| `hide_question` | `id`: string | Since 0.7.0. Take that question away. An `id` that isn't shown is fine |

The agent tools (`show_approval`, `hide_approval`, `set_agents`, `show_toast`, `show_question`, `hide_question`) are for the host only: Companion Hub keeps them out of its MCP server.

## Approvals

Companion Hub holds Claude Code's (and Codex's) permission requests (the `PermissionRequest` hook) while the user turns on "Answer permissions in the app and on Yoru". Each one goes to every connected Yoru with `show_approval`:

```json
{"id":"p3","tool":"Bash","text":"cargo test --workspace","project":"companion-hub","seconds":120,"always":true,"full":true}
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
  {"id":"s2","agent":"codex","project":"companion-hub","state":"work","task":"Fix the hooks","since_s":40,"steps":[]}]}
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

DualEye's `set_face`, `set_rotation` (here the `rotation` setting), `show_text` and `set_eyes` are not there: one screen, no faces. Short messages go through `show_toast`.
