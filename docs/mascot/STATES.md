# Mascot states (firmware 0.5.0)

`yoru.riv` follows the board's eye engine. Firmware 0.5.0 added moods (dizzy, scared, peek), gestures (hold, shoo, flail), knocks and leaning, so the file has them too. The values are the ones in `main/eyes.c` and `main/behaviour.c`, so a host can send the board and the app the same numbers.

The list of behaviours (follow the pointer, slap, annoyed after three slaps, dizzy, status tints, greeting) and the way they chain were taken from Novra's Grok Bot case study. Only the behaviours came from there. The character and the drawings are Yoru's own (`docs/CONCEPT.md`).

## New inputs on the `Yoru` view model

| Input | Type | Firmware | What it does |
|-------|------|----------|--------------|
| `mood` | enum `Mood` | `eyes_mood_t`, same names and order | `dizzy`: the eyes turn into spirals spinning opposite ways and three yellow stars circle the head. `scared`: the body trembles (±2.2 px). The other moods change no picture here; the host still sends their lid and scale values as before. |
| `gesture` | enum `Gesture`, new values `hold`, `shoo`, `flail` | `EYES_GESTURE_HOLD/SHOO/FLAIL` | The hand paths from `eyes.c`: hands at the temples swaying, the right hand pushing out, both hands up flapping. They loop until `gesture` goes back to `none`, like the older gestures. |
| `knock` | trigger | `eyes_knock(dx, dy)` | The body is pushed and springs back with the firmware's damped spring (about 48 px sideways, 32 px up or down, settled in 0.9 s). Set `knockX`/`knockY` first. |
| `knockX`, `knockY` | number, -1…1 | `dx`, `dy` | Direction: `knockX` < -0.25 left, > 0.25 right, otherwise `knockY` ≥ 0 down (a landing) or < 0 up. |
| `slap` | trigger | `slap()` in `behaviour.c` | A knock to the side given by the sign of `knockX`, counted. A third slap within 2.5 s of the one before makes Yoru annoyed. |
| `leanX`, `leanY` | number, -1…1 | `eyes_lean(x, y)` | The body sags 8 px / 6 px towards the low side and the gaze rolls 0.6 of the lean that way. |
| `greet` | trigger | the wave on boot and wake | One wave (`WAVE` timing, 1.75 s). It also plays once when the file loads. |
| `followPointer` | number, 0…1 | none (app only) | How strongly the eyes follow the pointer: 1 in the default instance, 0 when the host drives `gazeX`/`gazeY`. |
| `pointerX`, `pointerY` | number, artboard px | none | Written by the file (where the pointer is). The app reads them and never sets them. |

`status` (`thinking` blue while working, `done` green when finished, `error` red) and the rest of the inputs are unchanged from 0.4.

## State machine layers

The older layers (`Idle`, `Blink`, `Status`, `Sleep`, `Hands`) are still there. `Hands` has new states `Hold`, `Shoo`, `Flail` and `Hello`. Its entry now plays `Hello` and then tucks the hands, and they come out again if `handsShown` is set.

- **Mood**: `Normal`, `Dizzy`, `Scared`, picked from `mood`, each fading in over 250 ms.
- **Knock**: `Rest` and `KnockLeft/Right/Down/Up`, started from Any State by `knock`, or sideways by `slap`. Each one returns to `Rest` when it ends.
- **Slaps**: `Calm → Slapped1 → Slapped2 → Annoyed → Grumbling → Calm`. Each slap moves one step on. `Slapped1` and `Slapped2` last 2.5 s and drop back to `Calm` if no slap comes, which is the firmware's `SLAP_WINDOW_MS`. `Annoyed` (1.1 s) squashes the body, tilts the eyes into a frown and plays the shoo with the right hand. It is placed above `Hands`, so the shoo plays even if another gesture is running. `Grumbling` (1.7 s) holds the frown, which makes the scene 2.8 s long, the same as `annoyed` in `behaviour.c`.

## Pointer (web and app)

Two invisible halves of the artboard, `HitLeft` and `HitRight`, have listeners:

- **move**: moves `PointerDot` to the pointer. The node's position is bound back to `pointerX`/`pointerY`, and the `Look` node (around `Face`) follows it through an interpolator converter (0.35 s), so the eyes catch up a moment late.
- **click**: sets `knockX` to +0.9 (click on the left half, so Yoru is pushed right) or -0.9, then fires `slap`. A click on the web counts like a flick on the board.

On the board a slap is a quick flick across the screen (`SLAP_MAX_MS`, `SLAP_PX`), and the board plays the annoyed scene itself. A host showing the board's state in the app should send `knock` with the board's direction plus `mood = grumpy` and `gesture = shoo`, rather than `slap`, so the app doesn't count the slaps a second time.

## Rig changes

`Character` now holds `Lean → Knock → Shiver → [hands, Body]`. Each node is driven by one input only: `Lean` by the lean binding, `Knock` by the Knock layer, `Shiver` by the Mood layer (x) and the Slaps layer (scale), so the inputs never fight over a property. `Body` gained `Stars`, `Look` wraps `Face`, and each eye has a `Spiral` shape that stays hidden outside dizzy. The gaze converters (`gazeLeanToX/Y`) add the lean to `gazeX`/`gazeY`.

## Not verified

- The enum-driven transitions (`mood`, the new `gesture` values) can't be run in the editor's simulator. They are built the same way as the existing gesture transitions. The trigger chains (slap, knock, greet) and the startup wave were simulated and work.
- The formulas that read another property (`{{Yoru/leanX}}`, `{{Yoru/followPointer}}`) and the pointer binding (`PointerDot`'s position written to the view model) haven't been tried in a runtime yet. Check them in the app's Rive runtime before relying on them.
