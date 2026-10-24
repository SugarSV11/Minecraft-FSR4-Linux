"""Where the big NeuralCraft data lives (session 15: moved to the Steam disk, /dev/sdc1 ext4, outside SteamLibrary).
Resolution order: env NC_DATA_ROOT -> <steam disk>/NeuralCraft-Data when its copy was verified (marker file .verified-<name>) -> the old ~/Escritorio location.
  NEURAL          datasets   (FSR4-neural-data)
  evidence(n)     recordings of session n (FSR4-evidence-session<n>)
Code, Git, checkpoints under tools/ai/neural/runs and ~/neuralvenv stay where they are."""
import os
STEAM_ROOT = "/run/media/system/steam/NeuralCraft-Data"
OLD = os.path.expanduser("~/Escritorio")

def _root(name, sub):
    env = os.environ.get("NC_DATA_ROOT")
    if env: return os.path.join(env, sub, name)
    if os.path.exists(f"{STEAM_ROOT}/.verified-{name}"): return os.path.join(STEAM_ROOT, sub, name)
    return os.path.join(OLD, name)

NEURAL = _root("FSR4-neural-data", "datasets")
def evidence(n): return _root(f"FSR4-evidence-session{n}", "evidence")
