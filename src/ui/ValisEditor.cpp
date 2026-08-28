// src/ui/ValisEditor.cpp

#include "ui/ValisEditor.h"

#include "plugin/ValisProcessor.h"
#include "ui/ControlsView.h"
#include "ui/GraphView.h"
#include "ui/TurtleView.h"

namespace valis {

ValisEditor::ValisEditor(ValisProcessor& p) : juce::AudioProcessorEditor(&p), processor(p)
{
    const auto bg = juce::Colour(0xff1e1e22);
    tabs.addTab("Turtle", bg, new TurtleView(p), true);
    tabs.addTab("Graph",  bg, new GraphView(p), true);
    tabs.addTab("Knobs",  bg, new ControlsView(p), true);

    addAndMakeVisible(tabs);
    setResizable(true, true);
    setResizeLimits(600, 400, 4000, 3000);
    setSize(960, 640);
}

void ValisEditor::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void ValisEditor::resized()
{
    tabs.setBounds(getLocalBounds());
}

}  // namespace valis
