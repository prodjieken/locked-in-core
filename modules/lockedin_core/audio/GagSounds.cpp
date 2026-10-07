namespace lockedin
{

GagSounds::GagSounds (juce::AudioProcessorValueTreeState& apvts)
{
    toggle = apvts.getRawParameterValue (gagSoundsParamId);
}

int GagSounds::add (const juce::String& assetFileName, float gainDb)
{
    // A plugin with gag sounds must declare the toggle: ParamLayout::gagSoundToggle()
    jassert (toggle != nullptr);

    auto bytes = assets::data (assetFileName);
    if (bytes.isEmpty())
    {
        DBG ("lockedin::GagSounds: missing asset " << assetFileName);
        jassertfalse;
        return -1;
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (
        formats.createReaderFor (std::make_unique<juce::MemoryInputStream> (bytes, false)));

    if (reader == nullptr || reader->lengthInSamples <= 0)
    {
        DBG ("lockedin::GagSounds: can't decode " << assetFileName);
        jassertfalse;
        return -1;
    }

    Sound s;
    s.name = assetFileName;
    s.originalRate = reader->sampleRate;
    s.gain = dbToGain (gainDb);
    s.original.setSize ((int) juce::jlimit (1u, 2u, reader->numChannels), (int) reader->lengthInSamples);
    reader->read (&s.original, 0, (int) reader->lengthInSamples, 0, true, true);

    sounds.push_back (std::move (s));
    return (int) sounds.size() - 1;
}

void GagSounds::prepare (double sampleRate, int)
{
    for (auto& v : voices)
        v = {};

    fadeStep = (float) (1.0 / juce::jmax (1.0, sampleRate * 0.010));   // 10 ms fade

    for (auto& s : sounds)
    {
        const auto ratio = s.originalRate / sampleRate;
        const auto outLen = juce::jmax (1, (int) std::ceil (s.original.getNumSamples() / ratio));
        s.resampled.setSize (s.original.getNumChannels(), outLen);
        s.resampled.clear();

        for (int ch = 0; ch < s.original.getNumChannels(); ++ch)
        {
            juce::LagrangeInterpolator interp;
            interp.process (ratio, s.original.getReadPointer (ch), s.resampled.getWritePointer (ch),
                            outLen, s.original.getNumSamples(), 0);
        }
    }
}

void GagSounds::trigger (int soundId, int sampleOffset) noexcept
{
    if (! isEnabled() || ! juce::isPositiveAndBelow (soundId, (int) sounds.size()))
        return;

    // Free voice, or steal the one that has played the longest
    int best = 0;
    for (int i = 0; i < maxVoices; ++i)
    {
        if (voices[(size_t) i].sound < 0) { best = i; break; }
        if (voices[(size_t) i].position > voices[(size_t) best].position) best = i;
    }

    voices[(size_t) best] = { soundId, 0, juce::jmax (0, sampleOffset), 1.0f };
}

void GagSounds::process (juce::AudioBuffer<float>& buffer) noexcept
{
    const bool on = isEnabled();
    const auto numCh = buffer.getNumChannels();
    const auto numSamples = buffer.getNumSamples();

    for (auto& v : voices)
    {
        if (v.sound < 0)
            continue;

        const auto& s = sounds[(size_t) v.sound];
        const auto len = s.resampled.getNumSamples();
        const auto srcCh = s.resampled.getNumChannels();

        int i = juce::jmin (v.startOffset, numSamples);
        v.startOffset = juce::jmax (0, v.startOffset - numSamples);

        for (; i < numSamples && v.position < len; ++i, ++v.position)
        {
            if (! on)
            {
                v.fade -= fadeStep;
                if (v.fade <= 0.0f)
                    break;
            }

            const auto g = s.gain * v.fade;
            for (int ch = 0; ch < numCh; ++ch)
                buffer.addSample (ch, i, g * s.resampled.getSample (ch % srcCh, v.position));
        }

        if (v.position >= len || v.fade <= 0.0f)
            v = {};
    }
}

void GagSounds::stopAll() noexcept
{
    for (auto& v : voices)
        v = {};
}

} // namespace lockedin
