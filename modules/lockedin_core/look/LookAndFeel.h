#pragma once

namespace lockedin
{

/** Colours + font for one plugin. */
struct Theme
{
    juce::Colour background { 0xff15151a };
    juce::Colour panel      { 0xff23232b };
    juce::Colour accent     { 0xffffc23d };
    juce::Colour text       { 0xfff4f1ea };
    juce::Colour textDim    { 0xff9a968e };
    juce::Typeface::Ptr typeface;          // e.g. lockedin::assets::typeface ("LuckiestGuy-Regular.ttf")
};

/**
    House LookAndFeel. Knob art, in order of preference:

      1. A per-slider filmstrip:   laf.addKnobFilmstrip ("bigKnob", assets::image ("knob_big.png"), 128);
                                   LookAndFeel::useKnobArt (driveSlider, "bigKnob");
      2. The plugin's default filmstrip: laf.setDefaultKnobFilmstrip (assets::image ("knob.png"), 128);
      3. drawKnob(), which a plugin can override for vector knobs (the default is a simple dial).

    Filmstrips are a single column (or row) of equal square-ish frames, first
    frame = minimum. Missing images fall through to the next option.
*/
class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    explicit LookAndFeel (Theme theme = {});

    void setTheme (Theme newTheme);
    const Theme& getTheme() const noexcept   { return theme; }

    void setDefaultKnobFilmstrip (juce::Image strip, int numFrames, bool vertical = true);
    void addKnobFilmstrip (const juce::String& key, juce::Image strip, int numFrames, bool vertical = true);
    static void useKnobArt (juce::Slider& slider, const juce::String& key);

    /** Override for custom vector knob art. proportion is 0..1 along the slider's range. */
    virtual void drawKnob (juce::Graphics& g, juce::Rectangle<float> bounds, float proportion, juce::Slider& slider);

    //==========================================================================
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getLabelFont (juce::Label&) override;

    /** Theme font at a height (uses the plugin typeface when set). */
    juce::Font font (float height) const;

private:
    struct Filmstrip
    {
        juce::Image image;
        int frames = 0;
        bool vertical = true;
        bool isValid() const   { return image.isValid() && frames > 0; }
    };

    static void drawFilmstrip (juce::Graphics&, const Filmstrip&, juce::Rectangle<float>, float proportion);
    void applyColours();

    Theme theme;
    Filmstrip defaultKnob;
    std::map<juce::String, Filmstrip> knobs;
};

} // namespace lockedin
