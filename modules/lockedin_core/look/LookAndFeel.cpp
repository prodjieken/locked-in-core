namespace lockedin
{

static const juce::Identifier knobArtKey { "lockedin_knob" };

LookAndFeel::LookAndFeel (Theme t)
    : theme (std::move (t))
{
    applyColours();
}

void LookAndFeel::setTheme (Theme t)
{
    theme = std::move (t);
    applyColours();
}

void LookAndFeel::applyColours()
{
    setColour (juce::ResizableWindow::backgroundColourId, theme.background);
    setColour (juce::Label::textColourId, theme.text);
    setColour (juce::Slider::textBoxTextColourId, theme.text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::rotarySliderFillColourId, theme.accent);
    setColour (juce::Slider::rotarySliderOutlineColourId, theme.panel.brighter (0.2f));
    setColour (juce::Slider::thumbColourId, theme.accent);
    setColour (juce::TextButton::buttonColourId, theme.panel);
    setColour (juce::TextButton::buttonOnColourId, theme.accent);
    setColour (juce::TextButton::textColourOffId, theme.text);
    setColour (juce::TextButton::textColourOnId, theme.background);
    setColour (juce::ToggleButton::textColourId, theme.text);
    setColour (juce::ToggleButton::tickColourId, theme.accent);
    setColour (juce::ComboBox::backgroundColourId, theme.panel);
    setColour (juce::ComboBox::textColourId, theme.text);
    setColour (juce::ComboBox::outlineColourId, theme.panel.brighter (0.2f));
    setColour (juce::PopupMenu::backgroundColourId, theme.panel);
    setColour (juce::PopupMenu::textColourId, theme.text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, theme.accent);
    setColour (juce::PopupMenu::highlightedTextColourId, theme.background);
    setColour (juce::TextEditor::backgroundColourId, theme.background);
    setColour (juce::TextEditor::textColourId, theme.text);
    setColour (juce::TextEditor::outlineColourId, theme.panel.brighter (0.3f));
    setColour (juce::AlertWindow::backgroundColourId, theme.panel);
    setColour (juce::AlertWindow::textColourId, theme.text);
}

void LookAndFeel::setDefaultKnobFilmstrip (juce::Image strip, int numFrames, bool vertical)
{
    defaultKnob = { std::move (strip), numFrames, vertical };
}

void LookAndFeel::addKnobFilmstrip (const juce::String& key, juce::Image strip, int numFrames, bool vertical)
{
    knobs[key] = { std::move (strip), numFrames, vertical };
}

void LookAndFeel::useKnobArt (juce::Slider& slider, const juce::String& key)
{
    slider.getProperties().set (knobArtKey, key);
    slider.repaint();
}

void LookAndFeel::drawFilmstrip (juce::Graphics& g, const Filmstrip& f, juce::Rectangle<float> area, float proportion)
{
    const auto frame = juce::jlimit (0, f.frames - 1, (int) std::round (proportion * (float) (f.frames - 1)));
    const auto fw = f.vertical ? f.image.getWidth() : f.image.getWidth() / f.frames;
    const auto fh = f.vertical ? f.image.getHeight() / f.frames : f.image.getHeight();
    const auto sx = f.vertical ? 0 : frame * fw;
    const auto sy = f.vertical ? frame * fh : 0;

    const auto dest = area.withSizeKeepingCentre (juce::jmin (area.getWidth(), area.getHeight() * (float) fw / (float) fh),
                                                  juce::jmin (area.getHeight(), area.getWidth() * (float) fh / (float) fw));

    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (f.image.getClippedImage ({ sx, sy, fw, fh }), dest, juce::RectanglePlacement::stretchToFit);
}

void LookAndFeel::drawKnob (juce::Graphics& g, juce::Rectangle<float> bounds, float proportion, juce::Slider&)
{
    const auto size = juce::jmin (bounds.getWidth(), bounds.getHeight()) - 4.0f;
    const auto r = bounds.withSizeKeepingCentre (size, size);
    const auto start = juce::MathConstants<float>::pi * 1.25f;
    const auto end = juce::MathConstants<float>::pi * 2.75f;
    const auto angle = start + proportion * (end - start);

    g.setColour (theme.panel);
    g.fillEllipse (r);
    g.setColour (theme.panel.brighter (0.25f));
    g.drawEllipse (r.reduced (1.0f), 2.0f);

    juce::Path arc;
    arc.addCentredArc (r.getCentreX(), r.getCentreY(), size * 0.5f - 4.0f, size * 0.5f - 4.0f, 0.0f, start, angle, true);
    g.setColour (theme.accent);
    g.strokePath (arc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const auto c = r.getCentre();
    const auto tip = c.getPointOnCircumference (size * 0.32f, angle);
    g.setColour (theme.text);
    g.drawLine ({ c, tip }, 3.0f);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                    float, float, juce::Slider& slider)
{
    const juce::Rectangle<float> area ((float) x, (float) y, (float) w, (float) h);

    const auto key = slider.getProperties()[knobArtKey].toString();
    if (key.isNotEmpty())
    {
        const auto it = knobs.find (key);
        if (it != knobs.end() && it->second.isValid())
        {
            drawFilmstrip (g, it->second, area, pos);
            return;
        }
    }

    if (defaultKnob.isValid())
    {
        drawFilmstrip (g, defaultKnob, area, pos);
        return;
    }

    drawKnob (g, area, pos, slider);
}

juce::Typeface::Ptr LookAndFeel::getTypefaceForFont (const juce::Font& f)
{
    if (theme.typeface != nullptr)
        return theme.typeface;
    return juce::LookAndFeel_V4::getTypefaceForFont (f);
}

juce::Font LookAndFeel::font (float height) const
{
    if (theme.typeface != nullptr)
        return juce::Font (juce::FontOptions (theme.typeface).withHeight (height));
    return juce::Font (juce::FontOptions (height));
}

juce::Font LookAndFeel::getPopupMenuFont()               { return font (15.0f); }
juce::Font LookAndFeel::getLabelFont (juce::Label& l)    { return font (l.getFont().getHeight()); }

} // namespace lockedin
