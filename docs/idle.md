# Idle

What Yoru does when nothing else has its attention (`main/idle.c`, `main/idle_acts.c`). Nothing else means: no conversation, no menu or app open, no question on the screen, no reaction playing, no finger on the screen, not face down and no errand under way. `behaviour.c` decides that every 100 ms and hands `idle_step()` what it needs; idle only moves the eyes (`eyes.c`) and plays sounds.

## What plays

Between two acts Yoru waits 6 to 15 seconds (8 to 16 when drowsy), still, with the eyes wandering and blinking on their own. Then it picks the next act at random, by weight:

- **Mostly calm:** rest (stay where it is a while), drift (float somewhere near the middle), a look round, a slow blink at you (gaze), a curious tilt, a peek past the edge (sometimes hide and seek), a sniff in a corner.
- **Now and then:** a wink, a stretch, a dance, a loop, a bounce off the walls, stargazing, a hum, a firefly it follows with its eyes and pounces on (sometimes it gets away), a nod off, a sneeze, hiccups.
- **Rarely:** a wave, a shy peek, a hop, a giggle, a little cheer, hearts.

The rules:

- each act has a cooldown (from none for rest and drift to an hour for cheer and hearts), and the same act never plays twice in a row (resting may go on);
- at night (22:00 to 06:00, once the board knows the time) fireflies and stargazing come twice as often;
- with Do not disturb on, the lively acts (sneeze, hiccups, dance, loop, bounce, hum, hop, giggle, cheer) don't play, and no idle act makes a sound;
- after anything else had the eyes (a touch, a call, a reaction), it waits a moment and, if you were there, starts with a look at you.

Each act is a few phases of moves, faces and sounds, most of them one clip from `clips.c` (`docs/animations.md`), played from the middle.

## Drowsy and the nap

- **Drowsy:** nobody around (no touch, talk, case movement or computer use) for the "Sleepy after" setting, or the battery low. The first act is a yawn; then the eyes are sleepy, it floats a little lower and slower, and it only rests, drifts, nods off, yawns and stretches.
- **The nap:** nobody around for the "Asleep after" setting. It yawns, drifts to a cosy corner low down, gives a sleepy little wave and closes its eyes; `behaviour.c` then puts it to sleep (the screen dims, and goes dark a minute later).

## What interrupts it

- **The knock:** a notification of high or urgent priority, while Yoru is awake and free, brings it up to the glass, its face filling the screen; it knocks twice, looks at you hopefully and waits about 11 to 15 seconds. A tap meanwhile opens the inbox. Then it goes back to the middle.
- **Coming to you:** a touch, the wake word, a reaction or waking up: whatever it was doing stops, and it comes back to the middle if it had wandered off.
- **The greeting:** back after 15 minutes or more with nobody around, or the first time someone is around in the morning (05:00 to 12:00), it comes forward and waves hello (stretching first in the morning). Not with Do not disturb on.
- **Sleep:** asleep, nothing idle plays.

`get_state` reports what it is doing as `activity`: `idle`, `drowsy`, `call` (the knock), `nap`, `sleep`, `come` or `greet`.
