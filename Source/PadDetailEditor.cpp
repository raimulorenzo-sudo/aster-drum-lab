#include "PadDetailEditor.h"
#include "DrumSamplerLookAndFeel.h"
#include "ColorPalette.h"
#include "Typography.h"
#include "Spacing.h"

namespace
{
    juce::String formatDbFromLinear(double value)
    {
        if (value <= 0.0001)
            return "-inf dB";

        return juce::String(20.0 * std::log10(value), 1) + " dB";
    }

    juce::String formatPanValue(double value)
    {
        const double clamped = juce::jlimit(-1.0, 1.0, value);
        if (std::abs(clamped) < 0.005)
            return "C";

        const int amount = juce::roundToInt(std::abs(clamped) * 100.0);
        return (clamped < 0.0 ? "L" : "R") + juce::String(amount);
    }

    juce::String formatSemitones(double value)
    {
        return juce::String(value >= 0.0 ? "+" : "") + juce::String(value, 2) + " st";
    }

    juce::String formatMilliseconds(double seconds)
    {
        const double ms = seconds * 1000.0;
        const int decimals = ms < 10.0 ? 1 : 0;
        return juce::String(ms, decimals) + " ms";
    }

    juce::String formatPercent(double value)
    {
        return juce::String(value * 100.0, 1) + "%";
    }

    juce::String formatPercentWhole(double value)
    {
        return juce::String(value * 100.0, 0) + "%";
    }

    double playbackRangeSeconds(DrumSamplerAudioProcessor& proc, int padIndex)
    {
        if (padIndex < 0 || padIndex >= NUM_PADS)
            return 0.0;

        const auto& pad = proc.getKit().pads[static_cast<size_t>(padIndex)];
        juce::ScopedReadLock rl(proc.getFileManager().getReadWriteLock());
        const auto* buf = proc.getFileManager().getBufferNoLock(padIndex);
        const double sampleRate = proc.getFileManager().getSampleRate(padIndex);
        if (buf == nullptr || buf->getNumSamples() <= 0 || sampleRate <= 0.0)
            return 0.0;

        const double totalSamples = static_cast<double>(buf->getNumSamples());
        return juce::jmax(0.0, (pad.endPosition - pad.startPosition) * totalSamples / sampleRate);
    }

    juce::String formatFadeRatioMs(DrumSamplerAudioProcessor& proc, int padIndex, double ratio)
    {
        const double rangeSeconds = playbackRangeSeconds(proc, padIndex);
        if (rangeSeconds <= 0.0)
            return formatPercent(ratio);

        return formatMilliseconds(rangeSeconds * ratio);
    }

    void setupTitleLabel(juce::Label& label, const juce::String& text)
    {
        label.setText(text, juce::dontSendNotification);
        label.setFont(Typography::sectionTitle(10.7f));
        label.setColour(juce::Label::textColourId, ColorPalette::champagne());
        label.setJustificationType(juce::Justification::centredLeft);
    }

    void setupFieldLabel(juce::Label& label, const juce::String& text)
    {
        label.setText(text, juce::dontSendNotification);
        label.setFont(Typography::label(9.3f));
        label.setColour(juce::Label::textColourId, ColorPalette::textSub().withAlpha(0.82f));
        label.setJustificationType(juce::Justification::centredLeft);
    }

    void setupValueLabel(juce::Label& label)
    {
        label.setFont(Typography::ui(11.8f));
        label.setColour(juce::Label::textColourId, ColorPalette::textPrim());
        label.setColour(juce::Label::backgroundColourId, ColorPalette::panelInset().withAlpha(0.88f));
        label.setColour(juce::Label::outlineColourId, ColorPalette::borderSoft().withAlpha(0.92f));
        label.setColour(juce::Label::outlineWhenEditingColourId, ColorPalette::iceBlue());
        label.setJustificationType(juce::Justification::centredLeft);
    }

    juce::String compactFileName(juce::String text, int maxChars)
    {
        if (text.length() <= maxChars)
            return text;

        return text.substring(0, juce::jmax(0, maxChars - 3)) + "...";
    }

    void drawQuietSection(juce::Graphics& g, juce::Rectangle<int> area)
    {
        auto r = area.toFloat();
        juce::ColourGradient bg(ColorPalette::panelInset().withAlpha(0.46f), r.getCentreX(), r.getY(),
                                ColorPalette::backgroundDeep().withAlpha(0.30f), r.getCentreX(), r.getBottom(),
                                false);
        g.setGradientFill(bg);
        g.fillRoundedRectangle(r, Spacing::radiusControl);
        g.setColour(ColorPalette::borderSoft().withAlpha(0.72f));
        g.drawRoundedRectangle(r, Spacing::radiusControl, 1.0f);
    }

    int getSettingsPanelWidth(int availableWidth)
    {
        if (availableWidth < 780)
            return juce::jlimit(220, 260, juce::roundToInt(static_cast<float>(availableWidth) * 0.34f));

        return juce::jlimit(248, 320, juce::roundToInt(static_cast<float>(availableWidth) * 0.31f));
    }
}

PadDetailEditor::PadDetailEditor(DrumSamplerAudioProcessor& processor)
    : proc(processor)
{
    setupTitleLabel(settingsTitleLabel, "PAD SETTINGS");
    addAndMakeVisible(settingsTitleLabel);

    setupFieldLabel(midiTitleLabel, "MIDI NOTE");
    setupFieldLabel(padNameTitleLabel, "PAD NAME");
    setupFieldLabel(sampleFileTitleLabel, "SAMPLE FILE");
    setupFieldLabel(outputTitleLabel, "OUTPUT");
    setupFieldLabel(playModeTitleLabel, "PLAY MODE");
    setupFieldLabel(chokeTitleLabel, "CHOKE GROUP");

    for (auto* label : { &midiTitleLabel, &padNameTitleLabel, &sampleFileTitleLabel,
                         &outputTitleLabel, &playModeTitleLabel, &chokeTitleLabel })
        addAndMakeVisible(label);

    padNameLabel.setFont(Typography::uiMedium(13.0f));
    padNameLabel.setColour(juce::Label::textColourId, ColorPalette::textPrim());
    padNameLabel.setColour(juce::Label::backgroundColourId, ColorPalette::panelInset().withAlpha(0.88f));
    padNameLabel.setColour(juce::Label::outlineColourId, ColorPalette::borderSoft().withAlpha(0.92f));
    padNameLabel.setColour(juce::Label::outlineWhenEditingColourId, ColorPalette::iceBlue());
    padNameLabel.setEditable(false, true);
    padNameLabel.setJustificationType(juce::Justification::centredLeft);
    padNameLabel.onTextChange = [this]
    {
        if (currentPadIndex < 0) return;

        auto& pad = proc.getKit().pads[static_cast<size_t>(currentPadIndex)];
        const auto name = padNameLabel.getText(true).trim();
        if (name.isNotEmpty() && name != pad.padName)
        {
            pad.padName = name;
            proc.markKitDirty();
        }

        rebuildOutputAssignBox();
    };
    addAndMakeVisible(padNameLabel);

    setupValueLabel(midiLabel);
    midiLabel.setEditable(false, false);
    addAndMakeVisible(midiLabel);

    waveformFileLabel.setFont(Typography::sampleName(12.0f));
    waveformFileLabel.setColour(juce::Label::textColourId, ColorPalette::textSub());
    waveformFileLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(waveformFileLabel);

    setupValueLabel(sampleFileValueLabel);
    sampleFileValueLabel.setColour(juce::Label::textColourId, ColorPalette::sampleText());
    sampleFileValueLabel.setFont(Typography::sampleName(10.6f));
    addAndMakeVisible(sampleFileValueLabel);

    relinkSampleButton.setColour(juce::TextButton::textColourOffId, ColorPalette::iceBlueLight());
    relinkSampleButton.setVisible(false);
    relinkSampleButton.onClick = [this]
    {
        if (currentPadIndex < 0) return;
        const int targetPadIndex = currentPadIndex;

        const auto& pad = proc.getKit().pads[static_cast<size_t>(targetPadIndex)];
        juce::File initialFile;
        if (pad.sampleFilePath.isNotEmpty())
            initialFile = juce::File(pad.sampleFilePath);

        juce::File initialLocation = initialFile;
        if (! initialLocation.exists())
            initialLocation = initialFile.getParentDirectory();
        if (! initialLocation.exists())
            initialLocation = juce::File::getSpecialLocation(juce::File::userHomeDirectory);

        auto chooser = std::make_shared<juce::FileChooser>(
            "Relink sample for " + pad.padName,
            initialLocation,
            "*.wav;*.aiff;*.aif");

        auto flags = juce::FileBrowserComponent::openMode
                   | juce::FileBrowserComponent::canSelectFiles;

        chooser->launchAsync(flags,
            [this, chooser, targetPadIndex](const juce::FileChooser& fc)
            {
                const auto file = fc.getResult();
                if (file == juce::File{}) return;

                if (proc.relinkPadSample(targetPadIndex, file))
                {
                    if (currentPadIndex == targetPadIndex)
                    {
                        updateWaveform();
                        updateAllFromPad();
                    }
                    repaint();
                }
            });
    };
    addAndMakeVisible(relinkSampleButton);

    addAndMakeVisible(outputAssignBox);
    playModeBox.addItem("One Shot", 1);
    playModeBox.addItem("Gate", 2);
    addAndMakeVisible(playModeBox);

    chokeGroupBox.addItem("Off", 1);
    for (int i = 1; i <= 4; ++i)
        chokeGroupBox.addItem(juce::String(i), i + 1);
    addAndMakeVisible(chokeGroupBox);

    addAndMakeVisible(waveformDisplay);

    smartTrimLabel.setText("SMART TRIM", juce::dontSendNotification);
    smartTrimLabel.setFont(Typography::buttonText(9.4f));
    smartTrimLabel.setColour(juce::Label::textColourId, ColorPalette::textSec());
    smartTrimLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(smartTrimLabel);

    smartTrimToggle.setColour(juce::ToggleButton::textColourId, ColorPalette::textSec());
    smartTrimToggle.setColour(juce::ToggleButton::tickColourId, ColorPalette::iceBlue());
    smartTrimToggle.setToggleState(proc.getKit().smartTrimOnSampleLoad, juce::dontSendNotification);
    smartTrimToggle.onClick = [this]
    {
        if (proc.getKit().smartTrimOnSampleLoad != smartTrimToggle.getToggleState())
        {
            proc.getKit().smartTrimOnSampleLoad = smartTrimToggle.getToggleState();
            proc.markKitDirty();
        }
    };
    addAndMakeVisible(smartTrimToggle);

    autoFadeLabel.setText("AUTO FADE", juce::dontSendNotification);
    autoFadeLabel.setFont(Typography::buttonText(9.4f));
    autoFadeLabel.setColour(juce::Label::textColourId, ColorPalette::textSec());
    autoFadeLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(autoFadeLabel);

    autoFadeToggle.setColour(juce::ToggleButton::textColourId, ColorPalette::textSec());
    autoFadeToggle.setColour(juce::ToggleButton::tickColourId, ColorPalette::iceBlue());
    autoFadeToggle.setToggleState(proc.getKit().autoFadeOnTrim, juce::dontSendNotification);
    autoFadeToggle.onClick = [this]
    {
        if (proc.getKit().autoFadeOnTrim != autoFadeToggle.getToggleState())
        {
            proc.getKit().autoFadeOnTrim = autoFadeToggle.getToggleState();
            proc.markKitDirty();
        }
    };
    addAndMakeVisible(autoFadeToggle);

    btnReanalyze.setButtonText("RE-ANALYZE");
    btnReanalyze.setColour(juce::TextButton::textColourOffId, ColorPalette::iceBlue());
    btnReanalyze.onClick = [this]
    {
        if (currentPadIndex < 0) return;
        proc.reanalyzePad(currentPadIndex);
        updateAllFromPad();
    };
    addAndMakeVisible(btnReanalyze);

    setupSlider(sStart,    "START",        0.0,  1.0,   0.0,   0, "%");
    setupSlider(sEnd,      "END",          0.0,  1.0,   1.0,   0, "%");
    setupSlider(sFadeIn,   "FADE IN",      0.0,  1.0,   0.0,   0, "%");
    setupSlider(sFadeOut,  "FADE OUT",     0.0,  1.0,   0.0,   0, "%");
    setupSlider(sPitch,    "PITCH",      -24.0, 24.0,   0.0,   1, " st");
    setupSlider(sAttack,   "ATTACK",       0.0,  2.0,   0.002, 3, " s");
    setupSlider(sRelease,  "RELEASE",      0.0,  4.0,   0.05,  3, " s");
    setupSlider(sVolume,   "VOLUME",       0.0,  1.0,   1.0,   0, "%");
    setupSlider(sPan,      "PAN",         -1.0,  1.0,   0.0,   2);
    setupSlider(sVelSens,  "VELOCITY",     0.0,  1.0,   1.0,   0, "%");
    setupSlider(sHumanize, "HUMANIZE",     0.0,  1.0,   0.0,   0, "%");

    sStart.slider.textFromValueFunction = [] (double v) { return formatPercent(v); };
    sEnd.slider.textFromValueFunction = [] (double v) { return formatPercent(v); };
    sFadeIn.slider.textFromValueFunction = [this] (double v) { return formatFadeRatioMs(proc, currentPadIndex, v); };
    sFadeOut.slider.textFromValueFunction = [this] (double v) { return formatFadeRatioMs(proc, currentPadIndex, v); };
    sPitch.slider.textFromValueFunction = [] (double v) { return formatSemitones(v); };
    sAttack.slider.textFromValueFunction = [] (double v) { return formatMilliseconds(v); };
    sRelease.slider.textFromValueFunction = [] (double v) { return formatMilliseconds(v); };
    sVolume.slider.textFromValueFunction = [] (double v) { return formatDbFromLinear(v); };
    sPan.slider.textFromValueFunction = [] (double v) { return formatPanValue(v); };
    sVelSens.slider.textFromValueFunction = [] (double v) { return formatPercentWhole(v); };
    sHumanize.slider.textFromValueFunction = [] (double v) { return formatPercentWhole(v); };

    applyButtonStyle(btnOneShot, ColorPalette::iceBlueDim());
    applyButtonStyle(btnGate,    ColorPalette::iceBlueDim());
    applyButtonStyle(btnReverse, ColorPalette::iceBlueDim());
    applyButtonStyle(btnMute,    ColorPalette::muteColor());
    applyButtonStyle(btnSolo,    ColorPalette::soloColor());

    btnOneShot.setButtonText("ONE SHOT");
    btnGate.setButtonText("GATE");
    btnReverse.setButtonText("REVERSE");

    btnOneShot.setClickingTogglesState(true);
    btnGate.setClickingTogglesState(true);
    btnOneShot.setRadioGroupId(1001);
    btnGate.setRadioGroupId(1001);
    btnReverse.setClickingTogglesState(true);
    btnMute.setClickingTogglesState(true);
    btnSolo.setClickingTogglesState(true);

    for (auto* btn : { &btnOneShot, &btnGate, &btnReverse, &btnMute, &btnSolo })
        addAndMakeVisible(btn);

    wireCallbacks();
}

void PadDetailEditor::setupSlider(ParamSlider& ps,
                                  const juce::String& labelText,
                                  double rangeMin,
                                  double rangeMax,
                                  double defaultVal,
                                  int decimalPlaces,
                                  const juce::String& suffix)
{
    ps.decimals = decimalPlaces;
    ps.suffix = suffix;

    ps.label.setText(labelText, juce::dontSendNotification);
    ps.label.setFont(Typography::title(9.2f));
    ps.label.setColour(juce::Label::textColourId, ColorPalette::champagne());
    ps.label.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(ps.label);

    ps.slider.setRange(rangeMin, rangeMax);
    ps.slider.setValue(defaultVal, juce::dontSendNotification);
    ps.slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    ps.slider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    ps.slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.20f,
                                  juce::MathConstants<float>::pi * 2.80f,
                                  true);
    ps.slider.setDoubleClickReturnValue(true, defaultVal);
    ps.slider.setPopupDisplayEnabled(true, false, nullptr);
    addAndMakeVisible(ps.slider);

    ps.value.setFont(Typography::ui(10.0f));
    ps.value.setColour(juce::Label::textColourId, ColorPalette::textSec());
    ps.value.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(ps.value);

    updateValueLabel(ps);
}

void PadDetailEditor::applyButtonStyle(juce::TextButton& btn, juce::Colour onColour)
{
    btn.setColour(juce::TextButton::buttonColourId,   ColorPalette::bgDeep().withAlpha(0.70f));
    btn.setColour(juce::TextButton::buttonOnColourId, onColour);
    btn.setColour(juce::TextButton::textColourOffId,  ColorPalette::textSec());
    btn.setColour(juce::TextButton::textColourOnId,   ColorPalette::iceBlueLight());
}

void PadDetailEditor::updateValueLabel(ParamSlider& ps)
{
    juce::String text;

    if (&ps == &sVolume)         text = formatDbFromLinear(ps.slider.getValue());
    else if (&ps == &sPan)       text = formatPanValue(ps.slider.getValue());
    else if (&ps == &sPitch)     text = formatSemitones(ps.slider.getValue());
    else if (&ps == &sAttack)    text = formatMilliseconds(ps.slider.getValue());
    else if (&ps == &sRelease)   text = formatMilliseconds(ps.slider.getValue());
    else if (&ps == &sStart)     text = formatPercent(ps.slider.getValue());
    else if (&ps == &sEnd)       text = formatPercent(ps.slider.getValue());
    else if (&ps == &sFadeIn)    text = formatFadeRatioMs(proc, currentPadIndex, ps.slider.getValue());
    else if (&ps == &sFadeOut)   text = formatFadeRatioMs(proc, currentPadIndex, ps.slider.getValue());
    else if (&ps == &sVelSens)   text = formatPercentWhole(ps.slider.getValue());
    else if (&ps == &sHumanize)  text = formatPercentWhole(ps.slider.getValue());
    else                         text = juce::String(ps.slider.getValue(), ps.decimals);

    ps.value.setText(text, juce::dontSendNotification);
}

void PadDetailEditor::rebuildOutputAssignBox()
{
    outputAssignBox.clear(juce::dontSendNotification);

    const auto& kit = proc.getKit();
    const int activeCount = getActiveOutputCount(kit.outputMode);

    for (int i = 0; i < activeCount; ++i)
        outputAssignBox.addItem(kit.getOutputShortLabel(i), i + 1);
}

void PadDetailEditor::wireCallbacks()
{
    sStart.slider.onValueChange = [this]
    {
        if (currentPadIndex < 0) return;
        auto& pad = proc.getKit().pads[static_cast<size_t>(currentPadIndex)];
        const float v = juce::jmin(static_cast<float>(sStart.slider.getValue()), pad.endPosition - 0.001f);
        proc.setAutomatablePadParameter(currentPadIndex,
                                        PadParameterSpecs::Param::Start,
                                        juce::jlimit(0.0f, 1.0f, v));
        proc.applyAutoFadeOnTrim(currentPadIndex);
        sStart.slider.setValue(pad.startPosition, juce::dontSendNotification);
        sFadeIn.slider.setValue(proc.getKit().pads[static_cast<size_t>(currentPadIndex)].fadeIn, juce::dontSendNotification);
        sFadeOut.slider.setValue(proc.getKit().pads[static_cast<size_t>(currentPadIndex)].fadeOut, juce::dontSendNotification);
        waveformDisplay.setStartPosition(pad.startPosition);
        waveformDisplay.setFadeIn(proc.getKit().pads[static_cast<size_t>(currentPadIndex)].fadeIn);
        waveformDisplay.setFadeOut(proc.getKit().pads[static_cast<size_t>(currentPadIndex)].fadeOut);
        updateValueLabel(sStart);
        updateValueLabel(sFadeIn);
        updateValueLabel(sFadeOut);
    };

    sEnd.slider.onValueChange = [this]
    {
        if (currentPadIndex < 0) return;
        auto& pad = proc.getKit().pads[static_cast<size_t>(currentPadIndex)];
        const float v = juce::jmax(static_cast<float>(sEnd.slider.getValue()), pad.startPosition + 0.001f);
        proc.setAutomatablePadParameter(currentPadIndex,
                                        PadParameterSpecs::Param::End,
                                        juce::jlimit(0.0f, 1.0f, v));
        proc.applyAutoFadeOnTrim(currentPadIndex);
        sEnd.slider.setValue(pad.endPosition, juce::dontSendNotification);
        sFadeIn.slider.setValue(proc.getKit().pads[static_cast<size_t>(currentPadIndex)].fadeIn, juce::dontSendNotification);
        sFadeOut.slider.setValue(proc.getKit().pads[static_cast<size_t>(currentPadIndex)].fadeOut, juce::dontSendNotification);
        waveformDisplay.setEndPosition(pad.endPosition);
        waveformDisplay.setFadeIn(proc.getKit().pads[static_cast<size_t>(currentPadIndex)].fadeIn);
        waveformDisplay.setFadeOut(proc.getKit().pads[static_cast<size_t>(currentPadIndex)].fadeOut);
        updateValueLabel(sEnd);
        updateValueLabel(sFadeIn);
        updateValueLabel(sFadeOut);
    };

    sVolume.slider.onValueChange = [this]
    {
        if (currentPadIndex < 0) return;
        proc.setAutomatablePadParameter(currentPadIndex,
                                        PadParameterSpecs::Param::Volume,
                                        static_cast<float>(sVolume.slider.getValue()));
        updateValueLabel(sVolume);
    };

    sPan.slider.onValueChange = [this]
    {
        if (currentPadIndex < 0) return;
        proc.setAutomatablePadParameter(currentPadIndex,
                                        PadParameterSpecs::Param::Pan,
                                        static_cast<float>(sPan.slider.getValue()));
        updateValueLabel(sPan);
    };

    sPitch.slider.onValueChange = [this]
    {
        if (currentPadIndex < 0) return;
        proc.setAutomatablePadParameter(currentPadIndex,
                                        PadParameterSpecs::Param::Pitch,
                                        static_cast<float>(sPitch.slider.getValue()));
        updateValueLabel(sPitch);
    };

    sAttack.slider.onValueChange = [this]
    {
        if (currentPadIndex < 0) return;
        proc.setAutomatablePadParameter(currentPadIndex,
                                        PadParameterSpecs::Param::Attack,
                                        static_cast<float>(sAttack.slider.getValue()));
        updateValueLabel(sAttack);
    };

    sRelease.slider.onValueChange = [this]
    {
        if (currentPadIndex < 0) return;
        proc.setAutomatablePadParameter(currentPadIndex,
                                        PadParameterSpecs::Param::Release,
                                        static_cast<float>(sRelease.slider.getValue()));
        updateValueLabel(sRelease);
    };

    sFadeIn.slider.onValueChange = [this]
    {
        if (currentPadIndex < 0) return;
        const float v = static_cast<float>(sFadeIn.slider.getValue());
        proc.setAutomatablePadParameter(currentPadIndex,
                                        PadParameterSpecs::Param::FadeIn,
                                        v);
        waveformDisplay.setFadeIn(proc.getKit().pads[static_cast<size_t>(currentPadIndex)].fadeIn);
        updateValueLabel(sFadeIn);
    };

    sFadeOut.slider.onValueChange = [this]
    {
        if (currentPadIndex < 0) return;
        const float v = static_cast<float>(sFadeOut.slider.getValue());
        proc.setAutomatablePadParameter(currentPadIndex,
                                        PadParameterSpecs::Param::FadeOut,
                                        v);
        waveformDisplay.setFadeOut(proc.getKit().pads[static_cast<size_t>(currentPadIndex)].fadeOut);
        updateValueLabel(sFadeOut);
    };

    sVelSens.slider.onValueChange = [this]
    {
        if (currentPadIndex < 0) return;
        auto& pad = proc.getKit().pads[static_cast<size_t>(currentPadIndex)];
        const float nextValue = static_cast<float>(sVelSens.slider.getValue());
        if (std::abs(pad.velocitySens - nextValue) > 0.000001f)
        {
            pad.velocitySens = nextValue;
            proc.markKitDirty();
        }
        updateValueLabel(sVelSens);
    };

    sHumanize.slider.onValueChange = [this]
    {
        if (currentPadIndex < 0) return;
        auto& pad = proc.getKit().pads[static_cast<size_t>(currentPadIndex)];
        const float nextValue = static_cast<float>(sHumanize.slider.getValue());
        if (std::abs(pad.humanize - nextValue) > 0.000001f)
        {
            pad.humanize = nextValue;
            proc.markKitDirty();
        }
        updateValueLabel(sHumanize);
    };

    btnOneShot.onClick = [this]
    {
        if (currentPadIndex < 0) return;
        auto& pad = proc.getKit().pads[static_cast<size_t>(currentPadIndex)];
        if (pad.playbackMode != PlaybackMode::OneShot)
        {
            pad.playbackMode = PlaybackMode::OneShot;
            proc.markKitDirty();
        }
        playModeBox.setSelectedId(1, juce::dontSendNotification);
    };

    btnGate.onClick = [this]
    {
        if (currentPadIndex < 0) return;
        auto& pad = proc.getKit().pads[static_cast<size_t>(currentPadIndex)];
        if (pad.playbackMode != PlaybackMode::Gate)
        {
            pad.playbackMode = PlaybackMode::Gate;
            proc.markKitDirty();
        }
        playModeBox.setSelectedId(2, juce::dontSendNotification);
    };

    playModeBox.onChange = [this]
    {
        if (currentPadIndex < 0) return;
        auto& pad = proc.getKit().pads[static_cast<size_t>(currentPadIndex)];
        const auto nextMode = (playModeBox.getSelectedId() == 2) ? PlaybackMode::Gate : PlaybackMode::OneShot;
        if (pad.playbackMode != nextMode)
        {
            pad.playbackMode = nextMode;
            proc.markKitDirty();
        }
        btnOneShot.setToggleState(pad.playbackMode == PlaybackMode::OneShot, juce::dontSendNotification);
        btnGate.setToggleState(pad.playbackMode == PlaybackMode::Gate, juce::dontSendNotification);
    };

    chokeGroupBox.onChange = [this]
    {
        if (currentPadIndex < 0) return;
        auto& pad = proc.getKit().pads[static_cast<size_t>(currentPadIndex)];
        const int nextGroup = juce::jlimit(0, 4, chokeGroupBox.getSelectedId() - 1);
        if (pad.chokeGroup != nextGroup)
        {
            pad.chokeGroup = nextGroup;
            proc.markKitDirty();
        }
    };

    outputAssignBox.onChange = [this]
    {
        if (currentPadIndex < 0) return;
        const int selected = outputAssignBox.getSelectedId();
        if (selected <= 0) return;
        auto& pad = proc.getKit().pads[static_cast<size_t>(currentPadIndex)];
        if (pad.outputAssign != selected - 1)
        {
            pad.outputAssign = selected - 1;
            proc.markKitDirty();
        }
    };

    btnReverse.onClick = [this]
    {
        if (currentPadIndex < 0) return;
        const bool rev = btnReverse.getToggleState();
        proc.setAutomatablePadParameter(currentPadIndex,
                                        PadParameterSpecs::Param::Reverse,
                                        rev ? 1.0f : 0.0f);
        waveformDisplay.setReversed(rev);
    };

    btnMute.onClick = [this]
    {
        if (currentPadIndex < 0) return;
        proc.setAutomatablePadParameter(currentPadIndex,
                                        PadParameterSpecs::Param::Mute,
                                        btnMute.getToggleState() ? 1.0f : 0.0f);
    };

    btnSolo.onClick = [this]
    {
        if (currentPadIndex < 0) return;
        proc.setAutomatablePadParameter(currentPadIndex,
                                        PadParameterSpecs::Param::Solo,
                                        btnSolo.getToggleState() ? 1.0f : 0.0f);
    };

    waveformDisplay.onStartChanged = [this](float v)
    {
        if (currentPadIndex < 0) return;
        proc.setAutomatablePadParameter(currentPadIndex,
                                        PadParameterSpecs::Param::Start,
                                        v);
        proc.applyAutoFadeOnTrim(currentPadIndex);
        sStart.slider.setValue(v, juce::dontSendNotification);
        sFadeIn.slider.setValue(proc.getKit().pads[static_cast<size_t>(currentPadIndex)].fadeIn, juce::dontSendNotification);
        sFadeOut.slider.setValue(proc.getKit().pads[static_cast<size_t>(currentPadIndex)].fadeOut, juce::dontSendNotification);
        updateValueLabel(sStart);
        updateValueLabel(sFadeIn);
        updateValueLabel(sFadeOut);
    };

    waveformDisplay.onEndChanged = [this](float v)
    {
        if (currentPadIndex < 0) return;
        proc.setAutomatablePadParameter(currentPadIndex,
                                        PadParameterSpecs::Param::End,
                                        v);
        proc.applyAutoFadeOnTrim(currentPadIndex);
        sEnd.slider.setValue(v, juce::dontSendNotification);
        sFadeIn.slider.setValue(proc.getKit().pads[static_cast<size_t>(currentPadIndex)].fadeIn, juce::dontSendNotification);
        sFadeOut.slider.setValue(proc.getKit().pads[static_cast<size_t>(currentPadIndex)].fadeOut, juce::dontSendNotification);
        updateValueLabel(sEnd);
        updateValueLabel(sFadeIn);
        updateValueLabel(sFadeOut);
    };

    waveformDisplay.onFadeInChanged = [this](float v)
    {
        if (currentPadIndex < 0) return;
        proc.setAutomatablePadParameter(currentPadIndex,
                                        PadParameterSpecs::Param::FadeIn,
                                        v);
        sFadeIn.slider.setValue(proc.getKit().pads[static_cast<size_t>(currentPadIndex)].fadeIn,
                                juce::dontSendNotification);
        updateValueLabel(sFadeIn);
    };

    waveformDisplay.onFadeOutChanged = [this](float v)
    {
        if (currentPadIndex < 0) return;
        proc.setAutomatablePadParameter(currentPadIndex,
                                        PadParameterSpecs::Param::FadeOut,
                                        v);
        sFadeOut.slider.setValue(proc.getKit().pads[static_cast<size_t>(currentPadIndex)].fadeOut,
                                 juce::dontSendNotification);
        updateValueLabel(sFadeOut);
    };
}

void PadDetailEditor::loadPad(int padIndex)
{
    currentPadIndex = padIndex;
    updateAllFromPad();
    updateWaveform();
}

void PadDetailEditor::refresh()
{
    updateAllFromPad();
}

void PadDetailEditor::updateAllFromPad()
{
    if (currentPadIndex < 0) return;

    const auto& pad = proc.getKit().pads[static_cast<size_t>(currentPadIndex)];
    float playheadPosition = 0.0f;
    const bool hasPlayhead = proc.getPadPlayheadPosition(currentPadIndex, playheadPosition);

    padNameLabel.setText(pad.padName, juce::dontSendNotification);

    const auto noteName = getMidiNoteName(pad.midiNote);
    midiLabel.setText(noteName + "  (" + juce::String(pad.midiNote) + ")", juce::dontSendNotification);

    juce::String sampleHeadline;
    juce::String sampleValue;
    juce::Colour sampleColour = ColorPalette::sampleText();

    if (pad.sampleMissing)
    {
        const auto fileName = pad.sampleFileName.isNotEmpty() ? pad.sampleFileName : "Missing Sample";
        sampleHeadline = "MISSING SAMPLE";
        sampleValue = fileName + "  (relink required)";
        sampleColour = ColorPalette::danger().brighter(0.10f);
    }
    else if (pad.sampleFileName.isNotEmpty())
    {
        sampleHeadline = pad.sampleFileName;
        sampleValue = pad.sampleFileName;
    }
    else
    {
        sampleHeadline = "No sample loaded";
        sampleValue = "No sample loaded";
        sampleColour = ColorPalette::textSub();
    }

    waveformFileLabel.setColour(juce::Label::textColourId, sampleColour.withAlpha(0.92f));
    sampleFileValueLabel.setColour(juce::Label::textColourId, sampleColour);
    waveformFileLabel.setText(compactFileName(sampleHeadline, 56), juce::dontSendNotification);
    sampleFileValueLabel.setText(sampleValue, juce::dontSendNotification);
    relinkSampleButton.setVisible(pad.sampleMissing);

    sStart.slider.setValue(pad.startPosition, juce::dontSendNotification);
    sEnd.slider.setValue(pad.endPosition, juce::dontSendNotification);
    sVolume.slider.setValue(pad.volume, juce::dontSendNotification);
    sPan.slider.setValue(pad.pan, juce::dontSendNotification);
    sPitch.slider.setValue(pad.pitch, juce::dontSendNotification);
    sAttack.slider.setValue(pad.attack, juce::dontSendNotification);
    sRelease.slider.setValue(pad.release, juce::dontSendNotification);
    sFadeIn.slider.setValue(pad.fadeIn, juce::dontSendNotification);
    sFadeOut.slider.setValue(pad.fadeOut, juce::dontSendNotification);
    sVelSens.slider.setValue(pad.velocitySens, juce::dontSendNotification);
    sHumanize.slider.setValue(pad.humanize, juce::dontSendNotification);

    for (auto* ps : { &sStart, &sEnd, &sFadeIn, &sFadeOut, &sPitch, &sAttack,
                      &sRelease, &sVolume, &sPan, &sVelSens, &sHumanize })
        updateValueLabel(*ps);

    btnOneShot.setToggleState(pad.playbackMode == PlaybackMode::OneShot, juce::dontSendNotification);
    btnGate.setToggleState(pad.playbackMode == PlaybackMode::Gate, juce::dontSendNotification);
    btnReverse.setToggleState(pad.reverse, juce::dontSendNotification);
    btnMute.setToggleState(pad.mute, juce::dontSendNotification);
    btnSolo.setToggleState(pad.solo, juce::dontSendNotification);

    playModeBox.setSelectedId(pad.playbackMode == PlaybackMode::Gate ? 2 : 1,
                              juce::dontSendNotification);
    chokeGroupBox.setSelectedId(juce::jlimit(1, 5, pad.chokeGroup + 1),
                                juce::dontSendNotification);

    rebuildOutputAssignBox();
    const int activeCount = getActiveOutputCount(proc.getKit().outputMode);
    outputAssignBox.setSelectedId(juce::jlimit(0, activeCount - 1, pad.outputAssign) + 1,
                                  juce::dontSendNotification);

    waveformDisplay.setStartPosition(pad.startPosition);
    waveformDisplay.setEndPosition(pad.endPosition);
    waveformDisplay.setFadeIn(pad.fadeIn);
    waveformDisplay.setFadeOut(pad.fadeOut);
    waveformDisplay.setReversed(pad.reverse);
    waveformDisplay.setPlayhead(playheadPosition, hasPlayhead);

    smartTrimToggle.setToggleState(proc.getKit().smartTrimOnSampleLoad, juce::dontSendNotification);
    autoFadeToggle.setToggleState(proc.getKit().autoFadeOnTrim, juce::dontSendNotification);
}

void PadDetailEditor::updateWaveform()
{
    if (currentPadIndex < 0) return;

    juce::ScopedReadLock rl(proc.getFileManager().getReadWriteLock());
    const auto* buf = proc.getFileManager().getBufferNoLock(currentPadIndex);
    waveformDisplay.setSample(buf);
}

void PadDetailEditor::paint(juce::Graphics& g)
{
    g.fillAll(ColorPalette::bg());

    const bool compactOuter = getWidth() < 860 || getHeight() < 640;
    auto area = getLocalBounds().reduced(compactOuter ? 8 : 12);
    const int settingsW = getSettingsPanelWidth(area.getWidth());
    auto settings = area.removeFromRight(settingsW);
    area.removeFromRight(compactOuter ? 8 : 12);
    auto editor = area;

    auto editorRf = editor.toFloat();
    auto settingsRf = settings.toFloat();

    DrumSamplerLookAndFeel::drawPanelBackground(g, editorRf, true);
    DrumSamplerLookAndFeel::drawPanelBackground(g, settingsRf, true);

    g.setColour(ColorPalette::iceGlow().withAlpha(0.030f));
    g.drawRoundedRectangle(editorRf.reduced(1.0f), Spacing::radiusPanel - 1.0f, 1.0f);

    g.setColour(ColorPalette::divider().withAlpha(0.82f));
    g.drawVerticalLine(settings.getX() - 7, 14.0f, static_cast<float>(getHeight() - 14));

    g.setColour(ColorPalette::champagne());
    g.setFont(Typography::sectionTitle(10.5f));
    g.drawText("SAMPLE EDIT", editor.getX() + 20, editor.getY() + 16, 130, 18,
               juce::Justification::left, false);

    if (settingsTitleLabel.isVisible())
    {
        const auto title = settingsTitleLabel.getBounds().toFloat();
        const float lineY = title.getBottom() + 8.0f;

        g.setColour(ColorPalette::champagne().withAlpha(0.78f));
        g.fillRoundedRectangle(title.getX(), lineY, 30.0f, 1.0f, 0.5f);
        g.setColour(ColorPalette::borderSoft().withAlpha(0.78f));
        g.drawLine(title.getX() + 38.0f, lineY + 0.5f, title.getRight(), lineY + 0.5f, 1.0f);
    }

    auto drawFieldBackplate = [&g](const juce::Component& field)
    {
        if (! field.isVisible())
            return;

        const auto r = field.getBounds().expanded(2, 3).toFloat();
        g.setColour(ColorPalette::shadowInner().withAlpha(0.12f));
        g.fillRoundedRectangle(r, Spacing::radiusControl + 1.0f);
    };

    drawFieldBackplate(midiLabel);
    drawFieldBackplate(padNameLabel);
    drawFieldBackplate(sampleFileValueLabel);
    if (relinkSampleButton.isVisible())
        drawFieldBackplate(relinkSampleButton);
    drawFieldBackplate(outputAssignBox);
    drawFieldBackplate(playModeBox);
    drawFieldBackplate(chokeGroupBox);

    if (sStart.label.isVisible())
    {
        const int dividerY = sStart.label.getY() - 10;
        g.setColour(ColorPalette::divider());
        g.drawHorizontalLine(dividerY, static_cast<float>(editor.getX() + 18),
                             static_cast<float>(editor.getRight() - 18));
    }

    // Give the waveform a little hero presence from the parent surface.
    if (waveformDisplay.isVisible())
    {
        auto wave = waveformDisplay.getBounds().toFloat();
        for (int i = 3; i >= 1; --i)
        {
            const float expand = static_cast<float>(i) * 3.0f;
            g.setColour(ColorPalette::iceGlow().withAlpha(0.028f / static_cast<float>(i)));
            g.drawRoundedRectangle(wave.expanded(expand), Spacing::radiusCard + expand, 1.0f);
        }
    }

    // Group the knob rows and bottom control strip without making them heavy.
    if (sStart.label.isVisible())
    {
        auto row1 = sStart.label.getBounds()
                    .getUnion(sRelease.value.getBounds())
                    .expanded(8, 6);
        drawQuietSection(g, row1);
    }

    if (sVolume.label.isVisible())
    {
        auto row2 = sVolume.label.getBounds()
                    .getUnion(btnSolo.getBounds())
                    .expanded(8, 8);
        drawQuietSection(g, row2);

        const int sep1 = btnOneShot.getX() - 10;
        const int sep2 = btnMute.getX() - 10;
        g.setColour(ColorPalette::borderSoft().withAlpha(0.86f));
        g.drawVerticalLine(sep1, static_cast<float>(row2.getY() + 8), static_cast<float>(row2.getBottom() - 8));
        g.drawVerticalLine(sep2, static_cast<float>(row2.getY() + 8), static_cast<float>(row2.getBottom() - 8));

        g.setColour(ColorPalette::champagne().withAlpha(0.84f));
        g.setFont(Typography::sectionTitle(8.7f));
        g.drawText("PLAY MODE", btnOneShot.getX(), row2.getY() + 6, btnOneShot.getWidth(), 12,
                   juce::Justification::centred, false);
        g.drawText("PAD STATE", btnMute.getX(), row2.getY() + 6, btnMute.getWidth(), 12,
                   juce::Justification::centred, false);
    }
}

void PadDetailEditor::resized()
{
    const bool compactOuter = getWidth() < 860 || getHeight() < 640;

    auto area = getLocalBounds().reduced(compactOuter ? 8 : 12);
    const int settingsW = getSettingsPanelWidth(area.getWidth());
    const int editorAvailableW = area.getWidth() - settingsW - (compactOuter ? 8 : 12);
    const bool compactWidth  = editorAvailableW < 650;
    const bool compactHeight = getHeight() < 660;

    auto settings = area.removeFromRight(settingsW).reduced(compactWidth ? 12 : 22,
                                                            compactHeight ? 12 : 20);
    area.removeFromRight(compactWidth ? 8 : 12);
    auto editor = area.reduced(compactWidth ? 10 : 18,
                               compactHeight ? 12 : 16);

    auto top = editor.removeFromTop(compactWidth ? 80 : 46);

    if (compactWidth)
    {
        auto topRow = top.removeFromTop(34);
        waveformFileLabel.setBounds(topRow.reduced(2, 4));

        top.removeFromTop(4);
        auto actionRow = top.removeFromTop(34);

        const int smartToggleW = 42;
        const int autoFadeToggleW = 42;
        const int reverseW = 88;
        const int reanalyzeW = 102;

        btnReanalyze.setBounds(actionRow.removeFromRight(reanalyzeW).reduced(0, 4));
        actionRow.removeFromRight(6);
        btnReverse.setBounds(actionRow.removeFromRight(reverseW).reduced(0, 4));
        actionRow.removeFromRight(8);
        autoFadeToggle.setBounds(actionRow.removeFromRight(autoFadeToggleW).reduced(0, 4));
        autoFadeLabel.setBounds(actionRow.removeFromRight(88).reduced(0, 4));
        actionRow.removeFromRight(8);
        smartTrimToggle.setBounds(actionRow.removeFromRight(smartToggleW).reduced(0, 4));
        smartTrimLabel.setBounds(actionRow.reduced(0, 4));
    }
    else
    {
        waveformFileLabel.setBounds(top.removeFromLeft(juce::jmax(260, top.getWidth() - 390)).reduced(2, 5));
        btnReanalyze.setBounds(top.removeFromRight(108).reduced(0, 7));
        top.removeFromRight(8);
        btnReverse.setBounds(top.removeFromRight(96).reduced(0, 7));
        top.removeFromRight(10);
        autoFadeToggle.setBounds(top.removeFromRight(42).reduced(0, 7));
        autoFadeLabel.setBounds(top.removeFromRight(86).reduced(0, 7));
        top.removeFromRight(10);
        smartTrimToggle.setBounds(top.removeFromRight(42).reduced(0, 7));
        smartTrimLabel.setBounds(top.reduced(0, 7));
    }

    editor.removeFromTop(8);
    const int reservedControlsH = compactHeight ? 174 : 222;
    const int waveformH = juce::jlimit(compactHeight ? 210 : 270,
                                       compactHeight ? 360 : 410,
                                       editor.getHeight() - reservedControlsH);
    waveformDisplay.setBounds(editor.removeFromTop(waveformH));

    editor.removeFromTop(compactHeight ? 12 : 20);
    auto knobArea = editor;

    const int labelH = compactHeight ? 15 : 17;
    const int sliderH = compactHeight ? 48 : 52;
    const int valueH = compactHeight ? 14 : 16;
    auto layoutKnob = [labelH, sliderH, valueH](ParamSlider& ps, juce::Rectangle<int> r)
    {
        ps.label.setBounds(r.removeFromTop(labelH));
        ps.slider.setBounds(r.removeFromTop(sliderH));
        ps.value.setBounds(r.removeFromTop(valueH));
    };

    const int rowH = compactHeight ? 82 : 90;
    const int gap = compactWidth ? 4 : 7;
    auto row1 = knobArea.removeFromTop(rowH);
    const int knobW1 = row1.getWidth() / 7;
    for (auto* ps : { &sStart, &sEnd, &sFadeIn, &sFadeOut, &sPitch, &sAttack, &sRelease })
    {
        auto cell = row1.removeFromLeft(knobW1).reduced(gap, 0);
        layoutKnob(*ps, cell);
    }

    knobArea.removeFromTop(compactHeight ? 8 : 11);
    auto row2 = knobArea.removeFromTop(compactHeight ? 88 : 96);
    const int playBoxW = compactWidth ? juce::jlimit(86, 108, row2.getWidth() / 4) : 122;
    const int msBoxW = compactWidth ? juce::jlimit(68, 84, row2.getWidth() / 5) : 94;
    const int interGroupGap1 = compactWidth ? 6 : 8;
    const int interGroupGap2 = compactWidth ? 8 : 10;
    const int leftKnobW = juce::jmax(44, (row2.getWidth() - playBoxW - msBoxW - interGroupGap1 - interGroupGap2) / 4);
    for (auto* ps : { &sVolume, &sPan, &sVelSens, &sHumanize })
    {
        auto cell = row2.removeFromLeft(leftKnobW).reduced(gap, 0);
        layoutKnob(*ps, cell);
    }

    row2.removeFromLeft(interGroupGap1);
    auto playBox = row2.removeFromLeft(playBoxW);
    playBox.removeFromTop(compactHeight ? 16 : 18);
    btnOneShot.setBounds(playBox.removeFromTop(32).reduced(0, 2));
    btnGate.setBounds(playBox.removeFromTop(32).reduced(0, 2));

    row2.removeFromLeft(interGroupGap2);
    auto msBox = row2.removeFromLeft(msBoxW);
    msBox.removeFromTop(compactHeight ? 16 : 18);
    btnMute.setBounds(msBox.removeFromTop(32).reduced(0, 2));
    btnSolo.setBounds(msBox.removeFromTop(32).reduced(0, 2));

    settingsTitleLabel.setBounds(settings.removeFromTop(compactHeight ? 28 : 31));
    settings.removeFromTop(compactHeight ? 14 : 22);

    auto layoutField = [&](juce::Label& title, juce::Component& field, int fieldH = 34)
    {
        title.setBounds(settings.removeFromTop(17));
        settings.removeFromTop(2);
        field.setBounds(settings.removeFromTop(fieldH));
        settings.removeFromTop(compactHeight ? 10 : 15);
    };

    const int fieldH = compactHeight ? 30 : 34;
    layoutField(midiTitleLabel, midiLabel, fieldH);
    layoutField(padNameTitleLabel, padNameLabel, fieldH);
    sampleFileTitleLabel.setBounds(settings.removeFromTop(17));
    settings.removeFromTop(2);
    {
        auto sampleRow = settings.removeFromTop(fieldH);
        if (relinkSampleButton.isVisible())
        {
            const int buttonW = juce::jlimit(72, 84, sampleRow.getWidth() / 3);
            relinkSampleButton.setBounds(sampleRow.removeFromRight(buttonW));
            sampleRow.removeFromRight(6);
            sampleFileValueLabel.setBounds(sampleRow);
        }
        else
        {
            relinkSampleButton.setBounds(0, 0, 0, 0);
            sampleFileValueLabel.setBounds(sampleRow);
        }
    }
    settings.removeFromTop(compactHeight ? 10 : 15);
    layoutField(outputTitleLabel, outputAssignBox, fieldH);
    layoutField(playModeTitleLabel, playModeBox, fieldH);
    layoutField(chokeTitleLabel, chokeGroupBox, fieldH);
}
