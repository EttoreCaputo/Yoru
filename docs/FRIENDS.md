# Friends: Yorus that meet

Two or more Yorus in the same house find each other, greet each other and, later, play together and know where the other one is. Without the Mac and without Bluetooth.

## Why ESP-NOW, not BLE

- **RAM.** Bluetooth is off in the firmware (`CONFIG_BT_ENABLED`), and its controller and host would take tens of KB of internal RAM, which WiFi and ESP-SR already leave short. ESP-NOW runs on the WiFi radio that is already on and costs next to nothing.
- **Channel.** Boards on the same WiFi are on the access point's channel, so ESP-NOW reaches them while they stay connected. Messages take a few milliseconds.
- **Without the host.** It works with Yoru Hub closed, as the voice does since 0.14.0.
- **BLE's one advantage**, a rough distance from the signal strength, ESP-NOW has too: every message comes with its RSSI. It only tells "near" from "in another room".

What it doesn't do: reach a board with no WiFi set up (ESP-NOW starts when the WiFi has an address), or one that is hibernating (the radio is off).

## Steps

### 1. Hello (0.17.0, done)

`main/friends.c`. Every 4 s each board broadcasts a hello: magic `YORU`, version 1, type `hello`, whether it is awake, its firmware. Whoever hears it keeps it in a table of six (MAC, RSSI, last heard, awake, firmware). A friend not heard for 30 s is gone.

- **Greeting.** A friend heard for the first time, or again after 30 minutes apart, gets a wave (`wave`, the hand and the hello sound) and lifts Yoru's mood (the `friend` event: happy, company and excitement, not trust, which is for you). Only while Yoru is free: awake, no conversation, menu, card or reaction, not in do not disturb. Until then the friend waits.
- **`get_state`** has `friends`: `[{id, rssi, seen_s, awake, firmware}]`.
- **Radio.** ESP-NOW starts on `IP_EVENT_STA_GOT_IP` (the channel is the access point's from then) and stops on `WIFI_EVENT_STA_STOP` (WiFi off, hibernating).

### 2. The bump (to do)

Touching the two cases together introduces them. Both IMUs see the same knock within a few milliseconds; each says so (`bump`, with the time and the direction of the hit in the screen's axes), and the one that hears a matching bump knows the other is on that side. Then a happier greeting than a wave (they look at each other, `love`). It also answers "which side": the direction of the knock.

### 3. Where the other is (to do)

- **Side.** One board chirps, the other listens: `ears_dir.c` already tells left, ahead or right from the delay between its two mics, a few centimetres apart. A `chirp` message says when the sound leaves, so the listener knows which sound to measure.
- **Distance.** With the two clocks lined up over ESP-NOW (a few round trips, ms precision), the sound's travel time gives the distance: 1 ms is about 34 cm.
- Both are coarse: three sides, tens of centimetres. Enough to look the right way.

### 4. Together (to do)

Once each knows where the other is:

- **They look at each other** when the other wakes, talks or is touched.
- **The firefly crosses over:** it leaves one screen by the edge facing the other and comes in on the other one from the right side. So does a ball passed by tilting the cases.
- **Contagion:** one yawns, the other yawns soon after; one falls asleep, the other slows down.
- **Chatter** in their beeps, taking turns, the other one nodding.
- **Jealousy:** stroke one, and the other looks over and sulks a little.

### 5. One answers "Alexa" (to do)

Without the Mac, two boards that hear the wake word both answer today (Yoru Hub's voice floor only covers boards it drives). Each says how loud it heard it, and only the loudest answers.

## Security

Messages are neither signed nor encrypted: anyone on the channel can say hello as a Yoru. They only move the eyes and the mood, never settings or the voice. If later messages do more, they get a key shared when the boards bump (step 2).
