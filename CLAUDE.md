# ASTER Drum Rack — Working Notes

## Default workflow (per user instruction 2026-05-28)

ユーザーの明示的な指示「基本もうこの作業はすること」を受け、以下を**標準フロー**として扱う。

### コード変更を反映した後、明示的に依頼されたら次を実行する

1. **Web UI ビルド**: `npm --prefix ui-prototype run build`
2. **JUCE プラグインビルド (Debug)**: `cmake --build build --config Debug`
   - 設定対象: `DrumSampler_VST3` / `DrumSampler_AU` / `DrumSampler_Standalone`
   - CMakeLists で `COPY_PLUGIN_AFTER_BUILD TRUE` 指定済 → ビルド成功で自動的に下記へインストール:
     - VST3: `~/Library/Audio/Plug-Ins/VST3/ASTER Drum Rack.vst3`
     - AU:   `~/Library/Audio/Plug-Ins/Components/ASTER Drum Rack.component`
3. **Standalone 実行ファイル**: `build/DrumSampler_artefacts/Debug/Standalone/ASTER Drum Rack.app`(手動コピーは不要、ビルド先がそのまま実行用)

### 重要事項

- 「上書きインストール」と言われたら **VST3 + AU + Standalone の3つすべて** をビルドし、Library 配下を上書きする。
- ユーザーは毎回頼まなくても基本的にコード反映後はビルド + インストールを期待している (明示的依頼があったとき)。
- `build-and-open-standalone.command` が公式のビルド + 起動スクリプト。これを参考にするか、`cmake --build build --config Debug` を直接叩く。
- ビルド出力が長いので `run_in_background: true` で起動し、完了通知を待つ。
- ビルド失敗時は CMake のエラーメッセージを抜粋して報告。

## Project Layout

- `Source/`: C++ JUCE プラグイン本体 (PluginProcessor / PluginEditor / VoiceManager / etc.)
- `ui-prototype/`: React + Vite の Web UI (WebView 経由で JUCE と連携)
- `build/`: Debug ビルド成果物
- `CMakeLists.txt`: JUCE プラグイン構成。`FORMATS VST3 AU Standalone`、`COPY_PLUGIN_AFTER_BUILD TRUE`

## UI prototype shortcuts

- `cd ui-prototype && npm run dev` で Vite preview (port 5173)
- Tab structure: `PadEditorTabs` (TRIM / PLAYBACK / DYNAMICS / FX)
- 各 Layer は `LayerParams` (sample, trim, playback, dynamics, mixer, polarityInvert, fxChain[])
- FX Chain は `LayerParams.fxChain?: FxSlot[]`、初期で neutral EQ
- ビュー状態 (waveform zoom) は transient — 保存しない
