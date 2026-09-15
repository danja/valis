// src/ui/SampleBox.cpp

#include "ui/SampleBox.h"

#include "plugin/ValisProcessor.h"

namespace valis {

SampleBox::SampleBox(ValisProcessor& p, std::string node, juce::String boxTitle)
    : processor(p), nodeId(std::move(node)), title(std::move(boxTitle))
{
    addAndMakeVisible(loadButton);
    loadButton.onClick = [this] { chooseFile(); };
    refresh();
}

SampleBox::~SampleBox() = default;

void SampleBox::setTheme(const EquipmentTheme& t)
{
    theme = t;
    loadButton.setColour(juce::TextButton::buttonColourId, juce::Colour(theme.comboBg));
    loadButton.setColour(juce::TextButton::textColourOffId, juce::Colour(theme.labelText));
    repaint();
}

void SampleBox::refresh()
{
    const auto path = processor.sampleFor(nodeId);
    const juce::String full(path);
    fileName = full.isEmpty() ? "(no file)"
                              : juce::File::createFileWithoutCheckingPath(full).getFileName();

    // A relative path names no directory, so it survives createFileWithoutCheckingPath
    // unchanged; take the last segment either way.
    if (fileName.isEmpty())
        fileName = full.fromLastOccurrenceOf("/", false, false);
}

void SampleBox::chooseFile()
{
    chooser = std::make_unique<juce::FileChooser>(
        "Choose a sound file for " + title,
        juce::File(VALIS_EXAMPLES_DIR).getChildFile("samples"),
        "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");

    chooser->launchAsync(juce::FileBrowserComponent::openMode
                             | juce::FileBrowserComponent::canSelectFiles,
                         [this](const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file == juce::File())
            return;

        std::string error;
        if (processor.setSample(nodeId, file.getFullPathName().toStdString(), error))
        {
            message.clear();
        }
        else
        {
            // The previous file keeps playing; say why the new one did not.
            message = juce::String(error).fromLastOccurrenceOf(": ", false, false);
            if (message.isEmpty())
                message = juce::String(error);
        }

        refresh();
        repaint();
    });
}

void SampleBox::resized()
{
    auto area = getLocalBounds().reduced(10);
    loadButton.setBounds(area.removeFromBottom(26).reduced(2, 0));
}

void SampleBox::paint(juce::Graphics& g)
{
    const juce::Colour edgeDark(theme.edgeDark);
    const juce::Colour edgeLight(theme.edgeLight);

    const auto plate = getLocalBounds().toFloat().reduced(4.0f);
    g.setColour(juce::Colour(theme.plateBg));
    g.fillRoundedRectangle(plate, 4.0f);
    g.setColour(edgeDark);
    g.drawRoundedRectangle(plate, 4.0f, 1.4f);

    auto area = getLocalBounds().reduced(10);

    g.setColour(juce::Colour(theme.labelText));
    g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    g.drawText(title.toUpperCase(), area.removeFromTop(16), juce::Justification::centredLeft, true);

    area.removeFromBottom(30);   // the Load button's row

    // The file name in a lit window, the way a tape machine shows its reel.
    const auto window = area.removeFromTop(40).toFloat();
    g.setColour(juce::Colour(theme.meterBg));
    g.fillRoundedRectangle(window, 3.0f);
    g.setColour(edgeLight.withAlpha(0.4f));
    g.drawRoundedRectangle(window, 3.0f, 1.0f);

    g.setColour(juce::Colour(theme.meterText));
    g.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain));
    g.drawFittedText(fileName, window.toNearestInt().reduced(6, 2),
                     juce::Justification::centredLeft, 2, 0.8f);

    if (message.isNotEmpty())
    {
        g.setColour(juce::Colour(theme.accent));
        g.setFont(juce::FontOptions(11.0f));
        g.drawFittedText(message, area.reduced(0, 2), juce::Justification::topLeft, 2, 0.8f);
    }
}

}  // namespace valis
