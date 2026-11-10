# Yoru's own wake word

Yoru answers to «Hey Yoru». ESP-SR's WakeNet only hears the words Espressif trained (Alexa, Hi ESP...), and a new one can't be trained outside Espressif. So Yoru's word is a [microWakeWord](https://github.com/OHF-Voice/micro-wake-word) model: the kind ESPHome and Home Assistant's voice devices use, small enough for the board and trained on a computer from synthetic speech.

## On the board

- `main/wake_mww.cc` runs the model with TensorFlow Lite Micro (`espressif/esp-tflite-micro`) and ESPHome's audio features (`esphome/esp-micro-speech-features`): 40 mel bands every 10 ms, the same front end the model was trained with.
- ESP-SR's AFE stays: echo cancelling, noise suppression and the VAD. For an own word the AFE is made without WakeNet, and `voice.c` feeds the model what the AFE gives back, so the word is heard after the echo of Yoru's own voice is taken out.
- The model's answer is a probability, 0 to 255, every 30 ms. The word counts as heard when the mean of the last few answers passes the cutoff. Both numbers are per model, in the `MODELS` table of `wake_mww.cc`, next to the memory its tensors need (taken from PSRAM).
- `set_wake_word` takes the own words next to WakeNet's (`voice.wake_words` lists them, own words first). The default is still `alexa`: `heyyoru` is a word to choose until its model hears well enough.
- The model is a file in `main/wake/`, built into the firmware (about 50 KB).

## Training

Everything is in `tools/wakeword/`; what it downloads and makes goes to `tools/wakeword/work/` (about 30 GB, not in git).

1. `tools/wakeword/setup.sh`, once: a Python 3.10 environment (with `uv`), microWakeWord, Piper's sample generator, the rooms, noises and music the samples are mixed with, and microWakeWord's ready-made negative sets.
2. `tools/wakeword/work/venv/bin/python tools/wakeword/train.py`: Piper says the word in hundreds of voices, in a few spellings and, as phonemes, the way an Italian says it; it also says phrases that sound close and must not wake Yoru («hey you», «hey Yuri»...). The clips are mixed with rooms and noise, and the model trains on them against hours of other speech. Every stage keeps its work, so a stopped run goes on; `--fresh samples|features|model` redoes from a stage.
3. The model lands in `main/wake/hey_yoru.tflite`. The test report at the end gives, for each cutoff, how often the word is heard and how many false wakes an hour: choose the cutoff, put it in `MODELS` (as 0-255: the probability times 255), build and try with a real voice.

To make it better: more spellings and samples in `train.py`, more near phrases for what wakes it by mistake, more steps (`--steps`). Recordings of real people saying the word, added to `work/samples/word/`, help most.

## Where the model stands

The first model (0.20.18) learnt from English synthetic voices only: 6000 clips of the word, 5500 of near phrases, 20000 steps. On the board it heard 2 of 10 «Hey Yoru» from macOS's voices, woke on 3 of 7 near phrases («hey you...», «hey Joe...», «hey there...»), and never heard an Italian speaker. Run on recordings of that speaker it stayed near zero at any gain: the pronunciation, not the level.

The second model (0.20.19) adds the Italian pronunciation (four phoneme spellings, 6000 more clips), 30 real recordings from the board's mic (one speaker; each mixed with rooms and noise 100 times), eight more near phrases, and 30000 steps: an hour on an M1 Pro.

- In the training's own test, at cutoff 0.89: about one word in seven missed, one false wake an hour of talk at a table. That is more false wakes than the first model at the same cutoff.
- Run on the 30 recordings it learnt from: 19 heard at the cutoff in use (0.90), 14 at 0.97. Of 40 synthetic «hey Joe, your order is here» it had also learnt from, 7 still woke it at 0.97.
- So it underfits: a small model, a short word, and "hey" in front of many other things. Not yet measured live on the board.
- One step of the model takes 3 ms every 30 ms (a tenth of one core); its memory is in PSRAM.

Recording real voices: quit the app, run `yoru --voice-dump tools/wakeword/work/recordings`, hold PWR and say the word a few times with two seconds between; `train.py` cuts the recordings into words by itself.

What to try next: many more real recordings, from more people; a larger model (more filters); a longer or less common word than "hey" in front.

## Licence

The noise, music and room recordings used for training have mixed licences, some non-commercial. A model trained this way is for personal use; selling boards with it needs a model trained on data cleared for that.
