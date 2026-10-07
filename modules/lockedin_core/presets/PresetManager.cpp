namespace lockedin
{

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& apvts, const juce::String& name)
    : state (apvts), pluginName (name)
{
    refresh();
}

juce::File PresetManager::getUserFolder() const
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
               .getChildFile ("Locked In")
               .getChildFile (juce::File::createLegalFileName (pluginName));
}

bool PresetManager::isValidName (const juce::String& name)
{
    const auto t = name.trim();
    return t.isNotEmpty() && t.length() <= 64 && juce::File::createLegalFileName (t) == t;
}

bool PresetManager::isExcluded (const juce::String& paramId) const
{
    return excluded.contains (paramId);
}

void PresetManager::refresh()
{
    presets.clear();

    std::vector<Preset> factory, user;

    for (const auto& asset : assets::list())
    {
        if (asset.endsWithIgnoreCase (fileExtension))
        {
            Preset p;
            p.name = asset.dropLastCharacters ((int) strlen (fileExtension));
            p.isFactory = true;
            p.assetName = asset;
            factory.push_back (p);
        }
    }

    const auto folder = getUserFolder();
    if (folder.isDirectory())
    {
        for (const auto& f : folder.findChildFiles (juce::File::findFiles, false, juce::String ("*") + fileExtension))
        {
            Preset p;
            p.name = f.getFileNameWithoutExtension();
            p.file = f;
            user.push_back (p);
        }
    }

    auto byName = [] (const Preset& a, const Preset& b) { return a.name.compareNatural (b.name) < 0; };
    std::sort (factory.begin(), factory.end(), byName);
    std::sort (user.begin(), user.end(), byName);

    presets = factory;
    presets.insert (presets.end(), user.begin(), user.end());

    if (onChange)
        onChange();
}

int PresetManager::indexOf (const juce::String& name) const
{
    // User presets shadow factory ones with the same name
    for (int i = (int) presets.size(); --i >= 0;)
        if (presets[(size_t) i].name == name)
            return i;
    return -1;
}

bool PresetManager::load (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) presets.size()))
        return false;

    const auto& p = presets[(size_t) index];
    std::unique_ptr<juce::XmlElement> xml;

    if (p.isFactory)
        xml = juce::parseXML (assets::text (p.assetName));
    else
        xml = juce::parseXML (p.file);

    if (xml == nullptr || ! xml->hasTagName ("LockedInPreset"))
    {
        DBG ("lockedin::PresetManager: unreadable preset " << p.name);
        return false;
    }

    applyXml (*xml);
    currentName = p.name;

    if (onChange)
        onChange();

    return true;
}

void PresetManager::applyXml (const juce::XmlElement& xml)
{
    std::map<juce::String, float> values;
    for (auto* e : xml.getChildWithTagNameIterator ("Param"))
        values[e->getStringAttribute ("id")] = (float) e->getDoubleAttribute ("value");

    for (auto* p : state.processor.getParameters())
    {
        auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p);
        if (rp == nullptr || isExcluded (rp->getParameterID()))
            continue;

        const auto it = values.find (rp->getParameterID());
        const auto normalised = it != values.end() ? rp->convertTo0to1 (it->second)
                                                   : rp->getDefaultValue();

        rp->beginChangeGesture();
        rp->setValueNotifyingHost (normalised);
        rp->endChangeGesture();
    }
}

std::unique_ptr<juce::XmlElement> PresetManager::createXml() const
{
    auto xml = std::make_unique<juce::XmlElement> ("LockedInPreset");
    xml->setAttribute ("plugin", pluginName);
    xml->setAttribute ("version", 1);

    for (auto* p : state.processor.getParameters())
    {
        auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p);
        if (rp == nullptr || isExcluded (rp->getParameterID()))
            continue;

        auto* e = xml->createNewChildElement ("Param");
        e->setAttribute ("id", rp->getParameterID());
        e->setAttribute ("value", (double) rp->convertFrom0to1 (rp->getValue()));
    }

    return xml;
}

bool PresetManager::saveUser (const juce::String& rawName)
{
    const auto name = rawName.trim();
    if (! isValidName (name))
        return false;

    const auto folder = getUserFolder();
    if (! folder.createDirectory())
        return false;

    const auto file = folder.getChildFile (name + fileExtension);
    if (! createXml()->writeTo (file))
        return false;

    currentName = name;
    refresh();
    return true;
}

bool PresetManager::deleteUser (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) presets.size()) || presets[(size_t) index].isFactory)
        return false;

    const auto ok = presets[(size_t) index].file.moveToTrash();
    if (ok && presets[(size_t) index].name == currentName)
        currentName = {};

    refresh();
    return ok;
}

bool PresetManager::step (int delta)
{
    if (presets.empty())
        return false;

    auto i = indexOf (currentName);
    const auto n = (int) presets.size();
    i = i < 0 ? (delta > 0 ? 0 : n - 1) : ((i + delta) % n + n) % n;
    return load (i);
}

} // namespace lockedin
