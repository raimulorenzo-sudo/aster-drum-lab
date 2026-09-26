#include "WebViewEditor.h"
#include <algorithm>
#include <cmath>

void WebViewEditor::cancelBrowserWork()
{
    ++*browserDirectoryGeneration;
    ++*browserPreviewGeneration;
    ++*browserImportGeneration;
    browserImportBusy = false;
    audioProcessor.browserPreview.stop();
}

void WebViewEditor::emitBrowserResult(const juce::String& kind, int requestId, const juce::String& error)
{
    auto* value = new juce::DynamicObject();
    value->setProperty("kind", kind);
    value->setProperty("requestId", requestId);
    value->setProperty("error", error);
    webView.emitEventIfBrowserIsVisible("browserResult", juce::var(value));
}

void WebViewEditor::openBrowserDirectory(const juce::File& directory, int requestId)
{
    ++*browserPreviewGeneration;
    audioProcessor.browserPreview.stop();
    auto generation = browserDirectoryGeneration;
    const int ticket = ++*generation;
    const juce::Component::SafePointer<WebViewEditor> safe(this);
    browserWorkers.addJob([safe, directory, requestId, generation, ticket]
    {
        auto* result = new juce::DynamicObject();
        juce::var listing(result);
        result->setProperty("requestId", requestId);
        result->setProperty("path", directory.getFullPathName());
        result->setProperty("name", directory.getFileName());
        result->setProperty("parent", directory.getParentDirectory() == directory ? juce::String() : directory.getParentDirectory().getFullPathName());
        if (! directory.isDirectory() || ! directory.hasReadAccess())
            result->setProperty("error", "Folder unavailable. Select the folder again.");
        else
        {
            struct Entry { juce::File file; bool directory; };
            std::vector<Entry> entries;
            // Non-recursive. Never crawl a library or decode files to build the list.
            for (const auto& entry : juce::RangedDirectoryIterator(directory, false, "*", juce::File::findFilesAndDirectories))
            {
                if (generation->load() != ticket) return;
                const auto file = entry.getFile();
                if (file.isHidden()) continue;
                const bool isDirectory = entry.isDirectory();
                if (! isDirectory && ! isBrowserAudioFile(file)) continue;
                if (entries.size() >= 10000) { result->setProperty("truncated", true); break; }
                entries.push_back({file, isDirectory});
            }
            std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b)
            {
                if (a.directory != b.directory) return a.directory;
                return a.file.getFileName().compareNatural(b.file.getFileName()) < 0;
            });
            juce::Array<juce::var> files;
            for (const auto& entry : entries)
            {
                auto* row = new juce::DynamicObject();
                row->setProperty("name", entry.file.getFileName());
                row->setProperty("path", entry.file.getFullPathName());
                row->setProperty("directory", entry.directory);
                files.add(juce::var(row));
            }
            result->setProperty("entries", files);
        }
        juce::MessageManager::callAsync([safe, listing, generation, ticket]
        {
            if (! safe || generation->load() != ticket) return;
            if (listing.getProperty("error", {}).toString().isEmpty())
            {
                safe->browserDirectory = juce::File(listing["path"].toString());
                safe->browserListing = listing;
                const auto path = safe->browserDirectory.getFullPathName();
                safe->browserRecentPaths.removeString(path);
                safe->browserRecentPaths.insert(0, path);
                while (safe->browserRecentPaths.size() > 8) safe->browserRecentPaths.remove(8);
                safe->saveBrowserPreferences();
            }
            safe->webView.emitEventIfBrowserIsVisible("browserDirectory", listing);
            safe->webView.emitEventIfBrowserIsVisible("browserPreferences", safe->browserPreferences);
        });
    });
}

void WebViewEditor::handleBrowserMessage(const juce::String& type, const juce::var& payload)
{
    const int request = (int) payload.getProperty("requestId", 0);
    if (type == "browserInit")
    {
        loadBrowserPreferences();
        webView.emitEventIfBrowserIsVisible("browserPreferences", browserPreferences);
        return;
    }
    if (type == "browserStop")
    {
        ++*browserPreviewGeneration;
        audioProcessor.browserPreview.stop();
        emitBrowserResult("stopped", request);
        return;
    }
    if (type == "browserSettings")
    {
        if (! browserPreferences.isObject()) loadBrowserPreferences();
        double gain = (double) payload.getProperty("gain", 0.5);
        if (! std::isfinite(gain)) gain = 0.5;
        gain = juce::jlimit(0.0, 1.0, gain);
        browserPreferences.getDynamicObject()->setProperty("gain", gain);
        browserPreferences.getDynamicObject()->setProperty("autoAudition", (bool) payload.getProperty("autoAudition", true));
        audioProcessor.browserPreview.setGain((float) gain);
        saveBrowserPreferences();
        return;
    }
    if (type == "browserChooseFolder")
    {
        auto chooser = std::make_shared<juce::FileChooser>("Select sample folder", browserDirectory, "");
        const juce::Component::SafePointer<WebViewEditor> safe(this);
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [safe, chooser, request](const juce::FileChooser& result)
            {
                if (! safe || safe->activeWebTab != ActiveWebTab::Browser) return;
                if (result.getResult().isDirectory()) safe->openBrowserDirectory(result.getResult(), request);
                else safe->emitBrowserResult("folderCancelled", request);
            });
        return;
    }
    if (type == "browserOpenDirectory")
    {
        const auto path = payload.getProperty("path", {}).toString();
        if (juce::File::isAbsolutePath(path)) openBrowserDirectory(juce::File(path), request);
        else emitBrowserResult("error", request, "Invalid folder path.");
        return;
    }
    if (type != "browserPreview" && type != "browserImport") return;
    if (activeWebTab != ActiveWebTab::Browser) return;
    const auto path = payload.getProperty("path", {}).toString();
    bool listed = false;
    if (const auto* entries = browserListing.getProperty("entries", {}).getArray())
        for (const auto& entry : *entries)
            if (entry["path"].toString() == path && ! (bool) entry["directory"]) { listed = true; break; }
    if (! listed) { emitBrowserResult(type == "browserImport" ? "import" : "preview", request, "Choose a file in the current folder."); return; }
    const juce::File file(path);
    const bool importing = type == "browserImport";
    if (importing && browserImportBusy) { emitBrowserResult("import", request, "A sample is already loading."); return; }
    if (! importing)
    {
        audioProcessor.browserPreview.stop();
        browserPreviewRequest = request;
    }
    const int padIndex = (int) payload.getProperty("index", -1);
    const int layerIndex = (int) payload.getProperty("layerIndex", -1);
    juce::String snapshot;
    if (importing)
    {
        if (padIndex < 0 || padIndex >= NUM_PADS || layerIndex < 0
            || layerIndex >= audioProcessor.getKit().pads[(size_t) padIndex].layerCount())
        { emitBrowserResult("import", request, "Select an existing pad and layer."); return; }
        const juce::ScopedLock guard(audioProcessor.getCallbackLock());
        snapshot = audioProcessor.browserTargetToken(padIndex, layerIndex);
        browserImportBusy = true;
    }
    auto generation = importing ? browserImportGeneration : browserPreviewGeneration;
    const int ticket = ++*generation;
    const juce::Component::SafePointer<WebViewEditor> safe(this);
    browserWorkers.addJob([safe, generation, ticket, file, request, importing, payload, snapshot, padIndex, layerIndex]
    {
        auto decoded = std::make_shared<BrowserSample>(decodeBrowserSample(file, [generation, ticket] { return generation->load() != ticket; }));
        if (generation->load() != ticket) return;
        juce::MessageManager::callAsync([safe, generation, ticket, decoded, file, request, importing, payload, snapshot, padIndex, layerIndex]
        {
            if (! safe || generation->load() != ticket) return;
            if (importing) safe->browserImportBusy = false;
            auto error = decoded->error;
            if (! decoded->audio && error.isEmpty()) error = "Audio file could not be loaded.";
            if (error.isEmpty() && importing)
            {
                const juce::ScopedLock guard(safe->audioProcessor.getCallbackLock());
                const auto current = safe->audioProcessor.browserTargetToken(padIndex, layerIndex);
                if (current != snapshot || safe->selectedPadIndex != padIndex
                    || safe->selectedLayerIndices[(size_t) padIndex] != layerIndex)
                    error = "The kit or destination changed while loading. Please try again.";
                else if (! safe->audioProcessor.commitBrowserSample(padIndex, layerIndex, file,
                    (bool) payload.getProperty("replace", false), (int) payload.getProperty("expectedCount", -1),
                    (int) payload.getProperty("expectedActive", -1), std::move(*decoded)))
                    error = "The variations changed. Please select the destination again.";
                else
                {
                    ++*safe->browserPreviewGeneration;
                    safe->audioProcessor.browserPreview.stop();
                    safe->broadcastPadUpdate(padIndex);
                    safe->broadcastKitState();
                }
            }
            else if (error.isEmpty())
            {
                if (safe->audioProcessor.isNonRealtime()) error = "Audition is unavailable during offline export.";
               #if ASTER_DEMO_BUILD
                if (AsterDemoMode::hasExpired()) error = "Demo session expired.";
                else AsterDemoMode::beginOnFirstSound();
               #endif
                if (error.isEmpty())
                {
                    safe->audioProcessor.browserPreview.start(std::move(*decoded));
                    safe->browserWasPlaying = true;
                }
            }
            safe->emitBrowserResult(importing ? "import" : "preview", request, error);
        });
    });
}
