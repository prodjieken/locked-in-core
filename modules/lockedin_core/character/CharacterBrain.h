#pragma once

namespace lockedin
{

/**
    The character's state machine. Headless (no drawing), driven by MeterBus
    snapshots, ticked by the Editor at ~60 Hz. CharacterView draws whatever
    state/frame/intensity this reports.

    Three kinds of rule, all declared in C++ in your editor's constructor:

    1. Sustained states (hold while the audio says so). First matching rule wins,
       so declare the most extreme states first:

         brain.when ("strain").above ("gainReduction", 6.0f).forMs (200).releaseBelow (4.0f).releaseAfterMs (300);
         brain.when ("loud").above ("inputRms", -12.0f).releaseBelow (-16.0f);
         // nothing active -> the JSON's "default" state

       Hysteresis comes from three places so the character never flickers:
         - separate enter/release thresholds (releaseBelow / releaseAbove)
         - enter/release hold times (forMs / releaseAfterMs)
         - a minimum time in any state before switching (setMinimumDwellMs, default 120 ms)

    2. One-shots (play once, then go to the JSON's "then" state):

         brain.trigger ("flinch").onEvent ("clip").cooldownMs (800);
         brain.trigger ("gasp").whenRises ("inputPeak", -1.0f).rearmBelow (-6.0f);

    3. Intensity (continuous 0..1 per state; drives shake/rise/squash/tint and
       anything else you draw):

         brain.intensity ("strain").from ("gainReduction", 6.0f, 18.0f).smoothingMs (80);
*/
class CharacterBrain
{
public:
    using Condition = std::function<bool (const MeterSnapshot&)>;
    using Source    = std::function<float (const MeterSnapshot&)>;

    CharacterBrain (CharacterDef definition, const MeterBus& meterBus);

    //==========================================================================
    class StateRule
    {
    public:
        StateRule& above (const juce::String& meter, float threshold);
        StateRule& below (const juce::String& meter, float threshold);
        StateRule& releaseBelow (float threshold);   // for above(): leave when value drops under this
        StateRule& releaseAbove (float threshold);   // for below(): leave when value rises over this
        StateRule& forMs (float ms);                 // condition must hold this long to enter
        StateRule& releaseAfterMs (float ms);        // release condition must hold this long to leave (default 150)
        StateRule& condition (Condition enter, Condition release = {});   // custom logic

        bool isActive() const noexcept { return active; }

    private:
        friend class CharacterBrain;
        StateRule (CharacterBrain& b, int state) : brain (b), stateIndex (state) {}

        bool enterNow (const MeterSnapshot&) const;
        bool releaseNow (const MeterSnapshot&) const;
        void update (const MeterSnapshot&, double dtMs);

        CharacterBrain& brain;
        int stateIndex;
        MeterId meter = -1;
        bool isAbove = true, hasRelease = false, configured = false;
        float threshold = 0.0f, release = 0.0f;
        float enterMs = 0.0f, exitMs = 150.0f;
        Condition customEnter, customRelease;

        bool active = false;
        double enterTimer = 0.0, exitTimer = 0.0;
    };

    //==========================================================================
    class Trigger
    {
    public:
        Trigger& onEvent (const juce::String& eventName);             // MeterBus::fire() on the audio thread
        Trigger& whenRises (const juce::String& meter, float threshold);   // edge: crosses above threshold
        Trigger& rearmBelow (float threshold);                        // default: the threshold itself
        Trigger& when (Condition c);                                  // edge: false -> true
        Trigger& cooldownMs (float ms);
        Trigger& interrupting (bool shouldInterruptOtherOneShots = true);

    private:
        friend class CharacterBrain;
        Trigger (CharacterBrain& b, int state) : brain (b), stateIndex (state) {}
        bool update (const MeterSnapshot&, double dtMs);   // true = fire now

        enum class Kind { none, event, rise, condition };

        CharacterBrain& brain;
        int stateIndex;
        Kind kind = Kind::none;
        EventId event = -1;
        MeterId meter = -1;
        float threshold = 0.0f, rearm = 0.0f;
        bool hasRearm = false, interrupts = false;
        Condition cond;
        float cooldown = 0.0f;

        bool armed = true, lastCond = false;
        double sinceFired = 1.0e9;
    };

    //==========================================================================
    class Intensity
    {
    public:
        Intensity& from (const juce::String& meter, float atZero, float atOne);
        Intensity& from (Source s);
        Intensity& smoothingMs (float ms);

    private:
        friend class CharacterBrain;
        explicit Intensity (CharacterBrain& b) : brain (b) {}
        float target (const MeterSnapshot&) const;

        CharacterBrain& brain;
        MeterId meter = -1;
        float lo = 0.0f, hi = 1.0f;
        Source source;
        float smoothMs = 60.0f;
    };

    //==========================================================================
    StateRule& when (const juce::String& state);
    Trigger&   trigger (const juce::String& oneShotState);
    Intensity& intensity (const juce::String& state);

    void setMinimumDwellMs (float ms) noexcept   { minDwellMs = juce::jmax (0.0f, ms); }

    /** Advance by dtMs using this frame's meter values. */
    void tick (const MeterSnapshot& snapshot, double dtMs);

    /** Jump to a state right now (one-shots play from frame 0). */
    void play (const juce::String& state);

    //==========================================================================
    const CharacterDef& getDefinition() const noexcept   { return def; }
    int getStateIndex() const noexcept                   { return current; }
    const StateDef& getState() const noexcept            { return def.states[(size_t) current]; }
    const juce::String& getStateName() const noexcept    { return getState().name; }
    int getFrame() const noexcept;
    double getTimeInStateMs() const noexcept             { return timeInState; }
    bool isPlayingOneShot() const noexcept               { return ! getState().loop; }

    float getIntensity() const noexcept                  { return intensities[(size_t) current]; }
    float getIntensity (const juce::String& state) const;

    /** Called on every state change, from inside tick() / play(). Good place to fire gags. */
    std::function<void (const juce::String& from, const juce::String& to)> onStateChange;

private:
    int stateIndexOrAssert (const juce::String& name) const;
    MeterId meterOrAssert (const juce::String& name) const;
    void enter (int index);

    CharacterDef def;
    const MeterBus& bus;

    std::vector<std::unique_ptr<StateRule>> rules;
    std::vector<std::unique_ptr<Trigger>> triggers;
    std::vector<std::unique_ptr<Intensity>> intensitySources;   // indexed by state, may be null
    std::unique_ptr<Intensity> orphanIntensity;                 // for an unknown state name, so the builder reference stays valid
    std::vector<float> intensities;

    int current = 0, defaultIndex = 0;
    double timeInState = 0.0;
    float minDwellMs = 120.0f;

    JUCE_DECLARE_NON_COPYABLE (CharacterBrain)
};

} // namespace lockedin
