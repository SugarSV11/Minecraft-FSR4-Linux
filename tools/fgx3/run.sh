#!/usr/bin/env bash
# Builds and runs the FG x3 prototype on one dataset directory (see scripts/fgx3_dataset.py). Usage: run.sh DIR W H [REPS]
set -euo pipefail; cd "$(dirname "$0")"
toolbox run -c fedora-toolbox-42 bash -c "glslc --target-env=vulkan1.3 -O fgx3.comp -o /tmp/fgx3.spv && gcc -O2 fgx3.c -o /tmp/fgx3 -lvulkan && /tmp/fgx3 /tmp/fgx3.spv $* 2>&1 | grep -v WARNING"
