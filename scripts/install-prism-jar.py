#!/usr/bin/env python3
"""Install the verified candidate into an explicitly named, stopped Prism instance."""
import hashlib
import importlib.util
import os
from pathlib import Path
import shutil
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
INSTANCE_ROOT = Path.home() / ".var/app/org.prismlauncher.PrismLauncher/data/PrismLauncher/instances"
CANDIDATE = ROOT / "release/Minecraft-FSR4-Beta.jar"
BACKUPS = [
    ROOT / "release/Minecraft-FSR4-Beta.before-repair.jar",
    ROOT / "release/Minecraft-FSR4-Beta.before-fence-fix.jar",
    ROOT / "release/Minecraft-FSR4-Beta.before-alloc-fix.jar",
    ROOT / "release/Minecraft-FSR4-Beta.before-error-detail.jar",
    ROOT / "release/Minecraft-FSR4-Beta.before-log-persist.jar",
    ROOT / "release/Minecraft-FSR4-Beta.before-selftest-batch.jar",
    ROOT / "release/Minecraft-FSR4-Beta.before-safe-sync.jar",
    ROOT / "release/Minecraft-FSR4-Beta.before-fg-local.jar",
    ROOT / "release/Minecraft-FSR4-Beta.before-window-hide.jar",
]


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def main():
    if len(sys.argv) != 2 or not sys.argv[1].isdigit():
        raise SystemExit("Uso: python3 scripts/install-prism-jar.py NOMBRE_DE_INSTANCIA")
    instance = INSTANCE_ROOT / sys.argv[1]
    mods = instance / "minecraft/mods"
    old = mods / "Minecraft-FSR4-Beta.jar"
    if not instance.is_dir() or not mods.is_dir() or not old.is_file():
        raise SystemExit(f"Instancia o JAR anterior ausente: {old}")
    old_hash = digest(old)
    backup = next((path for path in BACKUPS if path.is_file() and digest(path) == old_hash), None)
    if backup is None:
        raise SystemExit("El JAR de la instancia difiere del respaldo; no se reemplazó")

    # Do not change files underneath a running game.
    for proc in Path("/proc").iterdir():
        if not proc.name.isdigit():
            continue
        try:
            command = (proc / "cmdline").read_bytes()
        except (OSError, PermissionError):
            continue
        if str(instance).encode() in command and b"java" in command.lower():
            raise SystemExit("La instancia parece estar en ejecución; no se reemplazó el JAR")

    spec = importlib.util.spec_from_file_location("fsr_verify", ROOT / "scripts/verify-autonomous-jar.py")
    verifier = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(verifier)
    verifier.verify(CANDIDATE)

    with tempfile.NamedTemporaryFile(prefix=".fsr4-", suffix=".jar", dir=mods, delete=False) as staging:
        staged = Path(staging.name)
    try:
        shutil.copy2(CANDIDATE, staged)
        if digest(staged) != digest(CANDIDATE):
            raise RuntimeError("La copia temporal no coincide con el candidato")
        os.replace(staged, old)
    finally:
        staged.unlink(missing_ok=True)
    print(f"Instalado: {old}")
    print(f"SHA-256: {digest(old)}")
    print(f"Respaldo anterior: {backup}")


if __name__ == "__main__":
    main()
