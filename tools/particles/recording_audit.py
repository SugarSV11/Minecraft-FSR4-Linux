#!/usr/bin/env python3
"""Read-only inventory of a particle recording before replay or neural training.

Usage: recording_audit.py RECORDING [RECORDING ...]
The screenshots alone cannot reconstruct the depth and motion planes.  This
tool deliberately does not inspect sealed TEST3 features or score models.
"""
import argparse
from pathlib import Path
import re


def inspect(root: Path) -> str:
    frames = sorted((root / "frames").glob("sc_*.png"))
    planes = root / "planes"
    # A/B recordings need both the normal and the particle-free planes.
    counts = {prefix: len(list(planes.glob(f"{prefix}_*.bin"))) for prefix in ("d", "m", "da", "ma")}
    timestamps = len(list(planes.glob("cam_*.txt")))
    log = root / "client.log"
    lines = log.read_text(errors="replace").splitlines() if log.is_file() else []
    commands = [m.group(1) for line in lines if (m := re.search(r"\[test\].*cmd tick (rate [0-9.]+|freeze|unfreeze)", line))]
    ready = bool(frames) and all(counts[p] >= len(frames) for p in ("d", "m")) and timestamps >= len(frames)
    ab_ready = ready and all(counts[p] >= len(frames) for p in ("da", "ma"))
    return (f"{root}: frames={len(frames)} depth={counts['d']} motion={counts['m']} "
            f"timestamps={timestamps} A/B_depth={counts['da']} A/B_motion={counts['ma']} "
            f"tick={','.join(commands) or 'unknown'} replay_ready={ready} ab_ready={ab_ready}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("recordings", nargs="+", type=Path)
    args = parser.parse_args()
    for root in args.recordings:
        print(inspect(root))


if __name__ == "__main__":
    main()
