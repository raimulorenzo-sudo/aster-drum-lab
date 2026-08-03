# ASTER Drum Lab v1.0.1 — Windows Codex handoff

This handoff is for building and signing the Windows x64 VST3 and AAX plug-ins
on a Windows PC. Do not create an EXE, MSI, PKG, release, or public upload.

## What is already prepared

- Repository: `https://github.com/raimulorenzo-sudo/aster-drum-lab.git`
- Branch: `codex/v1.0.0-windows-aax-handoff`
- Product version: `1.0.1`
- DAW-visible product name: `ASTER Drum Lab`
- Windows physical filename: `ASTERDrumLab`
- Build helper: `packaging/windows/build-plugins.ps1`
- Output:
  - `dist/ASTER-Drum-Lab-1.0.1-Windows-x64-VST3.zip`
  - `dist/ASTER-Drum-Lab-1.0.1-Windows-x64-AAX.zip`
  - one `.sha256` file beside each ZIP

The physical Windows filename intentionally has no spaces. AAX SDK 2.9
documents an AAE limitation that rejects Windows AAX binary names containing
spaces. The name shown inside a DAW remains `ASTER Drum Lab`.

## Never put these in Git or Codex chat

- AAX SDK files
- PACE Eden/Fusion tools or cache
- PFX/P12/private-key files
- certificate password
- PACE account password
- customer number
- iLok account credentials

Enter secrets directly into a local PowerShell session. The repository ignores
common certificate and build-output file types, but still inspect `git status`
before every commit.

## Windows prerequisites

- Git
- Visual Studio 2022 with `Desktop development with C++`
- MSVC x64 tools and Windows 10/11 SDK
- CMake
- Node.js 20 or later
- .NET SDK
- AAX SDK 2.9, normally placed at `C:\SDKs\aax-sdk-2-9-0`
- iLok License Manager
- licensed PACE Eden/Fusion signing tools
- physical iLok containing the AAX signing-tools license
- Windows Authenticode code-signing certificate
- Pro Tools and/or AAX Validator for final validation when available

Inno Setup is not required because this handoff does not create an installer.

## Ready-to-paste prompt for Windows Codex

Copy everything inside the following block and paste it into Codex on the
Windows PC:

```text
ASTER Drum Lab v1.0.1のWindows x64版VST3とAAXをビルドし、AAXをPACE署名して、ローカルへ上書きインストールしてください。EXE、MSI、PKG、GitHub Releaseは作成しないでください。

リポジトリ:
https://github.com/raimulorenzo-sudo/aster-drum-lab.git

使用ブランチ:
codex/v1.0.0-windows-aax-handoff

まず次を行ってください。
1. リポジトリがなければcloneし、指定ブランチへswitchする。既にある場合は未コミット変更を勝手に削除せず、状態を確認してからpull --ff-onlyする。
2. WINDOWS_CODEX_HANDOFF.mdとpackaging/windows/build-plugins.ps1を最後まで読む。
3. CMakeLists.txtがVERSION 1.0.1であること、git rev-parse --abbrev-ref HEADが指定ブランチであることを確認する。
4. Visual Studio 2022 C++、Windows SDK、CMake、Node.js、.NET SDK、AAX SDK 2.9、iLok License Manager、PACE Eden/Fusion、wraptool.exe、AuthentiCode証明書の有無を調べる。
5. 不足する通常の開発ツールは安全な方法で導入してよい。AAX SDK、PACE Tools、証明書、iLokライセンスが不足する場合は、正確な不足項目と入手元を報告してユーザーの操作を待つ。

重要な安全ルール:
- AAX SDK、PACEツール、PFX、秘密鍵、パスワード、PACEカスタマー番号、iLok認証情報をGitへ追加しない。
- パスワードやカスタマー番号をCodexチャットへ貼るよう求めない。
- 秘密値が必要なら、ユーザーにWindows PowerShellへ直接環境変数として入力してもらう。
- ユーザーの既存ファイルや未コミット変更を削除しない。
- mainへのmerge、tag作成、push、公開アップロードはしない。
- AAXを通常のsigntoolだけで完了扱いにしない。必ずPACE wraptoolで署名し、wraptool verifyを通す。

AAX SDKは原則C:\SDKs\aax-sdk-2-9-0に置き、Interfaces\ACFが存在することを確認してください。

秘密値はユーザー自身がPowerShellへ直接設定します。未設定なら、値そのものを聞かずに次の形式で設定するよう案内してください。

$env:AAX_SDK_PATH = "C:\SDKs\aax-sdk-2-9-0"
$env:PACE_CUSTOMER_NUMBER = "<PowerShellへ直接入力>"
$env:PACE_CUSTOMER_NAME = "ENIGMA"
$env:PACE_PRODUCT_NAME = "ASTER Drum Lab"
$env:WINDOWS_CERT_PFX = "C:\secure\enigma-code-signing.pfx"
$env:WINDOWS_CERT_PASSWORD = "<PowerShellへ直接入力>"

証明書をWindows証明書ストアへ登録済みの場合は、WINDOWS_CERT_PFXの代わりにWINDOWS_CERT_SHA1を使用できます。PACE_WRAPTOOLが自動検出されない場合だけ、wraptool.exeの絶対パスをPACE_WRAPTOOLへ設定してください。

環境が揃ったら、管理者権限のDeveloper PowerShell for VS 2022で次を実行してください。

.\packaging\windows\build-plugins.ps1 -SignVST3 -SignAAX -Install

このスクリプトはVST3/AAXのみを作り、EXEは作りません。以下をすべて検証してください。
- VST3: build-release-windows\DrumSampler_artefacts\Release\VST3\ASTERDrumLab.vst3
- AAX: build-release-windows\DrumSampler_artefacts\Release\AAX\ASTERDrumLab.aaxplugin
- PACE: wraptool verify --in <AAX bundle> が成功
- Authenticode: signtool verify /pa /v <AAX binary> が成功
- distにVST3/AAXのZIPとSHA-256が生成
- C:\Program Files\Common Files\VST3\ASTERDrumLab.vst3へ上書きインストール
- C:\Program Files\Common Files\Avid\Audio\Plug-Ins\ASTERDrumLab.aaxpluginへ上書きインストール

Pro ToolsまたはAAX Validatorがインストール済みなら、実際にAAXをスキャンしてロード確認してください。未導入なら署名検証まで行い、未実施項目として明記してください。

完了報告には次を含めてください。
- 使用したcommit SHA
- VST3/AAXのバージョンとx64確認
- PACE verify結果
- Authenticode verify結果
- インストール先
- ZIPの絶対パスとSHA-256
- Pro Tools/AAX Validatorの実施結果
- 残っている未検証事項

問題が出た場合は推測で成功扱いにせず、完全なエラー文、実行コマンド、確認済みの前提を報告して原因を修正してください。
```

## Expected manual preparation

Download the licensed AAX SDK and PACE signing toolkit from the authorized
Avid/PACE channels directly on the Windows PC. Do not transfer either through
the GitHub repository.

Connect the signing iLok before using `-SignAAX`. The script calls
`wraptool list` first so a missing signing-tools license fails before the AAX
bundle is modified.

If no commercial Authenticode certificate is available, stop and decide
explicitly whether to use a temporary self-signed certificate for testing.
Never label a self-signed build as a fully distribution-ready Windows release.
