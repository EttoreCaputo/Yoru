"""Trains Yoru's wake word, «Hey Yoru», as a microWakeWord model
(docs/wakeword.md). Run it with the environment tools/wakeword/setup.sh made:

    tools/wakeword/work/venv/bin/python tools/wakeword/train.py

It follows microWakeWord's training notebook: Piper speaks the word in many
voices, the clips are mixed with rooms, noise and music, and a small streaming
model learns to tell them from speech that is not the word. Every stage keeps
what it made in tools/wakeword/work, so a second run goes on from there;
`--fresh <stage>` redoes one (samples, features, model).

The result is copied to main/wake/hey_yoru.tflite, and the test report at the
end of the training says which cutoff to put in main/wake_mww.cc.
"""

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
WORK = HERE / "work"
MODEL_OUT = HERE.parent.parent / "main" / "wake" / "hey_yoru.tflite"

# The word, in spellings that Piper's English voices say differently (checked
# with piper_phonemize): a long, a closing and an open "o", the stress on the
# first part or on both.
WORD = ["hey yoru", "hey yoh roo", "hey yo-roo", "hey yaw roo"]
WORD_SAMPLES = 1500  # per spelling

# What must not wake Yoru although it sounds close. The ready-made negative
# sets have no idea of this word: these are what teach its edges.
NEAR = [
    "hey you", "hey your", "hey Yuri", "hey Laura", "hey Nora", "hey Rory",
    "hey Joe", "hey Jude", "hey Yoda", "hey Yoshi", "hey Google", "hey Siri",
    "a euro", "they're all", "hey", "you're a", "hello", "okay", "hey there",
    "say your", "stay or go", "eight or two",
]
NEAR_SAMPLES = 250  # per phrase

GENERATOR = WORK / "piper-sample-generator" / "generate_samples.py"


def speak(phrases: list[str], count: int, out: Path) -> None:
    """`count` clips of each phrase, in a folder of its own under `out`."""
    for i, phrase in enumerate(phrases):
        folder = out / f"{i:02d}"
        if folder.exists():
            continue
        part = out / f"{i:02d}.part"
        shutil.rmtree(part, ignore_errors=True)
        subprocess.run(
            [sys.executable, str(GENERATOR), phrase,
             "--max-samples", str(count), "--batch-size", "50",
             "--output-dir", str(part)],
            check=True,
            # Piper's checkpoint (from rhasspy's release, fetched by setup.sh)
            # is a whole pickled model, which torch 2.6 and later refuse to
            # load unless told to.
            env={**os.environ, "TORCH_FORCE_NO_WEIGHTS_ONLY_LOAD": "1"},
        )
        part.rename(folder)  # only a finished folder counts


def features(clips_dir: Path, out: Path) -> None:
    """The clips of `clips_dir`, mixed with rooms and noise, as the
    spectrograms the model trains on: training, validation and testing."""
    from mmap_ninja.ragged import RaggedMmap
    from microwakeword.audio.augmentation import Augmentation
    from microwakeword.audio.clips import Clips
    from microwakeword.audio.spectrograms import SpectrogramGeneration

    if out.exists():
        return
    part = out.with_suffix(".part")
    shutil.rmtree(part, ignore_errors=True)

    clips = Clips(
        input_directory=str(clips_dir),
        file_pattern="**/*.wav",
        max_clip_duration_s=None,
        remove_silence=False,
        random_split_seed=10,
        split_count=0.1,
    )
    augmenter = Augmentation(
        augmentation_duration_s=3.2,
        augmentation_probabilities={
            "SevenBandParametricEQ": 0.1,
            "TanhDistortion": 0.1,
            "PitchShift": 0.1,
            "BandStopFilter": 0.1,
            "AddColorNoise": 0.1,
            "AddBackgroundNoise": 0.75,
            "Gain": 1.0,
            "RIR": 0.5,
        },
        impulse_paths=["mit_rirs"],
        background_paths=["fma_16k", "audioset_16k"],
        background_min_snr_db=-5,
        background_max_snr_db=10,
        min_jitter_s=0.195,
        max_jitter_s=0.205,
    )
    # Training and validation run the model without streaming, so the same
    # spectrogram is repeated shifted by a frame; testing streams for real.
    for folder, split, repeat, slide in (
        ("training", "train", 2, 10),
        ("validation", "validation", 1, 10),
        ("testing", "test", 1, 1),
    ):
        spectrograms = SpectrogramGeneration(
            clips=clips, augmenter=augmenter, slide_frames=slide, step_ms=10
        )
        (part / folder).mkdir(parents=True)
        RaggedMmap.from_generator(
            out_dir=str(part / folder / "wakeword_mmap"),
            sample_generator=spectrograms.spectrogram_generator(split=split, repeat=repeat),
            batch_size=100,
            verbose=True,
        )
    part.rename(out)


def config(steps: int) -> Path:
    import yaml

    def negative(name: str, weight: float, strategy: str = "random") -> dict:
        return {
            "features_dir": name, "sampling_weight": weight, "penalty_weight": 1.0,
            "truth": False, "truncation_strategy": strategy, "type": "mmap",
        }

    settings = {
        "window_step_ms": 10,
        "train_dir": "trained_models/hey_yoru",
        "features": [
            {
                "features_dir": "features/word", "sampling_weight": 2.0,
                "penalty_weight": 1.0, "truth": True,
                "truncation_strategy": "truncate_start", "type": "mmap",
            },
            negative("features/near", 3.0, "truncate_start"),
            negative("negative_datasets/speech", 10.0),
            negative("negative_datasets/dinner_party", 10.0),
            negative("negative_datasets/no_speech", 5.0),
            # Only for validation and testing: the false wakes per hour.
            negative("negative_datasets/dinner_party_eval", 0.0, "split"),
        ],
        "training_steps": [steps],
        "positive_class_weight": [1],
        "negative_class_weight": [20],
        "learning_rates": [0.001],
        "batch_size": 128,
        "time_mask_max_size": [0],
        "time_mask_count": [0],
        "freq_mask_max_size": [0],
        "freq_mask_count": [0],
        "eval_step_interval": 500,
        "clip_duration_ms": 1500,
        "target_minimization": 0.9,
        "minimization_metric": None,
        "maximization_metric": "average_viable_recall",
    }
    path = WORK / "training_parameters.yaml"
    path.write_text(yaml.dump(settings))
    return path


def train(steps: int) -> None:
    """The model ESPHome's own words use (mixednet, about 50 KB)."""
    subprocess.run(
        [sys.executable, "-m", "microwakeword.model_train_eval",
         f"--training_config={config(steps)}",
         "--train", "1", "--restore_checkpoint", "1",
         "--test_tf_nonstreaming", "0", "--test_tflite_nonstreaming", "0",
         "--test_tflite_nonstreaming_quantized", "0",
         "--test_tflite_streaming", "0", "--test_tflite_streaming_quantized", "1",
         "--use_weights", "best_weights",
         "mixednet",
         "--pointwise_filters", "64,64,64,64",
         "--repeat_in_block", "1, 1, 1, 1",
         "--mixconv_kernel_sizes", "[5], [7,11], [9,15], [23]",
         "--residual_connection", "0,0,0,0",
         "--first_conv_filters", "32",
         "--first_conv_kernel_size", "5",
         "--stride", "3"],
        check=True,
    )
    made = (WORK / "trained_models" / "hey_yoru" / "tflite_stream_state_internal_quant"
            / "stream_state_internal_quant.tflite")
    MODEL_OUT.parent.mkdir(exist_ok=True)
    shutil.copyfile(made, MODEL_OUT)
    print(f"model: {MODEL_OUT} ({MODEL_OUT.stat().st_size} bytes)")


FRESH = {
    "samples": ["samples", "features", "trained_models/hey_yoru"],
    "features": ["features", "trained_models/hey_yoru"],
    "model": ["trained_models/hey_yoru"],
}

if __name__ == "__main__":
    args = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    args.add_argument("--steps", type=int, default=20000, help="training steps")
    args.add_argument("--fresh", choices=FRESH, help="redo from this stage on")
    args = args.parse_args()

    if not GENERATOR.exists():
        sys.exit("run tools/wakeword/setup.sh first")
    os.chdir(WORK)
    for name in FRESH.get(args.fresh, []):
        shutil.rmtree(WORK / name, ignore_errors=True)

    speak(WORD, WORD_SAMPLES, WORK / "samples" / "word")
    speak(NEAR, NEAR_SAMPLES, WORK / "samples" / "near")
    features(WORK / "samples" / "word", WORK / "features" / "word")
    features(WORK / "samples" / "near", WORK / "features" / "near")
    train(args.steps)
