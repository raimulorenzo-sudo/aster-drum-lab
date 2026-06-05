#pragma once
#include <JuceHeader.h>

// Typography centralises font size, weight, and letter spacing.
// JUCE 7 exposes plain/bold rather than true Medium weights, so "Medium" here
// uses the system sans-serif with slightly increased tracking.
namespace Typography
{
    inline juce::Font tracked(float size, int style, float tracking)
    {
        return juce::Font(juce::FontOptions(size, style).withKerningFactor(tracking));
    }

    // Base roles
    inline juce::Font body(float size = 12.0f)        { return tracked(size, juce::Font::plain, 0.012f); }
    inline juce::Font bodyMedium(float size = 12.0f)  { return tracked(size, juce::Font::plain, 0.018f); }
    inline juce::Font semiBold(float size = 12.0f)    { return tracked(size, juce::Font::bold,  0.014f); }
    inline juce::Font caption(float size = 10.0f)     { return tracked(size, juce::Font::plain, 0.040f); }
    inline juce::Font micro(float size = 9.0f)        { return tracked(size, juce::Font::plain, 0.050f); }

    // UI roles
    inline juce::Font productTitle(float size = 21.0f){ return tracked(size, juce::Font::plain, 0.150f); }
    inline juce::Font sectionTitle(float size = 10.5f){ return tracked(size, juce::Font::bold,  0.075f); }
    inline juce::Font padName(float size = 14.0f)     { return tracked(size, juce::Font::bold,  0.012f); }
    inline juce::Font midiNote(float size = 10.0f)    { return tracked(size, juce::Font::plain, 0.025f); }
    inline juce::Font sampleName(float size = 9.0f)   { return tracked(size, juce::Font::plain, 0.020f); }
    inline juce::Font buttonText(float size = 10.5f)  { return tracked(size, juce::Font::plain, 0.070f); }
    inline juce::Font knobLabel(float size = 9.2f)    { return tracked(size, juce::Font::bold,  0.060f); }
    inline juce::Font knobValue(float size = 10.0f)   { return tracked(size, juce::Font::plain, 0.018f); }
    inline juce::Font comboText(float size = 11.0f)   { return tracked(size, juce::Font::plain, 0.018f); }

    // Backward-compatible aliases already used by existing components.
    inline juce::Font ui(float size)                  { return body(size); }
    inline juce::Font uiMedium(float size)            { return bodyMedium(size); }
    inline juce::Font uiSemiBold(float size)          { return semiBold(size); }
    inline juce::Font label(float size = 10.0f)       { return caption(size); }
    inline juce::Font title(float size = 11.0f)       { return sectionTitle(size); }
    inline juce::Font display(float size)             { return productTitle(size); }
}
