# The MCP surface

Every key operation is reachable over HTTP MCP, because the interface and the
server are both thin adapters over the same `OpDispatcher`. There is no second
implementation: an edit made here takes exactly the path the Turtle editor
takes, including validation.

## Running it

The server is off unless asked for, and binds to loopback only:

```sh
VALIS_MCP=1 ./valis
```

| variable | default | meaning |
|---|---|---|
| `VALIS_MCP` | unset | set to anything but `0` to start the server |
| `VALIS_MCP_PORT` | `7676` | port to bind on `127.0.0.1` |
| `VALIS_MCP_TOKEN` | none | if set, requests need `Authorization: Bearer <token>` |

Check it is up:

```sh
curl -s localhost:7676/health
```

## Protocol

JSON-RPC 2.0 over `POST /mcp`. `initialize`, `tools/list` and `tools/call` are
supported.

```sh
curl -s localhost:7676/mcp -d '{"jsonrpc":"2.0","id":1,"method":"tools/list"}'
```

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
| `get_diagnostics` | whether the circuit loaded, its size, its latency |

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
