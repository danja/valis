// src/ui/ValisEditor.h

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace valis {

class ValisProcessor;

/// The three views in a tabbed container, topped by a File / Settings menu bar
/// and a global status bar at the bottom.
class ValisEditor final : public juce::AudioProcessorEditor,
                          public juce::MenuBarModel,
                          public juce::KeyListener,
                          private juce::ChangeListener
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
        settingsAudioMidi,
    };

    using juce::Component::keyPressed;
    bool keyPressed(const juce::KeyPress&, juce::Component*) override;
    void loadCircuit();
    void saveCircuit();
    void parentHierarchyChanged() override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void updateStatusBar();

    ValisProcessor& processor;
    juce::MenuBarComponent menuBar{this};
    juce::TabbedComponent tabs{juce::TabbedButtonBar::TabsAtTop};
    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::Button* standaloneOptionsButton = nullptr;

    juce::Label  statusLabel;
    juce::TextButton revertButton{"Revert"};

    juce::File   loadedFile;
    juce::String loadedFileTurtle;
    bool         fileModified = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ValisEditor)
};

}  // namespace valis
