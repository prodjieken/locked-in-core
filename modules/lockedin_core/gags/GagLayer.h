#pragma once

namespace lockedin
{

/**
    Overlay for one-off visual jokes. UI only: nothing here touches audio.
    The Editor owns one, keeps it on top and advances it at 60 Hz.

        gags().say ("bro that's -0.1 dBFS", { 240.0f, 120.0f });   // speech bubble pointing at a spot
        gags().slideIn ("NOTE: turn it down");                     // sticky note slides in from the right
        gags().shake (8.0f, 400.0f);                               // whole editor shakes
        gags().flash ("DING");                                     // big flash word

    Coordinates are editor design coordinates (unaffected by the size menu).
*/
class GagLayer : public juce::Component
{
public:
    enum class Edge { left, right, top, bottom };

    GagLayer();

    void say (const juce::String& text, juce::Point<float> pointsAt, float durationMs = 1800.0f);
    void slideIn (const juce::String& text, Edge from = Edge::right, float durationMs = 2200.0f,
                  juce::Image image = {});
    void shake (float amountPx = 6.0f, float durationMs = 350.0f);
    void flash (const juce::String& text = "DING", juce::Colour colour = juce::Colour (0xffffd84a),
                float durationMs = 550.0f);
    void clear();

    /** Called by the Editor each frame. */
    void advance (double dtMs);

    /** Current screen-shake offset; the Editor applies it to the whole stage. */
    juce::Point<float> getShakeOffset() const noexcept   { return shakeOffset; }

    bool isBusy() const noexcept   { return ! items.empty() || shakeLeft > 0.0; }

    /** Font used for bubbles/notes/flash (set from the plugin's theme). */
    void setFont (juce::Font f)    { font = std::move (f); }

    void paint (juce::Graphics&) override;

private:
    enum class Kind { bubble, note, flash };

    struct Item
    {
        Kind kind;
        juce::String text;
        juce::Point<float> target;
        Edge edge = Edge::right;
        juce::Colour colour;
        juce::Image image;
        double age = 0.0, life = 1000.0;
    };

    void paintBubble (juce::Graphics&, const Item&);
    void paintNote (juce::Graphics&, const Item&);
    void paintFlash (juce::Graphics&, const Item&);

    std::vector<Item> items;
    double shakeLeft = 0.0, shakeTotal = 0.0;
    float shakeAmount = 0.0f;
    juce::Point<float> shakeOffset;
    juce::Random random;
    juce::Font font { juce::FontOptions (18.0f, juce::Font::bold) };
};

} // namespace lockedin
