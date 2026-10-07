#pragma once

namespace lockedin
{

/**
    Base editor: fixed design size, no drag-resizing, and a small menu button
    (bottom-right) with Size 75% / 100% / 125%, the gag-sound toggle, and
    anything the plugin adds. The chosen size is saved with the session.

    Build your UI in DESIGN coordinates inside content(); scaling is applied
    for you, so you never think about the size menu.

        class TrunkEditor : public lockedin::Editor
        {
        public:
            TrunkEditor (TrunkProcessor& p) : Editor (p, 640, 420, makeTheme()), brain (...), view (brain)
            {
                content().addAndMakeVisible (view);
                attachCharacter (brain, view);
            }
            void layout (juce::Rectangle<int> area) override  { view.setBounds (area.reduced (40)); }
            void tick (const lockedin::MeterSnapshot& m, double dtMs) override { ... }
        };
*/
class Editor : public juce::AudioProcessorEditor,
               private juce::Timer
{
public:
    Editor (Processor& processor, int designWidth, int designHeight, Theme theme = {});
    ~Editor() override;

    static constexpr std::array<float, 3> scaleOptions { 0.75f, 1.0f, 1.25f };

    juce::Component& content() noexcept       { return contentComp; }
    GagLayer& gags() noexcept                 { return gagLayer; }
    LookAndFeel& getLnf() noexcept            { return lnf; }
    Processor& getProcessor() noexcept        { return proc; }
    const MeterSnapshot& getMeters() const noexcept { return snapshot; }
    juce::Rectangle<int> getDesignBounds() const noexcept { return { designW, designH }; }

    /** The brain is ticked and the view repainted every frame, before tick(). */
    void attachCharacter (CharacterBrain& brain, CharacterView& view);

    void setScale (float scale);
    float getScale() const noexcept           { return scale; }

    //==========================================================================
    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

protected:
    /** Lay out your components in design coordinates. Called once before first paint. */
    virtual void layout (juce::Rectangle<int> designArea)    { juce::ignoreUnused (designArea); }

    /** ~60 Hz, after the meter snapshot is taken and characters are ticked. */
    virtual void tick (const MeterSnapshot& meters, double dtMs)   { juce::ignoreUnused (meters, dtMs); }

    /** Add plugin-specific items to the corner menu. */
    virtual void addMenuItems (juce::PopupMenu& menu)        { juce::ignoreUnused (menu); }

private:
    class MenuButton : public juce::Button
    {
    public:
        MenuButton() : juce::Button ("menu") {}
        void paintButton (juce::Graphics&, bool over, bool down) override;
    };

    void timerCallback() override;
    void showMenu();
    void applyTransform();
    void layoutIfNeeded();

    Processor& proc;
    LookAndFeel lnf;
    const int designW, designH;
    float scale = 1.0f;

    juce::Component stage, contentComp;
    GagLayer gagLayer;
    MenuButton menuButton;

    MeterSnapshot snapshot;
    std::vector<std::pair<CharacterBrain*, CharacterView*>> characters;
    double lastTickMs = 0.0;
    bool laidOut = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Editor)
};

} // namespace lockedin
