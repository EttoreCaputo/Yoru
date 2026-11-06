# Yoru's animation library

Hand-keyed clips for the moments that matter, drawn in Rive and played on the board next to the eye engine. The eye engine (`main/eyes.c`) stays in charge of everything that reacts: gaze towards sounds, touch, tilt, depth, sleep, tints. A clip takes over only for one beat of acting, then hands Yoru back.

Why: the procedural engine moves Yoru well, but it can't act. A wake-up stretch, a sneeze or a gulped firefly need poses timed by hand, with anticipation, overshoot and overlap placed frame by frame. That is what makes Taby's clips (TRIIIS LABS) look alive. Their artwork is licensed for Taby only; everything here is Yoru's own.

## Motion identity

One personality for every clip, so they feel like the same creature:

- **Playful, light, a little clumsy.** Yoru is small and soft: snappy timing, 10–20% overshoot on arrivals, squash on every landing. Never heavy, never mechanical.
- **Signature easing:** ease-out-back `(0.175, 0.885, 0.32, 1.275)` for arrivals and pops; sine ease-in-out for breathing, bobbing and loops; ease-in for falls and the snap after a held pose. Never linear for anything that moves through space.
- **Durations** (30 fps, the engine's rate): quick 5 frames (170 ms), standard 9 frames (300 ms), slow 27 frames (900 ms). Holds are what make a pose read: at least 6 frames on every storytelling pose.
- **Volume stays constant:** squash and stretch keep `scaleX × scaleY ≈ 1`. Squash at 1.15 / 0.87 is the strongest the sphere takes; beyond it, it stops reading as a ball.
- **Overlap:** the hands lag the body by 2–3 frames and settle 3–4 frames after it. The eyes lead every turn by 2 frames (the engine's rule: the eyes lead the way).
- **Arcs:** the body travels on curves, never straight lines; hands swing on arcs around the body.
- **Asymmetry:** no twin poses. One hand leads, one eye squints a little more, the tilt is never exactly 0 in a held pose.

## The rest pose

Every clip starts and ends here, so the switch between the engine and a clip can't be seen:

- body centred where the engine left it, at distance 1 (`z = 1`), scale 1 / 1, no tilt;
- eyes the neutral pills, gaze straight ahead, no lids;
- hands tucked behind the body;
- no tint (the tint is added by the player, see Production).

A clip may end in a **mood pose** instead (sleepy lids, sulking gaze) when the engine takes over in that mood; the clip lists it.

## Units

Offsets are screen pixels at the resting distance; the sphere's radius is 98 px (`LOOK_SPHERE_R`). In `docs/mascot/yoru.riv` the character is drawn at the Rive file's scale: divide screen pixels by `LOOK_SCALE` (0.7). Up is negative y. Frames are at 30 fps from 0.

## The clips

Production order: the first five are the showpieces, the rest follow.

### 1. `celebrate` — a build passed, a task is done
Joy. 84 frames (2.8 s), once. Ends in the rest pose with happy eyes for the engine.

| Frames | Pose |
|---|---|
| 0–6 | Notices: eyes widen (scale 1.15), body rises 4 px. |
| 6–12 | Anticipation: crouch, squash 1.12 / 0.90, body down 10 px, hands pop out low at its sides. |
| 12–20 | Jump: up 60 px on an arc, stretch 0.90 / 1.12 while rising (ease-out), hands swing up over its head, right hand 2 frames ahead. |
| 20–34 | Apex and spin: the eyes slide off the right edge of the sphere and come back from the left (a full turn, eyes leading), body back to 1 / 1, hold the hang 4 frames at the top. |
| 34–40 | Fall (ease-in), stretch 0.94 / 1.06. |
| 40–44 | Land: squash 1.15 / 0.87, hands drag up. |
| 44–56 | Second smaller hop, 20 px, happy `^ ^` eyes. |
| 56–72 | Two claps (8 frames each), body bobbing 3 px with each. |
| 72–84 | Settle with overshoot 0.97 / 1.03 → 1 / 1, hands tuck (left first). |

Secondary: the green "done" tint blooms on the landing and fades. Ambient: a few sparks rising from the apex, gone by frame 50.

### 2. `wake_stretch` — waking up
Comfort. 90 frames (3 s), once. Starts with the eyes closed.

| Frames | Pose |
|---|---|
| 0–12 | Eyes half open (lids 0.5), gaze down, slow drowsy blink. |
| 12–24 | Anticipation: body sinks 6 px, squash 1.08 / 0.93, hands come out low. |
| 24–48 | Stretch: up 14 px, 0.88 / 1.14, hands far out and up (±120 px, −40 px) on arcs, eyes squeezed shut. |
| 48–60 | Hold, a 1 px tremble (the effort). |
| 60–66 | Release (ease-in): snap down to squash 1.10 / 0.91, hands fall in. |
| 66–78 | Overshoot 0.97 / 1.03 → 1 / 1 (ease-out-back). A big blink at 72. |
| 78–90 | Eyes open, look at you, hands tuck (right 3 frames after left). |

### 3. `sneeze` — now and then, idle (rare)
Comedy, the imperfect side. 60 frames (2 s), once.

| Frames | Pose |
|---|---|
| 0–14 | "Ah…": body rises 6 px and stretches 0.95 / 1.05, tilts back 6°, eyes squint (lids 0.4). |
| 14–20 | Hesitation: freezes, eyes flicker open. |
| 20–30 | "…ah…": rises further, 0.92 / 1.08, tilt 10°, eyes shut. |
| 30–34 | "CHOO": snaps forward and down in 4 frames (ease-in), squash 1.15 / 0.87, tilt −6°; Yoru is knocked back (z 1 → 1.1), hands flail out. |
| 34–48 | Wobble back to 1 / 1 and z 1 (two damped swings). |
| 48–60 | Dazed double blink, a sheepish glance to the side, back to you. |

### 4. `firefly_gulp` — the end of a hunt
Delight. 96 frames (3.2 s), once. Authored with the firefly at (+110, −40) from the body; the engine brings Yoru to that offset first.

| Frames | Pose |
|---|---|
| 0–10 | Stare: eyes on the firefly, slightly crossed, body still. |
| 10–20 | Wiggle wind-up: body back 8 px, squash 1.06 / 0.94, two small shimmies. |
| 20–26 | Pounce on an arc to the firefly, stretched 0.88 / 1.13 along the path, eyes leading. |
| 26–30 | Hands clap shut around it; squash on contact. |
| 30–44 | Peek: eyes down at the closed hands, a glow leaking between the fingers. |
| 44–52 | Hands to the lower front of the body (its "mouth"). |
| 52–64 | Gulp: a bulge, 1.08 / 0.95 then 0.96 / 1.05; the firefly's glow inside the body, low, fading over 30 frames. |
| 64–80 | Satisfied `^ ^` eyes, a tiny hop (a hiccup). |
| 80–96 | Back to rest, hands tuck. |

### 5. `greet_back` — you're back after a while
Friendliness. 72 frames (2.4 s), once.

| Frames | Pose |
|---|---|
| 0–4 | Freeze: it notices you. |
| 4–10 | Take: eyes 1.2 tall, body pops up 8 px with stretch 0.94 / 1.06. |
| 10–16 | Anticipation squash 1.08 / 0.93. |
| 16–28 | Hop towards you (z 1 → 0.85) on an arc, land with squash. |
| 28–56 | Wave with the right hand, the hand 3 frames behind the body's bob; happy eyes; tilt 5° towards the waving hand. |
| 56–72 | Back to z 1, hand tucks. |

### 6. `yawn` — tired
Calm. 66 frames (2.2 s), once. Ends in the sleepy mood pose (lids 0.45).

Slow inhale (24 frames): body grows tall 0.92 / 1.09 and tilts back, eyes squeeze shut, one hand rises to the lower front (covering a mouth it doesn't have). Hold 8 frames with a small shudder. Deflate (20 frames, ease-in-out): body sinks below rest by 6 px, squash 1.06 / 0.95, hand falls away. Settle into half-closed eyes.

### 7. `startle` — a loud sound
Surprise. 36 frames (1.2 s), once. Mirrored so Yoru jumps away from the sound's side.

Jump in 2 frames: up 40 px, 0.85 / 1.18, eyes huge (1.35), both hands up. Hang 6 frames. Fall (ease-in), land squash 1.12 / 0.89. Tremble ±2 px decaying over 10 frames, eyes still wide, then a blink. The engine then turns the gaze to the sound.

### 8. `knock_glass` — asking for you
Hope. 78 frames (2.6 s), once. The engine brings Yoru up to the glass (z 0.6) first.

The right hand comes up to the glass and grows as it nears it. Knock, knock: the hand pushes forward (scale 1.1) and back, 8 frames apart, the body nudging with each. Head tilts 8°, eyes big and hopeful (lids sloping outwards). Waits 20 frames, a small wave, the hand goes down.

### 9. `think_loop` — thinking for you
Focus. 72 frames (2.4 s), seamless loop (frame 72 = frame 0).

A hand under the "chin", eyes up-left; a sine bob of 3 px over the loop; at the half the eyes drift to up-right and back; one blink at frame 54. The blue "thinking" tint breathes with the bob.

### 10. `sulk_turn` — sulking
Pique. 90 frames (3 s), once. Ends in the sulking mood pose (gaze away, near the edge).

Huff: stretch 0.95 / 1.05, then a sharp turn away (the eyes slide to the far side of the sphere), the body drifts 30 px towards the edge, both hands crossed in front. Hold. Glances back once, half way, and snaps away again when it meets your eyes.

## Production

**In Rive** (`docs/mascot/yoru.riv`):
- One timeline per clip, named `clip_<id>` (`clip_celebrate`), at 30 fps on the 466 × 466 artboard, black background.
- Key the poses first (pose to pose), check the timing with only the keys, then add the breakdowns and the overlap. Loops: the last frame equals the first.
- Animate the body in a neutral grey with no tint and no badge: the player adds them, so a clip works in every state and with any body colour.
- The Rive MCP lives in the Rive desktop app (macOS) and only runs while the app is open; with it open, the timelines can be built from this file.

**Export:** a PNG sequence per clip, then a converter (to do, `tools/`) that crops every clip to the box its frames cover (even-aligned, as everything sent to the CO5300), scales it to the board's size, reduces it to a 256-colour palette and writes it to the `assets` partition. Format: GIF to begin with (LVGL 9.3 plays it); a format of our own if GIF turns out too slow next to WiFi and ESP-SR. With an indexed palette, recolouring Yoru or tinting it is rewriting the palette, not redrawing the frames.

**Budget:** the `assets` partition has about 3.9 MB. A clip cropped to the sphere and the hands' reach (about 300 × 300) should stay under 300 KB, so about a dozen fit; the 32 MB flash of some boards would hold many more.

**On the board** (to do): a clip player that waits for the engine to reach the rest pose, hides the engine's drawing for the clip's length, plays the clip at Yoru's position, and hands back the pose or mood the clip ends in. `behaviour.c` and `life.c` pick clips; the eye engine never does.
