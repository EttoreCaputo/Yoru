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
2. `tools/wakeword/work/venv/bin/python tools/wakeword/train.py`: Piper says the word in hundreds of voices, in a few spellings; it also says phrases that sound close and must not wake Yoru («hey you», «hey Yuri»...). The clips are mixed with rooms and noise, and the model trains on them against hours of other speech. Every stage keeps its work, so a stopped run goes on; `--fresh samples|features|model` redoes from a stage.
3. The model lands in `main/wake/hey_yoru.tflite`. The test report at the end gives, for each cutoff, how often the word is heard and how many false wakes an hour: choose the cutoff, put it in `MODELS` (as 0-255: the probability times 255), build and try with a real voice.

To make it better: more spellings and samples in `train.py`, more near phrases for what wakes it by mistake, more steps (`--steps`). Recordings of real people saying the word, added to `work/samples/word/`, help most.

## Where the model stands

The first model (0.20.18): 6000 clips of the word, 5500 of near phrases, 20000 steps, half an hour on an M1 Pro.

- In the training's own test, at the cutoff in use (0.90): about one word in six missed, less than one false wake in five hours of talk at a table. That test is kind: its words come from the same synthetic voices as the training's.
- On the board, with macOS's voices from the computer's speakers: 2 of 10 «Hey Yoru» heard, and 3 of 7 near phrases woke it («hey you...», «hey Joe...», «hey there...»). The averages that passed were barely over the cutoff. Not yet tried with a real voice.
- One step of the model takes 3 ms every 30 ms (a tenth of one core); its memory is in PSRAM.

What to try next: recordings of real voices at the board's mic, an Italian Piper voice among the samples (the English ones say the "r" and the vowels their way), longer near phrases that begin with «hey you», more steps.

## Licence

The noise, music and room recordings used for training have mixed licences, some non-commercial. A model trained this way is for personal use; selling boards with it needs a model trained on data cleared for that.
