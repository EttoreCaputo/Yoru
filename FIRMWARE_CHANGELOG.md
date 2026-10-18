# Firmware changelog

## 0.8.0

Yoru is no longer a Tamagotchi, for now: it becomes the companion of your coding agents, and the pet comes back later.

- **No more needs, moods or fireflies.** Yoru doesn't get hungry, bored or lonely any more, doesn't hunt fireflies, play, explore, sulk or dance on its own, and its mood no longer changes with what happens on the computer or in the room. A tap on the black does nothing.
- **What it still does.** It floats calmly near the middle and turns to sudden sounds. It reacts to touch (poke, stroke, slap) and to the case (lift, shake, knock, drop, face down). It gets sleepy and falls asleep when nobody is around. It does everything for the agents: the dots, the steps, the cards, the agents view, the nod, the wince, and knocking on the glass when one waits for you. It no longer wakes up by itself: a touch, the voice, lifting it or an agent wakes it.
- **Menu.** The Yoru page (needs and "Give a firefly") and "Computer reactions" are gone. "Hears the room" is now "Turns to sounds".
- For the computer: the `feed`, `get_pet` and `set_pet` tools are gone, and `get_state` reports `activity` instead of `pet`. Sounds and turning to sounds are still set with `set_setting` (`pet_sounds`, `pet_hear`).

## 0.7.2

- **Smoother agents view.** The little Yorus hop and squash in a few fixed steps, each drawn once, instead of a new shape almost every frame. With six agents on screen the animation no longer stutters, and the view uses less memory.

## 0.7.1

- **The agents view is a little family of Yorus.** Swipe up from the bottom edge (or press BOOT, or open Agents in the menu) and instead of a list you see one small Yoru for each agent session, in its agent's colour: Claude orange, Gemini yellow, Codex/GPT blue, Cursor green, OpenCode violet, Copilot white, Windsurf teal, Aider pink; any other agent is pearl like Yoru. Each one acts out what its agent is doing: bobbing with thinking dots while it works, hopping with a "!" when it needs you, smiling ^ ^ with a little twinkle when it's done, x x eyes and a shiver with a red badge when something failed, and dozing with a rising "z" when idle. Its project's name is underneath, in amber while it waits for you.
- Tap a little Yoru to bring its terminal to the front; hold it until a red ring appears ("Tap to stop"), then tap again to interrupt the agent. "Quiet 1 h" and the Claude plan's limits are at the bottom. Swipe right or down, or tap the title, to close it.

## 0.7.0

Yoru keeps an eye on your coding agents: Claude Code, Codex, Gemini CLI, Cursor and OpenCode, through Companion Hub.

- **A dot for each agent.** Along the top of the rim, one dot per session on the computer: blue and breathing while it works, amber and pulsing when it needs you, green when it's done, red when something failed.
- **What it's doing, at the bottom.** While an agent works, the step it's on shows over the bottom rim ("edit agents.c +42 -3", "run idf.py build"), in red if it failed. Companion Hub's messages (it finished, it needs you, the plan's limits are running out) show there too.
- **The agents view.** Swipe up from the bottom edge, press BOOT, or open Agents in the menu: every session with its project, what you asked it, its last step and how long ago. Tap one and its terminal comes to the front on the computer. Hold it, then tap again, and the agent is interrupted (as pressing Esc there). "Quiet for 1 hour" stops the agents' sounds, alerts and knocks for an hour; tap it again to end it. Below, the Claude plan's 5-hour and weekly limits. Swipe right or down, or tap the title, to close it.
- **BOOT is a shortcut.** A press brings up the terminal of the agent waiting for you, or the agents view when none is waiting. Held for a second, it turns "quiet" on or off. It can be switched off in Agents.
- **Always.** When Claude Code offers to remember a permission, the card has Always under Deny and Allow. A request too long to show in full (a long or multi-line command) can only be denied on Yoru: "On computer" means you read all of it there before allowing it.
- **Questions.** When an agent asks you to choose (Claude Code's questions with options), Yoru shows the question and its choices: tap one, or tick several and Send. With more than one question it goes through them in turn. "Terminal" lets you answer there instead.
- **Yoru reacts to them.** Tinted blue while an agent works, it claps when a turn ends and winces when one fails. When an agent waits for you and no card is up, it comes to the glass, knocks and points at the computer, every minute until you answer; touching it then brings that terminal to the front. While an agent waits, Yoru's own whims step back.
- **Agents in the menu,** second on the ring: open the view, quiet for an hour, and switches for the dots, the steps at the bottom, the agents' sounds and the BOOT button.
- For Companion Hub: the `agents` capability, the `set_agents`, `show_toast`, `show_question` and `hide_question` tools, `always` and `full` on `show_approval`, and the `answer` and `shortcut` notifications (`docs/protocol.md`). `get_state` reports the agents. Older Companion Hub versions keep working with Allow and Deny only.

## 0.6.0

Yoru has a life of its own, and the whole screen to live it in.

- **Needs, like a Tamagotchi.** Yoru now needs rest, food, fun, company and something new to look at. Each runs down with time and with what it does, and what it does next comes from what it needs most, not from chance: hungry, it hunts; bored, it plays; curious, it explores; tired, it naps; lonely, it comes to ask for you. Well looked after it is happier; left alone it gets hungry and a bit sad, but it never starves: it feeds and amuses itself. The levels survive a restart.
- **Fireflies.** Yoru eats little lights in the dark. Hungry, it spots one far away, creeps up on it, chases it as it flits and gulps it down. Tap the black anywhere (not Yoru) and a firefly drops there for it: it rushes to eat it, likes you for it, bats it away for fun when it isn't hungry, and shakes its head when it's full. "Give a firefly" in the new Yoru page of the menu, and the computer (`feed`), do the same.
- **It asks for you.** When it is hungry, lonely or bored and you're around, it comes right up to the glass, its face filling the screen, and knocks twice: a whimper when hungry, a wave when lonely. Touch it to answer, or feed it. Nobody answers, it sighs, goes back, and asks less often.
- **Depth.** Yoru floats in a space, not on a flat picture: far away it is small, dim and drawn towards the middle; up close it fills the screen. It glides along smooth curves, dashes with a little wind-up and overshoot, hops with a squash at each landing, stretches as it speeds along and looks where it's going.
- **Play.** It hops along the bottom of the screen as if it had a floor, plays peekaboo (gone past the edge, then just its face peeking over the rim from another side, then "boo!"), and loops through the space, far behind and right back up to the glass. It explores the distance and goes to the rim to peek past it, daydreams, sulks by the edge when it's unhappy, and dances when music plays.
- **Sleep that makes sense.** Tired, it yawns, stretches, finds a corner and drifts off; rested, it wakes by itself and stretches (not alone in the middle of the night). "Sleepy after" and "Asleep after" now count from the last sign of someone around, a touch, a voice or you using the computer, and are 5 and 10 minutes by default, so it has time to live while you work. Settings you changed are kept.
- **Yoru page in the menu**, first on the ring: what it's doing and its five needs as bars, and "Give a firefly".
- A new mood, hungry. `get_pet` reports the needs, what Yoru is doing and how long nobody has been around.
- Under the hood: the sphere is drawn at any size without slow scaling, so moving near and far stays smooth; the squashed spheres are no longer prepared at startup, which frees about 2 MB of memory for the sizes in use.

## 0.5.4

Answer Claude Code's permission requests on Yoru.

- **Allow or Deny on the screen.** With "Answer permissions in the app and on Yoru" on in Companion Hub, when Claude Code asks to run something, Yoru shows a card: the tool on top, what it wants to do (a command, a file), and Deny and Allow below, with the project and the seconds left. A ring along the edge of the screen runs down the time and turns red for the last ten seconds.
- **Tap to answer.** Yoru nods when you allow and shakes its head when you deny. If you answer in the app or in the terminal first, the card goes away by itself; it also goes when the time is up, and Claude Code then asks in the terminal.
- **Yoru waits with you.** While a request is up it wakes if it was asleep, shrinks to the middle and glances between the question and the buttons. Pokes, the menu and the power key's sleep wait until it's answered.
- Accented letters are shown without accents; characters the screen's font doesn't have show as `?`.

## 0.5.3

A test switch for a faster screen.

- **About → "80 MHz bus (test)".** Runs the link to the screen at 80 MHz instead of 40, which halves the time each picture takes to reach it. It's a test: the screen's chip is rated for 50 MHz at most, so it may work on your board, show wrong colours or garbage, or stay black. Flipping the switch restarts Yoru to apply it.
- **If the screen becomes unreadable:** restart Yoru three times in a row (reset button or power), each within 20 seconds of the last start, and it goes back to 40 MHz. The host can also turn it off (`set_setting` `lcd_80mhz` to 0), and a factory reset does too.
- The boot log says which speed the screen runs at.

## 0.5.2

No more tearing.

- **Smoother screen.** Yoru used to be sent to the screen in thin strips while the next ones were still being drawn, and while the screen was busy showing the previous picture, so a moving Yoru could show half in one place and half in another for a moment. Now each picture is drawn whole first and sent right as the screen starts a new refresh (60 times a second), top to bottom the way the screen draws, so every movement shows in one piece. The same in every rotation.
- Turning the screen no longer needs extra memory.

## 0.5.1

The menu is easier to get out of.

- **Swipe up closes the menu again.** On the ring of icons the swipe up never arrived, so the menu only closed by waiting. It works now, anywhere on the ring.
- **Leaving a setting.** On the arc screen (brightness, volume, the hours, the waits) a tap in the middle now goes back to the page; before, the arc took the tap and moved the value instead. Only the ring at the rim moves the value now.
- **Swipe right to go back,** on a page and on the arc screen, besides tapping the title or the middle.
- With the menu open Yoru shrinks to a smaller ball in the middle, clear of the icons, and grows back when it closes.

## 0.5.0

A menu on the board, and Yoru feels how you hold it.

- **Settings menu.** Swipe down from the top edge of the screen: a ring of icons appears around Yoru, who looks up at it with small eyes. Tap an icon for its page: Display, Sound, Behaviour, Voice, Connection, Battery, About. Rows fade out towards the edge of the circle; tap a switch to flip it, tap a value (brightness, volume, the night hours, how soon it gets sleepy) and drag your finger along the arc around the rim. Everything applies at once and is remembered. Tap the title to go back; swipe up on the ring, or leave it alone for a few seconds, and it closes. Labels are in English or Italian (Voice, Language).
- From the menu: night brightness and hours now work (when the computer sends the time), the wake word can be switched off or changed, the screen can be turned a quarter at a time or follow how the badge stands (Rotation, Automatic), and About shows the version, free memory, flash and screen load, with Restart and a Factory reset you confirm by holding the row.
- **Battery and PWR.** The Battery page shows the charge, whether it's on USB or charging, and the voltage, and Power off (hold the row). A short press of PWR puts Yoru to sleep and wakes it, or closes the menu.
- **Motion.** Pick Yoru up and it wakes, curious, looking at you; put it down and it settles with a blink. Tilt it and its eyes roll towards the low side. Shake it or spin it on the desk and it gets dizzy: spiral eyes rolling round, little stars circling its head, hands holding its head; keep shaking and it gets annoyed. A knock on the case is a poke, two knocks call it. Rock it gently, twice, and it falls asleep. Drop it and it panics, flapping its hands, then sighs with relief. Turn it face down and it goes to sleep with the microphone off; turn it back and it wakes. "Motion reactions" in Behaviour turns these off (face down still works: it's the privacy gesture).
- **Slaps.** A quick flick across the face knocks Yoru aside and it springs back; at the third in a row it pushes you away with a hand. Three pokes in a row now do the same.
- **Sound direction.** A sudden sound from one side (a clap, a door) makes Yoru glance that way.
- Scared now has its own face: tall, worried eyes and a trembling body.
- New reactions for the computer: dizzy, annoyed, falling, drowsy, lifted. The host can open the menu (`show_menu`), and `get_state` now reports the battery and the motion sensor.
- To check on your board: which way is "left" for the tilt and for sounds depends on how the parts sit on the board. If the eyes roll uphill, or turn away from a clap, tell us and it's a one-line fix.

## 0.4.3

Smoother colours, a new way to sleep.

- No more rings on the sphere: the screen can only show a limited number of shades, and the smooth pearl gradient came out in visible bands. The shading is now dithered with a pattern too fine to see, and so are the coloured tint and the halo.
- Falling asleep, Yoru no longer turns into a dark moon. It closes its eyes and drifts away into the dark, until it is a small dim dot in the middle of the screen with little z's rising from it. When it wakes up it comes back, waving hello.

## 0.4.2

Smoother moves.

- Pokes, surprises, stretches and yawns no longer play in slow motion. Squashing the sphere used to take the screen about 80 ms a frame, so everything else waited; the squashed shapes are now prepared in the background a few seconds after startup and simply swapped in.
- Every animation now follows the clock instead of counting frames: if the board is briefly busy, Yoru skips ahead instead of slowing down.
- The sphere and the hands are kept in the screen's own colour format, so even the gentle breathing costs less to draw.

## 0.4.1

Just Yoru.

- Yami, the alter ego, is gone: Yoru no longer turns into the black cat mask. The BOOT button does nothing again, and the "yami" and "yoru" reactions from the computer are no longer recognised.
- With the mask's pictures no longer prepared in the background after startup, a little more memory stays free for everything else.

## 0.4.0

Yoru has a body, two hands and an alter ego.

- The new look: instead of two glowing eyes, Yoru is a small pearl-grey sphere floating in the black, lit from the top left, with two little dark eyes that slide over its surface (they narrow near the edge, so it seems to turn its head) and the same moods as before: happy, curious, grumpy, sleepy, sad, surprised and the rest. It breathes gently while it's awake.
- Hands: two small floating hands pop out for gestures and tuck away again. It waves when it wakes up or says hello, claps when it's excited or proud, covers its eyes and then peeks when it's scared or shy, puts a hand under its chin while it thinks, points, and stretches with a yawn.
- Status tint: while it's thinking about your question, the bottom of the sphere turns blue with a soft halo and a small breathing dot; after an answer it turns green for a moment, and red when something went wrong.
- Asleep, the sphere goes dark like a new moon, with only a thin lit crescent, and its eyes become two dim dashes.
- Yami, the alter ego: press the BOOT button (or ask from the computer with the "yami" reaction) and green cracks run over the ball, it breaks into pieces that fly apart and snap together into a black cat mask lit from behind, with its eyes as two glowing holes. Press again (or "yoru") to turn back. Yami has the same expressions as Yoru.
- New reactions from the computer: wave, point, shy, clap, stretch, yami and yoru.
- Yoru now takes about 40% of the screen, so it floats in the black with room around it, and startup is about a second longer while its pictures are prepared.

## 0.3.3

A lighter touch.

- Yoru no longer asks the touch screen for news 60 times a second: the screen now signals when a finger lands, and only then is it read, much faster than before. The chip spends far less time waiting, awake and asleep, and touch, strokes and pokes respond as before.

## 0.3.2

A lighter sleep.

- While Yoru sleeps, only the little floating z's are redrawn, not the whole face: the screen work drops sharply, which leaves the chip more time to listen and costs less power. The same goes for the thinking dots, the sweat drop and the hearts. Nothing changes in how it looks.

## 0.3.1

More room to breathe, for the WiFi and what comes next.

- Yoru uses a lot less of the chip's fast internal memory: buffers and tasks that don't need it moved to the larger external memory. Nothing changes in how it looks or behaves; it leaves room for the WiFi and the settings menu.
- Fix: the audio self-test's tones no longer startle Yoru. It used to hear them as bangs in the room and get upset.

## 0.3.0

Yoru can hear you and talk to a computer.

- Wake word: say "Alexa" (or "Hi ESP", if you switch to it) and Yoru wakes up if it was asleep, opens its eyes wide and looks at you while you speak. Echo cancellation keeps its own voice from waking it.
- Conversation eyes: wide and attentive while it listens, thinking dots while it waits for an answer, warm eyes that swell with its voice while it speaks, a sad look if something went wrong. Touching it doesn't interrupt a conversation.
- It connects to a computer over USB, or over WiFi once it has been set up over USB, with the same encrypted link as DualEye. The computer side (the app) comes in a later version; for now the test scripts in `tools/` talk to it.
- From the computer you can read its state and mood, change any setting, make it react, play its sounds, set the volume and brightness, mute the microphone and pick the wake word.
- It has a voice of its own: little sounds when you poke it (a boop), stroke it (a purr), annoy it (a grumble), and with its reactions and whims (a happy chirp, a yawn, a sigh, a gasp...). Focus mode and the pet-sounds setting silence them; chimes still play.
- It says hi with a sound when a computer connects, and plays a short sound at startup.
- It listens to the room between conversations, as DualEye does: a bang startles it, claps call it, people talking keep it company, a long silence makes it drowsy. Muting the microphone stops this too.
- If the computer sends its mouse position, Yoru's eyes follow the pointer.

## 0.2.0

Yoru now has a mood that lasts, and remembers its settings.

- A mood of its own: how lively, cheerful and attached to you it is. It survives a restart and drifts back slowly over time.
- Its eyes rest in a look that suits the mood (bored, grumpy, sad or sleepy when it's like that) and its little whims follow the mood too, coming more often when it's lively.
- Petting counts: stroking it makes it more affectionate, and a well-loved Yoru sometimes answers with pink eyes and floating hearts. Poking it too much makes it grumpier for a while.
- New expressions: sad, loving, surprised and bored.
- Settings are saved on the board: brightness, dimming when asleep, volume, how soon it gets sleepy and falls asleep, focus mode (no whims, reactions or startup chirp) and touch reactions. The menu to change them comes next.
- Fix: the eyes no longer dart around every quarter of a second; they settle on a spot for a second or more, as intended.

## 0.1.0

First light on the Waveshare ESP32-S3-Touch-AMOLED-1.75C.

- Yoru opens its eyes: two glowing eyes on the black AMOLED that look around, blink, and now and then get curious or lost in thought.
- Touch: tap it and it's happy; tap it three times in a row and it gets grumpy. Move a finger on the screen and it follows it; stroke it and it's happy. A long press puts it to sleep, a tap wakes it up.
- Left alone it gets sleepy after 45 seconds and falls asleep after two minutes, dimming the screen.
- A short chirp from the speaker when it starts.
