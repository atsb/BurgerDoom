#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
: "${GLSLANG_VALIDATOR:=glslangValidator}"
"$GLSLANG_VALIDATOR" -V -S frag doom3do.frag.glsl -o doom3do.spv
echo "Built $(pwd)/doom3do.spv"
