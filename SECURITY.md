# Security

## Runtime safety model

Pitch Curve has no networking, telemetry, updater, shell execution, archive
extraction, or credential access. `JUCE_USE_CURL=0` and `JUCE_WEB_BROWSER=0`
remove JUCE's network and embedded-browser paths from the build. The plug-in
only reads an audio file explicitly selected or dropped by the user and the
state blob supplied by the host DAW.

Defensive limits:

- Imported audio is decoded to at most 60 seconds and 12 million samples.
- Reader sample rates and channel counts are validated before allocation.
- Decoding is split into bounded chunks and analysis checks cancellation
  between frames when the editor closes.
- Restored state is capped at 2 MiB and 4,096 validated contour points.
- Non-finite and out-of-range contour values are discarded or clamped.
- Unexpected oversized DAW blocks are processed in fixed-size chunks without
  allocating or accessing outside the prepared scratch buffer.
- Source dependencies and release workflow actions are pinned to immutable
  commit hashes.

Audio codecs still process untrusted local files. Only import audio from a
trusted source, and keep Pitch Curve/JUCE updated.

## Dependency review

- JUCE 8.0.10 is in its published security-support window through June 2029.
  The known JUCE archive-extraction CVEs affect versions before 6.1.5, and
  Pitch Curve does not expose archive extraction.
- Signalsmith Stretch has no published CVE at the time of this review.

## Reporting

Please report suspected vulnerabilities privately to the project maintainer.
Include the affected version, platform, host DAW, reproduction steps, and a
minimal test file where possible. Do not include private project audio.

## Installer trust

Release installers must be signed:

- macOS: Developer ID Application for binaries and Developer ID Installer for
  the `.pkg`, followed by Apple notarization and stapling.
- Windows: an Authenticode code-signing certificate for the plug-in,
  standalone application, and installer.

Unsigned packages produced locally are suitable for development verification,
not broad public distribution. SHA-256 checksum files detect accidental
corruption but do not replace code signing.
