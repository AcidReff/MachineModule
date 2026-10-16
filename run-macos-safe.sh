#!/bin/bash
set -euo pipefail

# Diagnostic launcher for an EXISTING app; no rebuild required.
project_dir="$(cd "$(dirname "$0")" && pwd)"
app="${1:-$project_dir/dist/macOS-Universal/Standalone/MachineModule.app}"
if [ "$(uname -s)" != "Darwin" ]; then
    echo "Run this launcher on the Mac where MachineModule freezes." >&2
    exit 1
fi
if [ ! -x "$app/Contents/MacOS/MachineModule" ]; then
    echo "Pass the full path to MachineModule.app as the first argument." >&2
    exit 1
fi
echo "Diagnostic: disabling voice AND master DSP JIT for this standalone process."
echo "Start with six voices, 44.1 kHz and a 512-sample buffer."
export MACHINEMODULE_VOICE_INTERPRETER=1
export MACHINEMODULE_INTERPRETER=1
exec "$app/Contents/MacOS/MachineModule"
