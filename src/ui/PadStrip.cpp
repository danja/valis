// src/ui/PadStrip.cpp

#include "ui/PadStrip.h"

namespace valis {

namespace {

juce::String panText(double pan)
{
    const int percent = static_cast<int>(std::lround(std::abs(pan) * 100.0));
    return percent == 0 ? juce::String("C") : (pan < 0 ? "L" : "R") + juce::String(percent);
}

}  // namespace

PadStrip::PadStrip()
{
    audition.setButtonText(juce::CharPointer_UTF8("\xe2\x96\xb6"));   // play triangle
    audition.setTooltip("Play this sound");
    audition.held = [this](bool down) { if (onAudition) onAudition(down); };
    addAndMakeVisible(audition);

    keys.setTooltip("The key, or a range like 40-64, this sound answers");
    keys.setSelectAllWhenFocused(true);
    keys.setInputRestrictions(7, "0123456789-");
    keys.onReturnKey = keys.onFocusLost = [this] { if (! showing && onKeys) onKeys(keys.getText()); };
    addAndMakeVisible(keys);

    name.setTooltip("What this sound is called");
    name.onReturnKey = name.onFocusLost = [this] { if (! showing && onName) onName(name.getText()); };
    addAndMakeVisible(name);

    fileLabel.setMinimumHorizontalScale(0.7f);
    fileLabel.setTooltip("The sound file");
    addAndMakeVisible(fileLabel);

    load.onClick = [this] { if (onLoad) onLoad(); };
    addAndMakeVisible(load);

    remove.setTooltip("Remove this sound from the kit");
    remove.onClick = [this] { if (onRemove) onRemove(); };
    addAndMakeVisible(remove);

    level.setRange(-36.0, 12.0, 0.1);
    level.setDoubleClickReturnValue(true, 0.0);
    level.setTextValueSuffix(" dB");
    level.setTooltip("Level of this sound (double-click for 0 dB)");
    level.onDragStart = [this] { dragging = true; };
    level.onDragEnd = [this] { dragging = false; if (onLevel) onLevel(static_cast<float>(level.getValue())); };
    level.onValueChange = [this] { if (! dragging && ! showing && onLevel) onLevel(static_cast<float>(level.getValue())); };
    styleSlider(level);

    pan.setRange(-1.0, 1.0, 0.01);
    pan.setDoubleClickReturnValue(true, 0.0);
    pan.setTooltip("Pan: left to right (double-click for the centre)");
    pan.textFromValueFunction = panText;
    pan.onDragStart = [this] { dragging = true; };
    pan.onDragEnd = [this] { dragging = false; if (onPan) onPan(static_cast<float>(pan.getValue())); };
    pan.onValueChange = [this] { if (! dragging && ! showing && onPan) onPan(static_cast<float>(pan.getValue())); };
    styleSlider(pan);
}

void PadStrip::styleSlider(juce::Slider& s)
{
    s.setSliderStyle(juce::Slider::LinearHorizontal);
    s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 58, 20);
    s.setScrollWheelEnabled(false);   // the panel scrolls; a slider under the pointer must not take the wheel
    addAndMakeVisible(s);
}

void PadStrip::show(const SamplePad& pad)
{
    showing = true;
    if (! keys.hasKeyboardFocus(false)) keys.setText(pad.keys, false);
    if (! name.hasKeyboardFocus(false)) name.setText(pad.name, false);
    fileLabel.setText(juce::File::createFileWithoutCheckingPath(juce::String(pad.file)).getFileName()
                          + (pad.zones > 1 ? "  (" + juce::String(pad.zones) + " layers)" : juce::String()),
                      juce::dontSendNotification);
    if (! dragging)
    {
        level.setValue(pad.levelDb, juce::dontSendNotification);
        pan.setValue(pad.pan, juce::dontSendNotification);
    }
    showing = false;
}

void PadStrip::setTheme(const EquipmentTheme& t)
{
    theme = t;
    for (auto* b : { static_cast<juce::TextButton*>(&audition), &load, &remove })
    {
        b->setColour(juce::TextButton::buttonColourId, juce::Colour(theme.comboBg));
        b->setColour(juce::TextButton::textColourOffId, juce::Colour(theme.labelText));
    }
    for (auto* e : { &keys, &name })
    {
        e->setColour(juce::TextEditor::backgroundColourId, juce::Colour(theme.meterBg));
        e->setColour(juce::TextEditor::textColourId, juce::Colour(theme.meterText));
        e->setColour(juce::TextEditor::outlineColourId, juce::Colour(theme.edgeDark));
    }
    fileLabel.setColour(juce::Label::textColourId, juce::Colour(theme.dimText));
    for (auto* s : { &level, &pan })
    {
        s->setColour(juce::Slider::trackColourId, juce::Colour(theme.accent));
        s->setColour(juce::Slider::textBoxTextColourId, juce::Colour(theme.labelText));
        s->setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(theme.edgeDark));
    }
    repaint();
}

void PadStrip::resized()
{
    auto area = getLocalBounds().reduced(4, 4);
    audition.setBounds(area.removeFromLeft(30));
    area.removeFromLeft(6);
    keys.setBounds(area.removeFromLeft(58));
    area.removeFromLeft(6);
    name.setBounds(area.removeFromLeft(130));
    area.removeFromLeft(6);
    remove.setBounds(area.removeFromRight(26));
    area.removeFromRight(6);
    const int sliders = juce::jmax(160, area.getWidth() * 55 / 100);
    auto right = area.removeFromRight(sliders);
    pan.setBounds(right.removeFromRight(right.getWidth() * 42 / 100));
    level.setBounds(right);
    area.removeFromRight(6);
    load.setBounds(area.removeFromRight(70));
    area.removeFromRight(6);
    fileLabel.setBounds(area);
}

void PadStrip::paint(juce::Graphics& g)
{
    g.setColour(juce::Colour(theme.edgeDark).withAlpha(0.35f));
    g.drawHorizontalLine(getHeight() - 1, 4.0f, static_cast<float>(getWidth() - 4));
}

}  // namespace valis
