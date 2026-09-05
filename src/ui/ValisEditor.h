// src/ui/ValisEditor.h

#pragma once

#include "ui/GraphView.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

namespace valis {

class ValisProcessor;

/// The three views in a tabbed container, topped by a File / Settings menu bar
/// and a global status bar at the bottom.
class ValisEditor final : public juce::AudioProcessorEditor,
                          public juce::MenuBarModel,
                          public juce::KeyListener,
                          public juce::MidiKeyboardState::Listener,
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
        settingsAutolayout,
    };

    using juce::Component::keyPressed;
    bool keyPressed(const juce::KeyPress&, juce::Component*) override;
    void reloadCircuit();
    void loadCircuit();
    void saveCircuit();
    void parentHierarchyChanged() override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void updateStatusBar();

    // MidiKeyboardState::Listener — routes virtual keyboard clicks to the processor.
    void handleNoteOn(juce::MidiKeyboardState*, int midiChannel, int noteNumber, float velocity) override;
    void handleNoteOff(juce::MidiKeyboardState*, int midiChannel, int noteNumber, float velocity) override;

    ValisProcessor& processor;
    juce::MenuBarComponent menuBar{this};
    juce::TabbedComponent tabs{juce::TabbedButtonBar::TabsAtTop};
    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::Button* standaloneOptionsButton = nullptr;

    juce::Label  statusLabel;
    juce::TextButton reloadButton{"Reload"};
    juce::TextButton revertButton{"Revert"};
    juce::TextButton keyboardButton{"MIDI Kbd"};

    GraphView*   graphView = nullptr;

    juce::File   loadedFile;
    juce::String loadedFileTurtle;
    bool         fileModified = false;

    // Shown below the tabs when the loaded circuit has MIDI elements but no audio
    // input — i.e. it is a synthesizer that cannot be heard without note events.
    juce::MidiKeyboardState keyboardState;
    juce::MidiKeyboardComponent keyboard{keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard};
    bool keyboardVisible = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ValisEditor)
};

}  // namespace valis
