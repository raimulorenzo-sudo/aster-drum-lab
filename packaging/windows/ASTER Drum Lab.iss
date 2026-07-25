#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef BuildRoot
  #error BuildRoot must point to DrumSampler_artefacts\Release
#endif
#ifndef OutputDir
  #define OutputDir "..\..\dist"
#endif
#ifndef WebView2Bootstrapper
  #error WebView2Bootstrapper must point to MicrosoftEdgeWebview2Setup.exe
#endif

#define AppName "ASTER Drum Lab"
#define CompanyName "ENIGMA"

[Setup]
AppId={{5B0E14CB-058A-46E5-A81A-10F8E8A53D42}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#CompanyName}
CreateAppDir=no
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutputDir}
OutputBaseFilename=ASTER-Drum-Lab-{#AppVersion}-Windows-x64-Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
UninstallDisplayName={#AppName}
VersionInfoVersion={#AppVersion}
VersionInfoCompany={#CompanyName}
VersionInfoDescription={#AppName} Installer
VersionInfoProductName={#AppName}

[Files]
Source: "{#BuildRoot}\VST3\ASTERDrumLab.vst3\*"; DestDir: "{commoncf64}\VST3\ASTERDrumLab.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
#ifdef IncludeAAX
Source: "{#BuildRoot}\AAX\ASTERDrumLab.aaxplugin\*"; DestDir: "{commoncf64}\Avid\Audio\Plug-Ins\ASTERDrumLab.aaxplugin"; Flags: ignoreversion recursesubdirs createallsubdirs
#endif
Source: "{#WebView2Bootstrapper}"; DestDir: "{tmp}"; DestName: "MicrosoftEdgeWebview2Setup.exe"; Flags: deleteafterinstall

[Run]
Filename: "{tmp}\MicrosoftEdgeWebview2Setup.exe"; Parameters: "/silent /install"; StatusMsg: "Installing Microsoft Edge WebView2 Runtime..."; Flags: waituntilterminated; Check: NeedsWebView2

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\ASTERDrumLab.vst3"
#ifdef IncludeAAX
Type: filesandordirs; Name: "{commoncf64}\Avid\Audio\Plug-Ins\ASTERDrumLab.aaxplugin"
#endif

[Code]
function HasWebView2Version(RootKey: Integer; SubKey: String): Boolean;
var
  Version: String;
begin
  Result :=
    RegQueryStringValue(
      RootKey,
      SubKey,
      'pv',
      Version
    ) and (Version <> '') and (Version <> '0.0.0.0');
end;

function NeedsWebView2: Boolean;
begin
  Result := not (
    HasWebView2Version(
      HKLM,
      'SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}'
    ) or
    HasWebView2Version(
      HKCU,
      'Software\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}'
    )
  );
end;
