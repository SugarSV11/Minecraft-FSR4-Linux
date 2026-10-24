#!/usr/bin/env python3
"""Check every Java FFM descriptor against the exported C API declarations."""
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
header = (ROOT / "native/include/fsr4_capi.h").read_text()
java = (ROOT / "fabric/src/client/java/dev/fsr4linux/client/nativ/Fsr4Native.java").read_text()
header = re.sub(r"/\*.*?\*/", "", header, flags=re.S)
header = re.sub(r"//.*", "", header)


def native_type(value):
    value = re.sub(r"\s+", " ", value.strip())
    if value == "void":
        return "V"
    if "*" in value:
        return "A"
    if "64_t" in value:
        return "J"
    if re.search(r"\b(int|uint32_t|int32_t)\b", value):
        return "I"
    raise ValueError(f"unknown C type: {value}")


prototypes = {}
for ret, name, args in re.findall(r"FSR4_API\s+([\w\s*]+?)\s+(fsr4_\w+)\s*\((.*?)\);", header, re.S):
    arguments = [] if args.strip() == "void" else args.split(",")
    prototypes[name] = native_type(ret), [native_type(arg) for arg in arguments]

descriptors = {}
for name, kind, args in re.findall(r'h\(lib,\s*"(fsr4_\w+)",\s*FunctionDescriptor\.(ofVoid|of)\((.*?)\)\)', java, re.S):
    codes = re.findall(r"\b[AIJ]\b", args)
    descriptors[name] = ("V", codes) if kind == "ofVoid" else (codes[0], codes[1:])

required = set(re.findall(r'"(fsr4_\w+)"', java.split("REQUIRED_SYMBOLS =", 1)[1].split("};", 1)[0]))
if prototypes.keys() != required or descriptors.keys() != required:
    raise SystemExit(f"ABI symbol set mismatch: C-only={prototypes.keys()-required}, Java-only={required-prototypes.keys()}, no-descriptor={required-descriptors.keys()}")
for name in sorted(required):
    if prototypes[name] != descriptors[name]:
        raise SystemExit(f"ABI signature mismatch {name}: C={prototypes[name]}, Java={descriptors[name]}")

library = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build/lib/libfsr4linux.so"
exports = set(re.findall(r"\b(fsr4_\w+)$", subprocess.check_output(
    ["nm", "-D", "--defined-only", str(library)], text=True), re.M))
if exports != required:
    raise SystemExit(f"ELF symbol mismatch: missing={required-exports}, extra={exports-required}")
print(f"Verified {len(required)} Java/C/ELF symbols and signatures")
