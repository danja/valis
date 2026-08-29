examples/rings-modal.ttl mode knob needs replacing with a more suitable UI component (dropdown/selector)

- UI enhancements for integer/enum parameters: dropdown selector, radio buttons, checkboxes in Knobs view
  (requires: new port annotation in ontology e.g. lv2:enumeration, new component in ControlsView.cpp)

The Pluck Decay control law isn't very good, the first 1% needs to be magnified to 10% of the rotation, is that a log function? That should probably be a property in the RDF.

## Recurring - once the above is clear

* update README.md and docs for recent changes
* check code for long files - if found, refactor
* check test coverage
* check MISTAKES.md for patterns, update CLAUDE.md as appropriate
