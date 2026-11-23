#pragma once
#include <JuceHeader.h>

// Kit-loading tests must not leave temporary paths in the user's recent list.
struct RecentKitGuard
{
    juce::File file { juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("ASTER Drum Lab/recent-kits.txt") };
    bool existed { file.existsAsFile() };
    juce::MemoryBlock data;
    RecentKitGuard() { if (existed) file.loadFileAsData(data); }
    ~RecentKitGuard()
    {
        if (existed) file.replaceWithData(data.getData(), data.getSize());
        else file.deleteFile();
    }
};
