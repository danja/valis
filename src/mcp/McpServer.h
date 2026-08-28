// src/mcp/McpServer.h
//
// An HTTP MCP server, in process. Every tool is a direct adapter to one Op, so
// this file adds a transport and nothing else - there is no second
// implementation of anything the UI can do.
//
// Threading: the HTTP thread never touches the audio thread and never blocks
// it. Work is marshalled onto the message thread, because the ops mutate the
// model that the editor also reads.
//
// Binds 127.0.0.1 only.

#pragma once

#include "valis/Ops.h"

#include <juce_events/juce_events.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace valis {

class McpServer
{
public:
    /// `makeDispatcher` is called on the message thread for each request, so
    /// the ops always see current state.
    explicit McpServer(std::function<OpDispatcher()> makeDispatcher);
    ~McpServer();

    /// Starts listening. An empty token means no authentication, which is only
    /// reasonable because the socket is bound to loopback.
    bool start(int port, const std::string& bearerToken = {});
    void stop();

    bool isRunning() const { return running.load(std::memory_order_acquire); }
    int  boundPort() const { return port; }

    /// Handles one JSON-RPC message. Exposed so it can be tested without a
    /// socket.
    std::string handleMessage(const std::string& request);

private:
    juce::var callTool(const juce::String& name, const juce::var& arguments,
                       juce::String& error);
    static juce::var toolManifest();

    std::function<OpDispatcher()> makeDispatcher;
    std::string token;
    int port = 0;

    std::atomic<bool> running{false};
    struct Impl;
    std::unique_ptr<Impl> impl;
};

}  // namespace valis
