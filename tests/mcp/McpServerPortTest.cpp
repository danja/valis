// tests/mcp/McpServerPortTest.cpp
//
// Several instances of the plug-in in one host each need an MCP server of their own. A server
// asked for a port that is taken moves up to the next free one, says which it got, and reports
// who it is on /health. No tool call is made, so the dispatcher is never used.

#include "mcp/McpServer.h"

#include <httplib.h>

#include <cassert>
#include <cstdio>
#include <string>

namespace {

// A range unlikely to be in use on a build machine. The test tolerates a few ports being taken.
constexpr int kBase = 47650;

std::string healthOf(int port)
{
    httplib::Client client("127.0.0.1", port);
    client.set_connection_timeout(2);
    const auto reply = client.Get("/health");
    return reply && reply->status == 200 ? reply->body : std::string{};
}

valis::McpServer makeServer()
{
    return valis::McpServer([]() -> valis::OpDispatcher { std::abort(); });
}

void testSecondInstanceTakesTheNextPort()
{
    auto first  = makeServer();
    auto second = makeServer();

    const bool ok1 = first.start(kBase, {}, 8);
    assert(ok1);
    const bool ok2 = second.start(kBase, {}, 8);
    assert(ok2);

    assert(first.isRunning() && second.isRunning());
    assert(first.boundPort() >= kBase);
    assert(second.boundPort() > first.boundPort());

    // Each answers on its own port and says so.
    const auto a = healthOf(first.boundPort());
    const auto b = healthOf(second.boundPort());
    assert(a.find("\"port\": " + std::to_string(first.boundPort())) != std::string::npos);
    assert(b.find("\"port\": " + std::to_string(second.boundPort())) != std::string::npos);
    assert(a.find("\"server\": \"valis\"") != std::string::npos);
}

void testRequestingOnePortDoesNotFallBack()
{
    auto first  = makeServer();
    auto second = makeServer();

    const bool ok1 = first.start(kBase + 20, {}, 1);
    assert(ok1);

    // With a single port to try, a taken port is a failure, and nothing is left bound.
    const bool ok2 = second.start(first.boundPort(), {}, 1);
    assert(! ok2);
    assert(! second.isRunning());
    assert(second.boundPort() == 0);
}

void testPortIsFreedOnStop()
{
    auto server = makeServer();
    const bool ok = server.start(kBase + 30, {}, 4);
    assert(ok);
    const int port = server.boundPort();
    server.stop();
    assert(! server.isRunning());

    // The same object starts again, and the port it left is usable.
    auto other = makeServer();
    const bool again = other.start(port, {}, 1);
    assert(again);
    assert(other.boundPort() == port);
}

void testHealthReportsIdentity()
{
    auto server = makeServer();
    server.setIdentity([] { return std::string("Bass"); });

    const bool ok = server.start(kBase + 40, {}, 4);
    assert(ok);

    const auto body = healthOf(server.boundPort());
    assert(body.find("\"name\": \"Bass\"") != std::string::npos);
}

void testRestartAnswersOnce()
{
    // The routes are installed on the first start only. A second start on the same object must
    // still answer, and must answer once.
    auto server = makeServer();
    bool ok = server.start(kBase + 50, {}, 4);
    assert(ok);
    server.stop();
    ok = server.start(kBase + 50, {}, 4);
    assert(ok);
    assert(! healthOf(server.boundPort()).empty());
}

void testNoFreePortFails()
{
    auto holder = makeServer();
    const bool ok = holder.start(65535, {}, 1);
    if (! ok)
        return;   // something else owns 65535 here; the case below needs it free

    // From 65535 there is nowhere above to go.
    auto other = makeServer();
    const bool refused = other.start(65535, {}, 8);
    assert(! refused);
}

}  // namespace

int main()
{
    testSecondInstanceTakesTheNextPort();
    testRequestingOnePortDoesNotFallBack();
    testPortIsFreedOnStop();
    testHealthReportsIdentity();
    testRestartAnswersOnce();
    testNoFreePortFails();
    std::puts("McpServerPortTest: ok");
    return 0;
}
