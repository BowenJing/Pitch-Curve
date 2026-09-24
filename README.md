# Pitch Curve

Pitch Curve is a cross-platform audio plug-in that captures the small pitch movements
of one performance and applies them to another. A contour can also be drawn
directly in the editor.

## Version 0.7.0

- Drag or browse for WAV, AIFF, FLAC, MP3, or OGG audio.
- YIN-based monophonic pitch tracking with confidence gating and median
  de-spiking.
- Learned movement is stored as cents relative to the source's median pitch, so
  the destination keeps its own musical register.
- Editable ±6-semitone base curve, reaching ±12 semitones at 200% Amount.
- Amount-aware semitone axis (±0 at 0%, ±6 at 100%, ±12 at 200%).
- Step-based 0–10 Smooth control for progressively rounder pitch motion.
- 30 FPS second/frame duration controls, with automatic frame carry.
- Unified time/amount controls, typography, spacing, and a comfortable slate/mint palette.
- The curve playhead returns to the start whenever host playback stops.
- Gap-free freehand drawing and an explicit Clear Curve action.
- 0–200% contour amount.
- Signalsmith Stretch high-quality, phase-coherent pitch processing.
- VST3 and standalone builds on macOS and Windows; Audio Unit builds on macOS.
- Session state restores both the parameter and learned/drawn contour.
- Defensive file/state limits, cancellable analysis, and bounded processing of
  oversized host blocks.
- Native package definitions for Linux, macOS Universal, Windows x64, and
  Windows ARM64.

This first version is intentionally for **monophonic material** such as vocals,
bass, leads, and solo instruments. Reliable extraction of independent pitch
curves from chords requires source separation and is outside this plug-in's
scope.

## Build

Prerequisites: CMake 3.22+, a C++17 compiler, Git, and the platform's plug-in
toolchain. CMake fetches pinned JUCE and Signalsmith Stretch sources.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Build products are written beneath `build/Contour_artefacts/`.
Installer commands and signing requirements are documented in
[`docs/PACKAGING.md`](docs/PACKAGING.md). Runtime threat boundaries and the
dependency review are documented in [`SECURITY.md`](SECURITY.md).

### macOS

Use current Xcode and CMake. The project requests a Universal Binary
(`arm64;x86_64`) and builds VST3, AU, and standalone targets.

```bash
cmake -S . -B build -G Xcode -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build --config Release
```

### Windows

Use Visual Studio 2022 on x64 Windows:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Windows on ARM can use `-A ARM64`; the host DAW must support native ARM64
plug-ins. Otherwise use the x64 build under the system's emulation layer.

## Signal path and limitations

When no contour exists or Amount is 0%, audio is sample-identical after the
fixed latency reported to the host. Keeping that latency constant prevents PDC
changes while a session is playing. With a contour active, the only DSP stage
is the pitch transposer; there is no EQ, compression, saturation, stereo
widening, or hidden gain processing. Pitch commands use a 25 ms ramp and
effect transitions use a 10 ms crossfade to avoid zipper noise and clicks.

No pitch shifter can promise zero artifacts for every signal. This
implementation uses Signalsmith Stretch's phase-coherent spectral processing,
which is designed to avoid the transient smearing and inter-channel phase
instability typical of simpler shifters. Keep movement moderate for the most
transparent result.

## Licensing

This repository and its unsigned development packages use JUCE's AGPLv3 option.
Choose a commercial JUCE licence and review all project rights before shipping
a closed-source product. Signalsmith Stretch is MIT licensed. See `LICENSE`
for project and third-party notices and `COPYING` for the complete AGPL terms.
