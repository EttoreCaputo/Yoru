# Yoru's own life

What Yoru does when nothing else has its attention, since 0.15.0 (`main/life*.c`). The design follows Anki's Vector, whose source is public (`os-vector/wire-os-victor`: `engine/moodSystem`, `highLevelAI.json`, `emotionevents/*.json`), with Shimeji's weighted next behaviours and the scoring of The Sims for picking acts.

Vector's own notes warn against letting emotions drive everything: they ran out of animations for every mix of feelings and lost control of the behaviour. Here, as in Vector after that lesson, the emotions only weigh and colour; explicit transitions with conditions and cooldowns decide.

## Layers

From the quickest to the slowest:

1. **Interrupts** (`life.c`). They always come first: something waits for you (it comes to the glass and knocks), a greeting when you're back after 15 minutes or in the morning, a nap when nobody is around, sleep, coming back to you when touched or called. Touches, voice, the menu, a card and the reactions in `behaviour.c` take the eyes away from it altogether. When one is over, life picks up where it left off; after a touch, a greeting or an answered call it picks up **with you** (Vector's resume suggestions).
2. **Emotions** (`life_mood.c`).
3. **States** (`life_mind.c`).
4. **Acts** (`life_acts.c`).

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

Each event pushes some of them (the `FEELS` table): a stroke is happy +0.35, company +0.5, excitement +0.1, trust +0.01; a drop is happy -0.5, excitement +0.8, trust -0.03; being ignored when it called is happy -0.3, company -0.4. The events: `poke`, `stroke`, `hold`, `talk`, `slap`, `shake`, `drop`, `lift`, `played` (a game), `timer` (one rang), `task` (one of his done well; `habit`, one ticked off, until 0.15.1), `woke`, `greeted`, `ignored`, `fun` (it had fun on its own), `sound` and `plugged` (`friend`, `friend_asleep` and `jealous`, for another Yoru, went with friends in 0.20.10). Time together raises trust by 0.004 an hour.

A push fades back to where the emotion settles along a curve of its own (Vector's TimeRatio graphs): excitement in four minutes, company in ten, cheer in fifteen, energy in twenty.

## States

| State | What it is | Lasts | Comes back after | Do not disturb |
|---|---|---|---|---|
| `lounge` | resting near you, now and then a drift or a look round | 2–5 min | — | yes |
| `watch` | a little nearer, with you: looks at you, a slow blink | 30–70 s | 3 min | yes |
| `explore` | the edges, the corners, a firefly | 40–90 s | 5 min | yes |
| `play` | on its own, for fun | 15–35 s | 10 min | no |
| `dream` | slow, eyes on the sky it came from | 1–3 min | 5 min | yes |
| `bored` | sighs, looks away | 40–90 s | 5 min | no |
| `seek` | lonely: looking round for you | 15–30 s | 15 min | no |
| `sulk` | looked for you and nobody came: off to a side, turned away | 30–60 s | 20 min | no |
| `drowsy` | yawns, nods off | 1–3 min | 1 min | yes |

Each has a look (the face it rests with, how far and how fast it drifts), a repertoire of acts with weights, an act it starts with (bored starts with a sigh, drowsy with a yawn, seek with a search), and weighted ways out. The weights are scaled by the emotions: `weight × max(0.05, 1 + k × emotion)` for each of up to two modifiers, where `night` counts as 1 at night and -1 by day. Lounging, for example, goes on to `explore` with weight 1.5 ×(1 + 0.8 stim)(1 + 0.8 energy), to `play` 0.8 ×(1 + happy)(1 + stim), to `drowsy` 1 ×(1 - 1.5 energy).

Some ways out don't wait for the state to end (Vector's interrupting transitions), checked between two acts:

- energy below -0.55: `drowsy`;
- company below -0.6 while lounging, bored, dreaming or exploring: `seek`;
- excitement above 0.65 with cheer above 0.3 and some energy, while lounging or bored: `play`;
- excitement below -0.6 while lounging, exploring or dreaming: `bored`;
- Do not disturb in a state that doesn't fit it: `lounge`.

Drowsy, with nobody around for the "sleepy after" setting and energy below -0.6, it asks for a nap, and from there it falls asleep as before.

## Acts

Yoru is mostly still. 0.15.0 had it doing something every few seconds, and it looked restless; since 0.15.1 it waits long between two acts (6–15 s when lounging, 2.5–6 s when playing, up to 16 s when dreaming), and in most states the commonest act is `rest`: staying where it is for 6–14 s (longer in the slow states) while the eyes wander and blink by themselves. Lounging, something you notice happens every half a minute or so. An act isn't picked again until its cooldown is over (minutes for the showy ones: 20 for a sneeze, 30 for hiccups, 10 for the firefly), and one of the last three is less likely. Under Do not disturb the loud ones are left out and no act makes a sound.

The acts: `rest`, `drift`, `look` (left, right, up), `gaze` (a little nearer, at you, a slow blink), `wink`, `tilt`, `stretch`, `sneeze`, `hiccup`, `peek` (out past the edge), `sniff` (a corner, little hops), `firefly`, `dance`, `loop`, `bounce` (off the walls), `stars`, `hum`, `sigh`, `yawn`, `nod` (nodding off, then a jolt), `search` (looking left and right for you, a whimper), `sad`, `sulk`.

**The rare ones** (since 0.20.20, flag `ACTF_RARE`): `wave`, `shy`, `hop`, `giggle`, `cheer`, `love`. Each is picked only once its milestone is reached (`growth.c`, docs/CONCEPT.md, Growing up), and has a cooldown of 30 to 60 minutes, so that it still surprises. They are the clips of reactions Yoru already had when touched or told, now done of its own accord: `wave` and `love` while it watches you, `shy` and `giggle` lounging, `hop` and `cheer` at play. `debug/life` can force them, unlocked or not.

0.15.0 also had a view up close, with only Yoru's eyes on the screen, light on black; it was taken out in 0.15.1.

**The firefly** is the one thing Yoru knows from home: a flickering green-gold light wanders over the screen, Yoru follows it with its eyes and its whole body, its eyes go wide, and it pounces (two times in three) with a clap and a giggle; otherwise it flies off upwards and Yoru goes "aww".

## Trying it

`debug/life` (docs/protocol.md) forces a state or an act, pushes an emotion with an event, and runs the life clock up to 300 times faster, so a whole day's cycle passes in minutes. `get_state` has `life`: the state, the act, how long it has been in it and how long it has left, and the emotions. Each change of state is logged with its reason (`mind: lounge -> explore (time)`), each event with the emotions after it.

To change how Yoru behaves, change the tables: `FEELS`, `ENERGY_BY_HOUR` and the decay curves in `life_mood.c`; `STATES`, the `*_ACTS` and `*_WAYS` tables and `urgent()` in `life_mind.c`; `ACTS` (cooldowns, flags) in `life_acts.c`.
