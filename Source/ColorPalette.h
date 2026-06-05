#pragma once
#include <JuceHeader.h>

// ─────────────────────────────────────────────────────────────────────────────
// ColorPalette  ─  プラグイン全体で使う色の定数（テーマ変更時はここだけ触る）
//
// デザインコンセプト:
//   Dark Modern ─ マットブラック / ディープチャコール / グラファイト
//   Ice Blue    ─ メインアクセント（選択・操作・メーター・波形・タブ）
//   Champagne   ─ 高級感のある差し色（ロゴ・Pad 番号・見出し・小装飾）少量だけ
//   テキスト    ─ Off White / Cool Gray の階層
//
// 禁止事項:
//   - ゲーミング風の派手なネオン
//   - 強すぎる光沢
//   - 古い DAW 風 / アナログ卓ヘビー
// ─────────────────────────────────────────────────────────────────────────────
namespace ColorPalette
{
    // ── Core theme tokens（新しい意味別トークン）──────────────────────────
    inline juce::Colour background()      { return juce::Colour(0xff080b0e); }
    inline juce::Colour backgroundDeep()  { return juce::Colour(0xff050709); }
    inline juce::Colour panel()           { return juce::Colour(0xff11161a); }
    inline juce::Colour panelRaised()     { return juce::Colour(0xff151b20); }
    inline juce::Colour panelInset()      { return juce::Colour(0xff0b1013); }
    inline juce::Colour borderStrong()    { return juce::Colour(0xff263139); }
    inline juce::Colour borderSoft()      { return juce::Colour(0xff1a242b); }
    inline juce::Colour textMain()        { return juce::Colour(0xffe7ecef); }
    inline juce::Colour textSub()         { return juce::Colour(0xff8d969d); }
    inline juce::Colour textMuted()       { return juce::Colour(0xff5f6a72); }
    inline juce::Colour accentBlue()      { return juce::Colour(0xff38bdf8); }
    inline juce::Colour accentBlueSoft()  { return juce::Colour(0xff1e86b8); }
    inline juce::Colour accentBlueGlow()  { return juce::Colour(0xff38bdf8); }
    inline juce::Colour accentGold()      { return juce::Colour(0xffd6b46a); }
    inline juce::Colour accentGoldDark()  { return juce::Colour(0xff8c7440); }

    // ── Control state tokens ─────────────────────────────────────────────
    inline juce::Colour controlBase()     { return juce::Colour(0xff0d1317); }
    inline juce::Colour controlRaised()   { return juce::Colour(0xff141b20); }
    inline juce::Colour controlHover()    { return juce::Colour(0xff1a252c); }
    inline juce::Colour controlPressed()  { return juce::Colour(0xff070b0e); }
    inline juce::Colour controlDisabled() { return juce::Colour(0xff0a0e11); }

    // ── Page selector tokens ─────────────────────────────────────────────
    inline juce::Colour pageSelectorPanelTop()      { return juce::Colour(0xff151b20); }
    inline juce::Colour pageSelectorPanelBottom()   { return juce::Colour(0xff0c1115); }
    inline juce::Colour pageButtonInactiveTop()     { return juce::Colour(0xff11171b); }
    inline juce::Colour pageButtonInactiveBottom()  { return juce::Colour(0xff070b0e); }
    inline juce::Colour pageButtonHoverTop()        { return juce::Colour(0xff182229); }
    inline juce::Colour pageButtonHoverBottom()     { return juce::Colour(0xff0c1318); }
    inline juce::Colour pageButtonSelectedTop()     { return juce::Colour(0xff14394b); }
    inline juce::Colour pageButtonSelectedBottom()  { return juce::Colour(0xff081c27); }
    inline juce::Colour pageButtonPressedTop()      { return juce::Colour(0xff080e12); }
    inline juce::Colour pageButtonPressedBottom()   { return juce::Colour(0xff05080a); }

    // ── Depth tokens ─────────────────────────────────────────────────────
    inline juce::Colour shadowOuter()     { return juce::Colour(0xff020405); }
    inline juce::Colour shadowInner()     { return juce::Colour(0xff000203); }
    inline juce::Colour highlightTop()    { return juce::Colour(0xff26313a); }

    // ── 背景 / サーフェス ─────────────────────────────────────────────────
    inline juce::Colour bg()          { return background(); }      // ベース（マットブラック）
    inline juce::Colour bgDeep()      { return backgroundDeep(); }  // 一段深い背景（波形 BG など）
    inline juce::Colour surface()     { return panel(); }           // パネル背景
    inline juce::Colour surfaceAlt()  { return panelRaised(); }     // 低いパネル背景
    inline juce::Colour surfaceHigh() { return controlRaised(); }   // 一段明るい面（ボタン off など）
    inline juce::Colour surfaceTop()  { return controlHover(); }    // hover / 強調された面
    inline juce::Colour field()       { return panelInset(); }      // 入力欄 / ComboBox 内部
    inline juce::Colour fieldTop()    { return juce::Colour(0xff10171c); }  // 入力欄上面

    // ── 罫線・区切り ──────────────────────────────────────────────────────
    inline juce::Colour border()      { return borderStrong(); }  // 細い区切り
    inline juce::Colour borderLight() { return juce::Colour(0xff3a4852); }  // やや明るい枠
    inline juce::Colour divider()     { return borderSoft(); }  // セクション分割
    inline juce::Colour innerShadow() { return shadowInner(); }

    // ── Ice Blue（メインアクセント） ──────────────────────────────────────
    inline juce::Colour iceBlue()      { return accentBlue(); }  // 選択・操作
    inline juce::Colour iceBlueLight() { return juce::Colour(0xff8bdcff); }  // hover
    inline juce::Colour iceBlueDim()   { return accentBlueSoft(); }  // 押下・track fill
    inline juce::Colour iceBlueBG()    { return juce::Colour(0xff0d3041); }  // 選択パッド背景
    inline juce::Colour iceBlueFill()  { return juce::Colour(0xff092533); }  // 微妙な青背景
    inline juce::Colour iceGlow()      { return accentBlueGlow(); }  // 弱い外側グロー用

    // ── Champagne Gold（差し色、面積は最小限） ───────────────────────────
    inline juce::Colour champagne()    { return accentGold(); }  // 見出し・ロゴ
    inline juce::Colour champagneDim() { return accentGoldDark(); }  // 暗め
    inline juce::Colour padNumber()    { return accentGold(); }  // Pad 番号（Champagne 寄り）

    // ── テキスト ─────────────────────────────────────────────────────────
    inline juce::Colour textPrim() { return textMain(); }  // 主要テキスト（Off White）
    inline juce::Colour textSec()  { return textSub(); }  // 補助テキスト
    inline juce::Colour textDim()  { return textMuted(); }  // 非アクティブ
    inline juce::Colour sampleText(){ return textMuted().withAlpha(0.82f); } // Sample File Name 用
    inline juce::Colour textOnAccent() { return juce::Colour(0xff0e1418); }  // アクセント面の上で読める黒
    inline juce::Colour textSoft() { return juce::Colour(0xffc8d0d4); }

    // ── 状態色（控えめに） ────────────────────────────────────────────────
    inline juce::Colour muteColor() { return juce::Colour(0xff8a4030); }  // ミュート
    inline juce::Colour soloColor() { return juce::Colour(0xff688f30); }  // ソロ
    inline juce::Colour danger()    { return juce::Colour(0xffc04848); }  // 警告

    // ── 波形 ──────────────────────────────────────────────────────────────
    inline juce::Colour waveformBG()   { return juce::Colour(0xff071015); }  // 波形背景
    inline juce::Colour waveformLine() { return juce::Colour(0xff38bdf8); }  // 波形本体
    inline juce::Colour waveformDim()  { return juce::Colour(0xff102b37); }  // 範囲外
    inline juce::Colour waveformGrid() { return juce::Colour(0xff13242c); }  // グリッド

    // ── パッドボタン ──────────────────────────────────────────────────────
    inline juce::Colour padEmpty()     { return juce::Colour(0xff0d1216); }
    inline juce::Colour padHasSample() { return juce::Colour(0xff111a20); }
    inline juce::Colour padSelected()  { return juce::Colour(0xff0e2d3c); }
    inline juce::Colour padDragOver()  { return juce::Colour(0xff164765); }
    inline juce::Colour padShadow()    { return juce::Colour(0xff030506); }

    // ── フェーダー（メタリック / グラファイト） ─────────────────────────
    inline juce::Colour faderTrackTop()    { return juce::Colour(0xff181818); }
    inline juce::Colour faderTrackBottom() { return juce::Colour(0xff121212); }
    inline juce::Colour faderTrackEdge()   { return juce::Colour(0xff2c2c2c); }
    inline juce::Colour faderThumbHi()     { return juce::Colour(0xff707070); }
    inline juce::Colour faderThumbLo()     { return juce::Colour(0xff343434); }
    inline juce::Colour faderThumbEdge()   { return juce::Colour(0xff1a1a1a); }
    inline juce::Colour faderThumbAccent() { return juce::Colour(0xff8a8a8a); }
    inline juce::Colour faderUnityMark()   { return juce::Colour(0xff4a4a4a); }
    inline juce::Colour faderNotch()       { return juce::Colour(0xff202020); }

    // ── メーター（Ice Blue グラデーション） ──────────────────────────────
    inline juce::Colour meterLow()  { return juce::Colour(0xff1a4060); }   // 底
    inline juce::Colour meterMid()  { return juce::Colour(0xff5bc0f0); }   // 中域
    inline juce::Colour meterHi()   { return juce::Colour(0xff8fdcff); }   // 上域
    inline juce::Colour meterPeak() { return juce::Colour(0xfffae6cc); }   // クリップ近傍
    inline juce::Colour meterBG()   { return juce::Colour(0xff0c0c0c); }
}
