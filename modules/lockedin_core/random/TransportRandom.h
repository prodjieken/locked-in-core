#pragma once

namespace lockedin
{

/**
    Deterministic "random" for the audio thread.

    Normal random generators depend on how many times they were called, which
    depends on the host's block size, so a bounce (big blocks) sounds different
    from playback (small blocks). TransportRandom never keeps a running state.
    Every draw is a hash of (instance seed, stream, position on the timeline),
    so the same spot in the song always gives the same number.

    Two kinds of draw:

      Musical grid (preferred for musical decisions): keyed by the host's PPQ
      position, quantised to a grid, so the result is identical at any block
      size AND any sample rate.

          sixteenths.process (random, [&] (int offset, int64_t tick)   // member: GridClock sixteenths { 0.25 };
          {
              if (random.chanceForTick (tick, 0.3f, 1))   // stream 1
                  triggerMeow (offset);
          });

      Sample position: keyed by the host's timeline sample position, for
      per-sample jitter. Identical at any block size (sample rate must match).

          const float wobble = random.uniformAt (i, 2);   // stream 2

    With no transport (stopped, or a host without a playhead) it falls back to
    an internal sample counter, so behaviour is still varied, just not
    tied to the song.

    The seed is per plugin instance and is saved in the session by
    lockedin::Processor, so two copies on different tracks don't act
    identically, and a reopened session behaves exactly the same.
*/
class TransportRandom
{
public:
    TransportRandom() = default;

    void setSeed (uint64_t newSeed) noexcept    { seed = newSeed; }
    uint64_t getSeed() const noexcept           { return seed; }

    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        freeRunSample = 0;
    }

    //==========================================================================
    /** Call once at the top of every processBlock (lockedin::Processor does it). */
    void beginBlock (juce::AudioPlayHead* playHead, int numSamples) noexcept
    {
        blockLength = juce::jmax (0, numSamples);
        transportDriven = false;
        bpm = 120.0;

        if (playHead != nullptr)
        {
            if (const auto pos = playHead->getPosition())
            {
                if (const auto b = pos->getBpm(); b.hasValue() && *b > 0.0)
                    bpm = *b;

                if (pos->getIsPlaying() || pos->getIsRecording())
                {
                    const auto ppq = pos->getPpqPosition();
                    const auto samples = pos->getTimeInSamples();

                    if (samples.hasValue())
                        startSample = *samples;
                    else if (ppq.hasValue())
                        startSample = (int64_t) std::llround (*ppq * 60.0 / bpm * sampleRate);

                    if (ppq.hasValue())
                        startPpq = *ppq;
                    else if (samples.hasValue())
                        startPpq = (double) *samples / sampleRate * bpm / 60.0;

                    transportDriven = ppq.hasValue() || samples.hasValue();
                }
            }
        }

        if (! transportDriven)
        {
            startSample = freeRunSample;
            startPpq = (double) freeRunSample / sampleRate * bpm / 60.0;
        }

        ppqPerSample = bpm / (60.0 * sampleRate);
        freeRunSample += blockLength;
    }

    bool isTransportDriven() const noexcept     { return transportDriven; }
    int64_t getBlockStartSample() const noexcept { return startSample; }
    double getBlockStartPpq() const noexcept    { return startPpq; }
    double getPpqPerSample() const noexcept     { return ppqPerSample; }
    double getBpm() const noexcept              { return bpm; }

    //==========================================================================
    // Sample-position draws

    /** Uniform in [0, 1) for the sample at offset within the current block. */
    float uniformAt (int sampleOffset, uint32_t stream = 0) const noexcept
    {
        return toUnit (hash (seed, stream, startSample + sampleOffset));
    }

    /** Uniform in [-1, 1). */
    float bipolarAt (int sampleOffset, uint32_t stream = 0) const noexcept
    {
        return uniformAt (sampleOffset, stream) * 2.0f - 1.0f;
    }

    bool chanceAt (int sampleOffset, float probability, uint32_t stream = 0) const noexcept
    {
        return uniformAt (sampleOffset, stream) < probability;
    }

    //==========================================================================
    // Musical-grid draws

    /** Which grid cell a PPQ position falls in, e.g. gridBeats = 0.25 for 16ths. */
    static int64_t tickIndex (double ppq, double gridBeats) noexcept
    {
        return (int64_t) std::floor (ppq / gridBeats + 1.0e-9);
    }

    float uniformForTick (int64_t tick, uint32_t stream = 0) const noexcept
    {
        return toUnit (hash (seed, stream ^ 0x5bd1e995u, tick));
    }

    bool chanceForTick (int64_t tick, float probability, uint32_t stream = 0) const noexcept
    {
        return uniformForTick (tick, stream) < probability;
    }

    int intForTick (int64_t tick, int maxExclusive, uint32_t stream = 0) const noexcept
    {
        return maxExclusive > 0 ? juce::jmin (maxExclusive - 1, (int) (uniformForTick (tick, stream) * (float) maxExclusive)) : 0;
    }

    /** Calls fn (sampleOffset, tickIndex) for every grid line that starts inside
        this block. Tick indices are stable across block sizes and sample rates. */
    template <typename Fn>
    void forEachGridTick (double gridBeats, Fn&& fn) const
    {
        if (gridBeats <= 0.0 || blockLength <= 0)
            return;

        const auto endPpq = startPpq + ppqPerSample * blockLength;
        auto tick = (int64_t) std::ceil (startPpq / gridBeats - 1.0e-9);

        for (;; ++tick)
        {
            const auto tickPpq = (double) tick * gridBeats;
            if (tickPpq >= endPpq - 1.0e-9)
                break;

            const auto offset = juce::jlimit (0, blockLength - 1, (int) std::floor ((tickPpq - startPpq) / ppqPerSample + 0.5));
            fn (offset, tick);
        }
    }

    //==========================================================================
    /** Stateless 64-bit mix (splitmix64 finaliser over the combined key). */
    static uint64_t hash (uint64_t seed, uint64_t stream, int64_t position) noexcept
    {
        auto x = seed ^ (stream * 0x9E3779B97F4A7C15ull) ^ ((uint64_t) position * 0xD1B54A32D192ED03ull);
        x = mix (x + 0x9E3779B97F4A7C15ull);
        return mix (x ^ (uint64_t) position);
    }

    static float toUnit (uint64_t h) noexcept
    {
        return (float) (h >> 40) * (1.0f / 16777216.0f);   // 24 bits -> [0, 1)
    }

private:
    static uint64_t mix (uint64_t z) noexcept
    {
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    uint64_t seed = 0x4c6f636b6564496eull;   // "LockedIn"
    double sampleRate = 44100.0, bpm = 120.0, startPpq = 0.0, ppqPerSample = 0.0;
    int64_t startSample = 0, freeRunSample = 0;
    int blockLength = 0;
    bool transportDriven = false;
};

//==============================================================================
/**
    Fires once per grid line (e.g. every 16th note) while playing, and never fires
    a grid line twice or skips one across block boundaries (tempo automation can
    make the host's next block start a hair before or after where the previous
    block ended). Keep one GridClock per grid size, as a processor member.

        GridClock sixteenths { 0.25 };
        ...
        sixteenths.process (random, [&] (int offset, int64_t tick)
        {
            if (random.chanceForTick (tick, 0.25f))
                gagSounds.trigger (meowId, offset);
        });
*/
class GridClock
{
public:
    explicit GridClock (double gridBeats = 0.25) : grid (gridBeats) {}

    void setGrid (double gridBeats) noexcept   { grid = gridBeats; reset(); }
    double getGrid() const noexcept            { return grid; }
    void reset() noexcept                      { hasLast = false; }

    template <typename Fn>
    void process (const TransportRandom& r, Fn&& fn)
    {
        if (grid <= 0.0)
            return;

        const auto firstTick = (int64_t) std::ceil (r.getBlockStartPpq() / grid - 1.0e-9);

        // A grid line that fell into the crack between two blocks: fire it now.
        if (hasLast && firstTick == lastTick + 2)
        {
            lastTick = lastTick + 1;
            fn (0, lastTick);
        }

        r.forEachGridTick (grid, [&] (int offset, int64_t tick)
        {
            if (hasLast && (tick == lastTick || tick == lastTick - 1))
                return;   // already fired at the end of the previous block

            hasLast = true;
            lastTick = tick;
            fn (offset, tick);
        });
    }

private:
    double grid;
    int64_t lastTick = 0;
    bool hasLast = false;
};

} // namespace lockedin
