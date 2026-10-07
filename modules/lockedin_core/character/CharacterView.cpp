namespace lockedin
{

CharacterView::CharacterView (CharacterBrain& b) : brain (b)
{
    setInterceptsMouseClicks (false, false);
    reloadArt();
}

void CharacterView::reloadArt()
{
    sheets.clear();
    for (const auto& s : brain.getDefinition().states)
        sheets.push_back (s.sheet.isNotEmpty() ? assets::image (s.sheet) : juce::Image());
    repaint();
}

juce::Colour CharacterView::placeholderColour (const juce::String& stateName)
{
    // Stable, distinct-ish colour per state name
    const auto h = (juce::uint32) stateName.hashCode();
    return juce::Colour::fromHSV ((float) (h % 360u) / 360.0f, 0.55f, 0.85f, 1.0f);
}

void CharacterView::paint (juce::Graphics& g)
{
    const auto& state = brain.getState();
    const auto idx = brain.getStateIndex();
    const auto frame = frameOverride ? juce::jlimit (0, juce::jmax (0, state.frames - 1), frameOverride (state, brain.getFrame()))
                                     : brain.getFrame();
    const auto intensity = brain.getIntensity();

    auto area = getLocalBounds().toFloat();

    // --- intensity-driven fx
    const auto shake = state.shakePx * intensity;
    const auto rise = state.risePx * intensity;
    const auto squash = juce::jlimit (0.0f, 0.9f, state.squash * intensity);

    auto offset = juce::Point<float> (0.0f, -rise);
    if (shake > 0.0f)
        offset += { (uiRandom.nextFloat() * 2.0f - 1.0f) * shake, (uiRandom.nextFloat() * 2.0f - 1.0f) * shake };

    const auto anchor = juce::Point<float> (area.getCentreX(), area.getBottom());
    const auto transform = juce::AffineTransform::scale (1.0f + squash * 0.5f, 1.0f - squash, anchor.x, anchor.y)
                               .translated (offset);

    juce::Graphics::ScopedSaveState save (g);
    g.addTransform (transform);

    const auto& sheet = juce::isPositiveAndBelow (idx, (int) sheets.size()) ? sheets[(size_t) idx] : juce::Image();
    const bool haveArt = sheet.isValid() && ! forcePlaceholder;

    if (haveArt)
        drawFrame (g, area, sheet, state, frame);
    else
        drawPlaceholder (g, area, state, frame, intensity, ! sheet.isValid() && state.sheet.isNotEmpty());

    if (state.tintArgb != 0 && state.tintAmount > 0.0f && intensity > 0.0f)
    {
        const auto tint = juce::Colour (state.tintArgb).withAlpha (juce::jlimit (0.0f, 1.0f, state.tintAmount * intensity));
        g.setColour (tint);
        if (haveArt)
        {
            // Tint only the character's pixels: redraw the frame as a silhouette
            const auto& s = state;
            const auto cols = s.columns > 0 ? s.columns : s.frames;
            const auto rows = (s.frames + cols - 1) / cols;
            const auto fw = sheet.getWidth() / cols;
            const auto fh = sheet.getHeight() / rows;
            const auto dest = area.withSizeKeepingCentre (juce::jmin (area.getWidth(), area.getHeight() * (float) fw / (float) fh),
                                                          juce::jmin (area.getHeight(), area.getWidth() * (float) fh / (float) fw));
            g.drawImage (sheet.getClippedImage ({ (frame % cols) * fw, (frame / cols) * fh, fw, fh }),
                         dest, juce::RectanglePlacement::stretchToFit, true);
        }
        else
        {
            g.fillRoundedRectangle (area.reduced (area.getWidth() * 0.1f), 14.0f);
        }
    }

    if (drawOverlay)
        drawOverlay (g, area);
}

void CharacterView::drawFrame (juce::Graphics& g, juce::Rectangle<float> area, const juce::Image& sheet,
                               const StateDef& s, int frame)
{
    const auto cols = s.columns > 0 ? s.columns : s.frames;
    const auto rows = (s.frames + cols - 1) / cols;
    const auto fw = sheet.getWidth() / cols;
    const auto fh = sheet.getHeight() / rows;

    if (fw <= 0 || fh <= 0)
        return;

    const auto dest = area.withSizeKeepingCentre (juce::jmin (area.getWidth(), area.getHeight() * (float) fw / (float) fh),
                                                  juce::jmin (area.getHeight(), area.getWidth() * (float) fh / (float) fw));

    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (sheet.getClippedImage ({ (frame % cols) * fw, (frame / cols) * fh, fw, fh }),
                 dest, juce::RectanglePlacement::stretchToFit);
}

void CharacterView::drawPlaceholder (juce::Graphics& g, juce::Rectangle<float> area, const StateDef& s,
                                     int frame, float intensity, bool missingArt)
{
    const auto box = area.reduced (area.getWidth() * 0.1f, area.getHeight() * 0.1f);
    const auto colour = placeholderColour (s.name);

    // body; brightness pulses with the frame so animation is visible
    const auto pulse = s.frames > 1 ? (float) frame / (float) (s.frames - 1) : 0.0f;
    g.setColour (colour.withMultipliedBrightness (0.75f + 0.25f * pulse));
    g.fillRoundedRectangle (box, 14.0f);
    g.setColour (colour.darker (0.6f));
    g.drawRoundedRectangle (box, 14.0f, 3.0f);

    // frame dots along the bottom
    const auto dotArea = box.reduced (16.0f).removeFromBottom (12.0f);
    const auto n = juce::jmin (s.frames, 24);
    const auto step = dotArea.getWidth() / (float) juce::jmax (1, n);
    for (int i = 0; i < n; ++i)
    {
        g.setColour (i == frame ? juce::Colours::white : juce::Colours::black.withAlpha (0.3f));
        g.fillEllipse (dotArea.getX() + step * (float) i + step * 0.5f - 4.0f, dotArea.getY(), 8.0f, 8.0f);
    }

    // intensity bar on the left edge
    if (intensity > 0.001f)
    {
        auto bar = box.reduced (6.0f).removeFromLeft (6.0f);
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.fillRoundedRectangle (bar, 3.0f);
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.fillRoundedRectangle (bar.removeFromBottom (bar.getHeight() * intensity), 3.0f);
    }

    // labels
    auto text = box.reduced (12.0f);
    g.setColour (juce::Colours::black.withAlpha (0.85f));
    g.setFont (juce::FontOptions (juce::jlimit (14.0f, 36.0f, box.getHeight() * 0.16f), juce::Font::bold));
    g.drawText (s.name.toUpperCase(), text.removeFromTop (text.getHeight() * 0.55f), juce::Justification::centredBottom);

    g.setFont (juce::FontOptions (juce::jlimit (11.0f, 18.0f, box.getHeight() * 0.08f)));
    juce::String info = "frame " + juce::String (frame + 1) + "/" + juce::String (s.frames)
                      + (s.loop ? "  loop" : "  one-shot");
    if (intensity > 0.001f)
        info << "  int " << juce::String (intensity, 2);
    g.drawText (info, text.removeFromTop (22.0f), juce::Justification::centred);

    if (missingArt)
    {
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("missing: " + assets::fileNameOf (s.sheet), text.removeFromTop (18.0f), juce::Justification::centred);
    }
}

} // namespace lockedin
