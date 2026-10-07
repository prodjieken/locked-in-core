# locked-in-core

Shared foundation for the **Locked In** character plugins: a cartoon character on the UI
reacts to what the DSP is actually doing.

Each plugin repo pulls this in as a git submodule and makes one `lockedin_add_plugin()` call.

| Piece | What it does |
|---|---|
| `lockedin_add_plugin()` | CMake: VST3 + AU, "Locked In" vendor, `com.lockedin.<name>`, manufacturer `Lkin`, universal Mac, static MSVC runtime, assets → BinaryData, INSTALL.md |
| `MeterBus` | Lock-free audio → UI values (`set`), events (`fire`) and sample-stamped events with a payload (`post`). Peak-hold mode so transients survive until the 60 Hz UI sees them |
| `CharacterBrain` + `CharacterView` | JSON-defined sprite states, C++ rules on MeterBus values with hysteresis, one-shots, per-state intensity → shake/rise/squash/tint. Missing art draws labelled placeholder boxes |
| `GagLayer` | UI-only jokes: speech bubble, sliding note, screen shake, DING flash |
| `GagSounds` | Sound effects mixed into the output, **only** when the `gagSounds` toggle is on (default off, never set by presets) |
| `TransportRandom` / `GridClock` | "Random" that's identical on playback and bounce (keyed to song position, not call count) |
| `ParamLayout` / `SmoothedParam` | One-line param declarations with units, skews, text parsing and per-sample smoothing |
| `PresetManager` / `PresetBar` | Factory presets from BinaryData, user presets in `Documents/Locked In/<Plugin>/` |
| `LookAndFeel` | Themed base, filmstrip knob support, `drawKnob()` hook |
| `Processor` / `Editor` | Base classes that wire all of the above. Fixed-size editor with a 75/100/125 % menu |

JUCE **8.0.15** (fetched automatically). C++17. macOS 11+ (arm64 + x86_64), Windows 10+ x64.
Formats: **VST3 + AU only** (no AAX, no Standalone).

---

## Starting a new plugin repo

```bash
mkdir trunk && cd trunk && git init
git submodule add https://github.com/prodjieken/locked-in-core.git locked-in-core
```

### `CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.22)
include(locked-in-core/cmake/LockedInDefaults.cmake)   # must come before project()
project(Trunk VERSION 1.0.0)

add_subdirectory(locked-in-core)

lockedin_add_plugin(Trunk
    CODE    Trnk                    # 4 chars, unique per plugin, NEVER change after release
    SOURCES Source/Trunk.h Source/Trunk.cpp
    ASSETS  assets/character.json
            assets/trunk_idle.png assets/trunk_bump.png
            assets/knob.png assets/honk.wav
    PRESETS presets/Default.lipreset presets/Subwoofer.lipreset)
```

Optional arguments: `PRODUCT_NAME "Next Door"` (name with a space; the default is the target name),
`VERSION`, `DESCRIPTION`, `VST3_CATEGORIES`, `INSTRUMENT`, `MIDI_EFFECT`, `NEEDS_MIDI_INPUT`, `NEEDS_MIDI_OUTPUT`.

**Never change after a plugin ships:** `CODE`, `PRODUCT_NAME` (it sets the bundle ID), and parameter IDs.
Changing any of them means existing sessions and presets no longer find the plugin or its settings.

### Build

```bash
cmake -S . -B build -G Ninja         # or -G Xcode / "Visual Studio 17 2022"
cmake --build build --config Release
```

On Mac the build also copies the plugin into `~/Library/Audio/Plug-Ins/{VST3,Components}`,
so you can rescan in FL Studio straight away. Turn that off with `-DLOCKEDIN_COPY_AFTER_BUILD=OFF`.

### CI (`.github/workflows/build.yml` in the plugin repo)

```yaml
on: [push, pull_request, workflow_dispatch]
jobs:
  build:
    uses: prodjieken/locked-in-core/.github/workflows/plugin-build.yml@main
    with:
      target: Trunk
      product-name: Trunk          # PRODUCT_NAME, e.g. "Next Door"
      plugin-code: Trnk
      # au-type: aumu              # for instruments (aumi for MIDI effects)
```

That builds Mac universal + Windows x64, runs `auval` and **pluginval at strictness 5** on VST3 and AU,
and uploads `Trunk-mac.zip` / `Trunk-windows.zip` with `INSTALL.md` inside.
If locked-in-core is a private repo, enable *Settings → Actions → General → Access →
"Accessible from repositories owned by the user"* on locked-in-core so plugin repos can call the workflow.

---

## Writing the plugin

`examples/hello-character` is the complete working reference (about 150 lines). The shape:

### Processor

```cpp
#include <lockedin_core/lockedin_core.h>

class TrunkProcessor : public lockedin::Processor
{
public:
    TrunkProcessor() : Processor (createParams()), drive (smoothed ("drive"))
    {
        bassId  = meters.add ("bassEnergy", -100.0f, lockedin::MeterBus::Mode::peakHold);
        bumpId  = meters.addEvent ("bump");
        honkId  = gagSounds.add ("honk.wav", -12.0f);
    }

    static lockedin::ParamLayout createParams()
    {
        lockedin::ParamLayout p;
        p.decibels ("drive", "Drive", 0.0f, 24.0f, 6.0f);   // smoothing defaults to 20 ms
        p.hertz    ("freq", "Frequency", 30.0f, 200.0f, 60.0f);
        p.percent  ("mix", "Mix", 100.0f);
        p.gagSoundToggle();                                 // only if the plugin has gag sounds
        return p;
    }

    void process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        // ... DSP using drive.next() per sample ...
        meters.set (bassId, bassDb);                          // UI reads it at 60 Hz
        meters.set (lockedin::MeterBus::Std::gainReduction, grDb);
        if (bigHit) { meters.fire (bumpId); gagSounds.trigger (honkId, hitOffset); }
    }

    juce::AudioProcessorEditor* createEditor() override { return new TrunkEditor (*this); }

private:
    lockedin::SmoothedParam& drive;
    lockedin::MeterId bassId;
    lockedin::EventId bumpId;
    int honkId;
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TrunkProcessor(); }
```

`inputRms`, `inputPeak`, `outputRms` and `outputPeak` (dBFS) are filled in for you. Set
`gainReduction` (positive dB) yourself if the plugin compresses.

### Editor + character

```cpp
class TrunkEditor : public lockedin::Editor
{
public:
    TrunkEditor (TrunkProcessor& p)
        : Editor (p, 640, 420, makeTheme()),                    // design size
          brain (loadDef(), p.meters), view (brain)
    {
        // Sustained states. First active rule wins, so list the extreme ones first.
        brain.when ("rattling").above ("bassEnergy", -6.0f).releaseBelow (-10.0f).forMs (150).releaseAfterMs (400);
        brain.when ("vibing").above ("inputRms", -40.0f).releaseBelow (-46.0f);

        // One-shots (loop: false in the JSON, with a "then" state)
        brain.trigger ("bump").onEvent ("bump").cooldownMs (600);

        // Continuous intensity per state, 0..1, drives the JSON "fx"
        brain.intensity ("rattling").from ("bassEnergy", -6.0f, 6.0f);

        brain.onStateChange = [this] (auto& from, auto& to)
        {
            if (to == "rattling") gags().say ("my mirrors!!", { 420.0f, 120.0f });
        };

        content().addAndMakeVisible (view);
        attachCharacter (brain, view);                       // ticked + repainted at 60 Hz
    }

    void layout (juce::Rectangle<int> area) override       { view.setBounds (area.reduced (60)); }
    ...
};
```

Lay everything out in **design coordinates** inside `content()`. The 75/100/125 % size menu scales
the whole thing, so you never handle scaling yourself.

### Character JSON

```json
{
  "default": "idle",
  "states": {
    "idle":     { "sheet": "trunk_idle.png",   "frames": 8, "fps": 8,  "loop": true },
    "rattling": { "sheet": "trunk_rattle.png", "frames": 6, "fps": 16, "loop": true, "columns": 3,
                  "fx": { "shake": 6, "squash": 0.1, "tint": "#ff4030", "tintAmount": 0.4 } },
    "bump":     { "sheet": "trunk_bump.png",   "frames": 5, "fps": 12, "loop": false, "then": "rattling" }
  }
}
```

- Sheets are grids of equal frames, left to right then top to bottom (`columns` defaults to all frames in one row).
- Assets are found by **file name**, so `"art/x.png"` and `"x.png"` mean the same file. File names must be unique.
- A missing sheet draws a coloured box labelled with the state name, frame number and intensity,
  so the rules can be tuned before any art exists.
- Bad JSON fails `CharacterDef::parse` with a message naming the state and field.

### Hysteresis (why the character doesn't flicker)

Three layers, all adjustable per rule:
1. **Separate enter/release thresholds:** `above (-12).releaseBelow (-16)`.
2. **Hold times:** `forMs` (must stay true this long to enter) and `releaseAfterMs` (must stay false this long to leave, default 150 ms).
3. **Minimum dwell:** no state switch until the current state has shown for 120 ms (`brain.setMinimumDwellMs`). One-shots always play to the end.

### Timed events (animation on an exact sample)

`fire()` only counts. When the UI must line something up with the audio (a paw landing on the key
the moment the glitch starts), post a **timed event** with a payload instead:

```cpp
stepId = meters.addEvent ("step");                       // processor constructor
meters.post (stepId, sampleOffset, durationSamples, key);  // audio thread; offset may be past the block to announce plans
```

```cpp
void tick (const lockedin::MeterSnapshot& m, double) override
{
    for (int i = 0; i < m.numTimed; ++i)
        schedule (m.timed[i]);                                  // id, time, value, data
    // m.audioNow = audio-clock sample being heard now; m.msUntil (t) = how long until t is heard
}
```

Times are on the **audio clock**: samples processed since the plugin was created. It never jumps with
the host transport, so it is safe to schedule against. `audioNow` assumes the device plays one block
behind the audio thread. Posts also count as `fire()`, so `brain.trigger().onEvent()` still works.
The queue holds 1024 events; with the editor closed it fills and new posts are dropped (never blocks).

`CharacterView::frameOverride` lets the plugin pick the sprite frame itself, e.g. a walk cycle driven
by where the next paw plant is rather than by time.

### Gags

```cpp
gags().say ("text", pointInDesignCoords);
gags().slideIn ("NOTE: turn it down");
gags().shake (8.0f);
gags().flash ("DING");
```

These are UI only. Sound effects in the audio path go through `gagSounds`, which stays silent unless the
user ticks **Gag sounds in output** in the corner menu (or automates the `gagSounds` parameter).
Presets never turn it on.

### Deterministic randomness

```cpp
lockedin::GridClock sixteenths { 0.25 };       // processor member

void process (...) override
{
    sixteenths.process (random, [&] (int offset, int64_t tick)
    {
        if (random.chanceForTick (tick, 0.3f))           // same 30 % of 16ths every playback AND bounce
            gagSounds.trigger (meowId, offset);
    });

    const float jitter = random.bipolarAt (i);           // per-sample, keyed to timeline sample position
}
```

Grid draws are identical at any block size **and** any sample rate. Sample draws are identical at any
block size (same sample rate). With no transport running it falls back to an internal sample counter.
The seed is per instance and saved with the session, so two copies of a plugin on different tracks
behave differently and a reopened session behaves identically.

### Knob art

```cpp
getLnf().setDefaultKnobFilmstrip (lockedin::assets::image ("knob.png"), 128);        // all rotary sliders
getLnf().addKnobFilmstrip ("big", lockedin::assets::image ("knob_big.png"), 64);
lockedin::LookAndFeel::useKnobArt (driveSlider, "big");
```

Filmstrips are vertical by default (pass `false` for horizontal), first frame = minimum. Subclass
`lockedin::LookAndFeel` and override `drawKnob()` for vector knobs.

### Presets

`*.lipreset` is small XML, `<LockedInPreset><Param id="drive" value="6"/></LockedInPreset>`, with values
in real units. Put factory presets in `PRESETS`; users save theirs to `Documents/Locked In/<Plugin>/`.
`lockedin::PresetBar` is a ready-made `< name >` strip.

---

## Shipping unsigned Mac builds

Builds are **ad-hoc signed** (JUCE does this automatically; Apple Silicon won't load unsigned code at all)
but not notarised, so customers have to clear the quarantine flag once. Each plugin's build writes
`<Target>_artefacts/INSTALL.md` from `templates/INSTALL.md.in` and CI puts it in the zip. It covers the
`xattr` command and the *Privacy & Security → Open Anyway* route.

---

## Working on locked-in-core itself

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure      # MeterBus (incl. timed events), levels, TransportRandom, character state machine
```

This builds `examples/hello-character` (installed to your plug-in folders on Mac) and the unit tests.

**Testing hello-character:** load *Locked In → Hello Character* on a track in FL Studio (VST3 or AU) and play
something. The placeholder box goes IDLE → LISTENING (above about -50 dBFS) → LOUD (above -12 dBFS, with
shake, tint and a speech bubble). Clip the input to see the STARTLED one-shot and DING. Turn on
*Gag sounds in output* in the ≡ menu to hear the ding too.

```
cmake/                LockedInDefaults.cmake (before project), LockedIn.cmake (lockedin_add_plugin)
modules/lockedin_core JUCE module (all the C++)
templates/            INSTALL.md.in
examples/             hello-character
tests/                headless unit tests
.github/workflows/    plugin-build.yml (reusable), build.yml (this repo)
```
