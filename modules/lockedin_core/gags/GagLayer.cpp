namespace lockedin
{

namespace
{
    float easeOutBack (float t)
    {
        const auto c1 = 1.70158f, c3 = c1 + 1.0f;
        return 1.0f + c3 * std::pow (t - 1.0f, 3.0f) + c1 * std::pow (t - 1.0f, 2.0f);
    }

    // 0 -> 1 over the first `in` ms, 1 -> 0 over the last `out` ms
    float envelope (double age, double life, double in, double out)
    {
        if (age < in)          return (float) (age / in);
        if (age > life - out)  return (float) juce::jmax (0.0, (life - age) / out);
        return 1.0f;
    }
}

GagLayer::GagLayer()
{
    setInterceptsMouseClicks (false, false);
    setPaintingIsUnclipped (false);
}

void GagLayer::say (const juce::String& text, juce::Point<float> pointsAt, float durationMs)
{
    // one bubble at a time; a new line replaces the old one
    items.erase (std::remove_if (items.begin(), items.end(), [] (const Item& i) { return i.kind == Kind::bubble; }), items.end());
    items.push_back ({ Kind::bubble, text, pointsAt, Edge::right, juce::Colours::white, {}, 0.0, durationMs });
    repaint();
}

void GagLayer::slideIn (const juce::String& text, Edge from, float durationMs, juce::Image image)
{
    items.push_back ({ Kind::note, text, {}, from, juce::Colour (0xfffff27a), std::move (image), 0.0, durationMs });
    repaint();
}

void GagLayer::shake (float amountPx, float durationMs)
{
    shakeAmount = juce::jmax (shakeAmount * (float) (shakeLeft / juce::jmax (1.0, shakeTotal)), amountPx);
    shakeLeft = shakeTotal = durationMs;
}

void GagLayer::flash (const juce::String& text, juce::Colour colour, float durationMs)
{
    items.push_back ({ Kind::flash, text, {}, Edge::right, colour, {}, 0.0, durationMs });
    repaint();
}

void GagLayer::clear()
{
    items.clear();
    shakeLeft = 0.0;
    shakeOffset = {};
    repaint();
}

void GagLayer::advance (double dtMs)
{
    const bool wasBusy = isBusy();

    for (auto& i : items)
        i.age += dtMs;
    items.erase (std::remove_if (items.begin(), items.end(), [] (const Item& i) { return i.age >= i.life; }), items.end());

    if (shakeLeft > 0.0)
    {
        shakeLeft = juce::jmax (0.0, shakeLeft - dtMs);
        const auto amt = shakeAmount * (float) (shakeLeft / juce::jmax (1.0, shakeTotal));
        shakeOffset = { (random.nextFloat() * 2.0f - 1.0f) * amt, (random.nextFloat() * 2.0f - 1.0f) * amt };
    }
    else
    {
        shakeOffset = {};
    }

    if (wasBusy || isBusy())
        repaint();
}

void GagLayer::paint (juce::Graphics& g)
{
    for (const auto& i : items)
    {
        switch (i.kind)
        {
            case Kind::bubble: paintBubble (g, i); break;
            case Kind::note:   paintNote (g, i);   break;
            case Kind::flash:  paintFlash (g, i);  break;
        }
    }
}

void GagLayer::paintBubble (juce::Graphics& g, const Item& i)
{
    const auto t = (float) juce::jlimit (0.0, 1.0, i.age / 180.0);
    const auto pop = easeOutBack (t);
    const auto alpha = envelope (i.age, i.life, 1.0, 200.0);

    auto f = font.withHeight (18.0f);
    const auto textW = juce::jlimit (60.0f, 260.0f, juce::GlyphArrangement::getStringWidth (f, i.text) + 28.0f);
    const auto lines = std::ceil (juce::GlyphArrangement::getStringWidth (f, i.text) / juce::jmax (1.0f, textW - 28.0f));
    const auto h = 22.0f * juce::jmax (1.0f, lines) + 18.0f;

    // bubble above-right of the target, kept on screen
    auto box = juce::Rectangle<float> (textW, h).withPosition (i.target.x + 12.0f, i.target.y - h - 26.0f);
    box = box.constrainedWithin (getLocalBounds().toFloat().reduced (6.0f));

    juce::Graphics::ScopedSaveState s (g);
    g.addTransform (juce::AffineTransform::scale (pop, pop, i.target.x, i.target.y));
    g.setOpacity (alpha);

    juce::Path p;
    p.addRoundedRectangle (box, 14.0f);
    const auto tailBase = juce::Point<float> (juce::jlimit (box.getX() + 16.0f, box.getRight() - 30.0f, i.target.x + 8.0f), box.getBottom() - 1.0f);
    p.startNewSubPath (tailBase);
    p.lineTo (i.target);
    p.lineTo (tailBase.translated (16.0f, 0.0f));
    p.closeSubPath();

    g.setColour (juce::Colours::white);
    g.fillPath (p);
    g.setColour (juce::Colours::black);
    g.strokePath (p, juce::PathStrokeType (2.5f));
    g.setFont (f);
    g.drawFittedText (i.text, box.reduced (12.0f, 8.0f).toNearestInt(), juce::Justification::centred, 4, 0.9f);
}

void GagLayer::paintNote (juce::Graphics& g, const Item& i)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto w = juce::jmin (220.0f, bounds.getWidth() * 0.45f), h = 120.0f;

    const auto inT = (float) juce::jlimit (0.0, 1.0, i.age / 350.0);
    const auto outT = (float) juce::jlimit (0.0, 1.0, (i.age - (i.life - 300.0)) / 300.0);
    const auto shown = easeOutBack (inT) * (1.0f - outT * outT);

    juce::Rectangle<float> rest;
    juce::Point<float> hidden;
    switch (i.edge)
    {
        case Edge::right:  rest = { bounds.getRight() - w - 20.0f, bounds.getCentreY() - h * 0.5f, w, h }; hidden = { w + 40.0f, 0.0f }; break;
        case Edge::left:   rest = { 20.0f, bounds.getCentreY() - h * 0.5f, w, h };                         hidden = { -(w + 40.0f), 0.0f }; break;
        case Edge::top:    rest = { bounds.getCentreX() - w * 0.5f, 20.0f, w, h };                          hidden = { 0.0f, -(h + 40.0f) }; break;
        case Edge::bottom: rest = { bounds.getCentreX() - w * 0.5f, bounds.getBottom() - h - 20.0f, w, h }; hidden = { 0.0f, h + 40.0f }; break;
    }

    const auto box = rest.translated (hidden.x * (1.0f - shown), hidden.y * (1.0f - shown));

    juce::Graphics::ScopedSaveState s (g);
    g.addTransform (juce::AffineTransform::rotation (-0.05f, box.getCentreX(), box.getCentreY()));

    g.setColour (juce::Colours::black.withAlpha (0.3f));
    g.fillRect (box.translated (4.0f, 5.0f));
    g.setColour (i.colour);
    g.fillRect (box);
    g.setColour (i.colour.darker (0.15f));
    g.fillRect (box.withHeight (16.0f));

    auto content = box.reduced (12.0f).withTrimmedTop (10.0f);
    if (i.image.isValid())
        g.drawImage (i.image, content.removeFromLeft (content.getHeight()), juce::RectanglePlacement::centred);

    g.setColour (juce::Colours::black.withAlpha (0.85f));
    g.setFont (font.withHeight (17.0f));
    g.drawFittedText (i.text, content.toNearestInt(), juce::Justification::centred, 4, 0.85f);
}

void GagLayer::paintFlash (juce::Graphics& g, const Item& i)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto t = (float) (i.age / i.life);
    const auto alpha = 1.0f - t * t;

    g.setColour (i.colour.withAlpha (0.35f * (1.0f - t)));
    g.fillRect (bounds);

    const auto scale = 0.6f + 0.6f * easeOutBack (juce::jmin (1.0f, t * 3.0f));
    juce::Graphics::ScopedSaveState s (g);
    g.addTransform (juce::AffineTransform::scale (scale, scale, bounds.getCentreX(), bounds.getCentreY()));

    const auto f = font.withHeight (juce::jmin (bounds.getHeight() * 0.3f, 96.0f));
    g.setFont (f);
    const auto textArea = bounds.toNearestInt();

    g.setColour (juce::Colours::black.withAlpha (alpha));
    for (auto d : { juce::Point<int> (-3, 0), { 3, 0 }, { 0, -3 }, { 0, 3 } })
        g.drawText (i.text, textArea.translated (d.x, d.y), juce::Justification::centred);

    g.setColour (i.colour.withAlpha (alpha));
    g.drawText (i.text, textArea, juce::Justification::centred);
}

} // namespace lockedin
