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
#include <mutex>
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
    /// If `port` is taken, tries the next `portsToTry - 1` ports in turn, so several instances
    /// (one per track in a host) each get a server of their own. `boundPort()` says which.
    /// Returns false, and binds nothing, if none was free.
    bool start(int port, const std::string& bearerToken = {}, int portsToTry = 1);
    void stop();

    /// Says what this instance is, for `/health`, so a client scanning ports can tell instances
    /// apart (the host's track name, say). Called on the HTTP thread, so it must be thread-safe.
    void setIdentity(std::function<std::string()> describe);

    bool isRunning() const { return running.load(std::memory_order_acquire); }
    int  boundPort() const { return port; }

    /// Handles one JSON-RPC message. Exposed so it can be tested without a
    /// socket.
    std::string handleMessage(const std::string& request);

private:
    struct Gate;
    juce::var callTool(const std::shared_ptr<Gate>& gate, const juce::String& name,
                       const juce::var& arguments, juce::String& error);
    static juce::var toolManifest();

    std::function<OpDispatcher()> makeDispatcher;
    std::string token;
    int port = 0;

    std::mutex identityMutex;
    std::function<std::string()> identity;

    std::atomic<bool> running{false};
    struct Impl;
    std::unique_ptr<Impl> impl;
};

}  // namespace valis
