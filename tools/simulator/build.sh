#!/usr/bin/env bash
# Build the desktop-simulator shared library that the Tkinter IDE loads.
set -euo pipefail

here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${here}/../.." && pwd)"
build_root="${repo_root}/build/simulator"

"${here}/fetch_lvgl.sh"

cmake -S "${here}/host" -B "${build_root}/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "${build_root}/build" --parallel

lib="$(ls "${build_root}"/build/lib/libpassport_sim.* 2>/dev/null | head -n 1 || true)"
echo "Built simulator library: ${lib}"
