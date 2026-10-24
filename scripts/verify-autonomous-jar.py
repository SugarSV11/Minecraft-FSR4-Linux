#!/usr/bin/env python3
"""Verify every bundled bootstrap resource in a release JAR."""
import hashlib
import pathlib
import sys
import zipfile

PREFIX = "dev/fsr4linux/bootstrap/"
EXPECTED = {
    "libfsr4linux.so", "fsr4_bridge.exe", "amd_fidelityfx_loader_dx12.dll",
    "amd_fidelityfx_upscaler_dx12.dll", "amd_fidelityfx_framegeneration_dx12.dll",
    "AMD_FIDELITYFX_LICENSE.md", "proton-wrapper.py",
}


def verify(path: pathlib.Path) -> dict[str, str]:
    with zipfile.ZipFile(path) as archive:
        members = {name[len(PREFIX):] for name in archive.namelist()
                   if name.startswith(PREFIX) and not name.endswith("/")}
        if members != EXPECTED | {"SHA256SUMS"}:
            raise ValueError(f"resource coverage mismatch: {members ^ (EXPECTED | {'SHA256SUMS'})}")
        lines = archive.read(PREFIX + "SHA256SUMS").decode("ascii").splitlines()
        hashes = {}
        for line in lines:
            digest, separator, name = line.partition("  ")
            if separator != "  " or len(digest) != 64 or any(c not in "0123456789abcdef" for c in digest) or name not in EXPECTED or name in hashes:
                raise ValueError(f"bad SHA256SUMS entry: {line!r}")
            actual = hashlib.sha256(archive.read(PREFIX + name)).hexdigest()
            if actual != digest:
                raise ValueError(f"hash mismatch for {name}: {digest} != {actual}")
            hashes[name] = digest
        if hashes.keys() != EXPECTED:
            raise ValueError("incomplete SHA256SUMS")
        return hashes


if __name__ == "__main__":
    for name, digest in verify(pathlib.Path(sys.argv[1])).items():
        print(digest, name)
