## Recurring - check periodically

As an end-to-end test, use valis mcp tools (not via curl) to build an interesting circuit, test it with reaper-mcp 

add a note somewhere in CLAUDE.md that a live running instance of Reaper may be used for testing over MCP with reaper-mcp, or a live instance of transmission host, just ask for a running instance. 

If exists, show loaded circuit filename in status bar. Change color if modified but not saved.

Release build GitHub Action fails at linux : 
The following tests FAILED:
	  3 - rdf_TurtleStoreTest (SEGFAULT)

I think there is some VST tesing code in ~/VST_SDK that might be useful for validation.

Make sure that the .so in the VST bundles has its executable flag set in both the local build and the GitHub Action


* update README.md and docs for recent changes
* check code for long files - if found, refactor
* check test coverage
* check MISTAKES.md for patterns, update CLAUDE.md as appropriate
