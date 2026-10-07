namespace lockedin
{

Editor::Editor (Processor& p, int w, int h, Theme theme)
    : juce::AudioProcessorEditor (p), proc (p), lnf (std::move (theme)), designW (w), designH (h)
{
    setLookAndFeel (&lnf);
    gagLayer.setFont (lnf.font (18.0f).boldened());

    stage.setInterceptsMouseClicks (false, true);
    stage.setBounds (0, 0, designW, designH);
    contentComp.setBounds (0, 0, designW, designH);
    gagLayer.setBounds (0, 0, designW, designH);

    stage.addAndMakeVisible (contentComp);
    stage.addAndMakeVisible (gagLayer);
    stage.addAndMakeVisible (menuButton);
    addAndMakeVisible (stage);

    menuButton.setTooltip ("Size and options");
    menuButton.setBounds (designW - 30, designH - 30, 24, 24);
    menuButton.onClick = [this] { showMenu(); };

    // Fixed size: no host or corner resizing.
    setResizable (false, false);

    float initial = 1.0f;
    for (auto s : scaleOptions)
        if (juce::approximatelyEqual (s, proc.getUiScale()))
            initial = s;
    setScale (initial);

    lastTickMs = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (60);
}

Editor::~Editor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void Editor::attachCharacter (CharacterBrain& brain, CharacterView& view)
{
    characters.emplace_back (&brain, &view);
}

void Editor::setScale (float s)
{
    scale = s;
    proc.setUiScale (s);
    setSize (juce::roundToInt ((float) designW * s), juce::roundToInt ((float) designH * s));
    applyTransform();
}

void Editor::applyTransform()
{
    const auto shake = gagLayer.getShakeOffset();
    stage.setTransform (juce::AffineTransform::translation (shake.x, shake.y).scaled (scale));
}

void Editor::paint (juce::Graphics& g)
{
    g.fillAll (lnf.getTheme().background);
}

void Editor::resized()
{
    // Size is fixed; the stage is transformed rather than re-laid out.
}

void Editor::visibilityChanged()
{
    if (isVisible())
        layoutIfNeeded();
}

void Editor::layoutIfNeeded()
{
    if (! laidOut)
    {
        laidOut = true;
        layout (contentComp.getLocalBounds());
        menuButton.toFront (false);
    }
}

void Editor::timerCallback()
{
    layoutIfNeeded();

    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto dt = juce::jlimit (0.0, 250.0, now - lastTickMs);
    lastTickMs = now;

    proc.meters.takeSnapshot (snapshot);

    for (auto& [brain, view] : characters)
    {
        brain->tick (snapshot, dt);
        view->repaint();
    }

    tick (snapshot, dt);

    const auto before = gagLayer.getShakeOffset();
    gagLayer.advance (dt);
    if (gagLayer.getShakeOffset() != before)
        applyTransform();
}

void Editor::showMenu()
{
    juce::PopupMenu menu;

    menu.addSectionHeader ("Size");
    for (auto s : scaleOptions)
        menu.addItem (juce::String (juce::roundToInt (s * 100.0f)) + "%", true, juce::approximatelyEqual (s, scale),
                      [safe = juce::Component::SafePointer<Editor> (this), s] { if (safe != nullptr) safe->setScale (s); });

    if (auto* gag = proc.apvts.getParameter (gagSoundsParamId))
    {
        menu.addSeparator();
        const bool on = gag->getValue() >= 0.5f;
        menu.addItem ("Gag sounds in output", true, on, [gag, on]
        {
            gag->beginChangeGesture();
            gag->setValueNotifyingHost (on ? 0.0f : 1.0f);
            gag->endChangeGesture();
        });
    }

    addMenuItems (menu);

    menu.addSeparator();
    menu.addItem (proc.getName() + " v" + juce::String (JucePlugin_VersionString) + "  -  Locked In", false, false, nullptr);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&menuButton));
}

void Editor::MenuButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    const auto* houseLnf = dynamic_cast<LookAndFeel*> (&getLookAndFeel());
    const auto theme = houseLnf != nullptr ? houseLnf->getTheme() : Theme();

    g.setColour (theme.panel.withAlpha (over || down ? 0.95f : 0.7f));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (over ? theme.accent : theme.textDim);

    const auto lineH = 2.0f, gap = 4.0f;
    auto lines = r.withSizeKeepingCentre (r.getWidth() * 0.55f, lineH * 3 + gap * 2);
    for (int i = 0; i < 3; ++i)
    {
        g.fillRoundedRectangle (lines.removeFromTop (lineH), 1.0f);
        lines.removeFromTop (gap);
    }
}

} // namespace lockedin
