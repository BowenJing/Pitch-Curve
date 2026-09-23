# Building installers

There is no single installer format shared by macOS, Windows, and Linux.
Contour produces a native package for each platform.

## Linux

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
cpack --config build/CPackConfig.cmake -G DEB
```

The `.deb` installs the VST3 bundle to `/usr/lib/vst3` and the standalone
application to `/usr/bin/contour`. A `.tar.gz` can be produced with `-G TGZ`.

## macOS Universal

```bash
cmake -S . -B build -G Xcode \
  -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DBUILD_TESTING=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cpack --config build/CPackConfig.cmake -C Release -G productbuild
```

The package contains Universal VST3 and AU plug-ins plus the standalone app.
Before public release, sign nested binaries with Developer ID Application,
build/sign the package with Developer ID Installer, submit it to Apple's
notary service, and staple the accepted ticket.

## Windows x64 or ARM64

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cpack --config build/CPackConfig.cmake -C Release -G NSIS
```

Replace `x64` with `ARM64` for a native Windows-on-ARM package. NSIS is
required. The installer copies `Contour.vst3` into the system Common Files VST3
directory. Sign binaries and the final installer with Authenticode before
public release.

For an unsigned x64 development build from Debian/Ubuntu, use LLVM 20,
Microsoft's CRT/SDK acquired with `xwin`, and NSIS. JUCE 8 explicitly does not
support MinGW.

```bash
sudo apt-get install clang-20 clang-tools-20 lld-20 llvm-20 nsis
cargo install xwin --locked
xwin --accept-license --arch x86_64 splat --output .xwin
cmake -S . -B build-windows \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/clang-cl-x64.cmake \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build-windows --parallel
cpack --config build-windows/CPackConfig.cmake -G NSIS
```

## Verification

Each CPack run emits a SHA-256 sidecar where supported. Release automation
should retain the installer, checksum, source commit, compiler version, and
dependency commit hashes together.
