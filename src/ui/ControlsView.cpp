// src/ui/ControlsView.cpp

#include "ui/ControlsView.h"

#include "plugin/ValisProcessor.h"
#include "valis/Ontology.h"
#include "valis/Vocabulary.h"

namespace valis {

namespace {
constexpr int kKnobWidth   = 104;
constexpr int kKnobHeight  = 152;
constexpr int kNameHeight  = 18;
constexpr int kValueHeight = 20;
constexpr int kTargetHeight = 14;
}

juce::String ControlsView::Knob::readout() const
{
    if (slider == nullptr)
        return {};

    const auto real = minimum + slider->getValue() * (maximum - minimum);
    const auto span = maximum - minimum;
    const int decimals = span > 100.0 ? 0 : (span > 1.0 ? 2 : 3);
    return juce::String(real, decimals) + unit;
}

ControlsView::ControlsView(ValisProcessor& p) : processor(p)
{
    p.addChangeListener(this);
    emptyMessage.setJustificationType(juce::Justification::centred);
    emptyMessage.setColour(juce::Label::textColourId, juce::Colours::grey);
    emptyMessage.setFont(juce::FontOptions(15.0f));
    emptyMessage.setText("This circuit declares no val:Param bindings.\n"
                         "Add one in the Turtle view to put a knob here.",
                         juce::dontSendNotification);
    addAndMakeVisible(emptyMessage);

    rebuild();
    startTimerHz(2);
}

ControlsView::~ControlsView()
{
    processor.removeChangeListener(this);
    // Attachments must go before the sliders they reference.
    knobs.clear();
}

void ControlsView::changeListenerCallback(juce::ChangeBroadcaster*)
{
    rebuild();
}

void ControlsView::timerCallback()
{
    // Cheap poll: the panel only has to change when the circuit does.
    const auto count = static_cast<int>(processor.circuit().params().size());
    if (count != lastBindingCount)
        rebuild();
}

void ControlsView::rebuild()
{
    knobs.clear();

    const auto& model = processor.circuit();
    lastBindingCount = static_cast<int>(model.params().size());

    for (const auto& binding : model.params())
    {
        const auto* element = model.findElement(binding.targetNode);
        if (element == nullptr || element->type == nullptr)
            continue;

        const auto* port = element->type->findProperty(binding.propertySymbol);
        if (port == nullptr)
            continue;

        Knob knob;
        knob.slider = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag,
                                                     juce::Slider::NoTextBox);
        knob.slider->setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff61afef));
        knob.slider->setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff3a3f4b));
        knob.slider->setColour(juce::Slider::thumbColourId, juce::Colour(0xffabb2bf));
        knob.slider->setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffabb2bf));
        knob.slider->setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);

        // The slot is normalised 0-1; the readout shows the real value in the
        // property's own units, which is what the user is actually setting.
        // Drawn in paint() rather than through Slider's text box, so the
        // formatting and the units are ours.
        knob.minimum = port->minimum;
        knob.maximum = port->maximum;
        knob.unit    = port->unitSymbol.empty() ? juce::String()
                                                : " " + juce::String(port->unitSymbol);

        addAndMakeVisible(*knob.slider);

        knob.name = std::make_unique<juce::Label>();
        knob.name->setJustificationType(juce::Justification::centred);
        knob.name->setColour(juce::Label::textColourId, juce::Colour(0xffabb2bf));
        knob.name->setFont(juce::FontOptions(13.0f, juce::Font::bold));
        knob.name->setText(binding.name.empty() ? port->name : binding.name,
                           juce::dontSendNotification);
        addAndMakeVisible(*knob.name);

        knob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor.state(),
            "p" + juce::String(binding.slot).paddedLeft('0', 2),
            *knob.slider);

        knob.target = juce::String(vocab::shortName(binding.targetNode)) + "." +
                      juce::String(binding.propertySymbol);

        // Repaint the readout as the knob moves.
        knob.slider->onValueChange = [this] { repaint(); };

        knobs.push_back(std::move(knob));
    }

    emptyMessage.setVisible(knobs.empty());
    resized();
    repaint();
}

void ControlsView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1e1e22));

    for (const auto& knob : knobs)
    {
        const auto area = knob.slider->getBounds();

        // The value, in the property's own units.
        g.setColour(juce::Colour(0xffabb2bf));
        g.setFont(juce::FontOptions(13.0f));
        g.drawText(knob.readout(),
                   area.getX(), area.getBottom() - kValueHeight, area.getWidth(), kValueHeight,
                   juce::Justification::centred, false);

        // And a faint caption naming what it drives, so the panel stays
        // legible against the circuit.
        g.setColour(juce::Colour(0xff5a6070));
        g.setFont(juce::FontOptions(11.0f));
        g.drawText(knob.target,
                   area.getX(), area.getBottom(), area.getWidth(), kTargetHeight,
                   juce::Justification::centred, true);
    }
}

void ControlsView::resized()
{
    emptyMessage.setBounds(getLocalBounds().reduced(40));

    auto area = getLocalBounds().reduced(12);
    const int perRow = juce::jmax(1, area.getWidth() / kKnobWidth);

    int index = 0;
    for (auto& knob : knobs)
    {
        const int row = index / perRow;
        const int column = index % perRow;

        juce::Rectangle<int> cell(area.getX() + column * kKnobWidth,
                                  area.getY() + row * kKnobHeight,
                                  kKnobWidth, kKnobHeight);

        knob.name->setBounds(cell.removeFromTop(kNameHeight));
        cell.removeFromBottom(kTargetHeight);   // the caption paint() draws

        // The slider owns the dial and the value box together; JUCE puts the
        // box at the bottom of whatever bounds it is given, so it needs room
        // for both or the readout is simply not drawn.
        knob.slider->setBounds(cell.reduced(2, 2));
        ++index;
    }
}

}  // namespace valis
