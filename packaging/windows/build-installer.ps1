[CmdletBinding()]
param(
    [switch]$Unsigned,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
if (Test-Path variable:PSNativeCommandUseErrorActionPreference) {
    $PSNativeCommandUseErrorActionPreference = $true
}
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$CMakeFile = Get-Content (Join-Path $Root "CMakeLists.txt") -Raw
$Version = [regex]::Match($CMakeFile, 'project\(AsterDrumLab VERSION ([0-9.]+)\)').Groups[1].Value
if (-not $Version) { throw "Could not read the project version from CMakeLists.txt." }
$BuildDir = Join-Path $Root "build-release-windows"
$Artefacts = Join-Path $BuildDir "DrumSampler_artefacts\Release"
$DistDir = Join-Path $Root "dist"
$VendorDir = Join-Path $PSScriptRoot "vendor"
$WebView2 = Join-Path $VendorDir "MicrosoftEdgeWebview2Setup.exe"
$WebView2PackageVersion = "1.0.3967.48"
$WebView2Package = Join-Path $env:USERPROFILE ".nuget\packages\microsoft.web.webview2\$WebView2PackageVersion"

function Find-SignTool {
    $tool = Get-Command signtool.exe -ErrorAction SilentlyContinue
    if ($tool) { return $tool.Source }
    $kits = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe" -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending
    if (-not $kits) { throw "signtool.exe was not found. Install the Windows SDK." }
    return $kits[0].FullName
}

function Sign-File([string]$Path) {
    if ($Unsigned) { return }
    if (-not $env:WINDOWS_CERT_PFX) { throw "Set WINDOWS_CERT_PFX to the code-signing PFX path." }
    if (-not $env:WINDOWS_CERT_PASSWORD) { throw "Set WINDOWS_CERT_PASSWORD." }
    $timestamp = if ($env:WINDOWS_TIMESTAMP_URL) { $env:WINDOWS_TIMESTAMP_URL } else { "http://timestamp.digicert.com" }
    & $script:SignTool sign /fd SHA256 /td SHA256 /tr $timestamp /f $env:WINDOWS_CERT_PFX /p $env:WINDOWS_CERT_PASSWORD $Path
    & $script:SignTool verify /pa /v $Path
}

if (-not $Unsigned) {
    $script:SignTool = Find-SignTool
}

New-Item -ItemType Directory -Force $DistDir, $VendorDir | Out-Null

if (-not (Test-Path $WebView2)) {
    Invoke-WebRequest "https://go.microsoft.com/fwlink/p/?LinkId=2124703" -OutFile $WebView2
}
$webViewSignature = Get-AuthenticodeSignature $WebView2
if ($webViewSignature.Status -ne "Valid" -or $webViewSignature.SignerCertificate.Subject -notmatch "Microsoft") {
    throw "The WebView2 bootstrapper does not have a valid Microsoft signature."
}

if (-not $SkipBuild) {
    Push-Location $Root
    try {
        npm --prefix ui-prototype ci
        npm --prefix ui-prototype run build

        if (-not (Test-Path $WebView2Package)) {
            $tempProject = Join-Path $env:TEMP "aster-webview2-restore"
            Remove-Item $tempProject -Recurse -Force -ErrorAction SilentlyContinue
            New-Item -ItemType Directory -Force $tempProject | Out-Null
            dotnet new classlib --force --output $tempProject | Out-Null
            dotnet add $tempProject package Microsoft.Web.WebView2 --version $WebView2PackageVersion | Out-Null
        }

        cmake -S . -B $BuildDir -G "Visual Studio 17 2022" -A x64 `
            -DASTER_COPY_PLUGIN_AFTER_BUILD=OFF `
            "-DJUCE_WEBVIEW2_PACKAGE_LOCATION=$WebView2Package"
        cmake --build $BuildDir --config Release --parallel
    }
    finally {
        Pop-Location
    }
}

$VstBinary = Join-Path $Artefacts "VST3\ASTER Drum Lab.vst3\Contents\x86_64-win\ASTER Drum Lab.vst3"
foreach ($file in @($VstBinary)) {
    if (-not (Test-Path $file)) { throw "Missing build artefact: $file" }
    Sign-File $file
}

$iscc = (Get-Command ISCC.exe -ErrorAction SilentlyContinue).Source
if (-not $iscc) {
    $defaultIscc = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe"
    if (Test-Path $defaultIscc) { $iscc = $defaultIscc }
}
if (-not $iscc) { throw "Inno Setup 6 was not found." }

$iss = Join-Path $PSScriptRoot "ASTER Drum Lab.iss"
& $iscc `
    "/DAppVersion=$Version" `
    "/DBuildRoot=$Artefacts" `
    "/DOutputDir=$DistDir" `
    "/DWebView2Bootstrapper=$WebView2" `
    $iss

$Installer = Join-Path $DistDir "ASTER-Drum-Lab-$Version-Windows-x64-Setup.exe"
if (-not (Test-Path $Installer)) { throw "Installer was not created: $Installer" }
Sign-File $Installer

$HashFile = "$Installer.sha256"
$Hash = (Get-FileHash -Algorithm SHA256 $Installer).Hash.ToLowerInvariant()
"$Hash  $([System.IO.Path]::GetFileName($Installer))" | Set-Content -Encoding ascii $HashFile

Write-Host "Created: $Installer"
Write-Host "Checksum: $HashFile"
if ($Unsigned) {
    Write-Warning "This installer is unsigned and is only suitable for local testing."
}
