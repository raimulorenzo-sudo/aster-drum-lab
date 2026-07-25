[CmdletBinding()]
param(
    [string]$AaxSdkPath = $env:AAX_SDK_PATH,
    [switch]$SkipBuild,
    [switch]$SignVST3,
    [switch]$SignAAX,
    [switch]$Install
)

$ErrorActionPreference = "Stop"
if (Test-Path variable:PSNativeCommandUseErrorActionPreference) {
    $PSNativeCommandUseErrorActionPreference = $true
}

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$CMakeFile = Get-Content (Join-Path $Root "CMakeLists.txt") -Raw
$Version = [regex]::Match($CMakeFile, 'project\(AsterDrumLab VERSION ([0-9.]+)\)').Groups[1].Value
if (-not $Version) {
    throw "Could not read the project version from CMakeLists.txt."
}

$BuildDir = Join-Path $Root "build-release-windows"
$Artefacts = Join-Path $BuildDir "DrumSampler_artefacts\Release"
$DistDir = Join-Path $Root "dist"
$VendorDir = Join-Path $PSScriptRoot "vendor"
$WebView2PackageVersion = "1.0.3967.48"
$WebView2NuGetPackage = Join-Path $env:USERPROFILE ".nuget\packages\microsoft.web.webview2\$WebView2PackageVersion"
$WebView2JucePackage = Join-Path $VendorDir "Microsoft.Web.WebView2.$WebView2PackageVersion"

if (-not $AaxSdkPath) {
    $DefaultAaxSdk = "C:\SDKs\aax-sdk-2-9-0"
    if (Test-Path (Join-Path $DefaultAaxSdk "Interfaces\ACF")) {
        $AaxSdkPath = $DefaultAaxSdk
    }
}
if (-not $AaxSdkPath -or -not (Test-Path (Join-Path $AaxSdkPath "Interfaces\ACF"))) {
    throw "AAX SDK 2.9 was not found. Set AAX_SDK_PATH or pass -AaxSdkPath."
}

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FilePath,
        [Parameter(ValueFromRemainingArguments = $true)]
        [string[]]$Arguments
    )

    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$FilePath failed with exit code $LASTEXITCODE."
    }
}

function Find-SignTool {
    $Command = Get-Command signtool.exe -ErrorAction SilentlyContinue
    if ($Command) {
        return $Command.Source
    }

    $Candidates = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe" -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending
    if (-not $Candidates) {
        throw "signtool.exe was not found. Install the Windows SDK."
    }
    return $Candidates[0].FullName
}

function Find-WrapTool {
    if ($env:PACE_WRAPTOOL) {
        if (-not (Test-Path $env:PACE_WRAPTOOL)) {
            throw "PACE_WRAPTOOL does not exist: $env:PACE_WRAPTOOL"
        }
        return (Resolve-Path $env:PACE_WRAPTOOL).Path
    }

    $Command = Get-Command wraptool.exe -ErrorAction SilentlyContinue
    if ($Command) {
        return $Command.Source
    }

    $Candidates = @(
        (Join-Path $env:ProgramFiles "PACEAntiPiracy\Eden\Fusion\bin\wraptool.exe"),
        (Join-Path $env:ProgramFiles "PACEAntiPiracy\Eden\Fusion\Current\bin\wraptool.exe"),
        (Join-Path ${env:ProgramFiles(x86)} "PACEAntiPiracy\Eden\Fusion\bin\wraptool.exe"),
        (Join-Path ${env:ProgramFiles(x86)} "PACEAntiPiracy\Eden\Fusion\Current\bin\wraptool.exe")
    )
    foreach ($Candidate in $Candidates) {
        if (Test-Path $Candidate) {
            return (Resolve-Path $Candidate).Path
        }
    }

    throw "PACE wraptool.exe was not found. Install the licensed PACE Eden/Fusion tools or set PACE_WRAPTOOL."
}

function Add-PlatformSigningArguments {
    param([System.Collections.Generic.List[string]]$Arguments)

    if ($env:WINDOWS_CERT_SHA1) {
        $Arguments.Add("--signid")
        $Arguments.Add($env:WINDOWS_CERT_SHA1)
        return
    }

    if (-not $env:WINDOWS_CERT_PFX) {
        throw "Set WINDOWS_CERT_PFX to a PKCS#12/PFX Authenticode certificate, or set WINDOWS_CERT_SHA1."
    }
    if (-not (Test-Path $env:WINDOWS_CERT_PFX)) {
        throw "WINDOWS_CERT_PFX does not exist: $env:WINDOWS_CERT_PFX"
    }

    $Arguments.Add("--keyfile")
    $Arguments.Add((Resolve-Path $env:WINDOWS_CERT_PFX).Path)
    if ($env:WINDOWS_CERT_PASSWORD) {
        $Arguments.Add("--keypassword")
        $Arguments.Add($env:WINDOWS_CERT_PASSWORD)
    }
}

function Sign-Vst3Binary {
    param(
        [string]$SignTool,
        [string]$BinaryPath
    )

    $TimestampUrl = if ($env:WINDOWS_TIMESTAMP_URL) {
        $env:WINDOWS_TIMESTAMP_URL
    } else {
        "http://timestamp.digicert.com"
    }

    $Arguments = @("sign", "/fd", "SHA256", "/td", "SHA256", "/tr", $TimestampUrl)
    if ($env:WINDOWS_CERT_SHA1) {
        $Arguments += @("/sha1", $env:WINDOWS_CERT_SHA1)
    } else {
        if (-not $env:WINDOWS_CERT_PFX) {
            throw "Set WINDOWS_CERT_PFX or WINDOWS_CERT_SHA1 before using -SignVST3."
        }
        if (-not (Test-Path $env:WINDOWS_CERT_PFX)) {
            throw "WINDOWS_CERT_PFX does not exist: $env:WINDOWS_CERT_PFX"
        }
        $Arguments += @("/f", (Resolve-Path $env:WINDOWS_CERT_PFX).Path)
        if ($env:WINDOWS_CERT_PASSWORD) {
            $Arguments += @("/p", $env:WINDOWS_CERT_PASSWORD)
        }
    }
    $Arguments += $BinaryPath

    Invoke-Checked $SignTool @Arguments
    Invoke-Checked $SignTool "verify" "/pa" "/v" $BinaryPath
}

function Write-ArchiveAndChecksum {
    param(
        [string]$BundlePath,
        [string]$ArchivePath
    )

    Remove-Item $ArchivePath -Force -ErrorAction SilentlyContinue
    Compress-Archive -Path $BundlePath -DestinationPath $ArchivePath -CompressionLevel Optimal
    $Hash = (Get-FileHash -Algorithm SHA256 $ArchivePath).Hash.ToLowerInvariant()
    "$Hash  $([System.IO.Path]::GetFileName($ArchivePath))" |
        Set-Content -Encoding ascii "$ArchivePath.sha256"
}

New-Item -ItemType Directory -Force $DistDir, $VendorDir | Out-Null

if (-not $SkipBuild) {
    Push-Location $Root
    try {
        Invoke-Checked "npm" "--prefix" "ui-prototype" "ci"
        Invoke-Checked "npm" "--prefix" "ui-prototype" "run" "build"

        if (-not (Test-Path $WebView2NuGetPackage)) {
            $TempProject = Join-Path $env:TEMP "aster-webview2-restore"
            Remove-Item $TempProject -Recurse -Force -ErrorAction SilentlyContinue
            New-Item -ItemType Directory -Force $TempProject | Out-Null
            Invoke-Checked "dotnet" "new" "classlib" "--force" "--output" $TempProject
            Invoke-Checked "dotnet" "add" $TempProject "package" "Microsoft.Web.WebView2" "--version" $WebView2PackageVersion
        }

        if (-not (Test-Path (Join-Path $WebView2JucePackage "build\native\include\WebView2.h"))) {
            Remove-Item $WebView2JucePackage -Recurse -Force -ErrorAction SilentlyContinue
            New-Item -ItemType Directory -Force $WebView2JucePackage | Out-Null
            Copy-Item (Join-Path $WebView2NuGetPackage "*") $WebView2JucePackage -Recurse -Force
        }

        $CMakeArguments = @(
            "-S", ".",
            "-B", $BuildDir,
            "-G", "Visual Studio 17 2022",
            "-A", "x64",
            "-DASTER_COPY_PLUGIN_AFTER_BUILD=OFF",
            "-DJUCE_WEBVIEW2_PACKAGE_LOCATION=$VendorDir",
            "-DASTER_AAX_SDK_PATH=$AaxSdkPath"
        )
        Invoke-Checked "cmake" @CMakeArguments
        Invoke-Checked "cmake" "--build" $BuildDir "--config" "Release" "--target" "DrumSampler_VST3" "DrumSampler_AAX" "--parallel"
    }
    finally {
        Pop-Location
    }
}

$VstBundle = Join-Path $Artefacts "VST3\ASTERDrumLab.vst3"
$VstBinary = Join-Path $VstBundle "Contents\x86_64-win\ASTERDrumLab.vst3"
$AaxBundle = Join-Path $Artefacts "AAX\ASTERDrumLab.aaxplugin"
$AaxBinary = Join-Path $AaxBundle "Contents\x64\ASTERDrumLab.aaxplugin"

foreach ($RequiredPath in @($VstBundle, $VstBinary, $AaxBundle, $AaxBinary)) {
    if (-not (Test-Path $RequiredPath)) {
        throw "Missing build artefact: $RequiredPath"
    }
}

$SignTool = $null
if ($SignVST3) {
    $SignTool = Find-SignTool
    Sign-Vst3Binary $SignTool $VstBinary
}

if ($SignAAX) {
    if (-not $env:PACE_CUSTOMER_NUMBER) {
        throw "Set PACE_CUSTOMER_NUMBER before using -SignAAX."
    }

    $WrapTool = Find-WrapTool
    Invoke-Checked $WrapTool "list"

    $PaceArguments = [System.Collections.Generic.List[string]]::new()
    $PaceArguments.Add("sign")
    $PaceArguments.Add("--in")
    $PaceArguments.Add($AaxBundle)
    $PaceArguments.Add("--customernumber")
    $PaceArguments.Add($env:PACE_CUSTOMER_NUMBER)
    $PaceArguments.Add("--customername")
    $PaceArguments.Add($(if ($env:PACE_CUSTOMER_NAME) { $env:PACE_CUSTOMER_NAME } else { "ENIGMA" }))
    $PaceArguments.Add("--productname")
    $PaceArguments.Add($(if ($env:PACE_PRODUCT_NAME) { $env:PACE_PRODUCT_NAME } else { "ASTER Drum Lab" }))
    Add-PlatformSigningArguments $PaceArguments

    $PaceArgumentArray = $PaceArguments.ToArray()
    Invoke-Checked $WrapTool @PaceArgumentArray
    Invoke-Checked $WrapTool "verify" "--in" $AaxBundle

    if (-not $SignTool) {
        $SignTool = Find-SignTool
    }
    Invoke-Checked $SignTool "verify" "/pa" "/v" $AaxBinary
}

$VstArchive = Join-Path $DistDir "ASTER-Drum-Lab-$Version-Windows-x64-VST3.zip"
$AaxArchive = Join-Path $DistDir "ASTER-Drum-Lab-$Version-Windows-x64-AAX.zip"
Write-ArchiveAndChecksum $VstBundle $VstArchive
Write-ArchiveAndChecksum $AaxBundle $AaxArchive

if ($Install) {
    $VstInstallRoot = Join-Path $env:CommonProgramFiles "VST3"
    $AaxInstallRoot = Join-Path $env:CommonProgramFiles "Avid\Audio\Plug-Ins"
    $VstInstallPath = Join-Path $VstInstallRoot "ASTERDrumLab.vst3"
    $AaxInstallPath = Join-Path $AaxInstallRoot "ASTERDrumLab.aaxplugin"

    New-Item -ItemType Directory -Force $VstInstallRoot, $AaxInstallRoot | Out-Null
    Remove-Item $VstInstallPath -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item $AaxInstallPath -Recurse -Force -ErrorAction SilentlyContinue
    Copy-Item $VstBundle $VstInstallPath -Recurse -Force
    Copy-Item $AaxBundle $AaxInstallPath -Recurse -Force

    Write-Host "Installed: $VstInstallPath"
    Write-Host "Installed: $AaxInstallPath"
}

Write-Host "Version: $Version"
Write-Host "Created: $VstArchive"
Write-Host "Checksum: $VstArchive.sha256"
Write-Host "Created: $AaxArchive"
Write-Host "Checksum: $AaxArchive.sha256"
if (-not $SignAAX) {
    Write-Warning "AAX was built but not PACE signed. It will not load in a shipping Pro Tools build."
}
