#pragma once

namespace lockedin
{

inline float gainToDb (float gain) noexcept    { return gain > 1.0e-5f ? 20.0f * std::log10 (gain) : -100.0f; }
inline float dbToGain (float db) noexcept      { return db > -100.0f ? std::pow (10.0f, db * 0.05f) : 0.0f; }

//==============================================================================
/**
    RMS + peak follower with fixed ballistics, so the numbers a character reacts
    to don't depend on the host's buffer size (FL at 64 samples vs a bounce at
    4096 gives the same reading). Audio thread only; prepare() first.
*/
class LevelMeter
{
public:
    void prepare (double sampleRate, float rmsWindowMs = 50.0f, float peakReleaseMs = 300.0f)
    {
        rmsCoeff  = std::exp (-1.0f / (float (sampleRate) * rmsWindowMs * 0.001f));
        peakCoeff = std::exp (-1.0f / (float (sampleRate) * peakReleaseMs * 0.001f));
        reset();
    }

    void reset() noexcept   { meanSquare = 0.0f; peak = 0.0f; }

    /** Feeds a block (all channels, louder channel wins). */
    void process (const juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept
    {
        const auto numCh = buffer.getNumChannels();
        if (numCh == 0)
            return;

        for (int i = startSample; i < startSample + numSamples; ++i)
        {
            float sq = 0.0f, pk = 0.0f;
            for (int ch = 0; ch < numCh; ++ch)
            {
                const auto x = buffer.getReadPointer (ch)[i];
                sq = juce::jmax (sq, x * x);
                pk = juce::jmax (pk, std::abs (x));
            }

            meanSquare = sq + rmsCoeff * (meanSquare - sq);
            peak = pk > peak ? pk : pk + peakCoeff * (peak - pk);
        }

        // flush denormals
        if (meanSquare < 1.0e-15f) meanSquare = 0.0f;
        if (peak < 1.0e-8f)        peak = 0.0f;
    }

    void process (const juce::AudioBuffer<float>& buffer) noexcept   { process (buffer, 0, buffer.getNumSamples()); }

    float getRmsDb() const noexcept    { return gainToDb (std::sqrt (meanSquare)); }
    float getPeakDb() const noexcept   { return gainToDb (peak); }

private:
    float rmsCoeff = 0.99f, peakCoeff = 0.999f;
    float meanSquare = 0.0f, peak = 0.0f;
};

} // namespace lockedin
