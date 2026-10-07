#pragma once

namespace lockedin
{

/**
    Comedic sound effects mixed into the plugin's OUTPUT.

    House rule: these plugins go on real mixes, so gag sounds are silent unless
    the user turns on the "gagSounds" parameter (ParamLayout::gagSoundToggle(),
    default OFF, never changed by presets). If the plugin forgot to declare that
    parameter, GagSounds stays silent forever.

    Setup, in the processor constructor:
        dingId = gagSounds.add ("ding.wav", -12.0f);     // asset file name, gain in dB

    Audio thread:
        gagSounds.trigger (dingId, sampleOffset);       // ignored while the toggle is off

    lockedin::Processor calls prepare() and mixes them in after your process()
    (and after the output meter, so the character doesn't react to its own gags).
*/
class GagSounds
{
public:
    explicit GagSounds (juce::AudioProcessorValueTreeState& apvts);

    /** Message thread, before audio starts. Decodes WAV/AIFF/FLAC/Ogg from the
        plugin's assets. Returns an id for trigger(), or -1 if the asset is missing. */
    int add (const juce::String& assetFileName, float gainDb = -6.0f);

    /** Resamples everything to the host rate (allocates; not realtime). */
    void prepare (double sampleRate, int maxBlockSize);

    /** Audio thread. Starts a sound at sampleOffset in the current block.
        Does nothing while the toggle is off (nothing gets queued for later). */
    void trigger (int soundId, int sampleOffset = 0) noexcept;

    /** Audio thread. Adds active sounds into buffer. If the toggle is switched
        off mid-sound, the sound fades out over 10 ms instead of clicking. */
    void process (juce::AudioBuffer<float>& buffer) noexcept;

    /** Audio thread. Stops everything immediately. */
    void stopAll() noexcept;

    bool isEnabled() const noexcept   { return toggle != nullptr && toggle->load (std::memory_order_relaxed) >= 0.5f; }
    bool hasToggle() const noexcept   { return toggle != nullptr; }
    int getNumSounds() const noexcept { return (int) sounds.size(); }

private:
    struct Sound
    {
        juce::String name;
        juce::AudioBuffer<float> original, resampled;
        double originalRate = 44100.0;
        float gain = 1.0f;
    };

    struct Voice
    {
        int sound = -1;
        int position = 0;
        int startOffset = 0;
        float fade = 1.0f;
    };

    static constexpr int maxVoices = 8;

    std::atomic<float>* toggle = nullptr;
    std::vector<Sound> sounds;
    std::array<Voice, maxVoices> voices {};
    float fadeStep = 0.002f;
};

} // namespace lockedin
