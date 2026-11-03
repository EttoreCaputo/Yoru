# Yoru's own life

What Yoru does when nothing else has its attention, since 0.15.0 (`main/life*.c`). The design follows Anki's Vector, whose source is public (`os-vector/wire-os-victor`: `engine/moodSystem`, `highLevelAI.json`, `emotionevents/*.json`), with Shimeji's weighted next behaviours and the scoring of The Sims for picking acts.

Vector's own notes warn against letting emotions drive everything: they ran out of animations for every mix of feelings and lost control of the behaviour. Here, as in Vector after that lesson, the emotions only weigh and colour; explicit transitions with conditions and cooldowns decide.

## Layers

From the quickest to the slowest:

1. **Interrupts** (`life.c`). They always come first: something waits for you (it comes to the glass and knocks), a greeting when you're back after 15 minutes or in the morning, a nap when nobody is around, sleep, coming back to you when touched or called. Touches, voice, the menu, a card and the reactions in `behaviour.c` take the eyes away from it altogether. When one is over, life picks up where it left off; after a touch, a greeting or an answered call it picks up **with you** (Vector's resume suggestions).
2. **Emotions** (`life_mood.c`).
3. **States** (`life_mind.c`).
4. **Acts** (`life_acts.c`), on the ball or up close.

All of it runs in the LVGL task, every 100 ms, and only sets targets for `eyes.c`.

## Emotions

Five dimensions from -1 to 1:

| Emotion | + | - | Settles at |
|---|---|---|---|
| `happy` | happy | sad | 0.15 + a quarter of `trust`, lower when alone |
| `energy` | lively | tired | the hour of the day (low at night, a dip after lunch), minus 0.06 for each hour awake (at most 0.4), lower still when drowsy or the battery is low |
| `social` | in company | lonely | 0.15 with someone around (touched, talked or used the computer in the last minute), down to -0.85 after 45 minutes alone |
| `stim` | excited | bored | 0, -0.2 at night |
| `trust` | at ease | wary | doesn't settle: it moves slowly and is kept in NVS (`life`/`trust`) |

Each event pushes some of them (the `FEELS` table): a stroke is happy +0.35, company +0.5, excitement +0.1, trust +0.01; a drop is happy -0.5, excitement +0.8, trust -0.03; being ignored when it called is happy -0.3, company -0.4. The events: `poke`, `stroke`, `hold`, `talk`, `slap`, `shake`, `drop`, `lift`, `played` (a game), `timer` (one rang), `habit` (one ticked off), `woke`, `greeted`, `ignored`, `fun` (it had fun on its own), `sound`, `plugged`. Time together raises trust by 0.004 an hour.

A push fades back to where the emotion settles along a curve of its own (Vector's TimeRatio graphs): excitement in four minutes, company in ten, cheer in fifteen, energy in twenty.

## States

| State | What it is | Lasts | Comes back after | Do not disturb |
|---|---|---|---|---|
| `lounge` | floating about near you | 45–120 s | — | yes |
| `watch` | up at the glass, only its eyes, with you | 20–50 s | 45 s | yes |
| `explore` | the edges, the corners, a firefly | 40–90 s | 60 s | yes |
| `play` | on its own, for fun | 25–55 s | 2 min | no |
| `dream` | slow, eyes on the sky it came from | 45–120 s | 2 min | yes |
| `bored` | sighs, looks away | 30–80 s | 1 min | no |
| `seek` | lonely: at the glass, looking for you | 12–25 s | 10 min | no |
| `sulk` | looked for you and nobody came: off to a side, turned away | 30–60 s | 15 min | no |
| `drowsy` | yawns, nods off | 1–3 min | 1 min | yes |

Each has a look (the face it rests with, how far and how fast it drifts), a repertoire of acts with weights, an act it starts with (bored starts with a sigh, drowsy with a yawn, seek with a search), and weighted ways out. The weights are scaled by the emotions: `weight × max(0.05, 1 + k × emotion)` for each of up to two modifiers, where `night` counts as 1 at night and -1 by day. Lounging, for example, goes on to `explore` ×(1 + 0.8 stim)(1 + 0.8 energy), to `play` ×(1 + happy)(1 + stim), to `drowsy` ×(1 - 1.5 energy).

Some ways out don't wait for the state to end (Vector's interrupting transitions), checked between two acts:

- energy below -0.55: `drowsy`;
- company below -0.6 while lounging, bored, dreaming or exploring: `seek`;
- excitement above 0.65 with cheer above 0.3 and some energy, while lounging or bored: `play`;
- excitement below -0.6 while lounging, exploring or dreaming: `bored`;
- Do not disturb in a state that doesn't fit it: `lounge`.

Drowsy, with nobody around for the "sleepy after" setting and energy below -0.6, it asks for a nap, and from there it falls asleep as before.

## Acts

Between two acts it waits as long as the state says (from under a second when playing to nine seconds when dreaming). An act isn't picked again until its cooldown is over, and one of the last three is less likely. Under Do not disturb the loud ones are left out and no act makes a sound.

On the ball: `drift`, `look` (left, right, up), `tilt`, `stretch`, `sneeze`, `hiccup`, `peek` (out past the edge), `sniff` (a corner, little hops), `firefly`, `dance`, `loop`, `bounce` (off the walls), `stars`, `hum`, `sigh`, `yawn`, `nod` (nodding off, then a jolt), `sulk`.

**The firefly** is the one thing Yoru knows from home: a flickering green-gold light wanders over the screen, Yoru follows it with its eyes and its whole body, its eyes turn to stars, and it pounces (two times in three) with a clap and a giggle; otherwise it flies off upwards and Yoru goes "aww".

## Up close

Since 0.15.0 Yoru can come right up to the glass, so near that only its eyes show: big, light on the black (the pearl of the sphere; on the ball they are dark holes in it and would vanish on black). The ball comes forward, blinks, and the eyes are there; leaving, they blink and the ball floats back from the glass. `eyes_close_up()` in `eyes.c`.

The eyes up close are drawn with a few LVGL shapes (rounded boxes, triangles, discs, lines, arcs) instead of the shaded sphere, so they cost less to draw and need no PSRAM, and they can take shapes the small eyes can't:

- the pill with its lids, as on the ball (every mood), the smile cut from below;
- `delighted`: `> <`, squeezed with joy;
- `smitten`: hearts, beating;
- `starry`: stars, turning and twinkling;
- `dizzy`: rings turning round;
- one eye shut (`eyes_wink()`), cross-eyed (`eyes_cross()`).

Moves move the pair: a hop hops it, nearer makes the eyes bigger, and they can leave the screen and come back. The acts up close: `c_drift`, `c_gaze` (at you, with a blink), `c_blink` (the cat's slow blink), `c_wink`, `c_tilt`, `c_hearts`, `c_starry`, `c_tricks` (cross-eyed, eyes rolled round, then `> <` and a giggle), `c_scan` (inspecting the glass, a tap on it), `c_peekaboo` (popping up from below the screen), `c_sneak` (sliding in from a side, a look round, away again), `c_bounce`, `c_doze`, `c_search` (looking for you, left and right, a whimper), `c_sad`.

The `watch` and `seek` states live up close: they stay at the glass between their acts. In the others an act up close is a one-off, and the ball floats back after it. A gesture (the hands), a status, sleep, the menu, a card or a conversation take it back to the ball at once.

## Trying it

`debug/life` (docs/protocol.md) forces a state or an act, pushes an emotion with an event, and runs the life clock up to 300 times faster, so a whole day's cycle passes in minutes. `get_state` has `life`: the state, the act, how long it has been in it and how long it has left, whether it is up close, and the emotions. Each change of state is logged with its reason (`mind: lounge -> explore (time)`), each event with the emotions after it.

To change how Yoru behaves, change the tables: `FEELS`, `ENERGY_BY_HOUR` and the decay curves in `life_mood.c`; `STATES`, the `*_ACTS` and `*_WAYS` tables and `urgent()` in `life_mind.c`; `ACTS` (cooldowns, flags) in `life_acts.c`.
