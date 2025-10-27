#!/usr/bin/env bash
set -euo pipefail
# Build locally with PlatformIO after you’ve placed these files into the firmware tree
if ! command -v pio >/dev/null 2>&1; then
  python -m pip install --user platformio
  export PATH="$HOME/.local/bin:$PATH"
fi
pio run -e tbeam
mkdir -p dist
cp .pio/build/tbeam/firmware.bin dist/firmware-tbeam-2.6.10.9ce4455+burst.bin
echo "Built: dist/firmware-tbeam-2.6.10.9ce4455+burst.bin"
