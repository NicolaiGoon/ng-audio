#pragma once

#include "Bridge.h"

namespace ng
{

/**
    The MCP server's logic (Model Context Protocol, JSON-RPC 2.0): initialize, ping,
    tools/list and tools/call. Every command is a tool, plus batch, undo and redo, and each
    call is passed to the running app through a BridgeClient. NgDawMcp feeds it stdin, one
    message per line, and prints its replies.
*/
class McpServer
{
public:
    McpServer();

    /** Where to find the running app (by default, bridge::getConnectionFile()). */
    void setConnectionFile (const juce::File&);

    /** Handles one line of JSON and returns the reply as one line, or "" for a notification. */
    juce::String handleLine (const juce::String&);

    /** Handles one JSON-RPC message; returns the reply, or void for a notification. */
    juce::var handle (const juce::var& message);

    static juce::var error (const juce::var& id, int code, const juce::String& text);

    /** The app's commands as MCP tools (name, description, inputSchema), plus batch, undo and redo. */
    static juce::var listTools (const CommandRegistry&);

    /** What an AI client is told about the tools when it connects. */
    static juce::String getInstructions();

private:
    static juce::var success (const juce::var& id, const juce::var& result);

    /** How the app's command log names who made a change. */
    static juce::String describeClient (const juce::String& client);

    juce::var callTool (const juce::String& name, const juce::var& arguments);

    CommandRegistry registry;
    BridgeClient client;
    juce::String source = "AI";
};

} // namespace ng
