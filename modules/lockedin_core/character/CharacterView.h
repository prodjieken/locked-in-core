#pragma once

namespace lockedin
{

/**
    Draws a CharacterBrain: the current state's sprite-sheet frame, with the
    state's JSON "fx" (shake / rise / squash / tint) scaled by its intensity.

    Missing sprite sheet -> a labelled coloured box showing the state name,
    frame number and intensity, so a plugin is fully testable before art exists.

    The Editor's clock ticks the brain; call view.repaint() after each tick
    (Editor::tick does both if you register the view with attachCharacter()).
*/
class CharacterView : public juce::Component
{
public:
    explicit CharacterView (CharacterBrain& brain);

    /** Reloads sprite sheets from the plugin's assets. */
    void reloadArt();

    /** Draw the placeholder box even when art exists (handy while tuning rules). */
    void setForcePlaceholder (bool b)   { forcePlaceholder = b; repaint(); }

    /** Override to draw extra stuff on top of the character (props, eyes following a value...). */
    std::function<void (juce::Graphics&, juce::Rectangle<float> characterBounds)> drawOverlay;

    /** Optional: choose the sprite frame yourself (e.g. a walk cycle driven by
        where the next paw plant is, instead of by time). Receives the brain's
        time-based frame; the result is clamped to the state's frame count. */
    std::function<int (const StateDef& state, int brainFrame)> frameOverride;

    void paint (juce::Graphics&) override;

    static juce::Colour placeholderColour (const juce::String& stateName);

private:
    void drawPlaceholder (juce::Graphics&, juce::Rectangle<float>, const StateDef&, int frame, float intensity, bool missingArt);
    void drawFrame (juce::Graphics&, juce::Rectangle<float>, const juce::Image&, const StateDef&, int frame);

    CharacterBrain& brain;
    std::vector<juce::Image> sheets;    // per state, may be invalid
    juce::Random uiRandom;              // UI-only randomness (shake jitter)
    bool forcePlaceholder = false;
};

} // namespace lockedin
