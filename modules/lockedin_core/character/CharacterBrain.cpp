namespace lockedin
{

CharacterBrain::CharacterBrain (CharacterDef definition, const MeterBus& meterBus)
    : def (std::move (definition)), bus (meterBus)
{
    if (def.states.empty())
        def = CharacterDef::placeholder ({ "idle" });

    defaultIndex = juce::jmax (0, def.indexOf (def.defaultState));
    current = defaultIndex;
    intensitySources.resize (def.states.size());
    intensities.assign (def.states.size(), 0.0f);
}

int CharacterBrain::stateIndexOrAssert (const juce::String& name) const
{
    const auto i = def.indexOf (name);
    if (i < 0)
    {
        DBG ("lockedin::CharacterBrain: unknown state \"" << name << "\" (check the character JSON)");
        jassertfalse;
    }
    return i;
}

MeterId CharacterBrain::meterOrAssert (const juce::String& name) const
{
    const auto id = bus.find (name);
    if (id < 0)
    {
        DBG ("lockedin::CharacterBrain: unknown meter \"" << name << "\" (register it with meters.add() in the processor)");
        jassertfalse;
    }
    return id;
}

//==============================================================================
CharacterBrain::StateRule& CharacterBrain::when (const juce::String& state)
{
    rules.push_back (std::unique_ptr<StateRule> (new StateRule (*this, stateIndexOrAssert (state))));
    return *rules.back();
}

CharacterBrain::Trigger& CharacterBrain::trigger (const juce::String& oneShotState)
{
    const auto idx = stateIndexOrAssert (oneShotState);
    jassert (idx < 0 || ! def.states[(size_t) idx].loop);   // trigger() is for one-shots (loop: false)
    triggers.push_back (std::unique_ptr<Trigger> (new Trigger (*this, idx)));
    return *triggers.back();
}

CharacterBrain::Intensity& CharacterBrain::intensity (const juce::String& state)
{
    const auto idx = stateIndexOrAssert (state);
    auto src = std::unique_ptr<Intensity> (new Intensity (*this));
    auto& ref = *src;

    if (idx >= 0)
        intensitySources[(size_t) idx] = std::move (src);
    else
        orphanIntensity = std::move (src);   // keeps the returned reference valid

    return ref;
}

//==============================================================================
CharacterBrain::StateRule& CharacterBrain::StateRule::above (const juce::String& m, float t)
{
    meter = brain.meterOrAssert (m); threshold = t; isAbove = true; configured = true;
    return *this;
}

CharacterBrain::StateRule& CharacterBrain::StateRule::below (const juce::String& m, float t)
{
    meter = brain.meterOrAssert (m); threshold = t; isAbove = false; configured = true;
    return *this;
}

CharacterBrain::StateRule& CharacterBrain::StateRule::releaseBelow (float t)
{
    jassert (isAbove);   // releaseBelow pairs with above()
    release = t; hasRelease = true;
    return *this;
}

CharacterBrain::StateRule& CharacterBrain::StateRule::releaseAbove (float t)
{
    jassert (! isAbove);  // releaseAbove pairs with below()
    release = t; hasRelease = true;
    return *this;
}

CharacterBrain::StateRule& CharacterBrain::StateRule::forMs (float ms)          { enterMs = juce::jmax (0.0f, ms); return *this; }
CharacterBrain::StateRule& CharacterBrain::StateRule::releaseAfterMs (float ms) { exitMs = juce::jmax (0.0f, ms);  return *this; }

CharacterBrain::StateRule& CharacterBrain::StateRule::condition (Condition enter, Condition rel)
{
    customEnter = std::move (enter);
    customRelease = std::move (rel);
    configured = true;
    return *this;
}

bool CharacterBrain::StateRule::enterNow (const MeterSnapshot& m) const
{
    if (customEnter)
        return customEnter (m);
    if (meter < 0)
        return false;
    return isAbove ? m[meter] > threshold : m[meter] < threshold;
}

bool CharacterBrain::StateRule::releaseNow (const MeterSnapshot& m) const
{
    if (customEnter)
        return customRelease ? customRelease (m) : ! customEnter (m);
    if (meter < 0)
        return true;

    const auto r = hasRelease ? release : threshold;
    return isAbove ? m[meter] < r : m[meter] > r;
}

void CharacterBrain::StateRule::update (const MeterSnapshot& m, double dtMs)
{
    if (stateIndex < 0 || ! configured)
    {
        active = false;
        return;
    }

    if (! active)
    {
        enterTimer = enterNow (m) ? enterTimer + dtMs : 0.0;
        if (enterNow (m) && enterTimer >= enterMs)
        {
            active = true;
            exitTimer = 0.0;
        }
    }
    else
    {
        exitTimer = releaseNow (m) ? exitTimer + dtMs : 0.0;
        if (exitTimer > 0.0 && exitTimer >= exitMs)
        {
            active = false;
            enterTimer = 0.0;
        }
    }
}

//==============================================================================
CharacterBrain::Trigger& CharacterBrain::Trigger::onEvent (const juce::String& name)
{
    kind = Kind::event;
    event = brain.bus.findEvent (name);
    if (event < 0)
    {
        DBG ("lockedin::CharacterBrain: unknown event \"" << name << "\" (register it with meters.addEvent())");
        jassertfalse;
    }
    return *this;
}

CharacterBrain::Trigger& CharacterBrain::Trigger::whenRises (const juce::String& m, float t)
{
    kind = Kind::rise; meter = brain.meterOrAssert (m); threshold = t;
    return *this;
}

CharacterBrain::Trigger& CharacterBrain::Trigger::rearmBelow (float t)    { rearm = t; hasRearm = true; return *this; }
CharacterBrain::Trigger& CharacterBrain::Trigger::when (Condition c)      { kind = Kind::condition; cond = std::move (c); return *this; }
CharacterBrain::Trigger& CharacterBrain::Trigger::cooldownMs (float ms)   { cooldown = juce::jmax (0.0f, ms); return *this; }
CharacterBrain::Trigger& CharacterBrain::Trigger::interrupting (bool b)   { interrupts = b; return *this; }

bool CharacterBrain::Trigger::update (const MeterSnapshot& m, double dtMs)
{
    sinceFired += dtMs;
    bool edge = false;

    switch (kind)
    {
        case Kind::event:
            edge = event >= 0 && m.fired (event) > 0;
            break;

        case Kind::rise:
        {
            if (meter < 0)
                break;
            const auto v = m[meter];
            if (armed && v > threshold)
            {
                edge = true;
                armed = false;
            }
            else if (! armed && v < (hasRearm ? rearm : threshold))
            {
                armed = true;
            }
            break;
        }

        case Kind::condition:
        {
            const auto now = cond ? cond (m) : false;
            edge = now && ! lastCond;
            lastCond = now;
            break;
        }

        case Kind::none:
            break;
    }

    if (! edge || stateIndex < 0 || sinceFired < cooldown)
        return false;

    sinceFired = 0.0;
    return true;
}

//==============================================================================
CharacterBrain::Intensity& CharacterBrain::Intensity::from (const juce::String& m, float atZero, float atOne)
{
    meter = brain.meterOrAssert (m); lo = atZero; hi = atOne; source = nullptr;
    return *this;
}

CharacterBrain::Intensity& CharacterBrain::Intensity::from (Source s)   { source = std::move (s); meter = -1; return *this; }
CharacterBrain::Intensity& CharacterBrain::Intensity::smoothingMs (float ms) { smoothMs = juce::jmax (0.0f, ms); return *this; }

float CharacterBrain::Intensity::target (const MeterSnapshot& m) const
{
    float v = 0.0f;
    if (source)
        v = source (m);
    else if (meter >= 0 && std::abs (hi - lo) > 1.0e-9f)
        v = (m[meter] - lo) / (hi - lo);
    return juce::jlimit (0.0f, 1.0f, v);
}

//==============================================================================
void CharacterBrain::tick (const MeterSnapshot& m, double dtMs)
{
    dtMs = juce::jlimit (0.0, 250.0, dtMs);   // a stalled UI shouldn't fast-forward everything

    for (auto& r : rules)
        r->update (m, dtMs);

    for (size_t i = 0; i < intensitySources.size(); ++i)
    {
        if (auto* src = intensitySources[i].get())
        {
            const auto t = src->target (m);
            const auto a = src->smoothMs > 0.0f ? (float) (1.0 - std::exp (-dtMs / src->smoothMs)) : 1.0f;
            intensities[i] += a * (t - intensities[i]);
        }
    }

    timeInState += dtMs;

    // One-shot triggers (first one to fire wins; all of them keep their edge state up to date)
    int fire = -1;
    for (auto& t : triggers)
    {
        const bool fires = t->update (m, dtMs);
        if (fires && fire < 0 && (! isPlayingOneShot() || t->interrupts))
            fire = t->stateIndex;
    }

    if (fire >= 0)
    {
        enter (fire);
        return;
    }

    // What the sustained rules want
    int desired = defaultIndex;
    for (auto& r : rules)
    {
        if (r->isActive())
        {
            desired = r->stateIndex;
            break;
        }
    }

    if (isPlayingOneShot())
    {
        if (timeInState >= getState().durationMs())
        {
            const auto thenIndex = def.indexOf (getState().then);
            enter (thenIndex >= 0 ? thenIndex : desired);
        }
        return;
    }

    if (desired != current && timeInState >= minDwellMs)
        enter (desired);
}

void CharacterBrain::play (const juce::String& state)
{
    const auto i = def.indexOf (state);
    if (i >= 0)
        enter (i);
}

void CharacterBrain::enter (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) def.states.size()))
        return;

    const bool same = index == current;
    if (same && def.states[(size_t) index].loop)
        return;   // already looping in this state

    const auto from = getStateName();
    current = index;
    timeInState = 0.0;

    if (onStateChange)
        onStateChange (from, getStateName());
}

int CharacterBrain::getFrame() const noexcept
{
    const auto& s = getState();
    const auto f = (int) std::floor (timeInState * s.fps / 1000.0);
    return s.loop ? f % s.frames : juce::jmin (f, s.frames - 1);
}

float CharacterBrain::getIntensity (const juce::String& state) const
{
    const auto i = def.indexOf (state);
    return i >= 0 ? intensities[(size_t) i] : 0.0f;
}

} // namespace lockedin
