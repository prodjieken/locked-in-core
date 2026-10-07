#pragma once

namespace lockedin
{

/**
    Base processor for Locked In plugins. Handles the boring parts so a plugin
    only writes its DSP:

      - APVTS from a ParamLayout, with a smoother for every continuous param
      - MeterBus with inputRms/inputPeak/outputRms/outputPeak filled for you
      - TransportRandom (per-instance seed, saved in the session)
      - GagSounds mixed into the output (only when the toggle is on)
      - PresetManager
      - state save/restore (params + UI size + seed + current preset name)

        class TrunkProcessor : public lockedin::Processor
        {
        public:
            TrunkProcessor() : Processor (createParams()) {}
            void process (juce::AudioBuffer<float>&, juce::MidiBuffer&) override { ... }
            juce::AudioProcessorEditor* createEditor() override { return new TrunkEditor (*this); }
        };
*/
class Processor : public juce::AudioProcessor
{
    // Declared first so it's initialised before apvts consumes the ParamLayout.
    std::vector<ParamLayout::Info> paramInfos;

public:
    explicit Processor (ParamLayout params, const BusesProperties& buses = defaultBuses());
    ~Processor() override = default;

    static BusesProperties defaultBuses();

    //==========================================================================
    // Override these

    /** Called from prepareToPlay, after the core pieces are prepared. */
    virtual void prepare (double sampleRate, int maximumBlockSize)   { juce::ignoreUnused (sampleRate, maximumBlockSize); }

    /** Your DSP. Smoothers are already updated, input already metered. */
    virtual void process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) = 0;

    virtual void release() {}

    //==========================================================================
    juce::AudioProcessorValueTreeState apvts;
    MeterBus meters;
    TransportRandom random;
    GagSounds gagSounds;
    PresetManager presets;

    /** Smoother for a continuous param declared with smoothingMs > 0.
        Look it up once (constructor) and keep the reference. */
    SmoothedParam& smoothed (const juce::String& paramId);

    /** Raw (unsmoothed) current value in real units. */
    std::atomic<float>& raw (const juce::String& paramId);

    float getUiScale() const noexcept          { return uiScale.load(); }
    void setUiScale (float s) noexcept         { uiScale.store (s); }

    //==========================================================================
    // juce::AudioProcessor
    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) final;
    using juce::AudioProcessor::processBlock;
    void reset() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override   { return 0.0; }

    bool hasEditor() const override                { return true; }

    int getNumPrograms() override                  { return 1; }
    int getCurrentProgram() override               { return 0; }
    void setCurrentProgram (int) override          {}
    const juce::String getProgramName (int) override { return presets.getCurrentName(); }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

private:
    std::map<juce::String, std::unique_ptr<SmoothedParam>> smoothers;
    std::vector<SmoothedParam*> smootherList;

    LevelMeter inputMeter, outputMeter;
    std::atomic<float> uiScale { 1.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Processor)
};

} // namespace lockedin
