namespace lockedin
{

namespace
{
    juce::uint32 parseColour (const juce::String& s, bool& ok)
    {
        auto hex = s.trim().trimCharactersAtStart ("#");
        ok = hex.length() == 6 && hex.containsOnly ("0123456789abcdefABCDEF");
        return ok ? (0xff000000u | (juce::uint32) hex.getHexValue32()) : 0u;
    }
}

juce::Result CharacterDef::parse (const juce::String& jsonText, CharacterDef& out)
{
    out = {};

    juce::var root;
    const auto parsed = juce::JSON::parse (jsonText, root);
    if (parsed.failed())
        return juce::Result::fail ("character JSON: " + parsed.getErrorMessage());

    auto* rootObj = root.getDynamicObject();
    if (rootObj == nullptr)
        return juce::Result::fail ("character JSON: top level must be an object");

    auto* statesObj = rootObj->getProperty ("states").getDynamicObject();
    if (statesObj == nullptr || statesObj->getProperties().isEmpty())
        return juce::Result::fail ("character JSON: needs a non-empty \"states\" object");

    for (const auto& prop : statesObj->getProperties())
    {
        const auto name = prop.name.toString();
        auto* s = prop.value.getDynamicObject();
        if (s == nullptr)
            return juce::Result::fail ("state \"" + name + "\" must be an object");

        StateDef def;
        def.name = name;
        def.sheet = s->getProperty ("sheet").toString();
        def.frames = s->hasProperty ("frames") ? (int) s->getProperty ("frames") : 1;
        def.columns = s->hasProperty ("columns") ? (int) s->getProperty ("columns") : 0;
        def.fps = s->hasProperty ("fps") ? (float) (double) s->getProperty ("fps") : 8.0f;
        def.loop = s->hasProperty ("loop") ? (bool) s->getProperty ("loop") : true;
        def.then = s->getProperty ("then").toString();

        if (def.frames < 1)
            return juce::Result::fail ("state \"" + name + "\": frames must be >= 1");
        if (def.fps <= 0.0f)
            return juce::Result::fail ("state \"" + name + "\": fps must be > 0");
        if (def.columns < 0)
            return juce::Result::fail ("state \"" + name + "\": columns must be >= 0");
        if (def.loop && def.then.isNotEmpty())
            return juce::Result::fail ("state \"" + name + "\": \"then\" only applies to one-shots (loop: false)");

        if (auto* fx = s->getProperty ("fx").getDynamicObject())
        {
            def.shakePx = (float) (double) fx->getProperty ("shake");
            def.risePx = (float) (double) fx->getProperty ("rise");
            def.squash = (float) (double) fx->getProperty ("squash");
            def.tintAmount = fx->hasProperty ("tintAmount") ? (float) (double) fx->getProperty ("tintAmount") : 0.5f;

            if (fx->hasProperty ("tint"))
            {
                bool ok = false;
                def.tintArgb = parseColour (fx->getProperty ("tint").toString(), ok);
                if (! ok)
                    return juce::Result::fail ("state \"" + name + "\": tint must look like \"#ff4040\"");
            }
        }

        out.states.push_back (def);
    }

    out.defaultState = rootObj->hasProperty ("default") ? rootObj->getProperty ("default").toString()
                                                        : out.states.front().name;

    if (out.indexOf (out.defaultState) < 0)
        return juce::Result::fail ("default state \"" + out.defaultState + "\" is not defined");

    for (const auto& s : out.states)
        if (s.then.isNotEmpty() && out.indexOf (s.then) < 0)
            return juce::Result::fail ("state \"" + s.name + "\": then-state \"" + s.then + "\" is not defined");

    return juce::Result::ok();
}

CharacterDef CharacterDef::placeholder (const juce::StringArray& stateNames)
{
    CharacterDef d;
    for (const auto& n : stateNames)
    {
        StateDef s;
        s.name = n;
        s.frames = 4;
        s.fps = 4.0f;
        d.states.push_back (s);
    }

    if (d.states.empty())
    {
        StateDef s;
        s.name = "idle";
        d.states.push_back (s);
    }

    d.defaultState = d.states.front().name;
    return d;
}

} // namespace lockedin
