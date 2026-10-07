namespace lockedin
{

PresetBar::PresetBar (PresetManager& m) : presets (m)
{
    for (auto* b : { &prev, &next, &name })
        addAndMakeVisible (b);

    prev.onClick = [this] { presets.loadPrevious(); };
    next.onClick = [this] { presets.loadNext(); };
    name.onClick = [this] { showMenu(); };

    presets.onChange = [this] { updateLabel(); };
    updateLabel();
}

PresetBar::~PresetBar()
{
    presets.onChange = nullptr;
}

void PresetBar::resized()
{
    auto r = getLocalBounds();
    const auto arrow = juce::jmin (r.getHeight(), 32);
    prev.setBounds (r.removeFromLeft (arrow));
    next.setBounds (r.removeFromRight (arrow));
    name.setBounds (r.reduced (4, 0));
}

void PresetBar::updateLabel()
{
    const auto n = presets.getCurrentName();
    name.setButtonText (n.isNotEmpty() ? n : juce::String ("Init"));
}

void PresetBar::showMenu()
{
    presets.refresh();

    juce::PopupMenu menu;
    const auto& list = presets.getPresets();
    const auto current = presets.getCurrentName();

    for (const bool factorySection : { true, false })
    {
        bool headerAdded = false;
        for (int i = 0; i < (int) list.size(); ++i)
        {
            const auto& p = list[(size_t) i];
            if (p.isFactory != factorySection)
                continue;

            if (! headerAdded)
            {
                menu.addSectionHeader (factorySection ? "Factory" : "Yours");
                headerAdded = true;
            }
            menu.addItem (p.name, true, p.name == current, [this, i] { presets.load (i); });
        }
    }

    menu.addSeparator();
    menu.addItem ("Save preset...", [this] { askForNameAndSave(); });

    const auto currentIndex = presets.indexOf (current);
    const bool canDelete = currentIndex >= 0 && ! list[(size_t) currentIndex].isFactory;
    menu.addItem ("Delete \"" + current + "\"", canDelete, false, [this, currentIndex]
    {
        presets.deleteUser (currentIndex);
    });

    menu.addItem ("Show presets folder", [this]
    {
        auto folder = presets.getUserFolder();
        folder.createDirectory();
        folder.revealToUser();
    });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&name));
}

void PresetBar::askForNameAndSave()
{
    saveDialog = std::make_unique<juce::AlertWindow> ("Save preset", "Name your preset:", juce::MessageBoxIconType::NoIcon, this);
    saveDialog->addTextEditor ("name", presets.getCurrentName());
    saveDialog->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    saveDialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    saveDialog->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int result)
    {
        if (result == 1 && saveDialog != nullptr)
        {
            const auto n = saveDialog->getTextEditorContents ("name").trim();
            if (! presets.saveUser (n))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Couldn't save",
                                                        "Use a name without / \\ : * ? \" < > | characters.", "OK", this);
        }
        saveDialog.reset();
    }), false);
}

} // namespace lockedin
