#pragma once

namespace lockedin
{

using MeterId = int;   // index of a value slot, -1 = invalid
using EventId = int;   // index of an event counter, -1 = invalid

class MeterBus;

//==============================================================================
/** An event posted from the audio thread with a sample-accurate time stamp
    and a small payload, so the UI can line an animation up with the exact
    sample the audio did something (MeterBus::post).

    `time` is on the bus's AUDIO CLOCK: a monotonic count of samples processed
    since the plugin was created. It never jumps with the host transport, so it
    is safe to schedule against. Compare it with MeterSnapshot::audioNow. */
struct TimedEvent
{
    EventId id = -1;
    int64_t time = 0;     // audio-clock sample the event happens at (may be in the future)
    float value = 0.0f;   // payload, meaning is up to the plugin
    int32_t data = 0;     // payload, meaning is up to the plugin
};

//==============================================================================
/** One frame's worth of MeterBus values, taken by the UI (60 Hz).
    Rules and views read from this, never from the bus directly, so every
    component sees the same numbers within a frame. */
struct MeterSnapshot
{
    static constexpr int maxValues = 64;
    static constexpr int maxEvents = 32;
    static constexpr int maxTimed = 256;

    std::array<float, maxValues> values {};
    std::array<uint32_t, maxEvents> eventsFired {};   // how many times each event fired since the previous snapshot
    const MeterBus* bus = nullptr;

    /** Timed events posted since the previous snapshot, oldest first. */
    std::array<TimedEvent, maxTimed> timed {};
    int numTimed = 0;

    /** Best estimate of the audio-clock sample being HEARD right now (the audio
        thread runs about one block ahead of the speakers). Never goes backwards. */
    int64_t audioNow = 0;
    double sampleRate = 44100.0;
    bool audioRunning = false;   // false until the first block, or when the host stopped calling process

    /** Milliseconds from now until an audio-clock time is heard (negative = already heard). */
    double msUntil (int64_t audioTime) const noexcept   { return (double) (audioTime - audioNow) * 1000.0 / sampleRate; }

    float operator[] (MeterId id) const noexcept     { return juce::isPositiveAndBelow (id, maxValues) ? values[(size_t) id] : 0.0f; }
    uint32_t fired (EventId id) const noexcept       { return juce::isPositiveAndBelow (id, maxEvents) ? eventsFired[(size_t) id] : 0u; }

    /** Name lookup. Fine for debugging; resolve ids once for anything per-frame. */
    float get (const juce::String& name) const;
};

//==============================================================================
/**
    Lock-free bridge from the audio thread to the UI.

    Setup (message thread, in your processor's constructor, before audio runs):
        auto bassId = meters.add ("bassEnergy");
        auto hitId  = meters.addEvent ("kick");

    Audio thread (no locks, no allocation):
        meters.set (bassId, energy);
        meters.fire (hitId);

    UI (one consumer, the Editor's 60 Hz clock):
        meters.takeSnapshot (snapshot);

    Timed events, for animation that must land on an exact sample (a paw
    hitting a key the moment the audio glitches):

        stepId = meters.addEvent ("step");                       // setup
        meters.post (stepId, sampleOffset, 0.0f, keyIndex);     // audio thread
        for (int i = 0; i < snap.numTimed; ++i)                 // UI
            schedule (snap.timed[i], snap.msUntil (snap.timed[i].time));

    Times are on the audio clock (samples processed since creation), and
    snapshot.audioNow estimates which of those samples is being heard now.

    Values are plain floats; by convention levels are in dBFS and gain
    reduction is a positive number of dB.
*/
class MeterBus
{
public:
    static constexpr int maxValues = MeterSnapshot::maxValues;
    static constexpr int maxEvents = MeterSnapshot::maxEvents;

    /** How audio-thread writes combine between two UI snapshots. Audio blocks run
        much faster than 60 Hz, so a transient written as "latest" can be overwritten
        before the UI sees it. Use peakHold for anything a character should react to. */
    enum class Mode
    {
        latest,     // UI sees the most recent write
        peakHold,   // UI sees the maximum written since its last snapshot
        troughHold  // UI sees the minimum written since its last snapshot
    };

    /** Slots every Locked In processor fills automatically. */
    struct Std
    {
        static constexpr MeterId inputRms      = 0;   // dBFS, ~50 ms window
        static constexpr MeterId inputPeak     = 1;   // dBFS, peak-hold
        static constexpr MeterId outputRms     = 2;   // dBFS, measured before gag sounds are mixed in
        static constexpr MeterId outputPeak    = 3;   // dBFS, peak-hold
        static constexpr MeterId gainReduction = 4;   // dB (positive), peak-hold; set by your DSP
        static constexpr int count = 5;
    };

    static constexpr float silenceDb = -100.0f;

    MeterBus()
    {
        static_assert (std::atomic<float>::is_always_lock_free, "atomic<float> must be lock-free");
        static_assert (std::atomic<uint32_t>::is_always_lock_free, "atomic<uint32_t> must be lock-free");
        static_assert (std::atomic<int64_t>::is_always_lock_free, "atomic<int64_t> must be lock-free");
        static_assert (std::atomic<double>::is_always_lock_free, "atomic<double> must be lock-free");

        add ("inputRms",      silenceDb, Mode::latest);
        add ("inputPeak",     silenceDb, Mode::peakHold);
        add ("outputRms",     silenceDb, Mode::latest);
        add ("outputPeak",    silenceDb, Mode::peakHold);
        add ("gainReduction", 0.0f,      Mode::peakHold);
    }

    //==========================================================================
    // Setup (message thread, before audio starts)

    MeterId add (const juce::String& name, float initialValue = 0.0f, Mode mode = Mode::latest)
    {
        jassert (find (name) < 0);                          // names must be unique
        const auto id = numValues.load (std::memory_order_relaxed);
        jassert (id < maxValues);                           // raise maxValues if you really need more
        if (id >= maxValues)
            return -1;

        auto& s = slots[(size_t) id];
        s.name = name;
        s.mode = mode;
        s.initial = initialValue;
        s.value.store (initialValue, std::memory_order_relaxed);
        s.held.store (emptyHold (mode), std::memory_order_relaxed);
        numValues.store (id + 1, std::memory_order_release);
        return id;
    }

    /** Adds prefix0..prefixN-1, e.g. addBands ("band", 4) for band energies. Returns the first id. */
    MeterId addBands (const juce::String& prefix, int count, float initialValue = silenceDb, Mode mode = Mode::peakHold)
    {
        MeterId first = -1;
        for (int i = 0; i < count; ++i)
        {
            const auto id = add (prefix + juce::String (i), initialValue, mode);
            if (i == 0)
                first = id;
        }
        return first;
    }

    EventId addEvent (const juce::String& name)
    {
        jassert (findEvent (name) < 0);
        const auto id = numEvents.load (std::memory_order_relaxed);
        jassert (id < maxEvents);
        if (id >= maxEvents)
            return -1;

        events[(size_t) id].name = name;
        numEvents.store (id + 1, std::memory_order_release);
        return id;
    }

    MeterId find (const juce::String& name) const
    {
        for (int i = 0; i < numValues.load (std::memory_order_acquire); ++i)
            if (slots[(size_t) i].name == name)
                return i;
        return -1;
    }

    EventId findEvent (const juce::String& name) const
    {
        for (int i = 0; i < numEvents.load (std::memory_order_acquire); ++i)
            if (events[(size_t) i].name == name)
                return i;
        return -1;
    }

    int getNumValues() const noexcept                 { return numValues.load (std::memory_order_acquire); }
    int getNumEvents() const noexcept                 { return numEvents.load (std::memory_order_acquire); }
    const juce::String& getName (MeterId id) const    { return slots[(size_t) juce::jlimit (0, maxValues - 1, id)].name; }
    const juce::String& getEventName (EventId id) const { return events[(size_t) juce::jlimit (0, maxEvents - 1, id)].name; }

    //==========================================================================
    // Audio thread: wait-free, allocation-free

    void set (MeterId id, float v) noexcept
    {
        if (! juce::isPositiveAndBelow (id, maxValues))
            return;

        auto& s = slots[(size_t) id];
        s.value.store (v, std::memory_order_relaxed);

        if (s.mode == Mode::peakHold)
        {
            auto cur = s.held.load (std::memory_order_relaxed);
            while (v > cur && ! s.held.compare_exchange_weak (cur, v, std::memory_order_relaxed)) {}
        }
        else if (s.mode == Mode::troughHold)
        {
            auto cur = s.held.load (std::memory_order_relaxed);
            while (v < cur && ! s.held.compare_exchange_weak (cur, v, std::memory_order_relaxed)) {}
        }
    }

    void fire (EventId id) noexcept
    {
        if (juce::isPositiveAndBelow (id, maxEvents))
            events[(size_t) id].count.fetch_add (1, std::memory_order_relaxed);
    }

    //==========================================================================
    // Audio clock + timed events (audio thread)

    /** Advances the audio clock. lockedin::Processor calls this at the top of
        every processBlock; call it yourself only if you don't use Processor. */
    void beginBlock (int numSamples, double sampleRate) noexcept
    {
        blockStart = streamPos;
        streamPos += juce::jmax (0, numSamples);

        const auto s = clockSeq.load (std::memory_order_relaxed);
        clockSeq.store (s + 1, std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_release);
        clockStart.store (blockStart, std::memory_order_relaxed);
        clockLength.store (juce::jmax (1, numSamples), std::memory_order_relaxed);
        clockRate.store (sampleRate > 0.0 ? sampleRate : 44100.0, std::memory_order_relaxed);
        clockWallMs.store (juce::Time::getMillisecondCounterHiRes(), std::memory_order_relaxed);
        clockSeq.store (s + 2, std::memory_order_release);
    }

    /** Audio-clock sample of the first sample in the current block. */
    int64_t getBlockStart() const noexcept      { return blockStart; }

    /** Posts a timed event at sampleOffset within the current block. The offset
        may be beyond the block (or negative) to announce something planned for
        later. Also counts as a fire(), so brain.trigger().onEvent() works too.
        Wait-free; returns false (and drops the event) if the UI is not draining
        the queue (editor closed) and it is full. */
    bool post (EventId id, int sampleOffset, float value = 0.0f, int32_t data = 0) noexcept
    {
        return postAt (id, blockStart + sampleOffset, value, data);
    }

    /** Same as post(), with an absolute audio-clock time. */
    bool postAt (EventId id, int64_t audioTime, float value = 0.0f, int32_t data = 0) noexcept
    {
        fire (id);

        const auto w = timedWrite.load (std::memory_order_relaxed);
        const auto r = timedRead.load (std::memory_order_acquire);
        if (w - r >= (uint32_t) timedCapacity)
            return false;

        timedQueue[(size_t) (w % (uint32_t) timedCapacity)] = { id, audioTime, value, data };
        timedWrite.store (w + 1, std::memory_order_release);
        return true;
    }

    /** Puts every value back to its initial value (e.g. in releaseResources / reset). */
    void resetValues() noexcept
    {
        for (int i = 0; i < getNumValues(); ++i)
        {
            auto& s = slots[(size_t) i];
            s.value.store (s.initial, std::memory_order_relaxed);
            s.held.store (emptyHold (s.mode), std::memory_order_relaxed);
        }
    }

    //==========================================================================
    // UI thread

    /** Current raw value without consuming any peak hold. */
    float read (MeterId id) const noexcept
    {
        return juce::isPositiveAndBelow (id, maxValues) ? slots[(size_t) id].value.load (std::memory_order_relaxed) : 0.0f;
    }

    /** Consumes peak/trough holds and event counts. Call from ONE place only
        (the Editor does it for you once per frame). */
    void takeSnapshot (MeterSnapshot& snap) noexcept
    {
        snap.bus = this;
        const auto nv = getNumValues();

        for (int i = 0; i < nv; ++i)
        {
            auto& s = slots[(size_t) i];
            float v = s.value.load (std::memory_order_relaxed);

            if (s.mode != Mode::latest)
            {
                const auto held = s.held.exchange (emptyHold (s.mode), std::memory_order_relaxed);
                if (! std::isinf (held))   // something was written since the last snapshot
                    v = held;
            }

            snap.values[(size_t) i] = v;
        }

        const auto ne = getNumEvents();
        for (int i = 0; i < ne; ++i)
        {
            const auto now = events[(size_t) i].count.load (std::memory_order_relaxed);
            snap.eventsFired[(size_t) i] = now - lastEventCounts[(size_t) i];   // wraps correctly
            lastEventCounts[(size_t) i] = now;
        }

        // Timed events (anything beyond maxTimed waits for the next snapshot)
        snap.numTimed = 0;
        {
            auto r = timedRead.load (std::memory_order_relaxed);
            const auto w = timedWrite.load (std::memory_order_acquire);
            while (r != w && snap.numTimed < MeterSnapshot::maxTimed)
            {
                snap.timed[(size_t) snap.numTimed++] = timedQueue[(size_t) (r % (uint32_t) timedCapacity)];
                ++r;
            }
            timedRead.store (r, std::memory_order_release);
        }

        estimateAudioNow (snap, juce::Time::getMillisecondCounterHiRes());
    }

    /** Clock estimate used by takeSnapshot (public so it can be tested with a fake wall clock). */
    void estimateAudioNow (MeterSnapshot& snap, double wallNowMs) noexcept
    {
        int64_t start = 0;
        int length = 1;
        double rate = 44100.0, wall = 0.0;
        uint32_t s1 = 0, s2 = 0;
        int spins = 0;

        do
        {
            s1 = clockSeq.load (std::memory_order_acquire);
            start = clockStart.load (std::memory_order_relaxed);
            length = clockLength.load (std::memory_order_relaxed);
            rate = clockRate.load (std::memory_order_relaxed);
            wall = clockWallMs.load (std::memory_order_relaxed);
            std::atomic_thread_fence (std::memory_order_acquire);
            s2 = clockSeq.load (std::memory_order_relaxed);
        }
        while ((s1 != s2 || (s1 & 1u) != 0) && ++spins < 64);

        snap.sampleRate = rate;

        if (s1 == 0)   // audio has never run
        {
            snap.audioRunning = false;
            snap.audioNow = lastAudioNow;
            return;
        }

        // While block N is being processed, the device is playing block N-1.
        const auto elapsedMs = wallNowMs - wall;
        const auto elapsed = (int64_t) (elapsedMs * rate / 1000.0);
        auto heard = start - length + juce::jlimit<int64_t> (0, length, elapsed);

        // Host stopped calling process (bypass, offline render in another thread, stall)
        snap.audioRunning = elapsedMs < 250.0 + 2000.0 * length / rate;

        if (heard < lastAudioNow && lastAudioNow - heard < (int64_t) rate)
            heard = lastAudioNow;   // jitter: never step backwards (a big jump back is a real reset)

        lastAudioNow = heard;
        snap.audioNow = heard;
    }

    static constexpr int timedCapacity = 1024;

private:
    static float emptyHold (Mode m) noexcept
    {
        return m == Mode::troughHold ? std::numeric_limits<float>::infinity()
                                     : -std::numeric_limits<float>::infinity();
    }

    struct Slot
    {
        std::atomic<float> value { 0.0f };
        std::atomic<float> held { 0.0f };
        Mode mode = Mode::latest;
        float initial = 0.0f;
        juce::String name;            // never touched by the audio thread
    };

    struct Event
    {
        std::atomic<uint32_t> count { 0 };
        juce::String name;
    };

    std::array<Slot, maxValues> slots;
    std::array<Event, maxEvents> events;
    std::array<uint32_t, maxEvents> lastEventCounts {};   // UI-side only
    std::atomic<int> numValues { 0 }, numEvents { 0 };

    // Audio clock (written by the audio thread, read by the UI through a seqlock)
    int64_t streamPos = 0, blockStart = 0;                 // audio thread only
    std::atomic<uint32_t> clockSeq { 0 };
    std::atomic<int64_t> clockStart { 0 };
    std::atomic<int> clockLength { 1 };
    std::atomic<double> clockRate { 44100.0 }, clockWallMs { 0.0 };
    int64_t lastAudioNow = 0;                              // UI only

    // Timed events: single producer (audio), single consumer (UI snapshot)
    std::array<TimedEvent, (size_t) timedCapacity> timedQueue {};
    std::atomic<uint32_t> timedWrite { 0 }, timedRead { 0 };

    JUCE_DECLARE_NON_COPYABLE (MeterBus)
};

inline float MeterSnapshot::get (const juce::String& name) const
{
    return bus != nullptr ? (*this)[bus->find (name)] : 0.0f;
}

} // namespace lockedin
