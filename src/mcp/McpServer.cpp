// src/mcp/McpServer.cpp

#include "mcp/McpServer.h"

#include "valis/Ontology.h"
#include "valis/Vocabulary.h"

#include <httplib.h>

#include <thread>

namespace valis {

namespace {

constexpr const char* kProtocolVersion = "2024-11-05";
constexpr const char* kTurtleResourceUri = "valis://turtle";
constexpr const char* kGraphResourceUri = "valis://graph";
constexpr const char* kDiagnosticsResourceUri = "valis://diagnostics";
constexpr const char* kElementTypesResourceUri = "valis://element-types";
constexpr const char* kParamsResourceUri = "valis://params";

/// Runs `work` on the message thread and waits for it. The ops mutate the model
/// the editor also reads, so they belong on one thread; the HTTP thread waits
/// rather than taking a lock the editor would have to honour.
template <typename Work>
auto onMessageThread(Work&& work) -> decltype(work())
{
    using Result = decltype(work());

    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
        return work();

    Result result{};
    juce::WaitableEvent done;

    juce::MessageManager::callAsync([&]
    {
        result = work();
        done.signal();
    });

    // A request that cannot be serviced within this window is a bug, not a
    // slow op: everything here is bounded work on in-memory structures.
    done.wait(5000);
    return result;
}

juce::var errorObject(int code, const juce::String& message)
{
    auto* object = new juce::DynamicObject();
    object->setProperty("code", code);
    object->setProperty("message", message);
    return juce::var(object);
}

juce::var textContent(const juce::String& text, bool isError = false)
{
    auto* item = new juce::DynamicObject();
    item->setProperty("type", "text");
    item->setProperty("text", text);

    juce::Array<juce::var> content;
    content.add(juce::var(item));

    auto* result = new juce::DynamicObject();
    result->setProperty("content", content);
    if (isError)
        result->setProperty("isError", true);

    return juce::var(result);
}

juce::var textResource(const char* uri, const char* name, const char* description,
                       const char* mimeType)
{
    auto* resource = new juce::DynamicObject();
    resource->setProperty("uri", uri);
    resource->setProperty("name", name);
    resource->setProperty("description", description);
    resource->setProperty("mimeType", mimeType);
    return juce::var(resource);
}

juce::var resourceManifest()
{
    juce::Array<juce::var> resources;
    resources.add(textResource(kTurtleResourceUri, "Current Turtle",
                               "The circuit's current Turtle source.",
                               "text/turtle"));
    resources.add(textResource(kGraphResourceUri, "Current graph",
                               "The current circuit as JSON.",
                               "application/json"));
    resources.add(textResource(kDiagnosticsResourceUri, "Current diagnostics",
                               "Whether the circuit loaded, its size, and its latency.",
                               "application/json"));
    resources.add(textResource(kElementTypesResourceUri, "Element types",
                               "Every element class the ontology declares, with ports, ranges, and units.",
                               "application/json"));
    resources.add(textResource(kParamsResourceUri, "Parameter bindings",
                               "The circuit's parameter bindings, with ranges, units, and current values.",
                               "application/json"));
    return resources;
}

juce::String diagnosticsToText(const std::vector<Diagnostic>& diagnostics)
{
    juce::StringArray lines;
    for (const auto& d : diagnostics)
        lines.add(juce::String(d.toString()));

    return lines.joinIntoString("\n");
}

juce::var resultOf(const OpResult& result)
{
    if (! result.ok)
        return textContent(result.diagnostics.empty()
                               ? juce::String("the operation failed")
                               : diagnosticsToText(result.diagnostics),
                           true);

    juce::String text(result.value);
    if (! result.diagnostics.empty())
        text += (text.isEmpty() ? "" : "\n") + diagnosticsToText(result.diagnostics);

    return textContent(text.isEmpty() ? juce::String("ok") : text);
}

juce::var readResource(std::function<OpDispatcher()> makeDispatcher, const juce::String& uri,
                       juce::String& error)
{
    return onMessageThread([&]() -> juce::var
    {
        auto ops = makeDispatcher();

        if (uri == kTurtleResourceUri)
        {
            const auto result = ops.getTurtle();
            if (! result.ok)
            {
                error = result.diagnostics.empty() ? "failed to read Turtle" : diagnosticsToText(result.diagnostics);
                return {};
            }

            auto* contents = new juce::DynamicObject();
            contents->setProperty("uri", uri);
            contents->setProperty("mimeType", "text/turtle");
            contents->setProperty("text", juce::String(result.value));

            juce::Array<juce::var> items;
            items.add(juce::var(contents));

            auto* response = new juce::DynamicObject();
            response->setProperty("contents", items);
            return juce::var(response);
        }

        if (uri == kGraphResourceUri || uri == kDiagnosticsResourceUri)
        {
            const auto result = uri == kGraphResourceUri ? ops.getGraph() : ops.getDiagnostics();
            if (! result.ok)
            {
                error = result.diagnostics.empty() ? "failed to read resource" : diagnosticsToText(result.diagnostics);
                return {};
            }

            auto* contents = new juce::DynamicObject();
            contents->setProperty("uri", uri);
            contents->setProperty("mimeType", "application/json");
            contents->setProperty("text", juce::String(result.value));

            juce::Array<juce::var> items;
            items.add(juce::var(contents));

            auto* response = new juce::DynamicObject();
            response->setProperty("contents", items);
            return juce::var(response);
        }

        if (uri == kElementTypesResourceUri)
        {
            juce::Array<juce::var> types;
            for (const auto& type : ops.listElementTypes())
            {
                auto* entry = new juce::DynamicObject();
                entry->setProperty("class", juce::String(type.classIri));
                entry->setProperty("label", juce::String(type.label));
                entry->setProperty("linear", type.linear);

                juce::Array<juce::var> ports;
                for (const auto& portDesc : type.ports)
                {
                    auto* p = new juce::DynamicObject();
                    p->setProperty("symbol", juce::String(portDesc.symbol));
                    p->setProperty("name", juce::String(portDesc.name));
                    p->setProperty("direction", portDesc.input ? "in" : "out");
                    p->setProperty("rate", portDesc.control ? "control" : "audio");
                    if (portDesc.control)
                    {
                        p->setProperty("default", portDesc.defaultValue);
                        p->setProperty("minimum", portDesc.minimum);
                        p->setProperty("maximum", portDesc.maximum);
                        if (! portDesc.unit.empty())
                            p->setProperty("unit", juce::String(portDesc.unit));
                    }
                    ports.add(juce::var(p));
                }
                entry->setProperty("ports", ports);
                types.add(juce::var(entry));
            }

            auto* contents = new juce::DynamicObject();
            contents->setProperty("uri", uri);
            contents->setProperty("mimeType", "application/json");
            contents->setProperty("text", juce::JSON::toString(types, true));

            juce::Array<juce::var> items;
            items.add(juce::var(contents));

            auto* response = new juce::DynamicObject();
            response->setProperty("contents", items);
            return juce::var(response);
        }

        if (uri == kParamsResourceUri)
        {
            juce::Array<juce::var> params;
            for (const auto& param : ops.listParams())
            {
                auto* entry = new juce::DynamicObject();
                entry->setProperty("slot", param.slot);
                entry->setProperty("name", juce::String(param.name));
                entry->setProperty("target", juce::String(param.targetNode));
                entry->setProperty("property", juce::String(param.property));
                entry->setProperty("value", param.value);
                entry->setProperty("minimum", param.minimum);
                entry->setProperty("maximum", param.maximum);
                if (! param.unit.empty())
                    entry->setProperty("unit", juce::String(param.unit));
                params.add(juce::var(entry));
            }

            auto* contents = new juce::DynamicObject();
            contents->setProperty("uri", uri);
            contents->setProperty("mimeType", "application/json");
            contents->setProperty("text", juce::JSON::toString(params, true));

            juce::Array<juce::var> items;
            items.add(juce::var(contents));

            auto* response = new juce::DynamicObject();
            response->setProperty("contents", items);
            return juce::var(response);
        }

        error = "unknown resource: " + uri;
        return {};
    });
}

/// A tool definition, so the manifest and the dispatch stay in one place.
struct ToolSpec
{
    const char* name;
    const char* description;
    const char* schema;   ///< the inputSchema, verbatim JSON
};

const ToolSpec kTools[] = {
    {"get_turtle", "Return the circuit's Turtle source.",
     R"({"type":"object","properties":{}})"},

    {"set_turtle", "Replace the circuit. Rejected if it will not compile; the previous circuit keeps playing.",
     R"({"type":"object","properties":{"turtle":{"type":"string","description":"the complete Turtle document"}},"required":["turtle"]})"},

    {"validate", "Check Turtle without installing it. Reports every problem found, with line and column where the parser found it.",
     R"({"type":"object","properties":{"turtle":{"type":"string"}},"required":["turtle"]})"},

    {"list_element_types", "Every element class the ontology declares, with its ports, ranges and units.",
     R"({"type":"object","properties":{}})"},

    {"get_graph", "The circuit as JSON: elements, their properties, and the arcs between them.",
     R"({"type":"object","properties":{}})"},

    {"add_node", "Add an element to the circuit.",
     R"({"type":"object","properties":{"id":{"type":"string","description":"IRI for the new element"},"class":{"type":"string","description":"element class, e.g. val:Ladder"}},"required":["id","class"]})"},

    {"remove_node", "Remove an element and every arc that touches it.",
     R"({"type":"object","properties":{"id":{"type":"string"}},"required":["id"]})"},

    {"connect", "Join an output port to an input port.",
     R"({"type":"object","properties":{"from_node":{"type":"string"},"from_port":{"type":"string"},"to_node":{"type":"string"},"to_port":{"type":"string"},"depth":{"type":"number","description":"modulation depth, control arcs only"}},"required":["from_node","from_port","to_node","to_port"]})"},

    {"disconnect", "Remove an arc between two ports.",
     R"({"type":"object","properties":{"from_node":{"type":"string"},"from_port":{"type":"string"},"to_node":{"type":"string"},"to_port":{"type":"string"}},"required":["from_node","from_port","to_node","to_port"]})"},

    {"list_params", "The circuit's parameter bindings, with their ranges, units and current values.",
     R"({"type":"object","properties":{}})"},

    {"get_param", "Read one parameter slot, in the property's own units.",
     R"({"type":"object","properties":{"slot":{"type":"integer"}},"required":["slot"]})"},

    {"set_param", "Set one parameter slot, in the property's own units. Out-of-range values are clamped and reported.",
     R"({"type":"object","properties":{"slot":{"type":"integer"},"value":{"type":"number"}},"required":["slot","value"]})"},

    {"get_diagnostics", "The circuit's current state: whether it loaded, its size, and its latency.",
     R"({"type":"object","properties":{}})"},

    {"load_file", "Load a Turtle circuit from a file path on the server machine.",
     R"({"type":"object","properties":{"path":{"type":"string","description":"absolute path to a .ttl file"}},"required":["path"]})"},
};

}  // namespace

struct McpServer::Impl
{
    httplib::Server server;
    std::thread thread;
};

McpServer::McpServer(std::function<OpDispatcher()> factory)
    : makeDispatcher(std::move(factory)), impl(std::make_unique<Impl>())
{
}

McpServer::~McpServer() { stop(); }

juce::var McpServer::toolManifest()
{
    juce::Array<juce::var> tools;

    for (const auto& spec : kTools)
    {
        auto* tool = new juce::DynamicObject();
        tool->setProperty("name", spec.name);
        tool->setProperty("description", spec.description);
        tool->setProperty("inputSchema", juce::JSON::parse(spec.schema));
        tools.add(juce::var(tool));
    }

    return tools;
}

juce::var McpServer::callTool(const juce::String& name, const juce::var& arguments,
                              juce::String& error)
{
    const auto string = [&arguments](const char* key) {
        return arguments[key].toString().toStdString();
    };

    return onMessageThread([&]() -> juce::var
    {
        auto ops = makeDispatcher();

        if (name == "get_turtle")          return resultOf(ops.getTurtle());
        if (name == "set_turtle")          return resultOf(ops.setTurtle(string("turtle")));
        if (name == "validate")            return resultOf(ops.validate(string("turtle")));
        if (name == "get_graph")           return resultOf(ops.getGraph());
        if (name == "get_diagnostics")     return resultOf(ops.getDiagnostics());

        if (name == "add_node")
            return resultOf(ops.addNode(string("id"), string("class")));

        if (name == "remove_node")
            return resultOf(ops.removeNode(string("id")));

        if (name == "connect")
        {
            std::optional<double> depth;
            if (arguments.hasProperty("depth"))
                depth = static_cast<double>(arguments["depth"]);

            return resultOf(ops.connect(string("from_node"), string("from_port"),
                                        string("to_node"),   string("to_port"), depth));
        }

        if (name == "disconnect")
            return resultOf(ops.disconnect(string("from_node"), string("from_port"),
                                           string("to_node"),   string("to_port")));

        if (name == "get_param")
            return resultOf(ops.getParam(static_cast<int>(arguments["slot"])));

        if (name == "set_param")
            return resultOf(ops.setParam(static_cast<int>(arguments["slot"]),
                                         static_cast<double>(arguments["value"])));

        if (name == "load_file")
        {
            const juce::File f(juce::String(string("path")));
            if (!f.existsAsFile())
                return textContent("file not found: " + juce::String(string("path")), true);
            const auto content = f.loadFileAsString();
            return resultOf(ops.setTurtle(content.toStdString()));
        }

        if (name == "list_element_types")
        {
            juce::Array<juce::var> types;
            for (const auto& type : ops.listElementTypes())
            {
                auto* entry = new juce::DynamicObject();
                entry->setProperty("class", juce::String(type.classIri));
                entry->setProperty("label", juce::String(type.label));
                entry->setProperty("linear", type.linear);

                juce::Array<juce::var> ports;
                for (const auto& portDesc : type.ports)
                {
                    auto* p = new juce::DynamicObject();
                    p->setProperty("symbol", juce::String(portDesc.symbol));
                    p->setProperty("name", juce::String(portDesc.name));
                    p->setProperty("direction", portDesc.input ? "in" : "out");
                    p->setProperty("rate", portDesc.control ? "control" : "audio");
                    if (portDesc.control)
                    {
                        p->setProperty("default", portDesc.defaultValue);
                        p->setProperty("minimum", portDesc.minimum);
                        p->setProperty("maximum", portDesc.maximum);
                        if (! portDesc.unit.empty())
                            p->setProperty("unit", juce::String(portDesc.unit));
                    }
                    ports.add(juce::var(p));
                }
                entry->setProperty("ports", ports);
                types.add(juce::var(entry));
            }
            return textContent(juce::JSON::toString(types, true));
        }

        if (name == "list_params")
        {
            juce::Array<juce::var> params;
            for (const auto& param : ops.listParams())
            {
                auto* entry = new juce::DynamicObject();
                entry->setProperty("slot", param.slot);
                entry->setProperty("name", juce::String(param.name));
                entry->setProperty("target", juce::String(param.targetNode));
                entry->setProperty("property", juce::String(param.property));
                entry->setProperty("value", param.value);
                entry->setProperty("minimum", param.minimum);
                entry->setProperty("maximum", param.maximum);
                if (! param.unit.empty())
                    entry->setProperty("unit", juce::String(param.unit));
                params.add(juce::var(entry));
            }
            return textContent(juce::JSON::toString(params, true));
        }

        error = "unknown tool: " + name;
        return {};
    });
}

std::string McpServer::handleMessage(const std::string& request)
{
    const auto parsed = juce::JSON::parse(juce::String(request));

    auto* response = new juce::DynamicObject();
    response->setProperty("jsonrpc", "2.0");

    if (! parsed.isObject())
    {
        response->setProperty("id", juce::var());
        response->setProperty("error", errorObject(-32700, "parse error"));
        return juce::JSON::toString(juce::var(response), true).toStdString();
    }

    const auto id = parsed["id"];
    const auto method = parsed["method"].toString();
    response->setProperty("id", id);

    if (method == "initialize")
    {
        auto* info = new juce::DynamicObject();
        info->setProperty("name", "valis");
        info->setProperty("version", "0.1.0");

        auto* tools = new juce::DynamicObject();
        auto* resources = new juce::DynamicObject();
        auto* capabilities = new juce::DynamicObject();
        capabilities->setProperty("tools", juce::var(tools));
        capabilities->setProperty("resources", juce::var(resources));

        auto* result = new juce::DynamicObject();
        result->setProperty("protocolVersion", kProtocolVersion);
        result->setProperty("capabilities", juce::var(capabilities));
        result->setProperty("serverInfo", juce::var(info));

        response->setProperty("result", juce::var(result));
    }
    else if (method == "tools/list")
    {
        auto* result = new juce::DynamicObject();
        result->setProperty("tools", toolManifest());
        response->setProperty("result", juce::var(result));
    }
    else if (method == "resources/list")
    {
        auto* result = new juce::DynamicObject();
        result->setProperty("resources", resourceManifest());
        response->setProperty("result", juce::var(result));
    }
    else if (method == "resources/read")
    {
        const auto params = parsed["params"];

        juce::String error;
        const auto result = readResource(makeDispatcher, params["uri"].toString(), error);

        if (error.isNotEmpty())
            response->setProperty("error", errorObject(-32602, error));
        else
            response->setProperty("result", result);
    }
    else if (method == "tools/call")
    {
        const auto params = parsed["params"];
        const auto name = params["name"].toString();

        juce::String error;
        const auto result = callTool(name, params["arguments"], error);

        if (error.isNotEmpty())
            response->setProperty("error", errorObject(-32602, error));
        else
            response->setProperty("result", result);
    }
    else if (method == "notifications/initialized")
    {
        // A notification carries no id and expects no response.
        return {};
    }
    else
    {
        response->setProperty("error", errorObject(-32601, "method not found: " + method));
    }

    return juce::JSON::toString(juce::var(response), true).toStdString();
}

bool McpServer::start(int requestedPort, const std::string& bearerToken)
{
    if (running.load(std::memory_order_acquire))
        return true;

    token = bearerToken;
    port  = requestedPort;

    impl->server.Post("/mcp", [this](const httplib::Request& request, httplib::Response& response)
    {
        if (! token.empty())
        {
            const auto header = request.get_header_value("Authorization");
            if (header != "Bearer " + token)
            {
                response.status = 401;
                response.set_content(R"({"error":"unauthorized"})", "application/json");
                return;
            }
        }

        const auto reply = handleMessage(request.body);
        if (reply.empty())
        {
            response.status = 202;   // a notification was accepted
            return;
        }

        response.set_content(reply, "application/json");
    });

    impl->server.Get("/health", [this](const httplib::Request&, httplib::Response& response)
    {
        response.set_content(R"({"status":"ok","server":"valis"})", "application/json");
    });

    // Loopback only. This is an editing surface for the machine the plugin runs
    // on, not a service.
    if (! impl->server.bind_to_port("127.0.0.1", port))
        return false;

    running.store(true, std::memory_order_release);
    impl->thread = std::thread([this] { impl->server.listen_after_bind(); });
    return true;
}

void McpServer::stop()
{
    if (! running.exchange(false, std::memory_order_acq_rel))
        return;

    impl->server.stop();
    if (impl->thread.joinable())
        impl->thread.join();
}

}  // namespace valis
