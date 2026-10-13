# MachineModule 0.4.2

Windows x64 standalone instrument and VST3 by AcidReff.

- Machinedrum OS 1.63 and verified UW X.13 firmware loading.
- X.13 synthesis expansions and tonal tuning.
- Selectable Drum / Chromatic MIDI modes; channels 1–16 address tracks 1–16 in Chromatic mode.
- Original firmware reverb, delay, master EQ and dynamics.
- Individual track outputs, kit SysEx import and a hardware-inspired interface.
- Fixed RND LFO initialization in both shape slots.

## Install

Extract `MachineModule-0.4.2-Windows-x64.zip`. Run `Standalone/MachineModule.exe`, or copy the **whole** `VST3/MachineModule.vst3` folder to your VST3 plugin directory and rescan in your DAW.

Select your own supported firmware using **FIRMWARE**. Firmware and ROM samples are not included. Start at 44.1 kHz, a 512-sample buffer and six voices; X.13 and dense kits can need more CPU.

## Validation

Engine RND tests passed with OS 1.63 and X.13 for both shape slots in FREE/TRIG/HOLD modes on tracks 1, 8 and 16. Processor tests covered audio rates/block sizes, effects, individual routing and all 548 saved parameters. Chromatic MIDI tests covered all 16 channels and octave tuning. These are automated engine/processor tests, not physical-hardware equivalence or testing in every DAW.

AGPL-3.0-only. See LICENSE and THIRD_PARTY_NOTICES.md. Source is available in this repository. Not affiliated with Elektron or Akai.
