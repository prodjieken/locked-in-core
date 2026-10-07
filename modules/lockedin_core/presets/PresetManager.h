#pragma once

namespace lockedin
{

/**
    Factory presets (embedded via lockedin_add_plugin's PRESETS) plus user
    presets saved to:  Documents/Locked In/<Plugin Name>/<preset>.lipreset

    A .lipreset is small XML:
        <LockedInPreset plugin="Trunk" version="1">
          <Param id="drive" value="6.0"/>
          ...
        </LockedInPreset>

    Values are stored in real units (dB, Hz...) so presets survive range tweaks.
    Parameters not in the file go to their defaults. The gag-sound toggle is
    never touched by loading a preset.

    Message thread only.
*/
class PresetManager
{
public:
    static constexpr const char* fileExtension = ".lipreset";

    struct Preset
    {
        juce::String name;
        bool isFactory = false;
        juce::String assetName;   // factory
        juce::File file;          // user
    };

    PresetManager (juce::AudioProcessorValueTreeState& apvts, const juce::String& pluginName);

    /** Documents/Locked In/<Plugin Name>. Created on first save. */
    juce::File getUserFolder() const;

    /** Rescans factory + user presets. Factory first, then user, each sorted by name. */
    void refresh();

    const std::vector<Preset>& getPresets() const noexcept   { return presets; }
    int indexOf (const juce::String& name) const;

    bool load (int index);
    bool load (const juce::String& name)                       { return load (indexOf (name)); }
    bool loadNext()                                            { return step (1); }
    bool loadPrevious()                                        { return step (-1); }

    /** Saves (or overwrites) a user preset. Returns false on a bad name or write failure. */
    bool saveUser (const juce::String& name);

    /** Moves a user preset to the Trash / Recycle Bin (recoverable). */
    bool deleteUser (int index);

    juce::String getCurrentName() const                         { return currentName; }
    void setCurrentName (const juce::String& n)                 { currentName = n; }   // restored from session state

    /** Applies preset XML to the parameters (used by load(); public for tests/migration). */
    void applyXml (const juce::XmlElement& xml);
    std::unique_ptr<juce::XmlElement> createXml() const;

    static bool isValidName (const juce::String& name);

    std::function<void()> onChange;   // list or current preset changed

private:
    bool step (int delta);
    bool isExcluded (const juce::String& paramId) const;

    juce::AudioProcessorValueTreeState& state;
    juce::String pluginName;
    juce::StringArray excluded { gagSoundsParamId };
    std::vector<Preset> presets;
    juce::String currentName;
};

} // namespace lockedin
