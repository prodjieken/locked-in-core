namespace lockedin
{

namespace
{
    juce::String pluginName()
    {
       #ifdef JucePlugin_Name
        return JucePlugin_Name;
       #else
        return "Locked In Plugin";
       #endif
    }
}

juce::AudioProcessor::BusesProperties Processor::defaultBuses()
{
   #if defined (JucePlugin_IsSynth) && JucePlugin_IsSynth
    return BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true);
   #elif defined (JucePlugin_IsMidiEffect) && JucePlugin_IsMidiEffect
    return BusesProperties();
   #else
    return BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                            .withOutput ("Output", juce::AudioChannelSet::stereo(), true);
   #endif
}

Processor::Processor (ParamLayout params, const BusesProperties& buses)
    : juce::AudioProcessor (buses),
      paramInfos (params.getInfos()),
      apvts (*this, nullptr, "PARAMS", params.release()),
      gagSounds (apvts),
      presets (apvts, pluginName())
{
    for (const auto& info : paramInfos)
    {
        if (info.smoothingMs > 0.0f)
        {
            if (auto* r = apvts.getRawParameterValue (info.id))
            {
                auto s = std::make_unique<SmoothedParam> (r, info.smoothingMs);
                smootherList.push_back (s.get());
                smoothers[info.id] = std::move (s);
            }
        }
    }

    // New instance: fresh seed. A restored session overwrites it in setStateInformation.
    random.setSeed ((uint64_t) juce::Random::getSystemRandom().nextInt64());
}

SmoothedParam& Processor::smoothed (const juce::String& id)
{
    const auto it = smoothers.find (id);
    jassert (it != smoothers.end());   // not declared, or declared with smoothingMs = 0
    if (it == smoothers.end())
    {
        // Keep running in release builds: smooth the raw value with a default time
        auto s = std::make_unique<SmoothedParam> (&raw (id), 20.0f);
        auto& ref = *s;
        smootherList.push_back (s.get());
        smoothers[id] = std::move (s);
        return ref;
    }
    return *it->second;
}

std::atomic<float>& Processor::raw (const juce::String& id)
{
    auto* r = apvts.getRawParameterValue (id);
    jassert (r != nullptr);   // unknown parameter id
    static std::atomic<float> dummy { 0.0f };
    return r != nullptr ? *r : dummy;
}

//==============================================================================
void Processor::prepareToPlay (double sampleRate, int maxBlock)
{
    for (auto* s : smootherList)
        s->prepare (sampleRate);

    inputMeter.prepare (sampleRate);
    outputMeter.prepare (sampleRate);
    random.prepare (sampleRate);
    gagSounds.prepare (sampleRate, maxBlock);
    meters.resetValues();

    prepare (sampleRate, maxBlock);
}

void Processor::releaseResources()
{
    gagSounds.stopAll();
    release();
}

void Processor::reset()
{
    inputMeter.reset();
    outputMeter.reset();
    gagSounds.stopAll();
    meters.resetValues();
}

void Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numIn = getTotalNumInputChannels();
    for (auto ch = numIn; ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    random.beginBlock (getPlayHead(), buffer.getNumSamples());
    meters.beginBlock (buffer.getNumSamples(), getSampleRate());

    for (auto* s : smootherList)
        s->update();

    if (numIn > 0)
    {
        inputMeter.process (buffer);
        meters.set (MeterBus::Std::inputRms, inputMeter.getRmsDb());
        meters.set (MeterBus::Std::inputPeak, inputMeter.getPeakDb());
    }

    process (buffer, midi);

    outputMeter.process (buffer);
    meters.set (MeterBus::Std::outputRms, outputMeter.getRmsDb());
    meters.set (MeterBus::Std::outputPeak, outputMeter.getPeakDb());

    gagSounds.process (buffer);
}

bool Processor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
   #if defined (JucePlugin_IsMidiEffect) && JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
   #else
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    #if ! (defined (JucePlugin_IsSynth) && JucePlugin_IsSynth)
     if (layouts.getMainInputChannelSet() != out)
         return false;
    #endif

    return true;
   #endif
}

const juce::String Processor::getName() const   { return pluginName(); }

bool Processor::acceptsMidi() const
{
   #if defined (JucePlugin_WantsMidiInput) && JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool Processor::producesMidi() const
{
   #if defined (JucePlugin_ProducesMidiOutput) && JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool Processor::isMidiEffect() const
{
   #if defined (JucePlugin_IsMidiEffect) && JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

//==============================================================================
void Processor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree root ("LockedInState");
    root.setProperty ("version", 1, nullptr);
    root.setProperty ("uiScale", (double) uiScale.load(), nullptr);
    root.setProperty ("seed", juce::String::toHexString ((juce::int64) random.getSeed()), nullptr);
    root.setProperty ("preset", presets.getCurrentName(), nullptr);
    root.appendChild (apvts.copyState(), nullptr);

    if (auto xml = root.createXml())
        copyXmlToBinary (*xml, destData);
}

void Processor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;

    const auto root = juce::ValueTree::fromXml (*xml);
    if (! root.hasType ("LockedInState"))
        return;

    if (root.hasProperty ("uiScale"))
        uiScale.store ((float) (double) root.getProperty ("uiScale"));

    if (root.hasProperty ("seed"))
        random.setSeed ((uint64_t) root.getProperty ("seed").toString().getHexValue64());

    presets.setCurrentName (root.getProperty ("preset").toString());

    const auto params = root.getChildWithName (apvts.state.getType());
    if (params.isValid())
        apvts.replaceState (params);
}

} // namespace lockedin
