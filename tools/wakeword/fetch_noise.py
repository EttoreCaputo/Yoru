"""Downloads what the wake word's samples are mixed with, as 16 kHz WAVs in
tools/wakeword/work: room impulse responses (MIT), everyday sounds (one part
of AudioSet) and music (a small Free Music Archive set). The same sources as
microWakeWord's training notebook. Their licences are mixed: a model trained
with them is for personal, non-commercial use.
"""

import os
import subprocess
import tarfile
import zipfile
from pathlib import Path

import datasets
import numpy as np
import scipy.io.wavfile
from tqdm import tqdm

WORK = Path(__file__).resolve().parent / "work"
HF = "https://huggingface.co/datasets"


def download(url: str, to: Path) -> None:
    subprocess.run(["curl", "-L", "--fail", "-o", str(to), url], check=True)


def write_wav(path: Path, samples) -> None:
    scipy.io.wavfile.write(path, 16000, (np.clip(samples, -1, 1) * 32767).astype(np.int16))


def convert(files: list[Path], out: Path) -> None:
    """Every audio file in `files` as a 16 kHz WAV in `out`."""
    out.mkdir(exist_ok=True)
    data = datasets.Dataset.from_dict({"audio": [str(f) for f in files]})
    data = data.cast_column("audio", datasets.Audio(sampling_rate=16000))
    for i in tqdm(range(len(data)), desc=out.name):
        try:
            row = data[i]
        except Exception as err:  # a broken file is not worth stopping for
            print(f"skipped {files[i].name}: {err}")
            continue
        write_wav(out / (files[i].stem + ".wav"), row["audio"]["array"])


def rooms() -> None:
    out = WORK / "mit_rirs"
    if out.exists():
        return
    out.mkdir()
    rirs = datasets.load_dataset("davidscripka/MIT_environmental_impulse_responses", split="train", streaming=True)
    for row in tqdm(rirs, desc="mit_rirs"):
        write_wav(out / row["audio"]["path"].split("/")[-1], row["audio"]["array"])


def sounds() -> None:
    """One part of AudioSet's balanced set: about 600 ten-second clips."""
    import io

    import librosa
    import pyarrow.parquet
    import soundfile

    out = WORK / "audioset_16k"
    if out.exists():
        return
    part = WORK / "audioset_16k.part"
    part.mkdir(exist_ok=True)
    table = WORK / "audioset.parquet"
    download(f"{HF}/agkphysics/AudioSet/resolve/main/data/bal_train/09.parquet", table)
    rows = pyarrow.parquet.ParquetFile(table)
    count = 0
    for batch in rows.iter_batches(batch_size=16, columns=["audio"]):
        for clip in batch.column("audio").to_pylist():
            try:
                samples, rate = soundfile.read(io.BytesIO(clip["bytes"]), dtype="float32", always_2d=True)
            except Exception as err:  # a broken clip is not worth stopping for
                print(f"skipped a clip: {err}")
                continue
            mono = librosa.resample(samples.mean(axis=1), orig_sr=rate, target_sr=16000)
            write_wav(part / f"{count:05d}.wav", mono)
            count += 1
    table.unlink()
    part.rename(out)
    print(f"audioset_16k: {count} clips")


def music() -> None:
    out = WORK / "fma_16k"
    if out.exists():
        return
    raw = WORK / "fma"
    raw.mkdir(exist_ok=True)
    archive = raw / "fma_xs.zip"
    download(f"{HF}/mchl914/fma_xsmall/resolve/main/fma_xs.zip", archive)
    with zipfile.ZipFile(archive) as z:
        z.extractall(raw)
    archive.unlink()
    convert(sorted(raw.glob("**/*.mp3")), out)


if __name__ == "__main__":
    os.chdir(WORK)
    rooms()
    sounds()
    music()
