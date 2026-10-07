#pragma once

#include <lockedin_core/lockedin_core.h>

/**
    Hello Character: the smallest complete Locked In plugin.

    Audio passes through (with an output trim). The input level goes on the
    MeterBus, and the character moves idle -> listening -> loud as you play.
    Clipping input (> -0.3 dBFS) fires a "clip" event that makes the
    character jump (one-shot), flashes DING, and, only if "Gag sounds in
    output" is turned on in the corner menu, plays a ding into the output.
*/
class HelloProcessor : public lockedin::Processor
{
public:
    HelloProcessor();

    void prepare (double sampleRate, int maxBlock) override;
    void process (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;

    lockedin::EventId clipEvent = -1;

private:
    static lockedin::ParamLayout createParams();

    lockedin::SmoothedParam& output;
    int dingSound = -1;
    bool wasClipping = false;
};

//==============================================================================
class HelloEditor : public lockedin::Editor
{
public:
    explicit HelloEditor (HelloProcessor&);

private:
    void layout (juce::Rectangle<int> area) override;
    void tick (const lockedin::MeterSnapshot&, double dtMs) override;

    static lockedin::CharacterDef loadCharacter();
    static lockedin::Theme makeTheme();

    lockedin::CharacterBrain brain;
    lockedin::CharacterView view;
    lockedin::PresetBar presetBar;

    juce::Label title, levelReadout;
    juce::Slider outputKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::AudioProcessorValueTreeState::SliderAttachment outputAttachment;
};
