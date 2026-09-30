# Pitch Curve

Pitch Curve is a cross-platform audio plug-in that captures the small pitch movements
of one performance and applies them to another. A contour can also be drawn
directly in the editor.

## Version 0.7.26

- Drag or browse for WAV, AIFF, FLAC, MP3, or OGG audio.
- YIN-based monophonic pitch tracking with confidence gating and median
  de-spiking.
- Learned movement is stored as cents relative to the source's median pitch, so
  the destination keeps its own musical register.
- Editable ±6-semitone base curve, reaching ±12 semitones at 200% Amount.
- Amount-aware semitone axis (±0 at 0%, ±6 at 100%, ±12 at 200%).
- Smooth defaults to 5. Smooth 0 preserves the continuous source polyline;
  levels 1–10 use progressively wider fixed local windows. A local-range gate
  protects peaks and valleys while shoulders and corners become rounder, with
  bounded influence and no overshoot.
- Pitch processing reads the same smoothed curve shown in the editor at each
  control-block midpoint, without an additional hidden 25 ms pitch ramp.
- 30 FPS second/frame duration controls up to 60 seconds, with automatic frame carry.
- A duration lock button disables the time knob and timecode fields,
  preserves duration when learning a new curve, and restores with session state.
- The compact lock button sits outside the time ring at its upper-right, with a
  clearer open-lock silhouette; all numeric fields use optically centred text.
- Second and frame fields include a one-pixel baseline correction to match the
  Amount and Smooth fields at both supported window sizes.
- The open-lock shackle ends cleanly at its gap; the icon is one pixel smaller
  and two pixels lower. Its unlocked colour is subtly subdued. Locking changes
  only the icon, not the time controls' appearance.
- Unified button styling, enlarged learn area, consistent typography, and a
  comfortable slate/mint palette.
- The curve playhead returns to the start whenever host playback stops and
  advances with a steady wall-clock animation from the first playback.
- A transiently unavailable host transport position no longer restarts a curve
  that was already playing.
- Gap-free freehand drawing and an explicit Clear Curve action. Double-clicking
  the curve editor does not clear the contour.
- 0–200% contour amount.
- Signalsmith Stretch high-quality, phase-coherent pitch processing.
- VST3 and standalone builds on macOS and Windows; Audio Unit builds on macOS.
- Session state restores both the parameter and learned/drawn contour.
- Session XML is preflighted for size, depth, node count, and forbidden DTD or
  entity declarations before parsing.
- Defensive file/state limits, cancellable analysis, lower-copy pitch analysis,
  and bounded processing of oversized host blocks.
- Audio imports reject implausible channel metadata, and installer migration
  avoids recursively deleting generic legacy application directories.
- Curve publication never exposes a write-locked slot to the audio thread.
- Shape-preserving smoothing replaces the previous 129-sample Gaussian kernel,
  reducing real-time work while retaining every authored peak and valley.
- Learn revalidates the selected file and temporarily locks curve editing so an
  asynchronous result cannot overwrite an edit made during analysis.
- Standalone processing uses its active audio clock instead of a non-playing
  host-transport flag.
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

On Linux, prefer the checked-in `linux-gcc-release` CMake preset. It pins GCC 13
instead of relying on the machine-wide `c++` alternative.

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
widening, or hidden gain processing. Pitch commands follow the same displayed
curve at 64-sample control intervals without a hidden pitch ramp. Effect
transitions use a 10 ms crossfade to avoid zipper noise and clicks.

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
