/*
    NgDawMcp: an MCP server (Model Context Protocol, over stdio) that lets Claude and other AI
    clients use Ng DAW. Every command the app has is a tool, plus batch, undo and redo. Each
    call is passed to the running app through its bridge (see core/Bridge.h), so it changes
    the project that's open there, shows up in the app's command log and undoes like anything
    done by hand.

    It reads JSON-RPC messages from stdin, one per line, and writes replies to stdout the same
    way. Nothing else may go to stdout.
*/

#include "core/McpServer.h"

#include <iostream>
#include <string>

int main (int, char**)
{
    std::ios::sync_with_stdio (false);
    ng::McpServer server;
    std::string line;

    while (std::getline (std::cin, line))
    {
        auto text = juce::String::fromUTF8 (line.c_str()).trim();

        if (text.isEmpty())
            continue;

        auto reply = server.handleLine (text);

        if (reply.isNotEmpty())
            std::cout << reply.toStdString() << "\n" << std::flush;
    }

    return 0;
}
