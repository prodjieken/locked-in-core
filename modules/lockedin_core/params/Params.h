#pragma once

namespace lockedin
{

/** Parameter id of the gag-sound toggle. Same in every plugin. */
inline const juce::String gagSoundsParamId { "gagSounds" };

//==============================================================================
/**
    Quick parameter declarations with display names, units, skews and smoothing.

        static lockedin::ParamLayout createParams()
        {
            lockedin::ParamLayout p;
            p.decibels ("drive", "Drive", -12.0f, 24.0f, 0.0f);
            p.hertz    ("tone", "Tone", 200.0f, 12000.0f, 2000.0f);
            p.percent  ("mix", "Mix", 100.0f);
            p.ms       ("attack", "Attack", 0.1f, 200.0f, 10.0f);
            p.choice   ("mode", "Mode", { "Clean", "Dirty", "Filthy" }, 0);
            p.toggle   ("lock", "Lock", false);
            p.gagSoundToggle();      // only if the plugin has gag sounds
            return p;
        }

    Smoothing time (ms) is the last argument on continuous params (default 20 ms,
    0 = none). lockedin::Processor builds a smoother for each one; in DSP:

        auto& drive = smoothed ("drive");     // keep the reference (look it up once, in the constructor)
        ...
        const float g = lockedin::dbToGain (drive.next());

    IDs are saved in sessions and presets: never rename one after release.
*/
class ParamLayout
{
public:
    struct Info
    {
        juce::String id;
        float smoothingMs = 0.0f;
    };

    ParamLayout& number (const juce::String& id, const juce::String& name,
                         float min, float max, float def,
                         const juce::String& unit = {}, int decimals = 2,
                         float step = 0.0f, float skewCentre = -1.0f, float smoothingMs = 20.0f)
    {
        juce::NormalisableRange<float> range (min, max, step);
        if (skewCentre > min && skewCentre < max)
            range.setSkewForCentre (skewCentre);

        const auto label = unit;
        auto toText = [label, decimals] (float v, int) { return juce::String (v, decimals) + (label.isNotEmpty() ? " " + label : juce::String()); };
        return addFloat (id, name, range, def, unit, toText, smoothingMs);
    }

    ParamLayout& decibels (const juce::String& id, const juce::String& name,
                           float min, float max, float def, float smoothingMs = 20.0f)
    {
        const bool minIsSilence = min <= -60.0f;
        auto toText = [minIsSilence, min] (float v, int)
        {
            if (minIsSilence && v <= min + 0.001f)
                return juce::String ("-inf dB");
            return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB";
        };
        return addFloat (id, name, { min, max, 0.01f }, def, "dB", toText, smoothingMs,
                         [min] (const juce::String& s) { return s.containsIgnoreCase ("inf") ? min : s.getFloatValue(); });
    }

    ParamLayout& percent (const juce::String& id, const juce::String& name, float def, float smoothingMs = 20.0f)
    {
        auto toText = [] (float v, int) { return juce::String (juce::roundToInt (v)) + "%"; };
        return addFloat (id, name, { 0.0f, 100.0f, 0.01f }, def, "%", toText, smoothingMs);
    }

    /** Logarithmic-feeling frequency control (skew centred on the geometric mean). */
    ParamLayout& hertz (const juce::String& id, const juce::String& name,
                        float min, float max, float def, float smoothingMs = 20.0f)
    {
        juce::NormalisableRange<float> range (min, max, 0.01f);
        range.setSkewForCentre (std::sqrt (min * max));
        auto toText = [] (float v, int)
        {
            return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz"
                                : juce::String (juce::roundToInt (v)) + " Hz";
        };
        return addFloat (id, name, range, def, "Hz", toText, smoothingMs,
                         [] (const juce::String& t) { return t.getFloatValue() * (t.containsIgnoreCase ("k") ? 1000.0f : 1.0f); });
    }

    ParamLayout& ms (const juce::String& id, const juce::String& name,
                     float min, float max, float def, float skewCentre = -1.0f, float smoothingMs = 0.0f)
    {
        juce::NormalisableRange<float> range (min, max, 0.01f);
        range.setSkewForCentre (skewCentre > min && skewCentre < max ? skewCentre : std::sqrt (juce::jmax (0.01f, min) * max));
        auto toText = [] (float v, int)
        {
            return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " s"
                                : juce::String (v, v < 10.0f ? 2 : 1) + " ms";
        };
        return addFloat (id, name, range, def, "ms", toText, smoothingMs,
                         [] (const juce::String& t)
                         {
                             const auto u = t.trim().toLowerCase();
                             return u.getFloatValue() * (u.endsWith ("s") && ! u.endsWith ("ms") ? 1000.0f : 1.0f);
                         });
    }

    ParamLayout& toggle (const juce::String& id, const juce::String& name, bool def)
    {
        params.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, version }, name, def));
        infos.push_back ({ id, 0.0f });
        return *this;
    }

    ParamLayout& choice (const juce::String& id, const juce::String& name, const juce::StringArray& options, int defIndex)
    {
        params.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, version }, name, options, defIndex));
        infos.push_back ({ id, 0.0f });
        return *this;
    }

    /** The comedic sound-effect toggle. Always OFF by default; never recalled
        from presets (PresetManager skips it), so a preset can't sneak gags into a mix. */
    ParamLayout& gagSoundToggle (const juce::String& name = "Gag Sounds")
    {
        return toggle (gagSoundsParamId, name, false);
    }

    /** Escape hatch for anything else. */
    ParamLayout& custom (std::unique_ptr<juce::RangedAudioParameter> p, float smoothingMs = 0.0f)
    {
        infos.push_back ({ p->getParameterID(), smoothingMs });
        params.push_back (std::move (p));
        return *this;
    }

    //==========================================================================
    const std::vector<Info>& getInfos() const noexcept   { return infos; }

    /** Hands the parameters to the APVTS (call once). */
    juce::AudioProcessorValueTreeState::ParameterLayout release()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;
        for (auto& p : params)
            layout.add (std::move (p));
        params.clear();
        return layout;
    }

    /** ParameterID version hint used for every parameter (AU needs one). */
    static constexpr int version = 1;

private:
    template <typename ToText>
    ParamLayout& addFloat (const juce::String& id, const juce::String& name,
                           juce::NormalisableRange<float> range, float def,
                           const juce::String& unit, ToText toText, float smoothingMs,
                           std::function<float (const juce::String&)> fromText = {})
    {
        jassert (def >= range.start && def <= range.end);

        auto attrs = juce::AudioParameterFloatAttributes()
                         .withLabel (unit)
                         .withStringFromValueFunction (toText);

        if (fromText)
            attrs = attrs.withValueFromStringFunction (fromText);

        params.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, version }, name, range, def, attrs));
        infos.push_back ({ id, smoothingMs });
        return *this;
    }

    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    std::vector<Info> infos;
};

//==============================================================================
/** A float parameter with per-sample smoothing. Built for you by lockedin::Processor. */
class SmoothedParam
{
public:
    SmoothedParam (std::atomic<float>* rawValue, float smoothingMilliseconds)
        : raw (rawValue), smoothingMs (smoothingMilliseconds)
    {
        jassert (raw != nullptr);
    }

    void prepare (double sampleRate)
    {
        smoother.reset (sampleRate, juce::jmax (0.0, (double) smoothingMs * 0.001));
        smoother.setCurrentAndTargetValue (raw->load (std::memory_order_relaxed));
    }

    /** Picks up the latest parameter value (Processor calls this every block). */
    void update() noexcept                 { smoother.setTargetValue (raw->load (std::memory_order_relaxed)); }

    float next() noexcept                  { return smoother.getNextValue(); }
    void skip (int numSamples) noexcept    { smoother.skip (numSamples); }
    float current() const noexcept         { return smoother.getCurrentValue(); }
    float target() const noexcept          { return smoother.getTargetValue(); }
    bool isSmoothing() const noexcept      { return smoother.isSmoothing(); }

private:
    std::atomic<float>* raw;
    float smoothingMs;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoother;
};

} // namespace lockedin
