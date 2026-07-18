cmake_minimum_required(VERSION 3.22)

# CMake 4.x との互換性ポリシー
if(POLICY CMP0175)
    cmake_policy(SET CMP0175 OLD)
endif()

project(AsterDrumLab VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ASTER_COPY_PLUGIN_AFTER_BUILD
    "Copy built plug-ins into the current user's plug-in folders"
    ON)

# ── JUCE 8 を自動ダウンロード ─────────────────────────────────────────────────
# 初回 configure で JUCE をクローンします。
# JUCE 8 は WebBrowserComponent の native integration (JS↔C++ bridge) をサポート。
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

set(ASTER_AAX_SDK_PATH "" CACHE PATH "Path to the licensed AAX SDK")
if(NOT ASTER_AAX_SDK_PATH AND DEFINED ENV{AAX_SDK_PATH})
    set(ASTER_AAX_SDK_PATH "$ENV{AAX_SDK_PATH}")
endif()

set(ASTER_PLUGIN_FORMATS VST3 AU)
if(ASTER_AAX_SDK_PATH)
    juce_set_aax_sdk_path("${ASTER_AAX_SDK_PATH}")
    list(APPEND ASTER_PLUGIN_FORMATS AAX)
    message(STATUS "AAX enabled with SDK: ${ASTER_AAX_SDK_PATH}")
else()
    message(STATUS "AAX disabled (set ASTER_AAX_SDK_PATH or AAX_SDK_PATH to enable)")
endif()

# ── プラグイン定義 ─────────────────────────────────────────────────────────────
juce_add_plugin(DrumSampler

    PLUGIN_MANUFACTURER_CODE  Enig
    PLUGIN_CODE               Adlb

    FORMATS                   ${ASTER_PLUGIN_FORMATS}

    PRODUCT_NAME              "ASTER Drum Lab"
    COMPANY_NAME              "ENIGMA"
    BUNDLE_ID                 "com.enigma.asterdrumlab"

    IS_SYNTH                  TRUE
    NEEDS_MIDI_INPUT          TRUE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE

    AU_MAIN_TYPE              kAudioUnitType_MusicDevice

    EDITOR_WANTS_KEYBOARD_FOCUS  FALSE

    COPY_PLUGIN_AFTER_BUILD      ${ASTER_COPY_PLUGIN_AFTER_BUILD}
    NEEDS_WEBVIEW2               TRUE
)

juce_generate_juce_header(DrumSampler)

# ── ソースファイル ─────────────────────────────────────────────────────────────
target_sources(DrumSampler
    PRIVATE
        Source/PadData.cpp
        Source/PadParameterSpecs.cpp
        Source/LayerParameterSpecs.cpp
        Source/KitData.cpp
        Source/AudioFileManager.cpp
        Source/SamplerVoice.cpp
        Source/VoiceManager.cpp
        Source/SmartTrim.cpp
        Source/WaveformDisplay.cpp
        Source/PadDetailEditor.cpp
        Source/DrumSamplerLookAndFeel.cpp
        Source/MixerChannelStrip.cpp
        Source/MixerView.cpp
        Source/RoutingPresets.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/WebViewEditor.cpp        # NEW: WebView ベースの新エディタ
        Source/PadDataJson.cpp          # NEW: PadData ↔ JSON 変換
)

# ── WebUI (React) 静的アセットを BinaryData に埋め込み ────────────────────────
# ui-prototype/ で `npm run build` を実行すると ui-prototype/dist/ が生成される。
# ここを glob して BinaryData.h に焼き付け、WebBrowserComponent から
# resource provider 経由で配信する。
#
# ハッシュ付きファイル名 (index-XXXX.js / index-XXXX.css) が毎ビルドで変わるため
# 個別指定ではなく GLOB_RECURSE + CONFIGURE_DEPENDS で自動追従にする。
file(GLOB_RECURSE WEBUI_DIST_FILES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/ui-prototype/dist/*"
)

if(NOT WEBUI_DIST_FILES)
    message(FATAL_ERROR
        "ui-prototype/dist/ にファイルがありません。\n"
        "先に `cd ui-prototype && npm run build` を実行してから CMake configure してください。")
endif()

juce_add_binary_data(DrumSamplerWebUI
    HEADER_NAME WebUIBinaryData.h
    NAMESPACE   WebUIBinaryData
    SOURCES     ${WEBUI_DIST_FILES}
)

# ── コンパイル定義 ──────────────────────────────────────────────────────────────
target_compile_definitions(DrumSampler
    PUBLIC
        JUCE_WEB_BROWSER=1              # WebView を有効化
        $<$<PLATFORM_ID:Windows>:JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1>
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_REPORT_APP_USAGE=0
)

# ── リンクライブラリ ────────────────────────────────────────────────────────────
target_link_libraries(DrumSampler
    PRIVATE
        DrumSamplerWebUI                # WebUI 静的アセット
        juce::juce_audio_basics
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_gui_basics
        juce::juce_gui_extra            # WebBrowserComponent はここ
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
