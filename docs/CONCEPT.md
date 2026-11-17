# Yoru: the concept

Yoru (夜, "night") is a desk assistant with expressive eyes on a round AMOLED. It runs on the Waveshare ESP32-S3-Touch-AMOLED-1.75C: a small, matte sphere with two dark eyes and two floating hands, in the middle of the 466 × 466 disc. True black switches the pixels around it off, so the edge of the case disappears and only the sphere seems to hover on your desk.

When you call it, it listens, thinks and answers, through Yoru Hub on your computer or, with the computer away, straight from the board. The rest of the time it shows what matters (notifications, timers, a meeting being recorded, the battery) and stays pleasant to look at, with small animations now and then.

Yoru is an AI assistant and says so. It has no backstory, no needs, no moods of its own and nothing to unlock: what it shows depends on what it is doing for you, not on how it is treated.

## Why a sphere with two eyes and two hands

Successful desk devices with a face show very little: two simple eyes, no mouth, no limbs (Cozmo/Vector, EMO, Eilik, Loona, Stack-chan, Dasai Mochi). A few features are enough to make a state readable at a glance.

Yoru keeps that rule and adds a body, as simple as a body can be. A sphere reads at any size, from the board to an app icon, and gives the eyes a surface to move on. Two floating hands give it gestures (a wave, a point, covering its eyes) without arms or a body that would make it less simple.

The sphere is smaller than the screen on purpose (about 40% of the disc): the black around it lets it float, leaves room for the hands, lets it move, and keeps the screen cheap to run and safe from burn-in.

## How it looks

- **Body:** a matte sphere lit softly from the top left, with a gentle shadow towards the bottom edge. No outline, no gloss.
- **Eyes:** two small, dark, vertical pills, close together and a little below the centre. They are holes in the light, not lights themselves.
- **Hands:** two small spheres of the same material, a bit flattened, floating beside the body with nothing joining them. They pop out for a gesture and tuck back behind the body at rest.
- **State tint:** the lower part of the sphere fills with a colour that tells what Yoru is doing (blue while thinking, green when something is done, red when something went wrong), with a faint halo of the same colour on the black around it.
- **Badge:** a small dot on the upper left of the sphere for short-lived status: blue with three dots while working, green when done, red on error.
- **Asleep:** the eyes close and the sphere drifts away, shrinking to a small dim dot in the middle of the screen with a few z's rising from it. The screen is almost all black.

## How it expresses itself

The eyes and the sphere are the whole vocabulary:

| Parameter | What it does |
|-----------|--------------|
| eye `w`, `h`, corner radius | Shape of each eye: tall pills by default, dashes when closed or blank |
| top lid (`amount`, `slope`) | Cuts the eye from above. Sloped inwards it reads as annoyed, outwards as worried, flat and low as sleepy |
| bottom lid (`amount`) | A curve rising from below, giving the `^ ^` happy eyes |
| per-eye scale and angle | Asymmetry: one eye smaller or tilted reads as curious |
| gaze (`x`, `y`) | Where it looks. The eyes slide over the surface and get narrower near the edge, so the sphere seems to turn |
| tilt | The whole face rotates a few degrees |
| body position | The sphere drifts, bobs and leans inside the disc, towards what it pays attention to |
| squash and stretch | Taps, surprises, bounces |
| hands (`shown`, position, scale per hand) | Out or tucked away, and where each one floats. Gestures are short paths for them: wave, point, cover the eyes, clap, stretch |
| blink | Every 2–5 s, sometimes double; never while asleep |
| body colour and state tint | The chosen colour at rest, the state tint and halo faded in and out smoothly |
| distance | How near it is: right here awake, a small dim dot far away when asleep |

Expressions (`EYES_MOOD_*` in the code) are targets for these parameters, and the renderer eases towards them. Whatever picks an expression never draws frames; it only moves the targets.

The hands make the gestures readable: a wave when you come back, a sleepy little wave before it sleeps, a hand pointing at where a sound came from, both hands over the eyes, a hand under the "chin" while thinking, a clap when something is done, a stretch.

Overlays are small extras drawn next to the sphere, never over the eyes: the badge while it works on something, `z`s when asleep, small hearts when stroked, the battery (a ring filling up while it charges, a small battery by its head when it runs low).

There are no dashboard screens that turn Yoru into gauges.

## Behaviour

### Idle
When nothing needs it, Yoru is mostly still, its eyes wandering and blinking on their own. Every several seconds it plays one of its animations, picked at random: a look round, a drift, a tilt, a stretch, a dance, a loop, a bounce off the walls, stargazing, a hum, a firefly it follows and pounces on, a wave, a hop, hearts. Nobody around for a while, it gets sleepy, then naps (Behaviour: "Sleepy after", "Asleep after"). See `docs/idle.md`.

### Depth
Yoru floats in a space seen in perspective. Going away it gets smaller, dimmer and drawn towards the middle; coming close it fills the disc. Moves follow smooth curves through that space, with a wind-up and an overshoot for a dash and a squash for each landing.

### Directed attention
It reacts *towards* things, never at random:
- **Sound:** the two mics give a direction, and the gaze turns to the noise; something loud makes it point.
- **Being lifted or tilted** (IMU): the sphere rolls and the gaze compensates as if gravity were real; shaken, it gets dizzy.
- **Touch:** a tap squashes the sphere, stroking the screen makes it lean towards the finger, a long press: it takes your finger in both hands, and lets go with a wave.
- **Notifications:** an important one makes it come up to the glass and knock; a timer makes it jump like an alarm clock.
- **Menu:** a swipe down from the top edge opens a menu like a watch's: Yoru shrinks into a bubble of a honeycomb of small round apps (notifications, timers, tasks, games, settings, do not disturb).

### Voice
On its own Yoru makes short sounds, not words: chirps, a "huh?", a trill. It speaks only when you call it, through text-to-speech, as a clear, concise and courteous assistant. Its spoken voice is male, like a male EVE (WALL-E): never childlike, never a flat neutral voice.

## Make it yours

The shape never changes: it is always a sphere with two eyes and two hands.

| What | What it changes | Values |
|------|-----------------|--------|
| Name | Shown in Yoru Hub and used by the assistant | Yoru by default |
| Colour | The colour of the sphere at rest; state tints still show over it | Pearl (default), Dawn, Mint, Lilac, Night, Gold, Rose: all available from Settings, Yoru |

## The mascot

The official Yoru is the default look: a pearl-grey matte sphere with two dark pill eyes and two floating hands. It is drawn in Rive with the same parameters as the firmware (eye shape, lids, gaze over the surface, tilt, squash and stretch, hands, state tint, light), so the mascot moves like the one on the desk.

The Rive file is `docs/mascot/yoru.riv` (preview: `yoru.png`), one 466 × 466 artboard. Its state machine, `Yoru`, takes the eye engine's parameters as view-model inputs, so a host can drive it with the same values it sends to the board:

| Input | Type | What it drives |
|-------|------|----------------|
| `gazeX`, `gazeY` | number, -1…1 | The eyes slide over the sphere and narrow towards the edge |
| `lidTop`, `lidSlope` | number, 0…1 and -1…1 | Top lid; positive slope is annoyed, negative is worried |
| `lidBottom` | number, 0…1 | Bottom lid, the `^ ^` happy eyes |
| `eyeScaleL`, `eyeScaleR` | number, 1 = normal | Per-eye scale |
| `tilt` | number, degrees | Rotates the face |
| `squash` | number, -1…1 | Squash (positive) and stretch (negative) of the body |
| `blink` | trigger | One blink |
| `handsShown` | boolean | Hands out or tucked away |
| `gesture` | enum | `none`, `wave`, `point`, `cover` (then peek), `think`, `clap`, `stretch`, `hold`, `shoo`, `flail` |
| `status` | enum | `idle`, `thinking`, `done`, `error`: tint, halo and badge |
| `sleep` | boolean | Drifts away to a small dim dot with closed eyes, and back |

The continuous parameters are bound directly, without easing: as on the board, whoever drives the file eases them.

The expressions, knocks, slaps, lean, greeting and pointer-follow are described in `docs/mascot/STATES.md`.

## On the board

- The sphere's shading for the current colour is rendered into PSRAM, and so is a smaller one for the hands; each frame only redraws the eyes, the tint, the hands and whatever moved. A new colour re-renders the shading.
- Scaling a picture costs LVGL about 80 ms a frame on this chip, so the sphere is never scaled: each size it takes on the way near or far is rendered once, in a few milliseconds, and the sizes in use are kept within about 3 MB of PSRAM.
- Everything around the sphere stays true black.
- Asleep, the far-away dot keeps almost every pixel off.
- The sphere's gradients are dithered as they are rendered, so the panel's 16-bit colour shows no rings; with 24-bit colour on (a setting) they are smooth without it.

## For hackers

- Open firmware. You pick the brain: a model on the computer through Ollama, Groq, OpenAI or Claude, with your own key. Speech-to-text and text-to-speech can run on the computer too (whisper.cpp, Piper). No account and no subscription.
- An MCP server and a tool registry let agents (Claude Code, Claude Desktop, anything that speaks MCP) make Yoru react, speak or show something.
- Integrations with things around the house are added as skills on the computer, not on the board.

## Wake word

His word is «Hey Yoru»: the "hey" gives the two clear syllables of the name something to hold on to. ESP-SR only ships fixed words, so it is a custom model, via microWakeWord ([wakeword.md](wakeword.md)). Since 0.20.18 a first model can be chosen; until it hears well enough to be the default, an ESP-SR word stands in: Alexa or Hi ESP.
