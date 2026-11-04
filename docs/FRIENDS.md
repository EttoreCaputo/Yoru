# Friends: Yorus that meet

Two or more Yorus in the same house find each other, greet each other and, later, play together and know where the other one is. Without the Mac and without Bluetooth.

## Why ESP-NOW, not BLE

- **RAM.** Bluetooth is off in the firmware (`CONFIG_BT_ENABLED`), and its controller and host would take tens of KB of internal RAM, which WiFi and ESP-SR already leave short. ESP-NOW runs on the WiFi radio that is already on and costs next to nothing.
- **Channel.** Boards on the same WiFi are on the access point's channel, so ESP-NOW reaches them while they stay connected. Messages take a few milliseconds.
- **Without the host.** It works with Yoru Hub closed, as the voice does since 0.14.0.
- **BLE's one advantage**, a rough distance from the signal strength, ESP-NOW has too: every message comes with its RSSI. It only tells "near" from "in another room".

What it doesn't do: reach a board with no WiFi set up (ESP-NOW starts when the WiFi has an address), or one that is hibernating (the radio is off).

## Steps

### 1. Hello (0.18.0, done)

`main/friends.c`. Every 4 s each board broadcasts a hello: magic `YORU`, version 1, type `hello`, whether it is awake, its firmware. Whoever hears it keeps it in a table of six (MAC, RSSI, last heard, awake, firmware). A friend not heard for 30 s is gone.

- **Greeting.** A friend heard for the first time, or again after 30 minutes apart, gets a wave (`wave`, the hand and the hello sound) and lifts Yoru's mood (the `friend` event: happy, company and excitement, not trust, which is for you). Only while Yoru is free: awake, no conversation, menu, card or reaction, not in do not disturb. Until then the friend waits.
- **`get_state`** has `friends`: `[{id, rssi, seen_s, awake, firmware}]`.
- **Radio.** ESP-NOW starts on `IP_EVENT_STA_GOT_IP` (the channel is the access point's from then) and stops on `WIFI_EVENT_STA_STOP` (WiFi off, hibernating).
- **Paused in 0.19.0 and 0.19.1**: off at every boot; since 0.19.1 `debug/friends` `{"on": true}` turns it on until the next restart, for the test below. On 2026-11-04, with two boards on 0.18.0 saying hello on the home router's 2.4 GHz network (channel 11), that network went down twice: the boards lost the access point (reason 201, not found) while still hearing each other, and the Mac no longer saw it. With the hello off on the one board within reach, the network came back. To find out before turning it back on: whether broadcast action frames every 4 s really upset that router (one board at a time, a longer interval, unicast once met), or whether it was something else.
- **Test on a phone hotspot, 2026-11-04.** Both boards on 0.19.1, on an iPhone's hotspot (2.4 GHz), the hello on through `debug/friends`: they met within seconds (RSSI about -65 dBm, each heard every 4 s) and over four minutes the hotspot lost no ping, neither board left the WiFi and both links stayed up. So the hello itself is harmless to an access point; the home router was unstable that day anyway. On by default again from 0.19.2.

### 2. The bump (0.20.0, done)

Touching the two cases together introduces them. Each IMU sees a sharp jolt along the glass (`MOTION_JOLT`, `motion.c`: over 0.7 g in the screen's plane, from rest or held, at most every 400 ms) and says so (`MSG_JOLT`); a board that felt its own within 250 ms of the other's knows they bumped. Its first sample says which way the case was pushed: the other board is the opposite way. That side is kept in NVS (namespace `friends`, the friend's MAC, degrees in the panel's axes) and shown as `side_deg` in `get_state`'s `friends`. Both then play `highfive` (hearts, a clap, the ta-da) and look at each other, waking if asleep.

The side is in the panel's axes and assumes both boards lie the same way up; a board turned over or moved needs another bump.

### 3. Where the other is (to do, maybe)

The bump answers "which side" well enough for now. Still possible:

- **Side.** One board chirps, the other listens: `ears_dir.c` already tells left, ahead or right from the delay between its two mics, a few centimetres apart. A `chirp` message says when the sound leaves, so the listener knows which sound to measure.
- **Distance.** With the two clocks lined up over ESP-NOW (a few round trips, ms precision), the sound's travel time gives the distance: 1 ms is about 34 cm.

### 4. Together (0.20.0, done)

Each board tells the others what it does (`MSG_EVENT`, `friend_ev_kind_t` in `friends.h`), and the other joins in while it's free (awake, no conversation, menu, card or reaction, not in do not disturb; `behaviour.c`, `friend_said`):

- **Contagion.** A yawn of its own (`yawn` act) makes the other yawn 1 to 3 s later, seven times in ten; a dance starts the other dancing. A sneeze or a hiccup: the other looks over and giggles. Falling asleep: the other looks over, slows down (`friend_asleep`: less energy and excitement) and yawns half the time. Acts done to join in aren't told again, so it doesn't echo back and forth.
- **Waking each other.** Woken any way (a touch, PWR, a voice), the other wakes too 1.2 s later and looks over; not at night (the dim hours), face down or in do not disturb.
- **Jealousy.** Stroke one or hold its hand, and the other looks over, sulks (`jealous`, at most every 20 s) and feels it (`jealous`). Stroke it within 30 s and it melts (`love`).
- **They look at each other** when the other wakes, talks, sneezes, falls asleep, bumps or chats, once the side is known.
- **Chatter.** Every 3 to 8 minutes while both are free, one starts: three to five turns of chirps back and forth (`chat`, `chat_reply`, 0.9 to 1.6 s apart), a giggle at the end (`giggle`), looking at each other.
- **The firefly crosses over.** With a friend awake whose side is known, half the fireflies get away towards it instead of upwards: Yoru waves it off, and on the other screen a firefly comes in from the edge facing this one, at the same height (`life_firefly_from`), and is mostly caught.
- **Pass the ball**, a game in Games (`game_pass.c`): tilt the case to roll a ball about; the stretch of rim facing the other Yoru opens (green) when that one has the game open too, and the ball rolls out there and into its screen. The passes are counted. Without a bump the game says to bump them; a ball lost on the way comes back with a tap after 8 s.

For tests, `debug/friends` takes `{"tell": "yawn"}` (any event above but `bump`), as if this board had done it, and `{"side_deg": 0}` to set where the nearest friend is.

### 5. One answers "Alexa" (0.20.0, done)

Without the Mac, two boards that hear the wake word both answered (Yoru Hub's voice floor only covers boards it drives). Now each says how loud it heard it (`MSG_HEARD`, AFE volume in dB); a board that hears of a friend's detection within 0.7 s of its own and louder (by over 0.5 dB, or the higher MAC on a tie) gives up within the first moments: it stops listening with `utterance_end` `reason` `yielded` and `speech` false, so a host drops it. PWR is never given up.

## Security

Messages are neither signed nor encrypted: anyone on the channel can say hello as a Yoru. They only move the eyes and the mood, roll a ball, or make a board stop listening to a wake word it heard more faintly; never settings or what the voice hears. If later messages do more, they get a key shared when the boards bump (step 2).

## On the air

All broadcast on the WiFi's channel, `YORU`, version 1, then the type: `hello` (1, 24 bytes, every 4 s and when the board falls asleep, wakes or opens the ball game: flags awake and playing, the firmware), `event` (2), `jolt` (3), `ball` (4: where along the opening x1000, the speed in px/s), `heard` (5: the volume x10); the last four are 12 bytes (`kind`, `a`, `b`). Boards before 0.20.0 drop them for their size. Only messages from a board whose hello was heard count.
