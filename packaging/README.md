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

## Windows

Run from a Visual Studio 2022 developer PowerShell on Windows with:

- Visual Studio 2022 C++ workload
- Windows SDK
- Node.js
- CMake
- .NET SDK
- Inno Setup 6
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
