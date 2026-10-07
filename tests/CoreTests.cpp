#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

#include <lockedin_core/meters/MeterBus.h>
#include <lockedin_core/meters/Levels.h>
#include <lockedin_core/random/TransportRandom.h>
#include <lockedin_core/character/CharacterDef.h>
#include <lockedin_core/character/CharacterBrain.h>
#include <lockedin_core/character/CharacterDef.cpp>
#include <lockedin_core/character/CharacterBrain.cpp>

using namespace lockedin;

//==============================================================================
struct MeterBusTests : juce::UnitTest
{
    MeterBusTests() : juce::UnitTest ("MeterBus", "lockedin") {}

    void runTest() override
    {
        beginTest ("standard slots exist");
        {
            MeterBus bus;
            expectEquals (bus.find ("inputRms"), MeterBus::Std::inputRms);
            expectEquals (bus.find ("gainReduction"), MeterBus::Std::gainReduction);
            expectEquals (bus.getNumValues(), MeterBus::Std::count);
        }

        beginTest ("latest vs peak hold");
        {
            MeterBus bus;
            const auto lat = bus.add ("lat", 0.0f, MeterBus::Mode::latest);
            const auto pk = bus.add ("pk", -100.0f, MeterBus::Mode::peakHold);
            MeterSnapshot s;

            bus.set (lat, 5.0f); bus.set (lat, 1.0f);
            bus.set (pk, -20.0f); bus.set (pk, -3.0f); bus.set (pk, -30.0f);
            bus.takeSnapshot (s);
            expectEquals (s[lat], 1.0f);
            expectEquals (s[pk], -3.0f);   // transient survived

            bus.takeSnapshot (s);           // nothing new written: falls back to latest write
            expectEquals (s[pk], -30.0f);
            expectEquals (s.get ("pk"), -30.0f);
        }

        beginTest ("events count between snapshots");
        {
            MeterBus bus;
            const auto e = bus.addEvent ("clip");
            MeterSnapshot s;
            bus.fire (e); bus.fire (e); bus.fire (e);
            bus.takeSnapshot (s);
            expectEquals ((int) s.fired (e), 3);
            bus.takeSnapshot (s);
            expectEquals ((int) s.fired (e), 0);
        }

        beginTest ("timed events carry block-relative time and payload");
        {
            MeterBus bus;
            const auto step = bus.addEvent ("step");
            MeterSnapshot s;

            bus.beginBlock (512, 48000.0);            // block 0: samples 0..511
            bus.post (step, 100, 0.5f, 7);
            bus.beginBlock (512, 48000.0);            // block 1: samples 512..1023
            bus.post (step, 10, 0.0f, 3);
            bus.post (step, 4000, 1.0f, -1);          // planned ahead
            bus.takeSnapshot (s);

            expectEquals (s.numTimed, 3);
            expectEquals ((int) s.timed[0].time, 100);
            expectEquals (s.timed[0].data, 7);
            expectEquals (s.timed[0].value, 0.5f);
            expectEquals ((int) s.timed[1].time, 522);
            expectEquals ((int) s.timed[2].time, 4512);
            expectEquals ((int) s.fired (step), 3);   // posts count as fires

            bus.takeSnapshot (s);
            expectEquals (s.numTimed, 0);
        }

        beginTest ("timed queue drops instead of blocking when nobody drains");
        {
            MeterBus bus;
            const auto e = bus.addEvent ("e");
            bus.beginBlock (64, 48000.0);
            int accepted = 0;
            for (int i = 0; i < MeterBus::timedCapacity + 50; ++i)
                accepted += bus.post (e, 0, 0.0f, i) ? 1 : 0;
            expectEquals (accepted, MeterBus::timedCapacity);

            MeterSnapshot s;
            int total = 0;
            for (int k = 0; k < 10; ++k) { bus.takeSnapshot (s); total += s.numTimed; }
            expectEquals (total, MeterBus::timedCapacity);
            expect (bus.post (e, 0), "accepts again once drained");
        }

        beginTest ("audio clock estimate");
        {
            MeterBus bus;
            MeterSnapshot s;
            bus.estimateAudioNow (s, 0.0);
            expect (! s.audioRunning);

            bus.beginBlock (480, 48000.0);            // 10 ms blocks
            bus.beginBlock (480, 48000.0);            // block start 480, published now
            const auto wall = juce::Time::getMillisecondCounterHiRes();

            bus.estimateAudioNow (s, wall);           // right at the callback: hearing the previous block's start
            expect (s.audioRunning);
            expectEquals ((int) s.audioNow, 0);

            bus.estimateAudioNow (s, wall + 5.0);     // halfway through
            expectEquals ((int) s.audioNow, 240);

            bus.estimateAudioNow (s, wall + 50.0);    // late callback: clamp, don't run ahead of processed audio
            expectEquals ((int) s.audioNow, 480);

            bus.estimateAudioNow (s, wall + 2.0);     // jitter never steps backwards
            expectEquals ((int) s.audioNow, 480);
            expectEquals (s.msUntil (480 + 4800), 100.0);

            bus.estimateAudioNow (s, wall + 5000.0);
            expect (! s.audioRunning);
        }

        beginTest ("concurrent timed producer and consumer");
        {
            MeterBus bus;
            const auto e = bus.addEvent ("e");
            constexpr int n = 100000;
            std::atomic<bool> done { false };

            std::thread audio ([&]
            {
                int sent = 0;
                while (sent < n)
                {
                    bus.beginBlock (32, 48000.0);
                    if (bus.post (e, 0, 0.0f, sent))
                        ++sent;
                    else
                        std::this_thread::yield();
                }
                done = true;
            });

            MeterSnapshot s;
            int expected = 0;
            bool inOrder = true;
            while (! done || expected < n)
            {
                bus.takeSnapshot (s);
                for (int i = 0; i < s.numTimed; ++i)
                    inOrder = inOrder && s.timed[(size_t) i].data == expected++;
                if (done && s.numTimed == 0 && expected < n)
                    break;
            }
            audio.join();
            expect (inOrder, "events arrive complete and in order");
            expectEquals (expected, n);
        }

        beginTest ("concurrent writer and reader");
        {
            MeterBus bus;
            const auto pk = bus.add ("pk", 0.0f, MeterBus::Mode::peakHold);
            const auto ev = bus.addEvent ("tick");
            std::atomic<bool> stop { false };
            constexpr int writes = 200000;

            std::thread audio ([&]
            {
                for (int i = 0; i < writes; ++i)
                {
                    bus.set (pk, (float) (i % 100));
                    bus.fire (ev);
                }
                stop = true;
            });

            MeterSnapshot s;
            uint64_t totalEvents = 0;
            bool inRange = true;
            while (! stop)
            {
                bus.takeSnapshot (s);
                totalEvents += s.fired (ev);
                inRange &= s[pk] >= 0.0f && s[pk] <= 99.0f;
            }
            audio.join();
            bus.takeSnapshot (s);
            totalEvents += s.fired (ev);

            expect (inRange);
            expectEquals ((int) totalEvents, writes);   // no event lost
        }
    }
};

//==============================================================================
struct LevelTests : juce::UnitTest
{
    LevelTests() : juce::UnitTest ("LevelMeter", "lockedin") {}

    static juce::AudioBuffer<float> sine (int n, double sr, float amp)
    {
        juce::AudioBuffer<float> b (2, n);
        for (int i = 0; i < n; ++i)
            for (int ch = 0; ch < 2; ++ch)
                b.setSample (ch, i, amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * i / sr));
        return b;
    }

    static float runInBlocks (const juce::AudioBuffer<float>& src, int blockSize)
    {
        LevelMeter m;
        m.prepare (48000.0);
        for (int start = 0; start < src.getNumSamples(); start += blockSize)
            m.process (src, start, juce::jmin (blockSize, src.getNumSamples() - start));
        return m.getRmsDb();
    }

    void runTest() override
    {
        beginTest ("sine RMS reads about -3 dB below peak");
        const auto s = sine (48000, 48000.0, 0.5f);        // peak -6 dBFS -> RMS ~ -9 dBFS
        expectWithinAbsoluteError (runInBlocks (s, 512), -9.03f, 0.3f);

        beginTest ("independent of block size");
        expectWithinAbsoluteError (runInBlocks (s, 32), runInBlocks (s, 4096), 1.0e-4f);

        beginTest ("silence floor");
        juce::AudioBuffer<float> z (2, 4800);
        z.clear();
        expectEquals (runInBlocks (z, 480), -100.0f);
    }
};

//==============================================================================
struct FakePlayHead : juce::AudioPlayHead
{
    bool playing = true, provideSamples = true, providePpq = true;
    double bpm = 128.0, sampleRate = 48000.0;
    int64_t sample = 0;

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setIsPlaying (playing);
        p.setBpm (bpm);
        if (provideSamples)
            p.setTimeInSamples (sample);
        if (providePpq)
            p.setPpqPosition ((double) sample / sampleRate * bpm / 60.0);
        return p;
    }
};

struct RandomTests : juce::UnitTest
{
    RandomTests() : juce::UnitTest ("TransportRandom", "lockedin") {}

    struct Hit { int64_t tick; int64_t absSample; bool decision; };

    // Renders `totalSamples` of timeline in blocks from `sizes` (cycled) and records every grid hit
    static std::vector<Hit> render (std::vector<int> sizes, double sr, int64_t totalSamples, double grid, int64_t startAt = 0)
    {
        FakePlayHead ph;
        ph.sampleRate = sr;
        ph.sample = startAt;

        TransportRandom r;
        r.setSeed (1234);
        r.prepare (sr);
        GridClock clock { grid };
        std::vector<Hit> hits;

        size_t k = 0;
        while (ph.sample < startAt + totalSamples)
        {
            const int n = sizes[k++ % sizes.size()];
            r.beginBlock (&ph, n);
            clock.process (r, [&] (int offset, int64_t tick)
            {
                hits.push_back ({ tick, ph.sample + offset, r.chanceForTick (tick, 0.5f, 7) });
            });
            ph.sample += n;
        }
        return hits;
    }

    void runTest() override
    {
        beginTest ("sample draws don't depend on block size");
        {
            FakePlayHead ph;
            TransportRandom a, b;
            a.setSeed (99); b.setSeed (99);
            a.prepare (48000.0); b.prepare (48000.0);

            std::vector<float> va, vb;
            for (ph.sample = 0; ph.sample < 4096; ph.sample += 64)
            {
                a.beginBlock (&ph, 64);
                for (int i = 0; i < 64; ++i) va.push_back (a.uniformAt (i, 3));
            }
            for (ph.sample = 0; ph.sample < 4096; ph.sample += 1024)
            {
                b.beginBlock (&ph, 1024);
                for (int i = 0; i < 1024; ++i) vb.push_back (b.uniformAt (i, 3));
            }
            expect (va == vb);
        }

        beginTest ("grid hits identical for playback-sized and bounce-sized blocks");
        {
            const auto total = (int64_t) (48000 * 8);
            const auto play = render ({ 64 }, 48000.0, total, 0.25);
            const auto bounce = render ({ 4096 }, 48000.0, total, 0.25);
            const auto ragged = render ({ 17, 301, 1024, 5, 999 }, 48000.0, total, 0.25);

            expect (play.size() > 60);
            expectEquals ((int) bounce.size(), (int) play.size());
            expectEquals ((int) ragged.size(), (int) play.size());

            bool same = true;
            for (size_t i = 0; i < play.size() && i < bounce.size() && i < ragged.size(); ++i)
            {
                same &= play[i].tick == bounce[i].tick && play[i].decision == bounce[i].decision;
                same &= play[i].tick == ragged[i].tick && play[i].decision == ragged[i].decision;
                same &= std::abs (play[i].absSample - bounce[i].absSample) <= 1;
                same &= std::abs (play[i].absSample - ragged[i].absSample) <= 1;
            }
            expect (same);
        }

        beginTest ("grid decisions identical across sample rates");
        {
            const auto a = render ({ 512 }, 44100.0, 44100 * 4, 0.25);
            const auto b = render ({ 512 }, 96000.0, 96000 * 4, 0.25);
            expectEquals ((int) a.size(), (int) b.size());
            bool same = a.size() == b.size();
            for (size_t i = 0; same && i < a.size(); ++i)
                same = a[i].tick == b[i].tick && a[i].decision == b[i].decision;
            expect (same);
        }

        beginTest ("no tick fired twice, none skipped");
        {
            const auto hits = render ({ 33, 4096, 7, 512 }, 48000.0, 48000 * 10, 0.25, 12345);
            bool contiguous = true;
            for (size_t i = 1; i < hits.size(); ++i)
                contiguous &= hits[i].tick == hits[i - 1].tick + 1;
            expect (contiguous);
        }

        beginTest ("falls back to a sample counter without transport");
        {
            TransportRandom r;
            r.prepare (48000.0);
            r.beginBlock (nullptr, 256);
            expect (! r.isTransportDriven());
            expectEquals ((int) r.getBlockStartSample(), 0);
            r.beginBlock (nullptr, 256);
            expectEquals ((int) r.getBlockStartSample(), 256);

            FakePlayHead stopped;
            stopped.playing = false;
            r.beginBlock (&stopped, 256);
            expect (! r.isTransportDriven());
        }

        beginTest ("different seeds give different numbers, distribution sane");
        {
            double sum = 0;
            int differs = 0;
            for (int64_t i = 0; i < 20000; ++i)
            {
                const auto a = TransportRandom::toUnit (TransportRandom::hash (1, 0, i));
                const auto b = TransportRandom::toUnit (TransportRandom::hash (2, 0, i));
                sum += a;
                differs += a != b ? 1 : 0;
                expect (a >= 0.0f && a < 1.0f);
            }
            expectWithinAbsoluteError (sum / 20000.0, 0.5, 0.02);
            expect (differs > 19900);
        }
    }
};

//==============================================================================
static const char* testJson = R"({
  "default": "idle",
  "states": {
    "idle":      { "sheet": "idle.png", "frames": 8, "fps": 8, "loop": true },
    "listening": { "sheet": "listening.png", "frames": 6, "fps": 10, "loop": true },
    "loud":      { "sheet": "art/loud.png", "frames": 4, "fps": 12, "loop": true,
                   "fx": { "shake": 5, "tint": "#ff4040", "tintAmount": 0.4 } },
    "startled":  { "sheet": "startled.png", "frames": 5, "fps": 10, "loop": false, "then": "loud" }
  }
})";

struct CharacterTests : juce::UnitTest
{
    CharacterTests() : juce::UnitTest ("Character", "lockedin") {}

    // Runs the brain at 60 fps for `ms` with inputRms held at `db`
    static void run (CharacterBrain& brain, MeterBus& bus, float db, double ms)
    {
        MeterSnapshot s;
        for (double t = 0; t < ms; t += 1000.0 / 60.0)
        {
            bus.set (MeterBus::Std::inputRms, db);
            bus.takeSnapshot (s);
            brain.tick (s, 1000.0 / 60.0);
        }
    }

    static void setupRules (CharacterBrain& b)
    {
        b.when ("loud").above ("inputRms", -12.0f).releaseBelow (-16.0f).forMs (100).releaseAfterMs (300);
        b.when ("listening").above ("inputRms", -50.0f).releaseBelow (-56.0f).forMs (50).releaseAfterMs (400);
        b.intensity ("loud").from ("inputRms", -12.0f, 0.0f).smoothingMs (0);
    }

    void runTest() override
    {
        beginTest ("JSON parses");
        CharacterDef def;
        auto r = CharacterDef::parse (testJson, def);
        expect (r.wasOk(), r.getErrorMessage());
        expectEquals ((int) def.states.size(), 4);
        expectEquals (def.defaultState, juce::String ("idle"));
        expect (def.find ("startled")->then == "loud");
        expect (! def.find ("startled")->loop);
        expect (def.find ("loud")->tintArgb == 0xffff4040u);
        expectEquals (def.find ("loud")->shakePx, 5.0f);

        beginTest ("JSON errors are explained");
        {
            CharacterDef bad;
            expect (CharacterDef::parse ("{ \"states\": {} }", bad).failed());
            expect (CharacterDef::parse (R"({"states":{"a":{"loop":false,"then":"nope"}}})", bad).getErrorMessage().contains ("nope"));
            expect (CharacterDef::parse (R"({"states":{"a":{"loop":true,"then":"a"}}})", bad).failed());
            expect (CharacterDef::parse (R"({"default":"x","states":{"a":{}}})", bad).failed());
            expect (CharacterDef::parse ("not json", bad).failed());
        }

        beginTest ("idle -> listening -> loud -> back down");
        {
            MeterBus bus;
            CharacterBrain brain (def, bus);
            setupRules (brain);

            run (brain, bus, -90.0f, 500);
            expectEquals (brain.getStateName(), juce::String ("idle"));

            run (brain, bus, -30.0f, 300);
            expectEquals (brain.getStateName(), juce::String ("listening"));

            run (brain, bus, -6.0f, 300);
            expectEquals (brain.getStateName(), juce::String ("loud"));
            expectWithinAbsoluteError (brain.getIntensity(), 0.5f, 0.01f);

            run (brain, bus, -14.0f, 1000);     // inside the hysteresis band: stays loud
            expectEquals (brain.getStateName(), juce::String ("loud"));

            run (brain, bus, -30.0f, 600);
            expectEquals (brain.getStateName(), juce::String ("listening"));

            run (brain, bus, -90.0f, 800);
            expectEquals (brain.getStateName(), juce::String ("idle"));
        }

        beginTest ("no flicker when level wobbles around the threshold");
        {
            MeterBus bus;
            CharacterBrain brain (def, bus);
            setupRules (brain);
            int changes = 0;
            brain.onStateChange = [&] (auto&, auto&) { ++changes; };

            run (brain, bus, -6.0f, 400);     // loud
            expectEquals (brain.getStateName(), juce::String ("loud"));
            changes = 0;

            for (int i = 0; i < 40; ++i)      // 12 s of 150 ms at -11 dB / 150 ms at -13 dB around the -12 dB threshold
                run (brain, bus, (i % 2) ? -11.0f : -13.0f, 150);

            expectEquals (changes, 0);

            // And a fast per-frame wobble while listening never promotes to loud
            run (brain, bus, -30.0f, 1000);
            changes = 0;
            MeterSnapshot s;
            for (int i = 0; i < 600; ++i)
            {
                bus.set (MeterBus::Std::inputRms, (i % 2) ? -11.0f : -13.0f);
                bus.takeSnapshot (s);
                brain.tick (s, 1000.0 / 60.0);
            }
            expectEquals (changes, 0);
        }

        beginTest ("brief spike shorter than forMs is ignored");
        {
            MeterBus bus;
            CharacterBrain brain (def, bus);
            setupRules (brain);
            run (brain, bus, -30.0f, 300);
            run (brain, bus, -3.0f, 50);      // 50 ms spike, loud needs 100 ms
            run (brain, bus, -30.0f, 100);
            expectEquals (brain.getStateName(), juce::String ("listening"));
        }

        beginTest ("one-shot on event plays through then returns to its then-state");
        {
            MeterBus bus;
            const auto clip = bus.addEvent ("clip");
            CharacterBrain brain (def, bus);
            setupRules (brain);
            brain.trigger ("startled").onEvent ("clip").cooldownMs (1000);

            juce::StringArray seen;
            brain.onStateChange = [&] (auto&, const juce::String& to) { seen.add (to); };

            run (brain, bus, -90.0f, 300);
            bus.fire (clip);
            run (brain, bus, -90.0f, 1000.0 / 60.0);
            expectEquals (brain.getStateName(), juce::String ("startled"));
            expect (brain.isPlayingOneShot());

            bus.fire (clip);                   // within cooldown: ignored
            run (brain, bus, -90.0f, 200);
            expectEquals (brain.getStateName(), juce::String ("startled"));
            expect (brain.getFrame() > 0 && brain.getFrame() < 5);

            run (brain, bus, -90.0f, 400);     // 500 ms one-shot finished -> "then": loud
            expect (seen.contains ("loud"));

            run (brain, bus, -90.0f, 1500);    // and rules take over again
            expectEquals (brain.getStateName(), juce::String ("idle"));
        }

        beginTest ("whenRises re-arms only after dropping below rearm level");
        {
            MeterBus bus;
            CharacterBrain brain (def, bus);
            brain.trigger ("startled").whenRises ("inputRms", -3.0f).rearmBelow (-10.0f);
            int fires = 0;
            brain.onStateChange = [&] (auto&, const juce::String& to) { if (to == "startled") ++fires; };

            run (brain, bus, -20.0f, 100);
            run (brain, bus, -1.0f, 700);      // fires once, stays above
            run (brain, bus, -5.0f, 700);      // dipped but not below rearm
            run (brain, bus, -1.0f, 700);
            expectEquals (fires, 1);
            run (brain, bus, -20.0f, 100);     // rearm
            run (brain, bus, -1.0f, 100);
            expectEquals (fires, 2);
        }

        beginTest ("loop frame counter");
        {
            MeterBus bus;
            CharacterBrain brain (def, bus);   // idle: 8 frames @ 8 fps
            run (brain, bus, -90.0f, 1000.0 + 500.0);
            expectEquals (brain.getFrame(), 4);
        }
    }
};

static MeterBusTests meterBusTests;
static LevelTests levelTests;
static RandomTests randomTests;
static CharacterTests characterTests;

int main()
{
    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.runTestsInCategory ("lockedin");

    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        failures += runner.getResult (i)->failures;

    std::cout << (failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED") << " (" << failures << " failures)" << std::endl;
    return failures == 0 ? 0 : 1;
}
