# Yoru — the character

Yoru (夜, "night") is a desk companion that lives in the black of a round AMOLED. It is a pair of glowing eyes, nothing else. The 466 × 466 disc is its head. True black switches the pixels off, so the edge of the case disappears and only the eyes float.

It is a pet and an assistant at the same time. When you don't need it, it lives its own life on your desk. When you call it, it listens, thinks and answers, using models on your own computer or an API you choose.

## Why eyes only

Every successful desk companion shows the same thing: two simple eyes on black, no mouth, no body. This holds for Cozmo/Vector, EMO, Eilik, Loona, Aibi, Stack-chan and Dasai Mochi. Pixar's Carlos Baena removed even the pupils and eyebrows from Cozmo, because a few features are enough to carry emotion. People remember what those robots *do*, not what they look like.

## How it expresses itself

The eye model is the whole vocabulary:

| Parameter | What it does |
|-----------|--------------|
| `w`, `h`, corner radius | Shape of each eye (a rounded "pill") |
| top lid (`amount`, `slope`) | Cuts the eye from above. Sloped inwards it reads as angry, outwards as worried or hot, flat and low as sleepy |
| bottom lid (`amount`) | A curve rising from below, giving the `^ ^` happy eyes |
| per-eye scale | Asymmetry: one eye bigger means curious or sceptical |
| gaze (`x`, `y`) | Where it looks, eased, with saccades |
| tilt | The whole face rotates a few degrees: curiosity, thinking |
| squash and stretch | Pokes, surprises, bounces to music |
| blink | Every 2–5 s, sometimes double; never while asleep |
| colour | One accent colour per state, faded smoothly |

Moods are targets for these parameters, and the renderer eases towards them. The mood engine never draws frames; it only moves the targets.

Overlays are small extras drawn next to the eyes, never instead of them:
- thinking dots while Claude Code or the LLM is working;
- `z`s when asleep;
- a sweat drop when the CPU is hot;
- small hearts when it is petted.

There are no dashboard screens that turn the eyes into gauges. PC data changes how Yoru *feels*, not what it shows.

## Behaviour

### Alive when ignored
- It looks around, blinks, and now and then sighs or stretches (squash, then a big blink).
- If nothing happens for a long time it gets bored, then drowsy, then falls asleep.
- At night it sleeps for real: its eyes dim and the screen is almost all black, which costs almost no power.

### Directed attention
It reacts *towards* things, never at random:
- **Sound:** the two mics give a direction, and the gaze turns to the noise.
- **Being lifted or tilted** (IMU): the gaze compensates as if gravity were real.
- **Shaking:** it gets dizzy (spiral eyes) and then grumpy.
- **Touch:** stroking the screen makes it happy and it leans its eyes towards the finger. A tap is a poke: once is fine, three in a row annoy it. A long press puts it to sleep, or into focus mode.
- **Music:** it bobs to the beat.
- **Settings:** a swipe down from the top edge opens a round menu for brightness, volume, the microphone and more. Yoru peeks at it from the edge while it is open (see the roadmap, M2).

### Moods from real life
- Happy when builds and tests pass, or when Claude Code finishes a task.
- Thinking while Claude Code works, focused when you are in a long session.
- Hot and worried when the CPU or GPU runs hot.
- Impressed, then tired, as the day's token usage grows.
- It greets you when you come back to the computer, and notices when you work late.

### Character
Yoru is curious, a bit sarcastic, and competent: an engineer friend, not a baby. It is a little imperfect on purpose (it misses a blink, gets startled, sulks briefly), because perfect is boring. It is **never needy**: it doesn't nag, doesn't starve, doesn't die. Neglect makes it sulk mildly at worst.

### Voice
The pet speaks in **sounds, not words**: short musical chirps, a sigh, a "huh?", a happy trill. Real speech, through text-to-speech, is reserved for when you call the assistant with the wake word. This keeps expectations honest: the companion never pretends to chat.

### Growing up
Activity unlocks things, with no chores involved:
- real work (Claude Code sessions, passing builds, days together) earns experience;
- experience unlocks eye styles, colours, rare animations and new reactions.

Updates add animations over time, so it keeps surprising people.

## For hackers

- Open firmware. Local brain by default (llama.cpp, local speech-to-text and text-to-speech), any OpenAI-compatible API if you prefer. No account and no subscription, and it keeps working if any cloud disappears.
- Moods, eye shapes and reactions are data that can be edited, shared and modded.
- An MCP server and a tool registry let agents (Claude Code, Claude Desktop, anything that speaks MCP) make Yoru react, speak or show something.
- Integrations with things around the house are added as skills on the computer, not on the board.

## Wake word

"Yoru" works as a wake word: two syllables with clear vowels. ESP-SR only ships fixed words, so it needs a custom model, via microWakeWord (see the DualEye wake-word study). Until then, an ESP-SR word stands in.
