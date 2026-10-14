# Firmware changelog

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
