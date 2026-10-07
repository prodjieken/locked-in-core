#pragma once

/**
    Plugin assets (anything passed to lockedin_add_plugin's ASSETS / PRESETS),
    looked up by FILE NAME only: "art/eli_idle.png" and "eli_idle.png" are the
    same asset. lockedin_add_plugin generates getNamed() and list() per plugin.
*/
namespace lockedin::assets
{
    /** Raw bytes, or nullptr if there's no asset with that file name. */
    const char* getNamed (const juce::String& fileName, int& sizeInBytes);

    /** File names of every embedded asset. */
    juce::StringArray list();

    /** Strips any folder part, so JSON can say "art/idle.png". */
    inline juce::String fileNameOf (const juce::String& path)
    {
        return path.fromLastOccurrenceOf ("/", false, false).fromLastOccurrenceOf ("\\", false, false);
    }

    inline bool exists (const juce::String& path)
    {
        int size = 0;
        return getNamed (fileNameOf (path), size) != nullptr;
    }

    inline juce::String text (const juce::String& path)
    {
        int size = 0;
        if (auto* data = getNamed (fileNameOf (path), size))
            return juce::String::fromUTF8 (data, size);
        return {};
    }

    inline juce::MemoryBlock data (const juce::String& path)
    {
        int size = 0;
        if (auto* d = getNamed (fileNameOf (path), size))
            return { d, (size_t) size };
        return {};
    }

    /** Decoded image (cached), or an invalid Image if missing. */
    inline juce::Image image (const juce::String& path)
    {
        int size = 0;
        if (auto* d = getNamed (fileNameOf (path), size))
            return juce::ImageCache::getFromMemory (d, size);
        return {};
    }

    inline juce::Typeface::Ptr typeface (const juce::String& path)
    {
        int size = 0;
        if (auto* d = getNamed (fileNameOf (path), size))
            return juce::Typeface::createSystemTypefaceFor (d, (size_t) size);
        return {};
    }
}
