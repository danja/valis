// src/ui/ValisEditor.h

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace valis {

class ValisProcessor;

/// The three views in a tabbed container, topped by a File / Settings menu bar.
class ValisEditor final : public juce::AudioProcessorEditor,
                          public juce::MenuBarModel
{
public:
    explicit ValisEditor(ValisProcessor&);
    ~ValisEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // MenuBarModel
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int menuIndex, const juce::String& menuName) override;
    void menuItemSelected(int menuItemID, int topLevelMenuIndex) override;

private:
    enum MenuIDs {
        fileLoad = 1,
        fileSave,
        settingsMcpToggle = 100,
    };

    void loadCircuit();
    void saveCircuit();

    ValisProcessor& processor;
    juce::MenuBarComponent menuBar{this};
    juce::TabbedComponent tabs{juce::TabbedButtonBar::TabsAtTop};
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ValisEditor)
};

}  // namespace valis
