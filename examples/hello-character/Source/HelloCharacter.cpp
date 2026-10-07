#include "HelloCharacter.h"

//==============================================================================
lockedin::ParamLayout HelloProcessor::createParams()
{
    lockedin::ParamLayout p;
    p.decibels ("output", "Output", -24.0f, 12.0f, 0.0f);
    p.gagSoundToggle();
    return p;
}

HelloProcessor::HelloProcessor()
    : Processor (createParams()),
      output (smoothed ("output"))
{
    clipEvent = meters.addEvent ("clip");
    dingSound = gagSounds.add ("ding.wav", -14.0f);
}

void HelloProcessor::prepare (double, int)
{
    wasClipping = false;
}

void HelloProcessor::process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Clip detection on the input, sample-accurate so the ding lands on the hit
    int clipAt = -1;
    for (int ch = 0; ch < buffer.getNumChannels() && clipAt < 0; ++ch)
    {
        const auto* x = buffer.getReadPointer (ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (std::abs (x[i]) > 0.966f)    // -0.3 dBFS
            {
                clipAt = i;
                break;
            }
    }

    if (clipAt >= 0 && ! wasClipping)
    {
        meters.fire (clipEvent);
        gagSounds.trigger (dingSound, clipAt);   // silent unless the user enabled gag sounds
    }
    wasClipping = clipAt >= 0;

    // Pass-through with a smoothed output trim
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const auto g = lockedin::dbToGain (output.next());
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.getWritePointer (ch)[i] *= g;
    }
}

juce::AudioProcessorEditor* HelloProcessor::createEditor()
{
    return new HelloEditor (*this);
}

//==============================================================================
lockedin::CharacterDef HelloEditor::loadCharacter()
{
    lockedin::CharacterDef def;
    const auto result = lockedin::CharacterDef::parse (lockedin::assets::text ("hello-character.json"), def);
    jassert (result.wasOk());
    if (result.failed())
        def = lockedin::CharacterDef::placeholder ({ "idle", "listening", "loud" });
    return def;
}

lockedin::Theme HelloEditor::makeTheme()
{
    lockedin::Theme t;
    t.accent = juce::Colour (0xff7cf0b5);
    return t;
}

HelloEditor::HelloEditor (HelloProcessor& p)
    : Editor (p, 480, 380, makeTheme()),
      brain (loadCharacter(), p.meters),
      view (brain),
      presetBar (p.presets),
      outputAttachment (p.apvts, "output", outputKnob)
{
    // Most extreme state first: first active rule wins.
    brain.when ("loud").above ("inputRms", -12.0f).releaseBelow (-16.0f).forMs (80).releaseAfterMs (350);
    brain.when ("listening").above ("inputRms", -50.0f).releaseBelow (-56.0f).forMs (40).releaseAfterMs (600);
    brain.trigger ("startled").onEvent ("clip").cooldownMs (1200);

    brain.intensity ("listening").from ("inputRms", -50.0f, -12.0f);
    brain.intensity ("loud").from ("inputRms", -12.0f, 0.0f).smoothingMs (80);

    brain.onStateChange = [this] (const juce::String&, const juce::String& to)
    {
        if (to == "startled")
        {
            gags().flash ("DING");
            gags().shake (8.0f);
        }
        else if (to == "loud")
        {
            gags().say ("ok that's LOUD", view.getBounds().toFloat().getCentre().translated (30.0f, -60.0f));
        }
    };

    title.setText ("HELLO CHARACTER", juce::dontSendNotification);
    title.setFont (getLnf().font (20.0f).boldened());
    title.setJustificationType (juce::Justification::centredLeft);

    levelReadout.setJustificationType (juce::Justification::centredRight);
    levelReadout.setColour (juce::Label::textColourId, getLnf().getTheme().textDim);

    for (auto* c : std::initializer_list<juce::Component*> { &view, &presetBar, &title, &levelReadout, &outputKnob })
        content().addAndMakeVisible (c);

    attachCharacter (brain, view);
}

void HelloEditor::layout (juce::Rectangle<int> area)
{
    area.reduce (14, 12);

    auto top = area.removeFromTop (30);
    title.setBounds (top.removeFromLeft (200));
    presetBar.setBounds (top.removeFromRight (220));

    auto bottom = area.removeFromBottom (90);
    outputKnob.setBounds (bottom.removeFromLeft (90));
    levelReadout.setBounds (bottom.removeFromRight (180).withTrimmedRight (26));

    view.setBounds (area.reduced (40, 6));
}

void HelloEditor::tick (const lockedin::MeterSnapshot& m, double)
{
    levelReadout.setText ("in " + juce::String (m[lockedin::MeterBus::Std::inputRms], 1) + " dB",
                          juce::dontSendNotification);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HelloProcessor();
}
