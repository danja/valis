// src/ui/ValisEditor.cpp

#include "ui/ValisEditor.h"

#include "plugin/ValisProcessor.h"
#include "ui/ControlsView.h"
#include "ui/GraphView.h"
#include "ui/TurtleView.h"

namespace valis {

ValisEditor::ValisEditor(ValisProcessor& p)
    : juce::AudioProcessorEditor(&p), processor(p)
{
    const auto bg = juce::Colour(0xff1e1e22);
    tabs.addTab("Turtle", bg, new TurtleView(p), true);
    tabs.addTab("Graph",  bg, new GraphView(p), true);
    tabs.addTab("Knobs",  bg, new ControlsView(p), true);

    addAndMakeVisible(menuBar);
    addAndMakeVisible(tabs);
    setResizable(true, true);
    setResizeLimits(600, 400, 4000, 3000);
    setSize(960, 640);
}

ValisEditor::~ValisEditor()
{
    // Detach before the model (this) is partially destroyed.
    menuBar.setModel(nullptr);
}

void ValisEditor::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void ValisEditor::resized()
{
    auto bounds = getLocalBounds();
    menuBar.setBounds(bounds.removeFromTop(getLookAndFeel().getDefaultMenuBarHeight()));
    tabs.setBounds(bounds);
}

juce::StringArray ValisEditor::getMenuBarNames()
{
    return { "File", "Settings" };
}

juce::PopupMenu ValisEditor::getMenuForIndex(int menuIndex, const juce::String&)
{
    juce::PopupMenu menu;

    if (menuIndex == 0)  // File
    {
        menu.addItem(fileLoad, "Load Circuit...");
        menu.addItem(fileSave, "Save Circuit...");
    }
    else if (menuIndex == 1)  // Settings
    {
       #if VALIS_WITH_MCP
        const bool running = processor.isMcpRunning();
        menu.addItem(settingsMcpToggle, "MCP Server", true, running);
        if (running)
            menu.addSectionHeader("Listening on port " + juce::String(processor.mcpPort()));
       #else
        menu.addItem(settingsMcpToggle, "MCP Server (not built)", false, false);
       #endif

        if (standaloneOptionsButton != nullptr)
        {
            menu.addSeparator();
            menu.addItem(settingsAudioMidi, "Audio/MIDI Settings...");
        }
    }

    return menu;
}

void ValisEditor::menuItemSelected(int menuItemID, int)
{
    switch (menuItemID)
    {
        case fileLoad: loadCircuit(); break;
        case fileSave: saveCircuit(); break;
       #if VALIS_WITH_MCP
        case settingsMcpToggle:
            if (processor.isMcpRunning()) processor.stopMcp();
            else                          processor.startMcp();
            break;
       #endif
        case settingsAudioMidi:
            // Reveal the standalone Options button just long enough to trigger
            // its built-in audio/MIDI settings dialog, then hide it again.
            if (standaloneOptionsButton != nullptr)
            {
                standaloneOptionsButton->setVisible(true);
                standaloneOptionsButton->triggerClick();
                standaloneOptionsButton->setVisible(false);
            }
            break;
        default: break;
    }
}

void ValisEditor::parentHierarchyChanged()
{
    if (standaloneOptionsButton != nullptr)
        return;

    // Walk up the component tree to find the JUCE standalone Options button
    // and hide it — our Settings menu provides the same functionality.
    for (auto* c = getParentComponent(); c != nullptr; c = c->getParentComponent())
    {
        for (int i = 0; i < c->getNumChildComponents(); ++i)
        {
            if (auto* b = dynamic_cast<juce::Button*>(c->getChildComponent(i)))
            {
                if (b->getButtonText() == "Options")
                {
                    standaloneOptionsButton = b;
                    b->setVisible(false);
                    return;
                }
            }
        }
    }
}

void ValisEditor::loadCircuit()
{
    fileChooser = std::make_unique<juce::FileChooser>("Load Circuit",
                                                      juce::File{}, "*.ttl");
    fileChooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& chooser)
        {
            const auto f = chooser.getResult();
            if (f.existsAsFile())
                processor.setTurtle(f.loadFileAsString());
        });
}

void ValisEditor::saveCircuit()
{
    fileChooser = std::make_unique<juce::FileChooser>("Save Circuit",
                                                      juce::File{}, "*.ttl");
    fileChooser->launchAsync(
        juce::FileBrowserComponent::saveMode |
        juce::FileBrowserComponent::canSelectFiles |
        juce::FileBrowserComponent::warnAboutOverwriting,
        [this](const juce::FileChooser& chooser)
        {
            auto f = chooser.getResult().withFileExtension("ttl");
            if (f.getFullPathName().isNotEmpty())
                f.replaceWithText(processor.getTurtle());
        });
}

}  // namespace valis
