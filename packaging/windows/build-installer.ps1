[CmdletBinding()]
param(
    [switch]$Unsigned,
    [switch]$SkipBuild,
    [string]$AaxSdkPath = $env:AAX_SDK_PATH
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
$WebView2NuGetPackage = Join-Path $env:USERPROFILE ".nuget\packages\microsoft.web.webview2\$WebView2PackageVersion"
$WebView2JucePackage = Join-Path $VendorDir "Microsoft.Web.WebView2.$WebView2PackageVersion"
if (-not $AaxSdkPath) {
    $defaultAaxSdk = "C:\SDKs\aax-sdk-2-9-0"
    if (Test-Path (Join-Path $defaultAaxSdk "Interfaces\ACF")) {
        $AaxSdkPath = $defaultAaxSdk
    }
}
$AaxEnabled = $AaxSdkPath -and (Test-Path (Join-Path $AaxSdkPath "Interfaces\ACF"))

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

        if (-not (Test-Path $WebView2NuGetPackage)) {
            $tempProject = Join-Path $env:TEMP "aster-webview2-restore"
            Remove-Item $tempProject -Recurse -Force -ErrorAction SilentlyContinue
            New-Item -ItemType Directory -Force $tempProject | Out-Null
            dotnet new classlib --force --output $tempProject | Out-Null
            dotnet add $tempProject package Microsoft.Web.WebView2 --version $WebView2PackageVersion | Out-Null
        }

        if (-not (Test-Path (Join-Path $WebView2JucePackage "build\native\include\WebView2.h"))) {
            Remove-Item $WebView2JucePackage -Recurse -Force -ErrorAction SilentlyContinue
            New-Item -ItemType Directory -Force $WebView2JucePackage | Out-Null
            Copy-Item (Join-Path $WebView2NuGetPackage "*") $WebView2JucePackage -Recurse -Force
        }

        $cmakeArgs = @(
            "-S", ".",
            "-B", $BuildDir,
            "-G", "Visual Studio 17 2022",
            "-A", "x64",
            "-DASTER_COPY_PLUGIN_AFTER_BUILD=OFF",
            "-DJUCE_WEBVIEW2_PACKAGE_LOCATION=$VendorDir",
            "-DASTER_AAX_SDK_PATH=$AaxSdkPath"
        )
        & cmake @cmakeArgs
        cmake --build $BuildDir --config Release --parallel
    }
    finally {
        Pop-Location
    }
}

$VstBinary = Join-Path $Artefacts "VST3\ASTER Drum Lab.vst3\Contents\x86_64-win\ASTER Drum Lab.vst3"
$AaxBundle = Join-Path $Artefacts "AAX\ASTER Drum Lab.aaxplugin"
$AaxBinary = Join-Path $AaxBundle "Contents\x64\ASTER Drum Lab.aaxplugin"
$binaries = @($VstBinary)
if ($AaxEnabled) { $binaries += $AaxBinary }
foreach ($file in $binaries) {
    if (-not (Test-Path $file)) { throw "Missing build artefact: $file" }
    Sign-File $file
}

$VstBundle = Join-Path $Artefacts "VST3\ASTER Drum Lab.vst3"
$VstArchive = Join-Path $DistDir "ASTER-Drum-Lab-$Version-Windows-x64-VST3.zip"
Remove-Item $VstArchive -Force -ErrorAction SilentlyContinue
Compress-Archive -Path $VstBundle -DestinationPath $VstArchive -CompressionLevel Optimal
$VstHash = (Get-FileHash -Algorithm SHA256 $VstArchive).Hash.ToLowerInvariant()
"$VstHash  $([System.IO.Path]::GetFileName($VstArchive))" |
    Set-Content -Encoding ascii "$VstArchive.sha256"

$AaxArchive = $null
if ($AaxEnabled) {
    $AaxArchive = Join-Path $DistDir "ASTER-Drum-Lab-$Version-Windows-x64-AAX.zip"
    Remove-Item $AaxArchive -Force -ErrorAction SilentlyContinue
    Compress-Archive -Path $AaxBundle -DestinationPath $AaxArchive -CompressionLevel Optimal
    $AaxHash = (Get-FileHash -Algorithm SHA256 $AaxArchive).Hash.ToLowerInvariant()
    "$AaxHash  $([System.IO.Path]::GetFileName($AaxArchive))" |
        Set-Content -Encoding ascii "$AaxArchive.sha256"
}

$iscc = (Get-Command ISCC.exe -ErrorAction SilentlyContinue).Source
if (-not $iscc) {
    $defaultIscc = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe"
    if (Test-Path $defaultIscc) { $iscc = $defaultIscc }
}
if (-not $iscc) { throw "Inno Setup 6 was not found." }

$iss = Join-Path $PSScriptRoot "ASTER Drum Lab.iss"
$isccArgs = @(
    "/DAppVersion=$Version",
    "/DBuildRoot=$Artefacts",
    "/DOutputDir=$DistDir",
    "/DWebView2Bootstrapper=$WebView2"
)
if ($AaxEnabled) { $isccArgs += "/DIncludeAAX=1" }
$isccArgs += $iss
& $iscc @isccArgs

$Installer = Join-Path $DistDir "ASTER-Drum-Lab-$Version-Windows-x64-Setup.exe"
if (-not (Test-Path $Installer)) { throw "Installer was not created: $Installer" }
Sign-File $Installer

$HashFile = "$Installer.sha256"
$Hash = (Get-FileHash -Algorithm SHA256 $Installer).Hash.ToLowerInvariant()
"$Hash  $([System.IO.Path]::GetFileName($Installer))" | Set-Content -Encoding ascii $HashFile

Write-Host "Created: $Installer"
Write-Host "Checksum: $HashFile"
Write-Host "Created: $VstArchive"
Write-Host "Checksum: $VstArchive.sha256"
if ($AaxEnabled) {
    Write-Host "Created: $AaxArchive"
    Write-Host "Checksum: $AaxArchive.sha256"
}
if ($Unsigned) {
    Write-Warning "This installer is unsigned and is only suitable for local testing."
}
