# The MCP surface

Every key operation is reachable over HTTP MCP, because the interface and the
server are both thin adapters over the same `OpDispatcher`. There is no second
implementation: an edit made here takes exactly the path the Turtle editor
takes, including validation.

## Running it

The server is off by default and binds to loopback only. Enable it from
**Settings → MCP Server** inside the plugin. The toggle persists in plugin
state, so a saved session reopens with the server in whatever state you left it.

Two environment variables configure it when Valis starts:

| variable | default | meaning |
|---|---|---|
| `VALIS_MCP_PORT` | `7676` | port to bind on `127.0.0.1` |
| `VALIS_MCP_TOKEN` | none | if set, requests need `Authorization: Bearer <token>` |

Check it is up:

```sh
curl -s localhost:7676/health
```

## Protocol

JSON-RPC 2.0 over `POST /mcp`. `initialize`, `tools/list`, `tools/call`,
`resources/list`, and `resources/read` are supported.

```sh
curl -s localhost:7676/mcp -d '{"jsonrpc":"2.0","id":1,"method":"tools/list"}'
```

## Resources

These read-only resources let a client inspect the current state without picking
a tool first.

| resource | contains |
|---|---|
| `valis://turtle` | the circuit's current Turtle source |
| `valis://graph` | the current circuit as JSON, including which elements a `val:Subcircuit` expanded to and which voice each belongs to |
| `valis://diagnostics` | load state, size, latency, and any diagnostics |
| `valis://element-types` | every declared element class with ports, ranges, units, and which ports carry events rather than signal |
| `valis://params` | the circuit's current parameter bindings |

## Tools

| tool | does |
|---|---|
| `get_turtle` | the circuit's Turtle source |
| `set_turtle` | replace the circuit; rejected if it will not compile |
| `validate` | check Turtle without installing it |
| `list_element_types` | every class, with ports, ranges and units |
| `get_graph` | the circuit as JSON |
| `add_node` / `remove_node` | add an element, or remove it and its arcs |
| `connect` / `disconnect` | join or separate two ports |
| `list_params` / `get_param` / `set_param` | the bound parameter slots |
| `get_sample` / `set_sample` | the sound file a `val:SampleLoad` node plays |
| `load_file` / `save_file` | read or write a Turtle file on the server machine |
| `note_on` / `note_off` / `all_notes_off` | play the circuit, as the editor's keyboard does |
| `get_profile` | what the circuit says it is: role, signals, genre, cautions |
| `read_outputs` | every control output's live value, or one node's |
| `render` | render the circuit offline to a wav and report peak and RMS |
| `get_diagnostics` | whether the circuit loaded, its size, latency, voices |

Every tool is an adapter over the same `Op` the views use, so anything the UI
can do is reachable here. Three of them are worth knowing about when building a
circuit from outside:

**`note_on`** is what the editor's virtual keyboard does. Without it a caller
can build an instrument and never hear it.

**`read_outputs`** returns the live value of every control output in the
circuit. It is what the Controls view shows for `val:Oscilloscope` and
`val:FreqAnalyzer`, so it is how a caller measures what it built rather than
guessing.

**`render`** compiles the current circuit into a fresh engine, plays an optional
note, writes a wav file and reports the peak, the RMS and whether every sample
was finite. The fresh engine means rendering neither hears nor disturbs what the
plugin is playing. This is the quickest way to tell whether a circuit actually
sounds.

## Guarantees

**Edits are atomic.** A rejected edit leaves the source exactly as it was, so a
failed call cannot corrupt the circuit for the next one.

**A failed edit does not silence the plugin.** Turtle that will not compile
leaves the previous circuit playing.

**Errors say what is wrong and where.** Parse errors carry line and column; a
structural error names the element or arc at fault.

```
4:0: missing ';' or '.'
Tanh has no port 'nosuch'
c: feedback loop with no val:UnitDelay to break it: g -> m
```

## An example

Insert a saturator between two existing elements:

```sh
call() { curl -s localhost:7676/mcp \
  -d "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",
       \"params\":{\"name\":\"$1\",\"arguments\":$2}}"; }

call add_node   '{"id":"urn:valis:basic#growl","class":"val:Tanh"}'
call disconnect '{"from_node":"urn:valis:basic#drive","from_port":"out",
                  "to_node":"urn:valis:basic#out","to_port":"in"}'
call connect    '{"from_node":"urn:valis:basic#drive","from_port":"out",
                  "to_node":"urn:valis:basic#growl","to_port":"in"}'
call connect    '{"from_node":"urn:valis:basic#growl","from_port":"out",
                  "to_node":"urn:valis:basic#out","to_port":"in"}'
```

The graph view redraws with the new node in place, and the audio keeps running
throughout.

## Using with Claude Code

Start Valis with the server enabled, then register it as an MCP server. In the
project or user settings file (`.claude/settings.json` or
`~/.claude/settings.json`):

```json
{
  "mcpServers": {
    "valis": {
      "type": "http",
      "url": "http://localhost:7676/mcp"
    }
  }
}
```

Or add it from the command line:

```sh
claude mcp add --transport http valis http://localhost:7676/mcp
```

Once registered, Claude Code can call any tool in the table above directly.
Describe the sound you want in plain English - the model can call `get_graph`
to read what is there, then `add_node`, `connect`, and `set_param` to build
or reshape the circuit while the audio keeps running.

A bearer token restricts access when the port is forwarded. Set it before
launching, then reference it in the client config:

```sh
VALIS_MCP_TOKEN=secret ./valis
```

```json
{
  "mcpServers": {
    "valis": {
      "type": "http",
      "url": "http://localhost:7676/mcp",
      "headers": { "Authorization": "Bearer secret" }
    }
  }
}
```

## Using with OpenAI Codex

Codex CLI also supports MCP over HTTP. Add a server entry to your Codex
configuration file (typically `~/.codex/config.toml`):

```toml
[[mcp_servers]]
name    = "valis"
type    = "http"
url     = "http://localhost:7676/mcp"
```

The exact key names may vary with Codex version - check `codex mcp --help` or
the Codex documentation if the above does not match. The Valis server speaks
standard JSON-RPC 2.0 MCP, so any client that supports the HTTP transport will
work.
