#pragma once

#include "CommandDispatcher.h"

namespace ng
{

/**
    Lets programs outside the app, such as the MCP server Claude uses, run commands in the
    project that's open. The app listens on a local port (127.0.0.1 only) and writes the port
    and a secret token to a connection file that only this user can read. A client reads the
    file, connects and sends requests; a request without the token is refused.

    Each request is one JSON object:
        {"token": "...", "source": "Claude", "request": {"command": "...", "params": {...}}}
        {"token": "...", "source": "Claude", "request": {"batch": "Bass line", "commands": [...]}}
        {"token": "...", "source": "Claude", "undo": true}      (or "redo": true)
        {"token": "...", "describe": true}                      (the commands, as MCP tools)
    and gets back the dispatcher's {"ok": ..., "result" or "error": ...}.
*/
namespace bridge
{
    /** Where the connection file is: NG_DAW_BRIDGE_FILE if set, otherwise bridge.json in the
        user's application data folder, under "Ng DAW"
        (~/.config, ~/Library/Application Support or %APPDATA%). */
    juce::File getConnectionFile();

    /** The connection file's contents: {port, token, pid}. */
    struct ConnectionInfo
    {
        int port = 0;
        juce::String token;
        int pid = 0;
    };

    std::optional<ConnectionInfo> readConnectionFile (const juce::File&);
}

/** The app's end: runs requests on the message thread, through the app's dispatcher, so
    they are logged and undone like anything else. */
class BridgeServer  : private juce::InterprocessConnectionServer
{
public:
    explicit BridgeServer (CommandDispatcher&);
    ~BridgeServer() override;

    /** Starts listening and writes the connection file. Returns an error, or an empty string. */
    juce::String start (const juce::File& connectionFile = bridge::getConnectionFile());
    void stop();

    bool isRunning() const noexcept     { return port > 0; }
    int getPort() const noexcept        { return port; }

    /** Handles one request (as JSON) and returns the reply (as JSON). Public for tests. */
    juce::String handleRequest (const juce::String& json);

private:
    class Connection;

    juce::InterprocessConnection* createConnectionObject() override;
    void removeConnection (Connection*);

    CommandDispatcher& dispatcher;
    juce::String token;
    juce::File file;
    int port = 0;

    juce::CriticalSection lock;
    juce::OwnedArray<Connection> connections;

    JUCE_DECLARE_WEAK_REFERENCEABLE (BridgeServer)
    JUCE_DECLARE_NON_COPYABLE (BridgeServer)
};

/** The other end, for the MCP server: connects to the running app and sends it requests,
    waiting for each reply. Not tied to the message thread. */
class BridgeClient  : private juce::InterprocessConnection
{
public:
    BridgeClient();
    ~BridgeClient() override;

    /** Sends a request object (the token is added) and returns the reply, connecting first if
        needed. On failure returns {"ok": false, "error": ...}, e.g. when the app isn't running. */
    juce::var send (const juce::var& request, int timeoutMs = 10 * 60 * 1000);

    void setConnectionFile (const juce::File& f)    { file = f; }

private:
    bool ensureConnected (juce::String& error);
    void connectionMade() override {}
    void connectionLost() override;
    void messageReceived (const juce::MemoryBlock&) override;

    juce::File file = bridge::getConnectionFile();
    juce::String token;
    juce::WaitableEvent replied;
    juce::CriticalSection replyLock;
    juce::String reply;
    std::atomic<bool> lost { false };

    JUCE_DECLARE_NON_COPYABLE (BridgeClient)
};

} // namespace ng
