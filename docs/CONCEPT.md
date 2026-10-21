# Yoru — the character

Yoru (夜, "night") is a little night creature that lives in the black of a round AMOLED. It is a small, matte sphere with two tiny dark eyes and two little floating hands, in the middle of the 466 × 466 disc. True black switches the pixels around it off, so the edge of the case disappears and only the little ball seems to hover on your desk.

It is a pet and an assistant at the same time. When you don't need it, it lives its own life on your desk. When you call it, it listens, thinks and answers, using models on your own computer or an API you choose.

## Why a sphere with two eyes and two hands

Every successful desk companion shows very little: two simple eyes, no mouth, no limbs. This holds for Cozmo/Vector, EMO, Eilik, Loona, Aibi, Stack-chan and Dasai Mochi. Pixar's Carlos Baena removed even the pupils and eyebrows from Cozmo, because a few features are enough to carry emotion. People remember what those robots *do*, not what they look like.

Yoru keeps that rule and adds one thing: a body, as simple as a body can be. The current wave of AI characters (OpenAI's dots, Meta's Jolly, the Grok Bot concept) shows that a soft, toy-like little guy makes an assistant easy to like and to trust. A sphere is the most toy-like shape there is, it reads at any size from the board to an app icon, and it gives the eyes a surface to move on, which is where most of the life comes from. Two floating hands, like Rayman's, give it gestures (a wave, a point, covering its eyes) without adding arms or a body that would make it less simple.

The sphere is smaller than the screen on purpose (about 40% of the disc on the board): the black around it is what makes it float, leaves room for the hands, lets it move, bounce and peek, and keeps the screen cheap to run and safe from burn-in.

## How it looks

- **Body:** a matte sphere lit softly from the top left, with a gentle shadow towards the bottom edge. No outline, no gloss by default.
- **Eyes:** two small, dark, vertical pills, close together and a little below the centre. They are holes in the light, not lights themselves.
- **Hands:** two small spheres of the same material, a bit flattened, floating beside the body with nothing joining them. They are not always there: they pop out when Yoru needs to gesture and tuck back behind the body when it rests, so most of the time it is just the ball.
- **State tint:** the lower part of the sphere fills with a colour that tells what Yoru is doing (blue while thinking, green when something is done, red when something went wrong), and a faint halo of the same colour glows on the black around it.
- **Badge:** a small dot on the upper left of the sphere for short-lived status: blue with three dots while working, green when done, red on error.
- **Asleep:** Yoru closes its eyes and drifts away into the dark, shrinking quickly at first and then slowly, until it is a small dim dot in the middle of the screen with a few z's rising from it. The screen is almost all black. Waking up, it comes back.

## How it expresses itself

The eyes and the sphere are the whole vocabulary:

| Parameter | What it does |
|-----------|--------------|
| eye `w`, `h`, corner radius | Shape of each eye: tall pills by default, dashes when closed or blank |
| top lid (`amount`, `slope`) | Cuts the eye from above. Sloped inwards it reads as angry, outwards as worried, flat and low as sleepy |
| bottom lid (`amount`) | A curve rising from below, giving the `^ ^` happy eyes |
| per-eye scale and angle | Asymmetry: one eye smaller or tilted means curious or sceptical |
| gaze (`x`, `y`) | Where it looks. The eyes slide over the surface and get narrower near the edge, so the sphere seems to turn its head |
| tilt | The whole face rotates a few degrees: curiosity, thinking |
| body position | The sphere drifts, bobs and leans inside the disc, towards what it pays attention to |
| squash and stretch | Pokes, surprises, bounces to music |
| hands (`shown`, position, scale per hand) | Out or tucked away, and where each one floats. Gestures are short paths for them: wave, point, cover the eyes, hold the cheeks, clap, stretch |
| blink | Every 2–5 s, sometimes double; never while asleep |
| body colour and state tint | The owner's colour at rest, the state tint and halo faded in and out smoothly |
| distance | How near Yoru is: right here awake, a small dim dot far away when asleep |

Moods are targets for these parameters, and the renderer eases towards them. The mood engine never draws frames; it only moves the targets.

The hands are what makes Yoru's gestures readable:
- **Hello and goodbye:** a wave when you come back, a sleepy little wave before it falls asleep.
- **Pointing:** a hand points at where a sound came from, or at the edge where the settings menu opens.
- **Shy and scared:** both hands cover the eyes, then one slides down to peek.
- **Thinking:** one hand under the "chin", the eyes up.
- **Happy and proud:** a little clap, or both hands up when a build passes.
- **Stretching:** hands far out on both sides, the body stretched tall, then a big blink.
- **Being stroked:** the hands tuck in and the body leans into the finger.

Overlays are small extras drawn next to the sphere, never over the eyes:
- the badge while Claude Code or the LLM is working;
- `z`s when asleep;
- a sweat drop when the CPU is hot;
- small hearts when it is petted.

There are no dashboard screens that turn Yoru into gauges. PC data changes how Yoru *feels*, not what it shows.

## Behaviour

### A life of its own
Yoru doesn't play random animations. Like a Tamagotchi, a Sims character or Cozmo, it has needs, and what it does comes from them (`main/needs.c`, `main/life.c`):

| Need | Runs down | Met by |
|------|-----------|--------|
| Rest | while awake, faster when busy or awake at night | sleeping |
| Food | slowly, over hours | fireflies: it hunts them in the dark, or you drop one with a tap on the black |
| Fun | when nothing happens | playing, music, you |
| Company | while nobody is around | touch, talk, being picked up, you at the computer |
| Curiosity | over time | exploring, sounds, what happens on the computer |

Each thing it can do scores by how well it meets the needs that press most, as the Sims pick; it does it for a while, then picks again, and the same thing never comes twice in a row:
- **Floating about** near you, the calm default.
- **Exploring:** off into the distance, to the rim of the screen to peek past it, and back.
- **Hunting:** a firefly lights up far away; it notices, creeps up, chases it as it flits, pounces and gulps it down.
- **Asking for you:** it comes up to the glass, its face filling the screen, and knocks twice: whimpering when hungry, waving when lonely. If nobody answers it sighs and goes back, and asks less often next time.
- **Playing:** hopping along the bottom of the disc as if it had a floor; hiding past the edge and peeking back from another; a loop through the space, far behind and right up to the glass.
- **Daydreaming**, **sulking** by the edge when it's unhappy, **dancing** to music.
- **Tired**, it yawns, stretches, finds a corner and drifts off; it wakes by itself once rested (not alone in the middle of the night), stretching.

Well cared for, it is happier; neglected, it gets hungry, sad or grumpy, but it never starves: it feeds and amuses itself, only less happily.

### Depth
Yoru floats in a space seen in perspective, not on a flat screen. Going away it gets smaller, dimmer and drawn towards the middle, as things do in the distance; coming close it fills the disc. Moves follow smooth curves through that space, with a wind-up and an overshoot for a dash, a squash for each landing; it stretches along the way it goes and its eyes lead the way.

### Alive when ignored
- Left alone (nobody touching, talking or at the computer) it gets drowsy, then naps (Behaviour: "Sleepy after", "Asleep after").
- At night it sleeps for real: it drifts away to a small dot, which costs almost no power.

### Directed attention
It reacts *towards* things, never at random:
- **Sound:** the two mics give a direction, and the gaze turns to the noise; something loud makes it point.
- **Being lifted or tilted** (IMU): the sphere rolls and the gaze compensates as if gravity were real.
- **Shaking:** it gets dizzy (spiral eyes) and then grumpy.
- **Touch:** stroking the screen makes it happy and the sphere leans towards the finger. A tap is a poke: the ball squashes and wobbles; once is fine, three in a row annoy it. A long press puts it to sleep, or into focus mode.
- **Music:** it bobs to the beat and the hands sway with it.
- **Menu:** a swipe down from the top edge opens a menu like a watch's: Yoru shrinks into a bubble of a honeycomb of small round apps (notifications, timers, habits, settings, do not disturb, agents), one of them rather than their centre, blinking and looking at the one under your finger; the honeycomb can be dragged, and shrinks towards the rim so more apps fit (see the roadmap, M2).

### Moods from real life
- Happy when builds and tests pass, or when Claude Code finishes a task.
- Thinking while Claude Code works, focused when you are in a long session.
- Hot and worried when the CPU or GPU runs hot.
- Impressed, then tired, as the day's token usage grows.
- It greets you when you come back to the computer, and notices when you work late.

### Character
Yoru is a small, soft creature: sweet, playful and endlessly curious, a little clumsy, easily amazed. It is cute on purpose, the way a toy is cute, because a little friend you want to look at is easier to trust with your day. It is a little imperfect on purpose (it misses a blink, gets startled, rolls a bit too far, sulks for a moment), because perfect is boring.

Cute does not mean helpless. When you call the assistant it is still useful and to the point; only its manners are childlike, not its answers. And it is **never needy**: it doesn't nag, doesn't starve, doesn't die. Neglect makes it sulk mildly at worst.

### Voice
The pet speaks in **sounds, not words**: short musical chirps, a sigh, a "huh?", a happy trill. Real speech, through text-to-speech, is reserved for when you call the assistant with the wake word. This keeps expectations honest: the companion never pretends to chat.

## Make it yours

Every Yoru is a little different, because its owner shapes it. The shape never changes: it is always a sphere with two eyes and two hands, so it is always recognisable. Nothing is locked behind money; some pieces are unlocked by spending time together (see Growing up).

| What | What it changes | Examples |
|------|-----------------|----------|
| Name | Shown in the app and in the greeting | Yoru by default; any name you like |
| Colour | The colour of the sphere at rest; state tints still show over it | pearl (default), mint, orange, red, violet, night blue |
| Eyes | The base shape of both eyes | pills (default), round dots, small squares, sleepy half-moons |
| Finish | How the sphere catches the light | matte (default), soft glow, pearl, speckled |

The name you choose is what it answers to on screen; the wake word stays "Yoru" until custom wake words exist. A look is a small piece of data, so it can be shared and modded like moods.

### Growing up
Activity unlocks things, with no chores involved:
- real work (Claude Code sessions, passing builds, days together) earns experience;
- experience unlocks colours, eye shapes, finishes, rare animations and new reactions.

Updates add animations over time, so it keeps surprising people.

## The mascot

The official Yoru is the default look: a pearl-grey matte sphere with two dark pill eyes and two floating hands. It is drawn in Rive with the same parameters as the firmware (eye shape, lids, gaze over the surface, tilt, squash and stretch, hands, state tint, light), so the mascot on the website, in the app and on the box moves exactly like the one on your desk.

When Yoru's agent splits work into parts, or Claude Code runs sub-agents, each part shows up in the app as a small sphere of its own colour next to Yoru, the same character in a different coat.

The Rive file is `docs/mascot/yoru.riv` (preview: `yoru.png`), one 466 × 466 artboard. Its state machine, `Yoru`, takes the eye engine's parameters as view-model inputs, so the host or the app can drive it with the same values it sends to the board:

| Input | Type | What it drives |
|-------|------|----------------|
| `gazeX`, `gazeY` | number, -1…1 | The eyes slide over the sphere and narrow towards the edge |
| `lidTop`, `lidSlope` | number, 0…1 and -1…1 | Top lid; positive slope is angry, negative is worried |
| `lidBottom` | number, 0…1 | Bottom lid, the `^ ^` happy eyes |
| `eyeScaleL`, `eyeScaleR` | number, 1 = normal | Per-eye scale |
| `tilt` | number, degrees | Rotates the face |
| `squash` | number, -1…1 | Squash (positive) and stretch (negative) of the body |
| `blink` | trigger | One blink |
| `handsShown` | boolean | Hands out or tucked away |
| `gesture` | enum | `none`, `wave`, `point`, `cover` (then peek), `think`, `clap`, `stretch`, `hold`, `shoo`, `flail` |
| `status` | enum | `idle`, `thinking`, `done`, `error`: tint, halo and badge |
| `sleep` | boolean | Drifts away to a small dim dot with closed eyes, and back |

The continuous parameters are bound directly, without easing: as on the board, the mood engine moves the targets and whoever drives the file eases them.

The moods, knocks, slaps, lean, greeting and pointer-follow added with firmware 0.5.0 are described in `docs/mascot/STATES.md`.

## On the board

- The sphere's shading for the current colour and finish is rendered into PSRAM, and so is a smaller one for the hands; each frame only redraws the eyes, the tint, the hands and whatever moved. A new colour or finish re-renders the shading.
- Scaling a picture costs LVGL about 80 ms a frame on this chip, so the sphere is never scaled: each size it takes on the way near or far is rendered once, in a few milliseconds (a table for the shading, no square roots), and the sizes in use are kept within about 3 MB of PSRAM.
- Everything around the sphere stays true black, as before.
- Asleep, the far-away dot keeps almost every pixel off.
- The sphere's gradients are dithered as they are rendered, so the panel's 16-bit colour shows no rings.

## For hackers

- Open firmware. Local brain by default (llama.cpp, local speech-to-text and text-to-speech), any OpenAI-compatible API if you prefer. No account and no subscription, and it keeps working if any cloud disappears.
- Moods, looks (colours, eyes, finishes) and reactions are data that can be edited, shared and modded.
- An MCP server and a tool registry let agents (Claude Code, Claude Desktop, anything that speaks MCP) make Yoru react, speak or show something.
- Integrations with things around the house are added as skills on the computer, not on the board.

## Wake word

"Yoru" works as a wake word: two syllables with clear vowels. ESP-SR only ships fixed words, so it needs a custom model, via microWakeWord (see the DualEye wake-word study). Until then, an ESP-SR word stands in.
