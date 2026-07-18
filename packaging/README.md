# ASTER Drum Lab packaging

## macOS

Formal distribution requires:

- Apple Developer Program membership
- `Developer ID Application` certificate
- `Developer ID Installer` certificate
- A `notarytool` keychain profile

```bash
export APP_SIGN_IDENTITY="Developer ID Application: ENIGMA (TEAMID)"
export INSTALLER_SIGN_IDENTITY="Developer ID Installer: ENIGMA (TEAMID)"
export NOTARY_PROFILE="aster-notary"
packaging/macos/build-pkg.sh
```

The signed path builds universal Release binaries, signs the AU and VST3 with
hardened runtime, signs the installer, submits it for
Apple notarization, staples the ticket, and runs Gatekeeper verification.

For local installer testing only:

```bash
packaging/macos/build-pkg.sh --unsigned
```

Set `AAX_SDK_PATH` (or place AAX SDK 2.9 at
`$HOME/SDKs/aax-sdk-2-9-0`) to include the universal AAX plug-in alongside AU
and VST3. AAX distribution additionally requires Avid/PACE signing; ordinary
Apple code signing alone is not sufficient for release in Pro Tools.

## Windows

Run from a Visual Studio 2022 developer PowerShell on Windows with:

- Visual Studio 2022 C++ workload
- Windows SDK
- Node.js
- CMake
- .NET SDK
- Inno Setup 6
- AAX SDK 2.9 (optional, for AAX builds)
- An Authenticode code-signing certificate

```powershell
$env:WINDOWS_CERT_PFX = "C:\secure\enigma-code-signing.pfx"
$env:WINDOWS_CERT_PASSWORD = "..."
.\packaging\windows\build-installer.ps1
```

The script signs the VST3 binary, verifies Microsoft's signature on the bundled
WebView2 Evergreen bootstrapper, builds the installer, then signs and verifies
the final installer with SHA-256 and an RFC 3161 timestamp.

For local installer testing only:

```powershell
.\packaging\windows\build-installer.ps1 -Unsigned
```

Set `AAX_SDK_PATH` (or place the SDK at `C:\SDKs\aax-sdk-2-9-0`) to build
VST3 and AAX from the same source and include both in the installer. Without an
AAX SDK, the script continues to build the VST3-only installer.

## GitHub Actions

The `Build Windows Installer` workflow builds the unsigned Windows x64 VST3
installer on GitHub's Windows runner. It runs after relevant changes reach
`main`, and it can also be started manually from the repository's Actions tab.

The downloadable artifact is named `ASTER-Drum-Lab-Windows-x64` and contains
the installer, a manual-install VST3 ZIP, and their SHA-256 checksums. GitHub
retains it for 14 days.
