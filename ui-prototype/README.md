# Drum Sampler UI Prototype

React + TypeScript + Vite で作る、JUCE プラグインの **PAD タブ** UI プロトタイプです。
DSP / オーディオエンジンには触れていません。後で JUCE WebView か React-JUCE 経由で
C++ 側と接続する想定で、パラメータ名は `Source/PadData.h` と揃えてあります。

## セットアップ

```bash
cd ui-prototype
npm install
npm run dev   # http://localhost:5173 が自動で開きます
```

## ディレクトリ構成

```
src/
  main.tsx                          ─ エントリ
  App.tsx                           ─ 画面全体のレイアウト + state
  App.module.css                    ─ 3 カラムグリッド
  types.ts                          ─ PadParams 型 (JUCE 側と同名フィールド)
  data/padData.ts                   ─ モック 16 パッド + MIDI ノート名関数
  utils/waveform.ts                 ─ 疑似波形生成
  styles/
    tokens.css                      ─ 色・サイズ・フォントの設計トークン
    global.css                      ─ リセット + body + utility
  components/
    Header/                         ─ ロゴ + KIT セレクター + SAVE/LOAD
    TabBar/                         ─ PADS / MIXER / FX / SETTINGS
    PadGrid/                        ─ Page A/B/C + 4×4 PadCell
    WaveformEditor/                 ─ 波形 + START/END/Fade マーカー + 7 ノブ
    BottomControlPanel/             ─ Play Mode / Choke / VELOCITY / HUMANIZE 他
    PadSettingsPanel/               ─ 右カラムの Pad 詳細フォーム
    Knob/                           ─ SVG ノブ (見た目専用、ドラッグなし)
    ToggleSwitch/                   ─ ピル型スイッチ (Smart Trim / Reverse 等)
    Dropdown/                       ─ 自前のセレクト UI
    IconButton/                     ─ 小さいボタン (アクティブ状態あり)
```

## カラーテーマ

`src/styles/tokens.css` の CSS 変数を書き換えれば、全体の色が一括で変わります。

- Ice Blue: `--accent-blue` (#38bdf8)
- Champagne Gold: `--accent-gold` (#d6b46a)
- Panel surfaces: `--panel`, `--panel-inset`, `--panel-raised`
