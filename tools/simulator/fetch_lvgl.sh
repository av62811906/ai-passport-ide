#!/usr/bin/env bash
# Ensure an LVGL 9.5.0 source tree is available for the desktop simulator.
#
# Preference order:
#   1. managed_components/lvgl__lvgl (already fetched by a device build)
#   2. build/simulator/lvgl         (cloned here, pinned to the device's version)
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
managed="${repo_root}/managed_components/lvgl__lvgl"
clone_dir="${LVGL_DIR:-${repo_root}/build/simulator/lvgl}"
lvgl_version="${LVGL_VERSION:-v9.5.0}"

if [[ -f "${managed}/CMakeLists.txt" ]]; then
    echo "Using managed LVGL: ${managed}"
    exit 0
fi

if [[ -f "${clone_dir}/CMakeLists.txt" ]]; then
    echo "Using cached LVGL clone: ${clone_dir}"
    exit 0
fi

echo "Fetching LVGL ${lvgl_version} into ${clone_dir}"
mkdir -p "$(dirname -- "${clone_dir}")"
git clone --depth 1 --branch "${lvgl_version}" \
    https://github.com/lvgl/lvgl.git "${clone_dir}"
