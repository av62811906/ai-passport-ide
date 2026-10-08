#!/usr/bin/env bash
# Build (if needed) and launch the AI Passport desktop IDE.
# Usage: ./run.sh [--lib PATH] [--scale N]
set -euo pipefail

here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

"${here}/build.sh"
exec python3 "${here}/passport_ide.py" "$@"
