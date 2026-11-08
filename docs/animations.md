# Yoru's animation library

Hand-keyed clips for the moments that matter, drawn in Rive and played on the board by the eye engine. The eye engine (`main/eyes.c`) stays in charge of everything that reacts: gaze towards sounds, touch, tilt, depth, sleep, tints. A clip takes over only for one beat of acting, then hands Yoru back.

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

All thirty-seven are animated in `yoru.riv` and played on the board: the first ten since 0.20.7 (`celebrate` and `wake_stretch` since 0.20.6), clips 11 to 22 since 0.20.8, 23 to 30 (with colour) since 0.20.9, 31 and 32 since 0.20.14, 33 to 37 since 0.20.15. `debug/clip` plays any of them by name (`docs/protocol.md`).

### 1. `celebrate` — a build passed, a task is done
Joy. 84 frames (2.8 s), once. Ends in the rest pose. **Animated** in `yoru.riv` (`clip_celebrate`).

| Frames | Pose |
|---|---|
| 0–6 | Notices: eyes widen (1.10 wide, 1.18 tall), body rises 4 px. |
| 6–12 | Anticipation: crouch, squash 1.12 / 0.90, body down 10 px, hands pop out low at its sides (110% then 100%). |
| 12–21 | Jump: up 60 px on an arc, stretch 0.90 / 1.12 while rising (ease-out), hands swing up into a wide V over its head, the right hand 2 frames ahead. |
| 21–35 | Apex and spin: the eyes slide off the right edge of the sphere (narrowing to 30%) and come back from the left with a small overshoot, a full turn with the eyes leading; body back to 1 / 1, hanging at the top; sparkles round its head. |
| 35–41 | Fall (ease-in), stretch 0.94 / 1.06, hands lagging above. |
| 41–45 | Land: squash 1.15 / 0.87, hands drag up, sparkles fade. |
| 45–57 | Second smaller hop, 20 px; the eyes turn into happy `^ ^` arches. |
| 57–73 | Hands up over its head for two claps (6 frames apart), the eyes looking up at them, the body bobbing 3 px with each. Over the head, not in front: hands in front of the face read as a muzzle. |
| 73–84 | Settle with overshoot 0.97 / 1.03 → 1 / 1, eyes back to pills, hands tuck (left first). |

Secondary: the player blooms the green "done" tint on the landing (it isn't in the clip, see Production). Ambient: the sparkles (the `Stars` node) from the apex to the landing.

### 2. `wake_stretch` — waking up
Comfort. 86 frames (2.9 s), once. Starts with the eyes closed. **Animated** in `yoru.riv` (`clip_wake_stretch`); played by the morning greeting, the stretch Yoru does on his own and `react` `stretch`.

| Frames | Pose |
|---|---|
| 0–12 | Eyes half open (0.5, the right a little lower), gaze down, slow drowsy blink. |
| 12–24 | Anticipation: body sinks 6 px, squash 1.08 / 0.93, hands come out low (the right 2 frames later). |
| 24–48 | Stretch: up 14 px, 0.88 / 1.14, a 3° lean, hands far out and high (±140 px, −105 px, on arcs), eyes squeezed shut. Out level with the face, the hands didn't read as a stretch. |
| 48–60 | Hold, a 1 px tremble (the effort). |
| 60–66 | Release (ease-in): snap down to squash 1.10 / 0.91, hands fall in. |
| 66–78 | Overshoot 0.97 / 1.03 → 1 / 1 (ease-out-back). A big blink at 72. |
| 78–90 | Eyes open, look at you, hands tuck (right 3 frames after left). |

### 3. `sneeze` — now and then, idle (rare)
Comedy, the imperfect side. 66 frames (2.2 s), once, either way round. **Animated**; played by the sneeze Yoru does on his own, the sound on the "choo".

| Frames | Pose |
|---|---|
| 0–14 | "Ah…": body rises 6 px and stretches 0.95 / 1.05, tilts back 6°, eyes squint (lids 0.4) and look up. |
| 14–20 | Hesitation: the eyes flicker open, a frame's dip. |
| 20–30 | "…ah…": rises further, 0.92 / 1.08, tilt 10°, eyes squeezed shut. |
| 30–34 | "CHOO": snaps forward and down in 4 frames (ease-in), squash 1.15 / 0.87, tilt −6°; knocked back (nearness 1 → 0.91), hands flung out. |
| 34–52 | Wobble back to 1 / 1 (two damped swings), hands tuck. |
| 48–66 | Dazed double blink, a sheepish glance to the side, back to you. |

### 4. `firefly_gulp` — the end of a hunt
Delight. 91 frames (3 s), once, either way round. **Animated**; the end of a hunt that catches the firefly. Authored with the firefly at (+110, −40) screen pixels from the body: the hunt brings Yoru there, plays the clip and hides the firefly when the hands close on it (900 ms in). Yoru ends where it pounced, and the engine carries on from there.

| Frames | Pose |
|---|---|
| 0–10 | Stare: eyes on the firefly (up and right), a little bigger and crossed. |
| 10–20 | Wiggle wind-up: body back 8 px, squash 1.06 / 0.94, two small shimmies. |
| 20–26 | Pounce on an arc onto the firefly, stretched 0.88 / 1.13 along the path, leaning 8° into it. |
| 26–32 | Hands clap shut around it over its head; squash on contact; the eyes look up at them. |
| 32–44 | The cupped hands come down to the bottom of the sphere, the eyes follow them. In front of the eyes they read as a muzzle. |
| 44–53 | Hands press in at the lower front (its "mouth"). |
| 52–64 | Gulp: eyes shut, a bulge, 1.08 / 0.95 then 0.96 / 1.05; the hands tuck. |
| 64–80 | Satisfied `^ ^` eyes, a tiny hop (a hiccup). |
| 80–91 | Eyes back to pills. |

The glow of the firefly inside the body isn't keyed yet.

### 5. `greet_back` — you're back after a while
Friendliness. 71 frames (2.4 s), once. **Animated**; played by the greeting when you come back (after the stretch in the morning).

| Frames | Pose |
|---|---|
| 0–4 | Freeze: it notices you. |
| 4–10 | Take: eyes 1.2 tall, body pops up 8 px with stretch 0.94 / 1.06. |
| 10–16 | Anticipation squash 1.08 / 0.93. |
| 16–28 | Hop towards you (nearness 1 → 1.18) on an arc, land with squash. |
| 28–56 | Wave with the right hand while the body bobs; happy eyes; tilt 5° towards the waving hand. |
| 56–71 | Back to its distance, hand tucks, eyes back to pills. |

### 6. `yawn` — tired
Calm. 61 frames (2 s), once. **Animated**; played by the yawn Yoru does on his own, before a nap and by `react` `yawn`. Ends with the eyes half shut (lids 0.58, the sleepy mood's), looking down.

Slow inhale (24 frames): body grows tall 0.92 / 1.09 and tilts back 6°, eyes squeeze shut, the right hand rises to the lower front (covering a mouth it doesn't have). Hold 8 frames with a small shudder. Deflate (20 frames, ease-in-out): body sinks below rest by 6 px, squash 1.06 / 0.95, hand falls away. Settle into half-closed eyes.

### 7. `startle` — a fright
Surprise. 37 frames (1.2 s), once, either way round. **Animated**; played by `react` `surprised`, jumping left or right at random. Authored jumping up and left, away from a sound on its right, looking at it.

Jump in 2 frames: up 40 px, 0.85 / 1.18, eyes huge (1.35), both hands up. Hang 6 frames. Fall (ease-in), land squash 1.12 / 0.89. Tremble ±2 px for 10 frames, eyes still wide, then a blink and back to the middle.

### 8. `knock_glass` — asking for you
Hope. 75 frames (2.5 s), once. **Animated**; played when Yoru comes up to the glass because something waits for you (the engine brings it there first).

The right hand comes up at the top right of the sphere and grows towards the glass (1.25). Knock, knock: it pushes forward (1.45) and back, 8 frames apart, the body nudging with each. Head tilts 8°, eyes big and hopeful (lids sloping down outside). Waits 20 frames with a blink, a small wave, the hand goes down. In front of the face the hand hid an eye.

### 9. `think` — thinking for you
Focus. 95 frames (3.2 s), once. **Animated**; played by `react` `thinking`. Not a loop after all: a loop starting and stopping would jump in and out of its pose.

The right hand comes under the "chin", eyes up-left with lids a little low, the right eye smaller; a 4 px bob; the hand taps the chin twice; the eyes drift up-right and back; a blink at frame 55; then the hand tucks and the face comes back.

### 10. `sulk_turn` — sulking
Pique. 119 frames (4 s), once, either way round. **Animated**; played by the sulk once Yoru is by its edge, turning towards it, with a "pfft" when it snaps away.

Huff: stretch 0.95 / 1.05 and out. A turn away (the face slides to the far side, narrowing to 0.55), the body drifts 30 px towards the edge, lids sloping cross, both hands crossed low in front, on the side it faces. Hold. Glances back, half way, and snaps away again when it meets your eyes. Hold, then a sigh and back to the rest pose.

### 11. `hiccup` — now and then, idle
Comedy. 83 frames (2.8 s), once, either way round. **Animated**; played by the hiccups Yoru gets on his own, the sounds on the jolts (230 and 1370 ms) and the giggle at 1800 ms.

A crouch and a jolt up (stretch 0.91 / 1.12, eyes wide), a land with a squash; again, bigger and tilted. Then happy eyes and little bobs, the right hand over his "mouth".

### 12. `dance` — music
Joy. 94 frames (3.1 s), once, either way round. **Animated**; played by the dance Yoru does on his own and `react` `dance`.

A beat every 12 frames (150 bpm): squash on the beat, up between, swaying 30 left and right with the tilt following; the arm on the side he sways to goes up. Then two hops in place with both arms up and out (not over the head: there they read as ears), and settle. Happy eyes throughout.

### 13. `hide_seek` — playing, idle
Mischief. 129 frames (4.3 s), once, either way round. **Animated**; played instead of peeking out of the glass about a third of the time.

Looks right and crouches, dashes out right (accelerating, stretched), and at frame 25 jumps to the far left off screen (the player holds across a jump this big instead of sweeping across). Waits, then peeks in from the left edge (half the body showing, eyes wide, a blink, a tilt), backs off a little and pops back to the middle on an arc, landing with a squash, hands out "ta-da", happy eyes.

### 14. `melt` — stroked
Contentment. 91 frames (3 s), once. **Animated**; played when a stroke is felt.

Happy eyes at once; the body flattens (1.15 / 0.86) and sinks 16, a little nearer, the hands flopped out low; it sways with the finger, then pulls itself together with a wobble.

### 15. `point_sound` — a noise off to one side
Alertness. 63 frames (2.1 s), once, either way round. **Animated**; played when a noise is heard well off to one side (|x| > 0.45), at most once every 45 s, and by `react` `point`. Authored for a noise on its right.

The eyes snap there first (a small start), the body leans 22 and tilts 7, the hand overshoots out pointing and settles; a blink; back.

### 16. `dizzy` — shaken or spun
Muddle. 89 frames (3 s), once, either way round. **Animated**; played by `react` `dizzy` (shaken, spun).

A wobble that dies down (tilt 12 → 0), the eyes rolling round and swapping sizes (one 1.38 tall while the other is 0.66), the hands bobbing out of phase. Then a quick head shake with the eyes shut, and open.

### 17. `nod_off` — drowsy
Sleepiness. 115 frames (3.8 s), once, either way round. **Animated**; played by the nod Yoru does on his own, the "eep" on the jolt (2130 ms).

Sinks 22 over two seconds as the lids fall (to 0.85) and the eyes look down; the head drops; a jolt up with huge eyes; a look left and right ("was I asleep?"); the lids settle a little low.

### 18. `curious_tilt` — something interesting
Curiosity. 69 frames (2.3 s), once, either way round. **Animated**; played by the tilt Yoru does on his own and `react` `curious`.

A tilt to 16 that settles at 14, leaning nearer (1.08); the left eye bigger, the right one smaller with its lid a little low; a blink; a "hm?" bob; back.

### 19. `sad_droop` — sad
Sadness. 101 frames (3.4 s), once. **Animated**; played by the sad mood Yoru falls into and `react` `sad`. Ends 26 lower than it started, and Yoru stays there.

Sinks slowly, heavier (1.05 / 0.95) and a little smaller; the lids slope down outside; looks down and away; the hands hang low. A sniffle, a sigh (rising and sinking again), and the face back to the mood's.

### 20. `shy_peek` — shy
Bashfulness. 97 frames (3.2 s), once. **Animated**; played by `react` `shy`.

Hunches, a little smaller, and covers both eyes with its hands (in front, low enough to hide the eyes whole); sways bashfully; the right hand slides aside and the right eye peeks, blinks; the hand snaps back; then the hands come down to a smile.

### 21. `sleepy_wave` — goodnight before a nap
Drowsiness. 89 frames (3 s), once, either way round. **Animated**; played in the nap once Yoru has reached its corner, waving with the hand away from the edge.

Lids half down; a slow, lazy wave; the hand drops; the eyes close, and open again half shut.

### 22. `lifted` — picked up
Surprise, then delight. 57 frames (1.9 s), once, either way round. **Animated**; played by `react` `lifted` (picked up).

A startled stretch up (0.90 / 1.14) with the hands flying out, a dangling sway that dies down with the hands lagging, a look down at the floor, then up at you with happy eyes.

## Clips with effects: what Yoru does for you (0.20.9)

These light Yoru up. Three tracks beyond the rig: `Prop` (a thing held, keyed like a hand: position from the body and scale, 0 hidden), `Halo`'s opacity (a glow round the body, with the body washed in the same colour) and `Stars`' opacity (how fast sparkles fly: off the prop while it shows, off the top of the head otherwise). Their colour isn't keyed: whoever plays the clip gives it, so one clip serves every service. On the board the prop is a rounded card in the colour with the service's glyph on it (the portal's: an envelope for mail, 31 for a calendar, a folder for files, its initial for anything else), or a glowing orb without a glyph; in Rive it is a purple ball. They play from `show_fetch`'s `kind` (`docs/protocol.md`).

### 23. `listen_loop` — listening
Attention. 61 frames (2 s), a loop, until Yoru stops listening. Cyan (the wake word's).

The right hand cupped by its "ear", a little tilt, a slow bob with the body breathing (1.02 / 0.98), the eyes a little big, a blink every loop, the glow breathing 0.3 → 0.6 and a few sparkles.

### 24. `memory_save` — it remembers something
Delight in knowing. 73 frames (2.4 s), once, either way round. Purple orb (the memory's colour).

An orb appears over its head; it looks up, crouches, reaches up and catches it in both hands, and pulls it down into its head: a stretch, a flash of glow and a burst of sparkles, then happy eyes and a nod.

### 25. `memory_forget` — it forgets something
Letting go. 57 frames (1.9 s), once, either way round. Purple orb.

Squinting, it reaches to its head and pulls an orb out, holds it up, flicks it away to the side, where it swells and pops in sparkles; a shake of the head and a blink.

### 26. `search_loop` — searching a service
Focus. 61 frames (2 s), a loop, until the search ends. In the service's colour. A web search plays `web_search_loop` instead, and searches at once `search_juggle_loop`.

Holding the card low in front with both hands, scanning left and right with a tilt and a hop each way, a blink; the glow breathing and sparkles off the card.

### 27. `search_found` — found
Triumph. 48 frames (1.6 s), once; after a search loop (26, 31, 32), when the last search out has come back with something.

A crouch and a hop with the card lifted high in both hands, glow and sparkles at full, happy eyes; the card shrinks away up there and the hands come down.

### 28. `send_note` — sending something for you
Purpose. 77 frames (2.6 s), once, either way round (the throw towards the middle of the screen). In the service's colour.

The card appears in its hands; it looks down and scribbles on it with the right hand; winds up (leaning back, the card pulled down and to the side) and throws it: the card flies off past the rim leaving sparkles, the hand follows through, the glow flashes. It watches it go, happy.

### 29. `jot_note` — a task or a timer
Tidiness. 65 frames (2.2 s), once. In the service's colour (a check for tasks, a bell for timers).

The card in its hands, two stamps on it with the right hand (each a pulse of glow and sparkles), then the card shrinks into its body with a flash; happy eyes, a nod.

### 30. `tool_failed` — it didn't work
Disappointment. 53 frames (1.8 s), once; after a search loop when every search failed, or for a send that failed, the card back in its hands.

The card trembles and sputters (the glow flickering, a few sparkles), then drops out of its hands and falls away; Yoru sinks a little with sad lids, looks down, and lets it go.

### 31. `web_search_loop` — searching the web
Showing off. 73 frames (2.4 s), a loop, until the search ends. The web's cyan, a globe.

The right hand up with the card balanced on it, spinning (it turns edge-on and back), the left hand out for balance; Yoru leans and wobbles under it. Halfway, a squash and a flick: the card flies up spinning faster, the eyes go wide and follow it, and it lands back on the hand with a little give. A glance at you and a blink while it spins on.

### 32. `search_juggle_loop` — searching several things at once
Fun under pressure. 73 frames (2.4 s), a loop, while two or more searches are out (they started together; Yoru Hub runs them at once). The web's cyan.

Juggling the card from hand to hand over its head: each throw a dip and a flick, the card spinning in a high arc, the eyes following it up and across, the body bobbing and squashing on every catch, glow and sparkles peaking at the top of each arc; a blink between throws. It keeps juggling until the last search is back, then one `search_found` (or `tool_failed` if none found anything).

## Meetings (0.20.15)

While the host records a meeting (`set_meeting`, `docs/protocol.md`): the app's icon, when the host sent it, is on the card in Yoru's hands and on the recording mark. Silent while recording.

### 33. `meeting_start` — a recording starts
Getting to work. 2.5 s, once. Yoru holds up the app's card, taps it (record!), nods to you, and the card floats up to where the recording mark appears.

### 34. `meeting_listen` — still recording
Paying attention. 3 s, once, every 150 s while recording. It leans in with a hand cupped by its side and nods twice, slowly.

### 35. `meeting_write_loop` — transcribing
Busy. 2 s, a loop, until the transcript is made. Scribbling the meeting down on the card, line after line, zig-zagging left to right and back for the next line, the eyes on the pen.

### 36. `meeting_squeeze_loop` — summarizing
Effort. 2 s, a loop, until the summary is made. The long transcript squeezed between its hands, twice, a squash on each squeeze.

### 37. `meeting_done` — the summary is ready
Pride. 3 s, once. A hop, the card thrown up over its head, jazz hands, happy eyes. A failure plays `tool_failed` instead.

## Production

**In Rive** (`docs/mascot/yoru.riv`, the cloud file "Yoru mascot"):
- One timeline per clip, named `clip_<id>` (`clip_celebrate`), on the 466 × 466 artboard, black background. The file's timelines run at 60 fps, so a storyboard frame is two timeline frames; the export takes every other frame.
- The rig a clip keys: `Character` (its scale is how near Yoru comes), `Body` (position, rotation, squash as scale), `HandL` / `HandR` (position, scale: 0 is tucked at ±112, 40; out is ±168, 40), `Look` (the face: x/y, and x scale for a turn), `EyeL` / `EyeR` (x, for crossed eyes), `EyeShapeL` / `EyeShapeR` (scale; 8% tall is shut), each eye's top corners (`L_TL`, `L_TR`, `R_TL`, `R_TR`: lowered, the top lid; one lower than the other, a slope) and bottom-middle vertex (`L_BM`, `R_BM`), and `Stars`. Happy `^ ^` eyes: eye scale 1.15 / 0.60, bottom-middle vertex at y −12 with radius 8. The hands are siblings of the body in the file: a clip moves them with it.
- Clips aren't in the state machine: they are timelines for export only, so they never fight the layers the app drives.
- Key the poses first (pose to pose), check the timing with only the keys, then add the breakdowns and the overlap. Loops: the last frame equals the first.
- Animate the body in a neutral grey with no tint and no badge: the player adds them, so a clip works in every state and with any body colour.
- The Rive MCP lives in the Rive desktop app (macOS) and only runs while the app is open; with it open, the timelines can be built from this file.

**On the board since 0.20.6: the clip as numbers, not pictures.** The clips only move the rig (body, hands, eyes, face), and the eye engine draws the same rig with the same parameters. So a clip goes to the board as its keys, not as frames: `python3 tools/clips/fetch.py` (with the Rive app open on the file) saves the keys of every `clip_*` timeline, with their cubic curves, into `tools/clips/keyframes.json`, and `python3 tools/clips/clips.py` samples them every 1/30 s into `main/clips.c` and `main/clips.h`. `eyes_play()` plays one, left for right if asked: for its length the clip sets the body's offset, squash, tilt and nearness (`Character`'s scale), the hands (relative to the body, in front of it where they overlap it), the face's offset and turn, and each eye's place, size, top lid (from its top corners) and bottom lid; moves, knocks, the mood and the tint go on underneath, and a gesture stops it. A clip that ends away from where it started (the firefly, the sad droop) leaves Yoru there; a jump of more than 300 between two frames (hide and seek) is held, not swept across. A clip costs about 11 KB of flash (3 s) and nothing to draw beyond what Yoru always draws. Left out on the board for now: the hands' rotation. Since 0.20.9 a clip also carries the prop, the glow and the sparkles (see Clips with effects), 34 numbers a frame; `eyes_play_fx()` gives their colour and glyph. To update a clip: key it in Rive, run the two scripts, build.

Frames are still the way for a clip that draws what the rig can't (a prop, a squiggle, a hand-drawn effect):

**Export (frames):** the frames of each clip, rendered by a Rive runtime scrubbing its timeline (the editor's export gives only the `.riv`), then a converter (to do, `tools/`) that crops every clip to the box its frames cover (even-aligned, as everything sent to the CO5300), scales it to the board's size, reduces it to a 256-colour palette and writes it to the `assets` partition. Format: GIF to begin with (LVGL 9.3 plays it); a format of our own if GIF turns out too slow next to WiFi and ESP-SR. With an indexed palette, recolouring Yoru or tinting it is rewriting the palette, not redrawing the frames.

**Budget:** the `assets` partition has about 3.9 MB. A clip cropped to the sphere and the hands' reach (about 300 × 300) should stay under 300 KB, so about a dozen fit; the 32 MB flash of some boards would hold many more.

**On the board, frames** (to do, for such clips): a clip player that waits for the engine to reach the rest pose, hides the engine's drawing for the clip's length, plays the clip at Yoru's position, and hands back the pose or mood the clip ends in. `behaviour.c` and `life.c` pick clips; the eye engine never does.
