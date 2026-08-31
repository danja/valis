// src/ui/GraphView.cpp

#include "ui/GraphView.h"

#include "plugin/ValisProcessor.h"
#include "valis/Ontology.h"
#include "valis/Vocabulary.h"

namespace valis {

namespace {
constexpr float kNodeWidth   = 150.0f;
constexpr float kNodeHeight  = 62.0f;
constexpr float kPinRadius   = 5.0f;
constexpr float kColumnGap   = 210.0f;
constexpr float kRowGap      = 100.0f;

juce::Colour colourForType(const ElementType* type)
{
    if (type == nullptr)
        return juce::Colour(0xff5a6070);

    const auto& key = type->implementation;
    if (key == "Input" || key == "Output")                      return juce::Colour(0xff56b6c2);
    if (key == "UnitDelay")                                     return juce::Colour(0xffe5c07b);
    if (! type->linear)                                         return juce::Colour(0xffe06c75);
    return juce::Colour(0xff61afef);
}
}  // namespace

GraphView::GraphView(ValisProcessor& p) : processor(p)
{
    rebuild();
    startTimerHz(4);
}

GraphView::~GraphView() = default;

OpDispatcher GraphView::ops()
{
    // The same surface the MCP server uses: one implementation, two callers.
    return processor.ops();
}

void GraphView::timerCallback()
{
    const auto& model = processor.circuit();
    const auto elements = static_cast<int>(model.elements().size());
    const auto arcs     = static_cast<int>(model.arcs().size());

    if (elements != lastElementCount || arcs != lastArcCount)
        rebuild();
}

void GraphView::autolayout()
{
    positions.clear();

    const auto& model = processor.circuit();

    // Replicate the depth pass so we can measure columns and rows before placing.
    std::map<std::string, int> depth;
    for (const auto& node : nodes) depth[node.id] = 0;

    for (int pass = 0; pass < static_cast<int>(nodes.size()); ++pass)
    {
        bool changed = false;
        for (const auto& arc : model.arcs())
        {
            const auto from = depth.find(arc.fromNode);
            const auto to   = depth.find(arc.toNode);
            if (from == depth.end() || to == depth.end()) continue;
            if (to->second < from->second + 1) { to->second = from->second + 1; changed = true; }
        }
        if (! changed) break;
    }

    std::map<int, int> rowsPerCol;
    for (const auto& node : nodes) rowsPerCol[depth[node.id]]++;

    int maxCol = 0, maxRows = 0;
    for (const auto& [col, count] : rowsPerCol)
    {
        maxCol  = juce::jmax(maxCol, col);
        maxRows = juce::jmax(maxRows, count);
    }

    // Scale column and row gaps to fill the visible area without overlap.
    float colGap = kColumnGap;
    float rowGap = kRowGap;

    if (auto* vp = findParentComponentOfClass<juce::Viewport>())
    {
        const float visW = static_cast<float>(vp->getMaximumVisibleWidth())  - 80.0f;
        const float visH = static_cast<float>(vp->getMaximumVisibleHeight()) - 80.0f;

        if (maxCol > 0)
            colGap = juce::jmax(kNodeWidth + 20.0f,
                                juce::jmin(kColumnGap, visW / static_cast<float>(maxCol)));
        if (maxRows > 1)
            rowGap = juce::jmax(kNodeHeight + 10.0f,
                                juce::jmin(kRowGap, visH / static_cast<float>(maxRows - 1)));
    }

    std::map<int, int> perColumn;
    for (auto& node : nodes)
    {
        const int col = depth[node.id];
        const int row = perColumn[col]++;
        positions[node.id] = { 40.0f + static_cast<float>(col) * colGap,
                               40.0f + static_cast<float>(row) * rowGap };
    }

    layout();

    if (auto* vp = findParentComponentOfClass<juce::Viewport>())
        vp->setViewPosition(0, 0);

    repaint();
}

void GraphView::rebuild()
{
    const auto& model = processor.circuit();
    lastElementCount = static_cast<int>(model.elements().size());
    lastArcCount     = static_cast<int>(model.arcs().size());

    nodes.clear();

    for (const auto& element : model.elements())
    {
        NodeBox box;
        box.id        = element.id;
        box.label     = element.label.empty() ? vocab::shortName(element.id) : element.label;
        box.typeLabel = vocab::shortName(element.typeIri);

        if (element.type != nullptr)
        {
            for (const auto& port : element.type->ports)
                box.pins.push_back({element.id, port.symbol, port.input, port.control, {}});
        }

        nodes.push_back(std::move(box));
    }

    layout();
    repaint();
}

void GraphView::layout()
{
    const auto& model = processor.circuit();

    // Depth from the inputs gives a readable left-to-right flow. Feedback arcs
    // are ignored for placement, which is what makes the loop visible as a
    // line running backwards rather than as a tangle.
    std::map<std::string, int> depth;
    for (const auto& node : nodes)
        depth[node.id] = 0;

    for (int pass = 0; pass < static_cast<int>(nodes.size()); ++pass)
    {
        bool changed = false;
        for (const auto& arc : model.arcs())
        {
            const auto from = depth.find(arc.fromNode);
            const auto to   = depth.find(arc.toNode);
            if (from == depth.end() || to == depth.end())
                continue;

            if (to->second < from->second + 1)
            {
                to->second = from->second + 1;
                changed = true;
            }
        }
        if (! changed)
            break;
    }

    std::map<int, int> perColumn;
    for (auto& node : nodes)
    {
        // A position the user has dragged wins over the automatic layout.
        if (const auto saved = positions.find(node.id); saved != positions.end())
        {
            node.bounds = {saved->second.x, saved->second.y, kNodeWidth, kNodeHeight};
        }
        else
        {
            const int column = depth[node.id];
            const int row = perColumn[column]++;
            node.bounds = {40.0f + static_cast<float>(column) * kColumnGap,
                           40.0f + static_cast<float>(row) * kRowGap,
                           kNodeWidth, kNodeHeight};
        }

        // Inputs on the left edge, outputs on the right, in declaration order.
        int inputs = 0, outputs = 0;
        for (const auto& pin : node.pins)
            (pin.input ? inputs : outputs)++;

        int inputIndex = 0, outputIndex = 0;
        for (auto& pin : node.pins)
        {
            const int count = pin.input ? inputs : outputs;
            const int index = pin.input ? inputIndex++ : outputIndex++;
            const float t = (static_cast<float>(index) + 1.0f) / (static_cast<float>(count) + 1.0f);

            pin.position = {pin.input ? node.bounds.getX() : node.bounds.getRight(),
                            node.bounds.getY() + t * node.bounds.getHeight()};
        }
    }

    // Size to content so the parent Viewport can scroll.
    float maxX = 200.0f, maxY = 200.0f;
    for (const auto& node : nodes)
    {
        maxX = juce::jmax(maxX, node.bounds.getRight() + 40.0f);
        maxY = juce::jmax(maxY, node.bounds.getBottom() + 40.0f);
    }
    const auto neededW = static_cast<int>(maxX);
    const auto neededH = static_cast<int>(maxY);
    if (neededW != getWidth() || neededH != getHeight())
        setSize(neededW, neededH);
}

const GraphView::NodeBox* GraphView::nodeAt(juce::Point<float> point) const
{
    for (auto it = nodes.rbegin(); it != nodes.rend(); ++it)
        if (it->bounds.contains(point))
            return &*it;
    return nullptr;
}

const GraphView::Pin* GraphView::pinAt(juce::Point<float> point) const
{
    for (const auto& node : nodes)
        for (const auto& pin : node.pins)
            if (pin.position.getDistanceFrom(point) < kPinRadius * 2.5f)
                return &pin;
    return nullptr;
}

void GraphView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff17171b));

    const auto& model = processor.circuit();
    const auto& ontology = processor.ontology();

    // -- arcs --------------------------------------------------------------
    for (const auto& arc : model.arcs())
    {
        const Pin* from = nullptr;
        const Pin* to   = nullptr;

        for (const auto& node : nodes)
            for (const auto& pin : node.pins)
            {
                if (pin.node == arc.fromNode && pin.port == arc.fromPort && ! pin.input) from = &pin;
                if (pin.node == arc.toNode   && pin.port == arc.toPort   && pin.input)   to   = &pin;
            }

        if (from == nullptr || to == nullptr)
            continue;

        // A control arc is drawn dashed, so modulation reads differently from
        // signal at a glance.
        const bool control = from->control;
        g.setColour(control ? juce::Colour(0xffc678dd) : juce::Colour(0xff8b92a0));

        juce::Path path;
        path.startNewSubPath(from->position);
        const float reach = juce::jmax(40.0f, std::abs(to->position.x - from->position.x) * 0.5f);
        path.cubicTo(from->position.translated(reach, 0.0f),
                     to->position.translated(-reach, 0.0f),
                     to->position);

        if (control)
        {
            const float dashes[] = {5.0f, 4.0f};
            juce::Path dashed;
            juce::PathStrokeType(1.6f).createDashedStroke(dashed, path, dashes, 2);
            g.fillPath(dashed);
        }
        else
        {
            g.strokePath(path, juce::PathStrokeType(1.8f));
        }
    }

    // The arc being dragged.
    if (draggingFrom.has_value())
    {
        g.setColour(juce::Colour(0xffe5c07b));
        juce::Path path;
        path.startNewSubPath(draggingFrom->position);
        path.lineTo(dragTo);
        g.strokePath(path, juce::PathStrokeType(1.5f));
    }

    // -- nodes -------------------------------------------------------------
    for (const auto& node : nodes)
    {
        const auto* element = model.findElement(node.id);
        const auto* type = element != nullptr ? element->type : nullptr;
        const auto accent = colourForType(type);

        g.setColour(juce::Colour(0xff23232a));
        g.fillRoundedRectangle(node.bounds, 5.0f);
        g.setColour(accent.withAlpha(0.7f));
        g.drawRoundedRectangle(node.bounds, 5.0f, 1.4f);

        g.setColour(accent);
        g.fillRect(node.bounds.getX() + 1.0f, node.bounds.getY() + 1.0f,
                   4.0f, node.bounds.getHeight() - 2.0f);

        g.setColour(juce::Colour(0xffe6e6e6));
        g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        g.drawText(node.label, node.bounds.reduced(12.0f, 6.0f).removeFromTop(18.0f),
                   juce::Justification::centredLeft, true);

        g.setColour(juce::Colour(0xff8b92a0));
        g.setFont(juce::FontOptions(11.0f));
        g.drawText(node.typeLabel,
                   node.bounds.reduced(12.0f, 6.0f).withTrimmedTop(20.0f),
                   juce::Justification::topLeft, true);

        for (const auto& pin : node.pins)
        {
            g.setColour(pin.control ? juce::Colour(0xffc678dd) : juce::Colour(0xff61afef));
            g.fillEllipse(pin.position.x - kPinRadius, pin.position.y - kPinRadius,
                          kPinRadius * 2.0f, kPinRadius * 2.0f);
        }
    }

    (void) ontology;

    if (message.isNotEmpty())
    {
        auto bar = getLocalBounds().removeFromBottom(26);
        g.setColour(juce::Colour(0xff3a2626));
        g.fillRect(bar);
        g.setColour(juce::Colour(0xffe06c75));
        g.setFont(juce::FontOptions(12.0f));
        g.drawText(message, bar.reduced(10, 0), juce::Justification::centredLeft, true);
    }
}

void GraphView::resized() { layout(); }

void GraphView::parentSizeChanged()
{
    if (auto* vp = findParentComponentOfClass<juce::Viewport>())
    {
        const int w = juce::jmax(getWidth(),  vp->getMaximumVisibleWidth());
        const int h = juce::jmax(getHeight(), vp->getMaximumVisibleHeight());
        if (w != getWidth() || h != getHeight())
            setSize(w, h);
    }
}

std::optional<Arc> GraphView::arcAt(juce::Point<float> point) const
{
    const auto& model = processor.circuit();
    constexpr float kHitThreshold = 8.0f;

    for (const auto& arc : model.arcs())
    {
        const Pin* from = nullptr;
        const Pin* to   = nullptr;

        for (const auto& node : nodes)
            for (const auto& pin : node.pins)
            {
                if (pin.node == arc.fromNode && pin.port == arc.fromPort && ! pin.input) from = &pin;
                if (pin.node == arc.toNode   && pin.port == arc.toPort   && pin.input)   to   = &pin;
            }

        if (from == nullptr || to == nullptr)
            continue;

        const auto p0    = from->position;
        const auto p3    = to->position;
        const float reach = juce::jmax(40.0f, std::abs(p3.x - p0.x) * 0.5f);
        const auto p1    = p0.translated(reach, 0.0f);
        const auto p2    = p3.translated(-reach, 0.0f);

        for (int i = 0; i <= 24; ++i)
        {
            const float t = static_cast<float>(i) / 24.0f;
            const float u = 1.0f - t;
            const juce::Point<float> pt = u*u*u * p0 + 3.0f*u*u*t * p1
                                        + 3.0f*u*t*t * p2 + t*t*t * p3;
            if (pt.getDistanceFrom(point) < kHitThreshold)
                return arc;
        }
    }
    return std::nullopt;
}

void GraphView::mouseDown(const juce::MouseEvent& event)
{
    const auto point = event.position;
    message.clear();

    if (event.mods.isPopupMenu())
    {
        const auto* node = nodeAt(point);
        if (node == nullptr)
            if (const auto arc = arcAt(point); arc.has_value())
            {
                showArcMenu(*arc, event.getPosition());
                return;
            }
        showMenuFor(node, event.getPosition());
        return;
    }

    if (const auto* pin = pinAt(point))
    {
        // Stage 2: an arc starts at an output pin.
        if (! pin->input)
        {
            draggingFrom = *pin;
            dragTo = point;
        }
        return;
    }

    if (const auto* node = nodeAt(point))
    {
        // Stage 1: dragging moves editor metadata only. The compiled circuit is
        // untouched, so the audio does not even glitch.
        draggedNode = const_cast<NodeBox*>(node);
        dragOffset = point - node->bounds.getPosition();
    }
}

void GraphView::mouseDrag(const juce::MouseEvent& event)
{
    if (draggedNode != nullptr)
    {
        const auto position = event.position - dragOffset;
        positions[draggedNode->id] = position;
        layout();
        repaint();
        return;
    }

    if (draggingFrom.has_value())
    {
        dragTo = event.position;
        repaint();
    }
}

void GraphView::mouseUp(const juce::MouseEvent& event)
{
    draggedNode = nullptr;

    if (! draggingFrom.has_value())
        return;

    const auto from = *draggingFrom;
    draggingFrom.reset();

    const auto* target = pinAt(event.position);
    if (target == nullptr || ! target->input)
    {
        repaint();
        return;
    }

    warnAboutReformatting();

    auto dispatcher = ops();
    const auto result = dispatcher.connect(from.node, from.port, target->node, target->port);

    if (! result.ok && ! result.diagnostics.empty())
        message = result.diagnostics.front().toString();

    rebuild();
}

void GraphView::showArcMenu(const Arc& arc, juce::Point<int> where)
{
    juce::PopupMenu menu;
    const auto label = juce::String(vocab::shortName(arc.fromNode)) + "." + arc.fromPort
                     + "  \u2192  "
                     + juce::String(vocab::shortName(arc.toNode))   + "." + arc.toPort;
    menu.addItem(1, "Delete arc: " + label);

    const auto fromNode = arc.fromNode, fromPort = arc.fromPort;
    const auto toNode   = arc.toNode,   toPort   = arc.toPort;
    const auto screenPos = localPointToGlobal(where);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(
                           juce::Rectangle<int>(screenPos.x, screenPos.y, 1, 1)),
                       [this, fromNode, fromPort, toNode, toPort](int choice)
                       {
                           if (choice != 1)
                               return;
                           warnAboutReformatting();
                           auto dispatcher = ops();
                           const auto result = dispatcher.disconnect(fromNode, fromPort,
                                                                     toNode, toPort);
                           if (! result.ok && ! result.diagnostics.empty())
                               message = result.diagnostics.front().toString();
                           rebuild();
                       });
}

void GraphView::showMenuFor(const NodeBox* node, juce::Point<int> where)
{
    juce::PopupMenu menu;

    // Stage 3: the palette is built from the ontology, so a new element class
    // appears here the moment it is declared - no UI change needed.
    juce::PopupMenu addMenu;
    auto dispatcher = ops();
    const auto types = dispatcher.listElementTypes();

    int id = 1000;
    std::map<int, std::string> classForId;
    for (const auto& type : types)
    {
        classForId[id] = type.classIri;
        addMenu.addItem(id++, type.label.empty() ? vocab::shortName(type.classIri) : type.label);
    }

    menu.addSubMenu("Add element", addMenu);

    if (node != nullptr)
    {
        menu.addSeparator();
        menu.addItem(1, "Delete " + juce::String(node->label));
    }

    const std::string nodeId = node != nullptr ? node->id : std::string();

    const auto screenPos = localPointToGlobal(where);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(
                           juce::Rectangle<int>(screenPos.x, screenPos.y, 1, 1)),
                       [this, nodeId, classForId](int choice)
                       {
                           if (choice == 0)
                               return;

                           warnAboutReformatting();
                           auto menuOps = ops();
                           OpResult result;

                           if (choice == 1 && ! nodeId.empty())
                           {
                               result = menuOps.removeNode(nodeId);
                               positions.erase(nodeId);
                           }
                           else if (const auto it = classForId.find(choice); it != classForId.end())
                           {
                               // A generated id, unique against what is there.
                               const auto base = processor.circuit().id() + "-" +
                                                 vocab::shortName(it->second);
                               std::string candidate = base;
                               for (int n = 2; processor.circuit().findElement(candidate) != nullptr; ++n)
                                   candidate = base + std::to_string(n);

                               result = menuOps.addNode(candidate, it->second);
                           }

                           if (! result.ok && ! result.diagnostics.empty())
                               message = result.diagnostics.front().toString();

                           rebuild();
                       });
}

void GraphView::warnAboutReformatting()
{
    if (warnedAboutReformatting)
        return;

    warnedAboutReformatting = true;
    message = "Editing here rewrites the Turtle: comments and layout are not preserved.";
}

}  // namespace valis
