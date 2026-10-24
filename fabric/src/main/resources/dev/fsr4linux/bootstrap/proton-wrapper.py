#!/usr/bin/env python3
"""
Proton wrapper for FSR4-Linux with Flatpak support.
Launches Wine with the correct environment for the bundled Wine runtime.
Supports both regular and Flatpak environments for Vulkan/D3D12 interop.
"""

import os
import sys
import subprocess
import shutil
import platform
from pathlib import Path

def is_flatpak() -> bool:
    """Detect if running inside Flatpak."""
    return os.path.exists("/.flatpak-info") or os.getenv("FLATPAK_ID") is not None

def get_flatpak_gpu_devices() -> list:
    """Get GPU devices available in Flatpak sandbox."""
    devices = []
    dri_path = Path("/dev/dri")
    if dri_path.exists():
        for dev in dri_path.iterdir():
            if dev.name.startswith("renderD") or dev.name.startswith("card"):
                devices.append(str(dev))
    return devices

def main():
    # Determine paths
    proton_dir = Path(__file__).parent.resolve()
    wine_bin = proton_dir / "files" / "bin" / "wine64"
    wine_lib = proton_dir / "files" / "lib"
    wine_lib64 = proton_dir / "files" / "lib64"

    # Check if wine64 exists
    if not wine_bin.exists():
        for alt in [
            proton_dir / "files" / "bin" / "wine",
            proton_dir / "bin" / "wine64",
            proton_dir / "bin" / "wine",
        ]:
            if alt.exists():
                wine_bin = alt
                break
        else:
            print(f"Error: wine64 not found in {proton_dir}", file=sys.stderr)
            return 1

    # Set up environment
    env = os.environ.copy()
    flatpak = is_flatpak()

    # LD_LIBRARY_PATH for Wine runtime libraries
    ld_paths = []
    if wine_lib.exists():
        ld_paths.append(str(wine_lib))
        ld_paths.append(str(wine_lib / "x86_64-linux-gnu"))
        ld_paths.append(str(wine_lib / "i386-linux-gnu"))
    if wine_lib64.exists():
        ld_paths.append(str(wine_lib64))
    # Add system paths as fallback
    ld_paths.append("/usr/lib")
    ld_paths.append("/usr/lib/x86_64-linux-gnu")
    ld_paths.append("/usr/lib/i386-linux-gnu")
    env["LD_LIBRARY_PATH"] = ":".join(ld_paths) + (":" + env.get("LD_LIBRARY_PATH", "") if env.get("LD_LIBRARY_PATH") else "")

    # Wine prefix
    prefix = env.get("WINEPREFIX", str(Path.home() / ".wine"))
    env["WINEPREFIX"] = prefix

    # Ensure prefix exists
    Path(prefix).mkdir(parents=True, exist_ok=True)

    # Windows system directories
    env["WINEDLLOVERRIDES"] = "mscoree,mshtml="

    # Disable crash dialogs
    env["WINEDEBUG"] = "-all"

    # ===== FLATPAK / VULKAN / VKD3D SPECIFIC SETUP =====
    if flatpak:
        # Ensure Vulkan ICD is visible to the helper
        icd_paths = [
            "/usr/share/vulkan/icd.d/radeon_icd.x86_64.json",
            "/usr/share/vulkan/icd.d/radeon_icd.i686.json",
            "/var/lib/flatpak/runtime/org.freedesktop.Platform.GL.default/x86_64/23.08/active/share/vulkan/icd.d/radeon_icd.x86_64.json",
            "/var/lib/flatpak/runtime/org.freedesktop.Platform.GL.default/x86_64/23.08/active/share/vulkan/icd.d/radeon_icd.i686.json",
            "/usr/lib/x86_64-linux-gnu/vulkan/icd.d/radeon_icd.x86_64.json",
            "/usr/lib/i386-linux-gnu/vulkan/icd.d/radeon_icd.i686.json",
        ]
        icd_found = []
        for p in icd_paths:
            if Path(p).exists():
                icd_found.append(p)
        if icd_found:
            env["VK_ICD_FILENAMES"] = ":".join(icd_found)
            print(f"[fsr4linux] Flatpak: VK_ICD_FILENAMES={env['VK_ICD_FILENAMES']}", file=sys.stderr)

        # Ensure VKD3D uses the correct Vulkan device
        gpu_devices = get_flatpak_gpu_devices()
        if gpu_devices:
            # Use the first render device (typically the discrete GPU)
            render_dev = next((d for d in gpu_devices if "renderD" in d), gpu_devices[0])
            env["VK_DRIVER_FILES"] = env.get("VK_DRIVER_FILES", "")
            env["VKD3D_CONFIG"] = env.get("VKD3D_CONFIG", "") + ",dxr11,upload_hiz"
            print(f"[fsr4linux] Flatpak: GPU devices={gpu_devices}, using {render_dev}", file=sys.stderr)

        # Ensure the helper can access the GPU device nodes
        for dev in gpu_devices:
            try:
                os.stat(dev)
                print(f"[fsr4linux] Flatpak: GPU device accessible: {dev}", file=sys.stderr)
            except OSError as e:
                print(f"[fsr4linux] Flatpak: WARNING - GPU device not accessible: {dev} ({e})", file=sys.stderr)

        # Disable layers that might interfere with the helper
        for v in ["VK_INSTANCE_LAYERS", "VK_LAYER_PATH", "VK_ADD_LAYER_PATH", "VK_LAYER_ENABLES", "VK_LAYER_MESSAGE_ID_FILTER", "VK_LOADER_LAYERS_ENABLE"]:
            if v in env:
                del env[v]

    # Disable crash dialogs
    env["WINEDEBUG"] = "-all"

    # Command to run
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <command> [args...]", file=sys.stderr)
        return 1

    cmd = [str(wine_bin)] + sys.argv[1:]

    # Run
    try:
        print(f"[fsr4linux] Launching: {' '.join(cmd)}", file=sys.stderr)
        if flatpak:
            print(f"[fsr4linux] Environment: VK_ICD_FILENAMES={env.get('VK_ICD_FILENAMES', 'unset')}, VKD3D_CONFIG={env.get('VKD3D_CONFIG', 'unset')}", file=sys.stderr)
        result = subprocess.run(cmd, env=env)
        return result.returncode
    except Exception as e:
        print(f"Error launching Wine: {e}", file=sys.stderr)
        return 1

if __name__ == "__main__":
    sys.exit(main())