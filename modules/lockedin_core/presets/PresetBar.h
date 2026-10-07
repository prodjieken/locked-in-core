#pragma once

namespace lockedin
{

/** Compact preset strip:  [<] [ Preset Name v ] [>]
    Clicking the name opens a menu with factory/user presets, Save, Delete and
    "Show presets folder". */
class PresetBar : public juce::Component
{
public:
    explicit PresetBar (PresetManager& manager);
    ~PresetBar() override;

    void resized() override;

private:
    void showMenu();
    void askForNameAndSave();
    void updateLabel();

    PresetManager& presets;
    juce::TextButton prev { "<" }, next { ">" }, name;
    std::unique_ptr<juce::AlertWindow> saveDialog;
};

} // namespace lockedin
