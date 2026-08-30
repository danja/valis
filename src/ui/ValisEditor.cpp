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
    tabs.addTab("Knobs",  bg, new ControlsView(p), true);
    tabs.addTab("Graph",  bg, new GraphView(p), true);
    tabs.addTab("Turtle", bg, new TurtleView(p), true);

    statusLabel.setFont(juce::FontOptions(13.0f));
    statusLabel.setJustificationType(juce::Justification::centredLeft);
    revertButton.onClick = [this] { processor.revert(); };

    addAndMakeVisible(menuBar);
    addAndMakeVisible(tabs);
    addAndMakeVisible(statusLabel);
    addAndMakeVisible(revertButton);

    p.addChangeListener(this);
    updateStatusBar();

    setResizable(true, true);
    setResizeLimits(600, 400, 4000, 3000);
    setSize(960, 640);
}

ValisEditor::~ValisEditor()
{
    processor.removeChangeListener(this);
    menuBar.setModel(nullptr);
}

void ValisEditor::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));

    // Status bar background.
    auto bar = getLocalBounds().removeFromBottom(28);
    const bool hasErrors = ! processor.lastDiagnostics().empty();
    g.setColour(hasErrors ? juce::Colour(0xff3a2626) : juce::Colour(0xff1a2a1e));
    g.fillRect(bar);
}

void ValisEditor::resized()
{
    auto bounds = getLocalBounds();
    menuBar.setBounds(bounds.removeFromTop(getLookAndFeel().getDefaultMenuBarHeight()));

    auto bar = bounds.removeFromBottom(28).reduced(8, 4);
    revertButton.setBounds(bar.removeFromRight(64));
    bar.removeFromRight(6);
    statusLabel.setBounds(bar);

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

void ValisEditor::changeListenerCallback(juce::ChangeBroadcaster*)
{
    if (loadedFile != juce::File{})
        fileModified = (processor.getTurtle() != loadedFileTurtle);
    updateStatusBar();
}

void ValisEditor::updateStatusBar()
{
    const auto diags = processor.lastDiagnostics();
    if (diags.empty())
    {
        const auto& elems = processor.circuit().elements();
        bool hasMidi  = false;
        bool hasInput = false;
        for (const auto& e : elems)
        {
            if (e.typeIri.find("/Midi") != std::string::npos)
                hasMidi = true;
            if (e.typeIri.ends_with("/Input"))
                hasInput = true;
        }

        juce::String msg;
        if (loadedFile != juce::File{})
        {
            msg = loadedFile.getFileName();
            if (fileModified)
                msg += "*";
            msg += "  \u2014  ";
        }
        msg += "Circuit OK";
        if (hasMidi && !hasInput)
            msg += "  \u2014  Synthesizer \u2014 play MIDI notes";

        const auto textColour = fileModified ? juce::Colour(0xffe5c07b)
                                             : juce::Colour(0xff98c379);
        statusLabel.setColour(juce::Label::textColourId, textColour);
        statusLabel.setText(msg, juce::dontSendNotification);
    }
    else
    {
        statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffe06c75));
        juce::String text = diags.front().toString();
        if (diags.size() > 1)
            text += "   (+" + juce::String(diags.size() - 1) + " more)";
        statusLabel.setText(text, juce::dontSendNotification);
    }
    repaint();
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
            {
                const auto turtle = f.loadFileAsString();
                processor.setTurtle(turtle);
                loadedFile       = f;
                loadedFileTurtle = turtle;
                fileModified     = false;
            }
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
            {
                const auto turtle = processor.getTurtle();
                f.replaceWithText(turtle);
                loadedFile       = f;
                loadedFileTurtle = turtle;
                fileModified     = false;
                updateStatusBar();
            }
        });
}

}  // namespace valis
