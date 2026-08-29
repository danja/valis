when the plugin loses focus and gains it again, it loads the defaults, not the previous knob settings

A bottom section of the plugin should act as a status bar giving any useful info. The Revert button can be on the bar.

changes to the Turtle, when read as valid/possible, should update the graph and the DSP. While the Turtle isn't usable, a warning indicator should show in the Status Bar.

examples/rings-modal.ttl gives no output. The mode knob needs replacing with a more  suitable UI component 

- UI enhancements for integer/enum parameters: dropdown selector, radio buttons, checkboxes in Knobs view
  (requires: new port annotation in ontology e.g. lv2:enumeration, new component in ControlsView.cpp)

The tabs should be in the order : Knobs, Graph,  Turtle

Aren't the State options on the Settings menu redundant given that there is the File menu?

## Recurring - once the above is clear

* update README.md and docs for recent changes
* check code for long files - if found, refactor
* check test coverage
* check MISTAKES.md for patterns, update CLAUDE.md as appropriate
