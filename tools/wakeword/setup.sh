#!/bin/bash
# Sets up what training Yoru's wake word needs (docs/wakeword.md), in
# tools/wakeword/work (not in git, about 30 GB): a Python 3.10 environment
# with microWakeWord and Piper's sample generator, the rooms and noises the
# samples are mixed with, and microWakeWord's ready-made negative features.
# Safe to run again: what is there is kept.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
WORK="$HERE/work"
mkdir -p "$WORK"
cd "$WORK"

if [ ! -d venv ]; then
    uv venv --python 3.10 venv
fi
# shellcheck disable=SC1091
source venv/bin/activate

if [ ! -d micro-wake-word ]; then
    git clone --depth 1 https://github.com/OHF-Voice/micro-wake-word.git
fi
if [ ! -d piper-sample-generator ]; then
    # The branch that runs Piper's generator on Apple's GPU.
    git clone --depth 1 -b mps-support https://github.com/kahrendt/piper-sample-generator
fi
GENERATOR=piper-sample-generator/models/en_US-libritts_r-medium.pt
if [ ! -s "$GENERATOR" ]; then
    curl -L --fail -o "$GENERATOR" \
        https://github.com/rhasspy/piper-sample-generator/releases/download/v2.0.0/en_US-libritts_r-medium.pt
fi

# The audio-metadata fork is the one microWakeWord's notebook installs. Its
# pymicro-features fork for macOS no longer builds with today's clang, and
# isn't needed: the released package has a wheel for Apple silicon.
uv pip install --only-binary pymicro-features pymicro-features \
    'git+https://github.com/whatsnowplaying/audio-metadata@d4ebb238e6a401bb1a5aaaac60c9e2b3cb30929f'
uv pip install -e ./micro-wake-word
# datasets 4 decodes audio another way than microWakeWord expects.
uv pip install torch torchaudio piper-phonemize-cross==1.2.1 scipy tqdm soundfile librosa 'datasets<4' tensorboard

# microWakeWord's negative features: speech, a dinner party, no speech.
mkdir -p negative_datasets
for name in dinner_party dinner_party_eval no_speech speech; do
    if [ ! -d "negative_datasets/$name" ]; then
        curl -L --fail -o "negative_datasets/$name.zip" \
            "https://huggingface.co/datasets/kahrendt/microwakeword/resolve/main/$name.zip"
        unzip -q "negative_datasets/$name.zip" -d negative_datasets
        rm "negative_datasets/$name.zip"
    fi
done

# Rooms (impulse responses), sounds (AudioSet) and music (FMA) at 16 kHz.
python "$HERE/fetch_noise.py"

echo "setup done: $(du -sh "$WORK" | cut -f1) in $WORK"
