// src/ui/ValisEditor.cpp

#include "ui/ValisEditor.h"

#include "plugin/ValisProcessor.h"

namespace valis {

namespace {
/// Stands in for a view until its milestone lands, so the tab layout and the
/// plugin wrapper can be exercised in a host from M0 onwards.
class PlaceholderView final : public juce::Component
{
public:
    explicit PlaceholderView(juce::String labelText) : text(std::move(labelText)) {}

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff1e1e22));
        g.setColour(juce::Colours::grey);
        g.setFont(juce::FontOptions(16.0f));
        g.drawText(text, getLocalBounds(), juce::Justification::centred);
    }

private:
    juce::String text;
};
}  // namespace

ValisEditor::ValisEditor(ValisProcessor& p) : juce::AudioProcessorEditor(&p), processor(p)
{
    const auto bg = juce::Colour(0xff1e1e22);
    tabs.addTab("Turtle", bg, new PlaceholderView("Turtle editor - M7"), true);
    tabs.addTab("Graph",  bg, new PlaceholderView("Graph view - M9"),    true);
    tabs.addTab("Knobs",  bg, new PlaceholderView("Controls - M8"),      true);

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
