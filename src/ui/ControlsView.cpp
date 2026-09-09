// src/ui/ControlsView.cpp

#include "ui/ControlsView.h"

#include "ui/ScopeBox.h"
#include "ui/SpectrumBox.h"
#include "plugin/ValisProcessor.h"
#include "valis/Ontology.h"
#include "valis/Vocabulary.h"

namespace valis {

namespace {
constexpr int kKnobWidth          = 104;
constexpr int kKnobHeight         = 152;
constexpr int kNameHeight         = 18;
constexpr int kValueHeight        = 20;
constexpr int kTargetHeight       = 14;
constexpr int kSectionLabelHeight = 18;  // label drawn above each row of knobs
constexpr int kRowHeight          = kSectionLabelHeight + kKnobHeight;
constexpr int kMargin             = 12;
constexpr int kTopBand            = 30;  // brand plate and pilot jewel
constexpr int kPlateInset          = 6;  // chassis edge around the faceplate
}

void drawScrew(juce::Graphics& g, juce::Point<float> centre, float angle,
               const EquipmentTheme& theme)
{
    g.setColour(juce::Colour(theme.screw));
    g.fillEllipse(centre.x - 5.0f, centre.y - 5.0f, 10.0f, 10.0f);
    g.setColour(juce::Colour(theme.screwSlot));
    const juce::Line<float> slot(centre.getPointOnCircumference(3.5f, angle),
                                 centre.getPointOnCircumference(3.5f, angle + juce::MathConstants<float>::pi));
    g.drawLine(slot, 1.8f);
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
    // The dial LookAndFeel on this view covers the whole tab: children with
    // no LookAndFeel of their own inherit it.
    setLookAndFeel(&equipmentLnf);
    applyTheme();
    emptyMessage.setJustificationType(juce::Justification::centred);
    emptyMessage.setFont(juce::FontOptions(15.0f));
    emptyMessage.setText("This circuit declares no val:Param bindings.\n"
                         "Add one in the Code tab to put a knob here.",
                         juce::dontSendNotification);
    addAndMakeVisible(emptyMessage);

    rebuild();
    startTimerHz(30);
}

void ControlsView::applyTheme()
{
    theme = themeByName(processor.getUiTheme().toStdString());
    equipmentLnf.setScheme(theme);
    emptyMessage.setColour(juce::Label::textColourId, juce::Colour(theme.dimText));
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
    // Cheap poll: the panel only has to change when the circuit does. Identity
    // is the ordered element and binding lists, not just their sizes, so a new
    // circuit with the same shape still rebuilds the boxes.
    const auto& model = processor.circuit();
    std::vector<std::string> elementIds, paramKeys;
    for (const auto& elem : model.elements())
        elementIds.push_back(elem.id);
    for (const auto& binding : model.params())
        paramKeys.push_back(binding.targetNode + "." + binding.propertySymbol +
                            "#" + std::to_string(binding.slot));
    if (elementIds != lastElementIds || paramKeys != lastParamKeys)
        rebuild();

    for (auto& scope : scopes)
        scope->refresh();
    for (auto& spectrum : spectrums)
        spectrum->refresh();
}

void ControlsView::rebuild()
{
    applyTheme();
    knobs.clear();
    scopes.clear();
    spectrums.clear();

    const auto& model = processor.circuit();
    lastElementIds.clear();
    lastParamKeys.clear();
    for (const auto& elem : model.elements())
        lastElementIds.push_back(elem.id);

    for (const auto& binding : model.params())
    {
        lastParamKeys.push_back(binding.targetNode + "." + binding.propertySymbol +
                                "#" + std::to_string(binding.slot));

        const auto* element = model.findElement(binding.targetNode);
        if (element == nullptr || element->type == nullptr)
            continue;

        const auto* port = element->type->findProperty(binding.propertySymbol);
        if (port == nullptr)
            continue;

        Knob knob;

        knob.minimum = port->minimum;
        knob.maximum = port->maximum;
        knob.unit    = port->unitSymbol.empty() ? juce::String()
                                                : " " + juce::String(port->unitSymbol);
        knob.target  = juce::String(vocab::shortName(binding.targetNode)) + "." +
                       juce::String(binding.propertySymbol);

        knob.name = std::make_unique<juce::Label>();
        knob.name->setJustificationType(juce::Justification::centred);
        knob.name->setColour(juce::Label::textColourId, juce::Colour(theme.labelText));
        knob.name->setFont(juce::FontOptions(13.0f, juce::Font::bold));
        knob.name->setText(binding.name.empty() ? port->name : binding.name,
                           juce::dontSendNotification);
        addAndMakeVisible(*knob.name);

        knob.sectionName = binding.section;

        const auto paramId = "p" + juce::String(binding.slot).paddedLeft('0', 2);

        if (port->enumeration && ! port->scalePoints.empty())
        {
            knob.comboBox = std::make_unique<juce::ComboBox>();
            knob.comboBox->setColour(juce::ComboBox::backgroundColourId, juce::Colour(theme.comboBg));
            knob.comboBox->setColour(juce::ComboBox::textColourId, juce::Colour(theme.labelText));
            knob.comboBox->setColour(juce::ComboBox::outlineColourId, juce::Colour(theme.edgeDark));
            knob.comboBox->setColour(juce::ComboBox::arrowColourId, juce::Colour(theme.accent));
            for (const auto& [value, label] : port->scalePoints)
                knob.comboBox->addItem(label, static_cast<int>(value) + 1);
            addAndMakeVisible(*knob.comboBox);
            knob.comboAttachment =
                std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
                    processor.state(), paramId, *knob.comboBox);
        }
        else
        {
            knob.slider = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag,
                                                          juce::Slider::NoTextBox);
            // Drawn by EquipmentLookAndFeel; no per-slider colours needed.
            addAndMakeVisible(*knob.slider);
            knob.attachment =
                std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                    processor.state(), paramId, *knob.slider);
            knob.slider->onValueChange = [this] { repaint(); };
        }

        knobs.push_back(std::move(knob));
    }

    // Graphic boxes for monitor elements: an oscilloscope per Oscilloscope
    // node, a spectrum analyzer per FreqAnalyzer node. Each takes the
    // footprint of two knobs.
    static_assert(ScopeBox::kWidth == SpectrumBox::kWidth, "monitor boxes share a column");
    static_assert(ScopeBox::kHeight == SpectrumBox::kHeight, "monitor boxes share a row height");
    for (const auto& elem : model.elements())
    {
        if (elem.type == nullptr)
            continue;

        const juce::String label = elem.label.empty()
            ? juce::String(vocab::shortName(elem.id))
            : juce::String(elem.label);

        if (elem.type->implementation == "Oscilloscope")
        {
            auto box = std::make_unique<ScopeBox>(processor, elem.id, label);
            box->setTheme(theme);
            addAndMakeVisible(*box);
            scopes.push_back(std::move(box));
        }
        else if (elem.type->implementation == "FreqAnalyzer")
        {
            auto box = std::make_unique<SpectrumBox>(processor, elem.id, label);
            box->setTheme(theme);
            addAndMakeVisible(*box);
            spectrums.push_back(std::move(box));
        }
    }

    emptyMessage.setVisible(knobs.empty() && scopes.empty() && spectrums.empty());
    resized();
    repaint();
}

void ControlsView::paint(juce::Graphics& g)
{
    const juce::Colour panel(theme.panel);
    const juce::Colour face(theme.faceplate);
    const juce::Colour edgeDark(theme.edgeDark);
    const juce::Colour edgeLight(theme.edgeLight);
    const juce::Colour ink(theme.labelText);
    const juce::Colour dim(theme.dimText);
    const juce::Colour accent(theme.accent);
    const juce::Colour glow(theme.glow);

    g.fillAll(panel);

    // Faceplate with an engraved edge and a raised inner highlight.
    const auto plate = getLocalBounds().reduced(kPlateInset).toFloat();
    g.setColour(face);
    g.fillRoundedRectangle(plate, 6.0f);
    g.setColour(edgeDark);
    g.drawRoundedRectangle(plate, 6.0f, 2.0f);
    g.setColour(edgeLight.withAlpha(0.35f));
    g.drawRoundedRectangle(plate.reduced(3.0f), 4.0f, 1.0f);

    // Chassis screws in the faceplate corners.
    if (plate.getWidth() > 60.0f && plate.getHeight() > 60.0f)
    {
        drawScrew(g, plate.getTopLeft() + juce::Point<float>(14.0f, 14.0f), 0.7f, theme);
        drawScrew(g, plate.getTopRight() + juce::Point<float>(-14.0f, 14.0f), 2.1f, theme);
        drawScrew(g, plate.getBottomLeft() + juce::Point<float>(14.0f, -14.0f), 1.4f, theme);
        drawScrew(g, plate.getBottomRight() + juce::Point<float>(-14.0f, -14.0f), 2.8f, theme);
    }

    // Brand plate and pilot jewel.
    const float bandBottom = plate.getY() + kTopBand;
    g.setFont(juce::FontOptions(15.0f, juce::Font::bold));
    g.setColour(ink);
    g.drawText("VALIS", plate.getX() + 26.0f, plate.getY() + 2.0f,
               120.0f, kTopBand - 4.0f, juce::Justification::centredLeft, true);
    g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    g.setColour(dim);
    g.drawText("PARAMETER CONTROLLER", plate.getX() + 86.0f, plate.getY() + 2.0f,
               220.0f, kTopBand - 4.0f, juce::Justification::centredLeft, true);
    const auto jewel = juce::Point<float>(plate.getRight() - 26.0f, plate.getY() + kTopBand * 0.5f);
    g.setColour(glow);
    g.fillEllipse(jewel.x - 8.0f, jewel.y - 8.0f, 16.0f, 16.0f);
    g.setColour(accent);
    g.fillEllipse(jewel.x - 4.5f, jewel.y - 4.5f, 9.0f, 9.0f);
    g.setColour(edgeDark);
    g.drawLine(jewel.x + 8.0f, jewel.y, plate.getRight() - 10.0f, jewel.y, 1.0f);
    g.setColour(edgeDark);
    g.drawLine(plate.getX() + 10.0f, bandBottom, jewel.x - 8.0f, bandBottom, 1.0f);
    g.setColour(edgeLight.withAlpha(0.5f));
    g.drawLine(plate.getX() + 10.0f, bandBottom + 1.0f, jewel.x - 8.0f, bandBottom + 1.0f, 1.0f);

    // Engraved grooves between adjacent sections on the same row.
    for (const auto& d : vertDivs)
    {
        g.setColour(edgeDark);
        g.fillRect(d.x, d.yTop, 1, d.yBot - d.yTop);
        g.setColour(edgeLight.withAlpha(0.5f));
        g.fillRect(d.x + 1, d.yTop, 1, d.yBot - d.yTop);
    }

    // Section name plates.
    g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    for (const auto& h : sectionHeaders)
    {
        const auto strip = juce::Rectangle<float>(static_cast<float>(h.x), static_cast<float>(h.y),
                                                  static_cast<float>(h.w),
                                                  kSectionLabelHeight - 2.0f);
        g.setColour(juce::Colour(theme.plateBg));
        g.fillRoundedRectangle(strip, 2.0f);
        g.setColour(edgeDark);
        g.drawRoundedRectangle(strip, 2.0f, 1.0f);
        g.setColour(ink);
        g.drawText(h.name.toUpperCase(), strip.getX() + 6.0f, strip.getY(),
                   strip.getWidth() - 6.0f, strip.getHeight(),
                   juce::Justification::centredLeft, true);
    }

    const auto mono = juce::FontOptions(juce::Font::getDefaultMonospacedFontName(),
                                        13.0f, juce::Font::bold);
    for (const auto& knob : knobs)
    {
        const juce::Rectangle<int> area = knob.isEnum()
            ? knob.comboBox->getBounds()
            : knob.slider->getBounds();

        if (! knob.isEnum())
        {
            g.setColour(accent);
            g.setFont(mono);
            g.drawText(knob.readout(),
                       area.getX(), area.getBottom() - kValueHeight, area.getWidth(), kValueHeight,
                       juce::Justification::centred, false);
        }

        g.setColour(dim);
        g.setFont(juce::FontOptions(11.0f));
        g.drawText(knob.target,
                   area.getX(), area.getBottom(), area.getWidth(), kTargetHeight,
                   juce::Justification::centred, true);
    }
}

void ControlsView::parentSizeChanged()
{
    if (auto* vp = findParentComponentOfClass<juce::Viewport>())
        setSize(vp->getMaximumVisibleWidth(), getHeight());
}

void ControlsView::resized()
{
    const int w = getWidth();
    if (w <= 0)
        return;

    const int perRow = juce::jmax(1, (w - 2 * kMargin) / kKnobWidth);

    // Group consecutive knobs that share the same sectionName.
    struct Group { juce::String sec; int start, count; };
    std::vector<Group> groups;
    for (int i = 0; i < static_cast<int>(knobs.size()); ++i)
    {
        const auto& sec = knobs[static_cast<std::size_t>(i)].sectionName;
        if (groups.empty() || groups.back().sec != sec)
            groups.push_back({ sec, i, 1 });
        else
            ++groups.back().count;
    }

    // Shared measure/layout pass. When apply is true, sets component bounds
    // and populates sectionHeaders and vertDivs.
    auto pass = [&](bool apply) -> int
    {
        // Content starts below the brand band painted at the top.
        int curY = kMargin + kTopBand;
        int col  = 0;

        for (auto& g : groups)
        {
            // Flush to a new row if this group would straddle the boundary.
            // This keeps every section wholly on one row (unless it is wider
            // than perRow, in which case wrapping is unavoidable).
            if (col > 0 && col + g.count > perRow)
            {
                curY += kRowHeight;
                col = 0;
            }

            if (apply)
            {
                // Section label spanning this group's column range.
                if (g.sec.isNotEmpty())
                {
                    const int hW = juce::jmin(g.count, perRow - col) * kKnobWidth;
                    sectionHeaders.push_back({ kMargin + col * kKnobWidth, curY, hW, g.sec });
                }

                // Vertical divider before mid-row group starts.
                if (col > 0)
                    vertDivs.push_back({ kMargin + col * kKnobWidth,
                                         curY, curY + kRowHeight });
            }

            // Place each knob in the group.
            for (int ki = g.start; ki < g.start + g.count; ++ki)
            {
                if (apply)
                {
                    auto& knob = knobs[static_cast<std::size_t>(ki)];
                    const int x = kMargin + col * kKnobWidth;
                    const int y = curY + kSectionLabelHeight;
                    juce::Rectangle<int> cell(x, y, kKnobWidth, kKnobHeight);

                    knob.name->setBounds(cell.removeFromTop(kNameHeight));
                    cell.removeFromBottom(kTargetHeight);

                    if (knob.isEnum())
                        knob.comboBox->setBounds(cell.removeFromTop(28).reduced(4, 2));
                    else
                        knob.slider->setBounds(cell.reduced(2, 2));
                }

                ++col;
                if (col >= perRow)
                {
                    curY += kRowHeight;
                    col = 0;
                }
            }
        }

        // Flush the last partial row, or reserve space for the empty message.
        if (col > 0 || (groups.empty() && scopes.empty() && spectrums.empty()))
            curY += kRowHeight;

        // Monitor boxes flow left to right, each two knob cells wide.
        const int boxCount = static_cast<int>(scopes.size() + spectrums.size());
        if (boxCount > 0)
        {
            if (apply)
                sectionHeaders.push_back({ kMargin, curY, w - 2 * kMargin, "Monitors" });
            curY += kSectionLabelHeight;

            const int boxCols = juce::jmax(1, (w - 2 * kMargin) / ScopeBox::kWidth);
            int boxCol = 0;
            const auto place = [&](juce::Component& box)
            {
                if (apply)
                    box.setBounds(kMargin + boxCol * ScopeBox::kWidth, curY,
                                  ScopeBox::kWidth, ScopeBox::kHeight);
                if (++boxCol >= boxCols)
                {
                    curY += ScopeBox::kHeight;
                    boxCol = 0;
                }
            };
            for (auto& scope : scopes)
                place(*scope);
            for (auto& spectrum : spectrums)
                place(*spectrum);
            if (boxCol > 0)
                curY += ScopeBox::kHeight;
        }

        return curY + kMargin;
    };

    // Measure first to get the required height.
    const int needed = pass(false);
    if (needed != getHeight())
    {
        setSize(w, needed);
        return;
    }

    emptyMessage.setBounds(getLocalBounds().reduced(40));

    sectionHeaders.clear();
    vertDivs.clear();
    pass(true);
    repaint();
}

}  // namespace valis
