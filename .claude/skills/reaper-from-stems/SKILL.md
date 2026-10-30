---
name: reaper-from-stems
description: Set up the tools and build a Reaper project from rendered Valis stems or MIDI files. Use when asked to open a song in Reaper, mix or master it there, or to drive Reaper through reaper-mcp.
---

# Reaper from stems

The reasoning and the problems are in `docs/halloween-process.md`. Read it first.

## What Reaper holds

Reaper holds recordings of the instruments (stems from `valis_render`), not live plug-ins, because an instance of
the plug-in cannot yet be given its circuit without opening its window (`TODO.md`). A MIDI project is also possible:
`scripts/halloween/mkrpp.py` writes a `.rpp` with one track per MIDI file, which makes no sound until a Valis
instance is added to each track by hand.

## Setting up (once)

1. Valis MCP server: off by default. Turn it on in Settings, or start with `VALIS_MCP_PORT` set. Check with
   `curl -s localhost:7676/health`. Register it: `claude mcp add --transport http valis http://localhost:7676/mcp`,
   and start a new Claude Code session. Several instances take ports 7676, 7677 and so on.
2. `reaper-mcp` (`/chalet/github/reaper-mcp`): install in a virtualenv, with `mcp` below version 2. In Reaper,
   enable Python in Preferences, Plug-ins, ReaScript, and run `scripts/enable_reapy.py` once, then restart Reaper.
   Register it with `claude mcp add --scope user reaper -- /path/to/.venv/bin/reaper-mcp-server`. Reaper must be running.
3. Ask the user to confirm a running Reaper before using its tools (CLAUDE.md).

## Building the project

1. Stems and their levels: take them from the mix script's `LEVELS` table, so the project starts where the Python
   mix did. Set volume and pan per track with `set_track_volume` and `set_track_pan`; they are not in a `.rpp`
   written by script.
2. `import_audio_file` at 0:00, one track per stem. Keep the tempo equal to the song's.
3. Leave room for the vocal: a track for it, and no master limiter until mastering.

## Known problems (reaper-mcp)

- `add_master_fx` and `apply_limiter` add the effect and then report an error. Do not retry; list the master chain.
- `render_project` can outrun the MCP timeout and write no file. Render from Reaper's own Render dialog instead.
- A changed file on disk is not noticed until the item is reloaded.
