# Apple Silicon playback freeze investigation - 0.4.5 candidate

Reported case: OS 1.63 plays for several seconds on an M2 Pro, then standalone and
VST3 become unresponsive. The Universal app works on the user's Intel Mac running
macOS 12.6. Native ARM64 JIT is a suspect; no hang sample or native reproduction
is available here, so its exact cause is not confirmed.

## Test the existing app first - no rebuild

Quit the existing standalone, then launch it from Terminal with both DSP JIT
paths disabled. Adjust the app path to its actual location:

```bash
env MACHINEMODULE_VOICE_INTERPRETER=1 MACHINEMODULE_INTERPRETER=1 \
    "/Applications/MachineModule.app/Contents/MacOS/MachineModule"
```

Alternatively, use the supplied launcher:

```bash
bash run-macos-safe.sh "/Applications/MachineModule.app"
```

These variables affect this process and its children only. They do not change
other running applications or persist a system setting. They bypass JIT execution
in the existing wrapper; the rebuilt 0.4.5 ARM64 slice additionally disables the
DSP library's internal JIT dispatch at compile time.

Use the same firmware and playing pattern, with six voices, 44.1 kHz and a
512-sample buffer. Play for at least two minutes. In Activity Monitor, note whether
the process Kind is Apple (native ARM64) or Intel (Rosetta). Report whether the
freeze still occurs. This test narrows the cause; it does not confirm it by itself.

## Rebuild the Universal candidate

If your Mac repository already has local build fixes, the supplied focused
`MachineModule-AppleSilicon-interpreter-hotfix.patch` preserves those build files.
From your existing repository, check and apply it (adjust the patch path):

```bash
git apply --check "$HOME/Downloads/MachineModule-AppleSilicon-interpreter-hotfix.patch"
git apply "$HOME/Downloads/MachineModule-AppleSilicon-interpreter-hotfix.patch"
```

Then rebuild using your existing Universal build command. If the check reports
conflicts, share its output rather than forcing the patch. The focused patch
changes only five DSP/engine files and keeps your existing version number and
build configuration. Its older `md-jit-smoke` tool is not suitable for confirming
JIT execution after this patch; use playback, `md-smoke` and `md-master-probe`.

Alternatively, use the complete source package below in a new folder.

Build from the supplied 0.4.5 source package:

```bash
bash build-macos.sh "$HOME/Developer/JUCE"
```

The script builds both `x86_64` and `arm64` and defaults the deployment target to
macOS 12.0, allowing the user's Monterey 12.6 build setup. Use Xcode 14.2 on that
machine, or a suitable newer Xcode on a newer Mac. The JUCE source version is 8.0.8.
See [Universal build instructions](MACOS-UNIVERSAL.md).

Native ARM64 defaults to **interpreted voices AND interpreted original master
effects**. This bypasses the unvalidated ARM recompiler, including internal DSP
interrupt dispatch, and skips its compilation warm-up. It still runs the user's
original firmware synthesis, reverb, delay, EQ and dynamics. CPU use can increase;
lower the voice budget if audio overloads. Intel Mac and Windows keep JIT enabled.

The wrapper prints the effective mode when loading firmware. On native ARM64 the
expected line is:

```text
MachineModule DSP: voices=interpreter master=interpreter groups=3
```

For X.13, groups is 1. This mitigation applies to both the standalone and VST3
after rebuilding. Replace the complete VST3 bundle and rescan/restart the DAW so
that it loads version 0.4.5.

## Exercise the rebuilt native ARM64 engine

On the M-series Mac, from the built source folder:

```bash
FIRMWARE="$HOME/Documents/elektron_sps1-1uw_os1.63.bin"
arch -arm64 ./build-macos-universal/Release/md-stability-smoke "$FIRMWARE" 60
arch -arm64 ./build-macos-universal/Release/md-master-probe "$FIRMWARE"
arch -arm64 ./build-macos-universal/Release/md-smoke "$FIRMWARE"
```

The stability check reports interpreter mode, then exercises six active voices,
three DSP workers, machine changes, retriggers, fully idle periods, original
effects and tempo changes. It prints progress and a final audio hash. It renders
60 seconds of audio offline; its elapsed wall time depends on CPU speed/load.

`md-jit-smoke` now exits with **77 / SKIP** when JIT is disabled. This avoids
claiming a JIT/interpreter match when both engines are actually interpreted.
Use the stability check and original master probe for the default ARM64 build.

## Capture a hang if it persists

While the app or DAW is frozen, select it in Activity Monitor and choose
**Sample Process** before force-quitting. Paste the report's header and call graph,
along with the macOS version, process Kind, firmware, machine/kit and the last
action before the freeze. Include the interpreter-launch result. Apple's
[Activity Monitor diagnostics guide](https://support.apple.com/guide/activity-monitor/run-system-diagnostics-actmntr2225/mac)
describes the sample report.

The sample can distinguish a DSP execution stall, a worker waiting on another
thread, and a problem elsewhere. A successful Windows render or a successful
ARM64 compilation does not establish that this native hang has been fixed.

## Experimental ARM JIT

`MACHINEMODULE_ENABLE_ARM64_JIT=ON` is an experimental CMake option for debugging
only. It restores the old ARM JIT path. The normal build script explicitly keeps
it OFF. Do not use an experimental build in a working DAW project.

## Backup

Previous source/Universal build preparation, Windows standalone and complete VST3:
`../MachineModule-backups/0.4.4-before-apple-silicon-stability`. The Mac binaries
were built externally and were not available in this workspace to back up.
