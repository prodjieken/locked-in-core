#pragma once

namespace lockedin
{

/**
    One animation state, as described in the plugin's character JSON.

    {
      "default": "idle",
      "states": {
        "idle":    { "sheet": "eli_idle.png", "frames": 8, "fps": 8, "loop": true },
        "strain":  { "sheet": "eli_strain.png", "frames": 6, "fps": 12, "loop": true,
                     "fx": { "shake": 4, "tint": "#ff4040", "tintAmount": 0.5 } },
        "flinch":  { "sheet": "eli_flinch.png", "frames": 5, "fps": 15, "loop": false, "then": "idle" }
      }
    }

    sheet      file name of the sprite sheet (looked up in the plugin's ASSETS by file name;
               any folder part is ignored). Missing art draws a labelled placeholder box.
    frames     number of frames in the sheet
    columns    frames per row (default: all frames in one row)
    fps        playback speed
    loop       true = loops while the state is active; false = one-shot
    then       one-shots only: state to go to when finished (default: back to rule control)
    fx         effects scaled by the state's intensity (0..1):
                 shake (px), rise (px, moves up), squash (0..1), tint ("#rrggbb"), tintAmount (0..1)
*/
struct StateDef
{
    juce::String name;
    juce::String sheet;
    int frames = 1;
    int columns = 0;
    float fps = 8.0f;
    bool loop = true;
    juce::String then;

    float shakePx = 0.0f;
    float risePx = 0.0f;
    float squash = 0.0f;
    juce::uint32 tintArgb = 0;   // 0 = none
    float tintAmount = 0.0f;

    double durationMs() const noexcept   { return fps > 0.0f ? 1000.0 * frames / fps : 0.0; }
};

struct CharacterDef
{
    juce::String defaultState;
    std::vector<StateDef> states;

    int indexOf (const juce::String& name) const
    {
        for (size_t i = 0; i < states.size(); ++i)
            if (states[i].name == name)
                return (int) i;
        return -1;
    }

    const StateDef* find (const juce::String& name) const
    {
        const auto i = indexOf (name);
        return i >= 0 ? &states[(size_t) i] : nullptr;
    }

    /** Parses the JSON. On failure, returns an error naming the bad state/field
        and leaves `out` with whatever parsed so far. */
    static juce::Result parse (const juce::String& jsonText, CharacterDef& out);

    /** Fallback used when the JSON is missing or broken, so the plugin still opens. */
    static CharacterDef placeholder (const juce::StringArray& stateNames);
};

} // namespace lockedin
