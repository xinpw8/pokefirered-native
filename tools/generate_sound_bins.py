#!/usr/bin/env python3

import argparse
import subprocess
from pathlib import Path


def parse_args():
    parser = argparse.ArgumentParser(description="Generate upstream direct-sound .bin assets from tracked .wav files.")
    parser.add_argument("--pokedir", required=True, help="Path to the pokefirered checkout")
    parser.add_argument("--wav2agb", required=True, help="Path to the wav2agb executable")
    return parser.parse_args()


def needs_regen(source: Path, output: Path) -> bool:
    return not output.exists() or output.stat().st_mtime_ns < source.stat().st_mtime_ns


def build_args(wav2agb: Path, pokedir: Path, source: Path, output: Path):
    rel = source.relative_to(pokedir / "sound")
    if rel.parts[:2] == ("direct_sound_samples", "cries"):
        return [str(wav2agb), "-b", "-c", "-l", "1", "--no-pad", str(source), str(output)]
    return [str(wav2agb), "-b", str(source), str(output)]


def main():
    args = parse_args()
    pokedir = Path(args.pokedir).resolve()
    wav2agb = Path(args.wav2agb).resolve()
    direct_sound_dir = pokedir / "sound" / "direct_sound_samples"

    generated = 0
    for source in sorted(direct_sound_dir.rglob("*.wav")):
        output = source.with_suffix(".bin")
        if not needs_regen(source, output):
            continue
        subprocess.run(build_args(wav2agb, pokedir, source, output), check=True)
        generated += 1

    print(f"generated {generated} direct-sound bin file(s)")


if __name__ == "__main__":
    main()
