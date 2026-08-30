As an end-to-end test, use valis mcp tools (not via curl) to build an interesting circuit, test it with reaper-mcp 

I think there is some VST tesing code in ~/VST_SDK that might be useful for validation.

Maximise compatibility with /home/danny/github/transmission - try it as a test host using MCP. We maintain Transmission so can changes things over there if need be.

* test coverage — Envelope, Compressor, Scale, Mixer, Delay, DryWet have no dedicated behaviour tests (covered indirectly by engine/circuit tests)

## Recurring - check periodically

* for new material, check test coverage
* ensure README.md and docs are up-to-date