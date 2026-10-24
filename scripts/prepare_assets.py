#!/usr/bin/env python3
"""Prepare an isolated assets dir for the test profile (never touches ~/.minecraft).

Downloads the official Minecraft version JSON + asset index for the requested version from Mojang,
re-uses (symlinks) any object already present in ~/.minecraft/assets, downloads the rest and verifies SHA-1.
Usage: prepare_assets.py <version> <out_dir>
"""
import hashlib, json, os, sys, urllib.request
from concurrent.futures import ThreadPoolExecutor

ver, out = sys.argv[1], os.path.abspath(sys.argv[2])
manifest = json.load(urllib.request.urlopen("https://piston-meta.mojang.com/mc/game/version_manifest_v2.json"))
vurl = next(v["url"] for v in manifest["versions"] if v["id"] == ver)
vjson = json.load(urllib.request.urlopen(vurl))
os.makedirs(f"{out}/indexes", exist_ok=True)
idx_info = vjson["assetIndex"]
idx = json.load(urllib.request.urlopen(idx_info["url"]))
json.dump(idx, open(f"{out}/indexes/{idx_info['id']}.json", "w"))
json.dump(vjson, open(f"{out}/../{ver}.json", "w"))
home = os.path.expanduser("~/.minecraft/assets/objects")
todo = []
for name, o in idx["objects"].items():
    h = o["hash"]; d = f"{out}/objects/{h[:2]}"; p = f"{d}/{h}"
    if os.path.exists(p): continue
    os.makedirs(d, exist_ok=True)
    src = f"{home}/{h[:2]}/{h}"
    if os.path.exists(src): os.symlink(src, p)
    else: todo.append((h, p))
print(f"index {idx_info['id']}: {len(idx['objects'])} objects, {len(todo)} to download")
def get(t):
    h, p = t
    data = urllib.request.urlopen(f"https://resources.download.minecraft.net/{h[:2]}/{h}").read()
    assert hashlib.sha1(data).hexdigest() == h, h
    open(p, "wb").write(data)
with ThreadPoolExecutor(8) as ex: list(ex.map(get, todo))
print("assets ready:", out)
