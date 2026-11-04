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

### 2. Meeting up close (0.20.0, reworked in 0.20.2)

Bring the two boards together, quickly, from apart: they high-five (hearts, a clap, the ta-da) and look at each other, waking if asleep. No knock is needed.

- **How it's seen.** The ESP-NOW signal between two boards jumps when they come together. Measured on two boards: -64 to -86 dBm a few tens of cm apart and more, -50 to -55 lying near each other, -37 to -45 brought against each other, within half a second. A meeting is two hellos in a row at -46 dBm or more less than 1.5 s after one at -58 or less, with one of the two boards moved in that time (`friends.c`): boards lying near each other don't count, nor do the jumps the signal makes on its own between boards lying still; once per 5 s.
- **Fast hellos.** While a board is being moved (`motion_moving()`: not still for half a second) its hello says so (`FLAG_MOVING`) and goes every 250 ms instead of 4 s; a board that hears a moving friend does the same for 2 s, so both follow the signal closely. The one that sees the jump tells the other (`MSG_CLOSE`), so both play it.
- **Which side.** After meeting, each board asks "Tap the edge facing your friend" for 9 s. A tap more than 140 px from the middle sets where the other is (`friends_found_at`), and tells it (`MSG_SIDE`, the angle in the panel's axes): the other takes the opposite way and stops asking. Kept in NVS (namespace `friends`, the friend's MAC, degrees), shown as `side_deg` in `get_state`'s `friends`; it assumes both lie the same way up.
- **Tried and dropped:** a knock felt by both IMUs at once (0.20.0 to 0.20.2 in testing). Too hard a knock was needed, and a gentler threshold took two boards moved by hand together for a bump; the direction of a hit sampled every 10 ms was all over the place.

### 3. Where the other is by sound (to do, maybe)

A tap answers "which side" for now. Still possible without it:

- **Side.** One board chirps, the other listens: `ears_dir.c` already tells left, ahead or right from the delay between its two mics, a few centimetres apart. A `chirp` message says when the sound leaves, so the listener knows which sound to measure.
- **Distance.** With the two clocks lined up over ESP-NOW (a few round trips, ms precision), the sound's travel time gives the distance: 1 ms is about 34 cm.

### 4. Together (0.20.0, done)

Each board tells the others what it does (`MSG_EVENT`, `friend_ev_kind_t` in `friends.h`), and the other joins in while it's free (awake, no conversation, menu, card or reaction, not in do not disturb; `behaviour.c`, `friend_said`):

- **Contagion.** A yawn of its own (`yawn` act) makes the other yawn 1 to 3 s later, seven times in ten; a dance starts the other dancing. A sneeze or a hiccup: the other looks over and giggles. Falling asleep: the other looks over, slows down (`friend_asleep`: less energy and excitement) and yawns half the time. Acts done to join in aren't told again, so it doesn't echo back and forth.
- **Waking each other.** Woken any way (a touch, PWR, a voice), the other wakes too 1.2 s later and looks over; not at night (the dim hours), face down or in do not disturb.
- **Jealousy.** Stroke one or hold its hand, and the other looks over, sulks (`jealous`, at most every 20 s) and feels it (`jealous`). Stroke it within 30 s and it melts (`love`).
- **They look at each other** when the other wakes, talks, sneezes, falls asleep, meets it up close or chats, once the side is known.
- **Chatter.** Every 3 to 8 minutes while both are free, one starts: three to five turns of chirps back and forth (`chat`, `chat_reply`, 0.9 to 1.6 s apart), a giggle at the end (`giggle`), looking at each other.
- **The firefly crosses over.** With a friend awake whose side is known, half the fireflies get away towards it instead of upwards: Yoru waves it off, and on the other screen a firefly comes in from the edge facing this one, at the same height (`life_firefly_from`), and is mostly caught.
- **Pass the ball**, a game in Games (`game_pass.c`): tilt the case to roll a ball about; the stretch of rim facing the other Yoru opens (green) when that one has the game open too, and the ball rolls out there and into its screen. The passes are counted. Without a side the game says to bring them together; a ball lost on the way comes back with a tap after 8 s.

`debug/friends` answers `meetings` (how many times they were brought together) and takes `"hello_ms"` to say hello that often until the next restart, to watch the signal. For tests, `debug/friends` takes `{"tell": "yawn"}` (any event above but `bump`), as if this board had done it, and `{"side_deg": 0}` to set where the nearest friend is.

### 5. One answers "Alexa" (0.20.0, done)

Without the Mac, two boards that hear the wake word both answered (Yoru Hub's voice floor only covers boards it drives). Now each says how loud it heard it (`MSG_HEARD`, AFE volume in dB); a board that hears of a friend's detection within 0.7 s of its own and louder (by over 0.5 dB, or the higher MAC on a tie) gives up within the first moments: it stops listening with `utterance_end` `reason` `yielded` and `speech` false, so a host drops it. PWR is never given up.

## Security

Messages are neither signed nor encrypted: anyone on the channel can say hello as a Yoru. They only move the eyes and the mood, roll a ball, or make a board stop listening to a wake word it heard more faintly; never settings or what the voice hears. If later messages do more, they get a key shared when the boards bump (step 2).

## On the air

All broadcast on the WiFi's channel, `YORU`, version 1, then the type: `hello` (1, 24 bytes, every 4 s, every 250 ms while either is moved, and when the board falls asleep, wakes or opens the ball game: flags awake, playing and moving, the firmware), `event` (2), `close` (3: we were brought together), `ball` (4: where along the opening x1000, the speed in px/s), `heard` (5: the volume x10), `side` (6: where we found the receiver, degrees); the last five are 12 bytes (`kind`, `a`, `b`). Boards before 0.20.0 drop them for their size. Only messages from a board whose hello was heard count.
