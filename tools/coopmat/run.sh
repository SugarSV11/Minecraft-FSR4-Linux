#!/usr/bin/env bash
# Builds and runs the cooperative-matrix GEMM benchmark (needs glslc + vulkan headers: toolbox fedora-toolbox-42). Usage: run.sh [M N K]  (N,K multiples of 16; M multiple of 16; 2x2 kernel needs M,N multiples of 32)
set -euo pipefail; cd "$(dirname "$0")"
toolbox run -c fedora-toolbox-42 bash -c "
glslc --target-env=vulkan1.3 -O gemm_coop.comp -o /tmp/gemm_coop.spv && glslc --target-env=vulkan1.3 -O gemm_scalar.comp -o /tmp/gemm_scalar.spv && glslc --target-env=vulkan1.3 -O gemm_coop2.comp -o /tmp/gemm_coop2.spv &&
gcc -O2 gemm_bench.c -o /tmp/gemm_bench -lvulkan -lm && /tmp/gemm_bench /tmp/gemm_coop.spv /tmp/gemm_scalar.spv /tmp/gemm_coop2.spv $* 2>&1 | grep -v WARNING"
