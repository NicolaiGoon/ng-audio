#include "McpServer.h"

namespace ng
{
namespace
{
    const char* const serverVersion = "0.1.0";

    const char* const instructions =
        "Ng DAW is a music production app (a DAW). These tools change the project open in it, which must be running. "
        "Start with get_state (view \"summary\" for a quick overview) to see the channels (instruments and audio tracks, "
        "holding clips) and mixer strips (faders and effects); ids from it work wherever a channel, strip or clip is asked "
        "for, and so do names. Times are in beats from the start of the song. Use batch to make a change of several "
        "commands as one undo step, referring to earlier results with \"$<n>.<field>\". Each tool call or batch is one "
        "step in the app's undo history and appears in its command log, where the person can undo it.";

    juce::var object (std::initializer_list<std::pair<const char*, juce::var>> properties)
    {
        auto obj = new juce::DynamicObject();

        for (auto& [name, value] : properties)
            obj->setProperty (name, value);

        return juce::var (obj);
    }

    juce::var tool (const juce::String& name, const juce::String& description, const juce::var& schema)
    {
        return object ({ { "name", name }, { "description", description }, { "inputSchema", schema } });
    }

    /** The app's commands as tools, plus batch, undo and redo. */
    juce::var listTools (const CommandRegistry& registry)
    {
        juce::Array<juce::var> tools;
        auto commands = registry.describe();

        for (auto& t : *commands.getArray())
            tools.add (t);

        auto commandItem = params::schema ({ { "command", params::prop ("string", "A tool name, e.g. create_track") },
                                             { "params", params::prop ("object", "That tool's arguments") } },
                                           { "command" });

        auto batchCommands = params::prop ("array", "The commands, run in order");
        batchCommands.getDynamicObject()->setProperty ("items", commandItem);

        tools.add (tool ("batch",
                         "Runs several commands as one undo step, e.g. adding a part and putting effects on it. If any command "
                         "fails, all of them are undone. A string param \"$<n>.<field>\" is that field of command n's result "
                         "(counting from 0), so later commands can use what earlier ones created: "
                         "[{\"command\": \"create_track\", \"params\": {\"name\": \"Bass\"}}, "
                         "{\"command\": \"add_midi_clip\", \"params\": {\"channel\": \"$0.channel\", \"notes\": [...]}}]. "
                         "Fields can go deeper (\"$1.clips.0.id\"); write \"$$\" for a literal \"$\". Returns each command's result.",
                         params::schema ({ { "label", params::prop ("string", "What the change does, as the undo history and the "
                                                                              "app's command log show it, e.g. \"Add a bass line\"") },
                                           { "commands", batchCommands } },
                                         { "label", "commands" })));

        tools.add (tool ("undo", "Undoes the newest change in the project (a command, a batch, or something done by hand).",
                         params::schema ({})));
        tools.add (tool ("redo", "Redoes the change undo last took back.", params::schema ({})));
        return tools;
    }

    juce::var textResult (const juce::String& text, bool isError)
    {
        juce::Array<juce::var> content { object ({ { "type", "text" }, { "text", text } }) };
        return object ({ { "content", content }, { "isError", isError } });
    }
}

McpServer::McpServer()
{
    registerProjectCommands (registry);
}

void McpServer::setConnectionFile (const juce::File& f)
{
    client.setConnectionFile (f);
}

juce::String McpServer::handleLine (const juce::String& line)
{
    juce::var message;
    juce::var reply;

    if (juce::JSON::parse (line, message).failed() || ! message.isObject())
        reply = error ({}, -32700, "Parse error");
    else
        reply = handle (message);

    return reply.isVoid() ? juce::String() : juce::JSON::toString (reply, true);
}

//==============================================================================
juce::var McpServer::handle (const juce::var& message)
{
    auto id = message["id"];
    auto method = message["method"].toString();
    auto isNotification = ! message.hasProperty ("id");

    if (method == "initialize")
    {
        auto clientName = message["params"]["clientInfo"]["name"].toString();
        source = describeClient (clientName);

        auto requested = message["params"]["protocolVersion"].toString();
        auto version = juce::StringArray { "2024-11-05", "2025-03-26", "2025-06-18" }.contains (requested)
                           ? requested : juce::String ("2025-06-18");

        return success (id, object ({ { "protocolVersion", version },
                                      { "capabilities", object ({ { "tools", object ({}) } }) },
                                      { "serverInfo", object ({ { "name", "ng-daw" }, { "title", "Ng DAW" },
                                                                { "version", serverVersion } }) },
                                      { "instructions", instructions } }));
    }

    if (isNotification)
        return {};

    if (method == "ping")
        return success (id, object ({}));

    if (method == "tools/list")
        return success (id, object ({ { "tools", listTools (registry) } }));

    if (method == "tools/call")
        return success (id, callTool (message["params"]["name"].toString(), message["params"]["arguments"]));

    return error (id, -32601, "Unknown method: " + method);
}

//==============================================================================
juce::var McpServer::error (const juce::var& id, int code, const juce::String& text)
{
    return object ({ { "jsonrpc", "2.0" }, { "id", id }, { "error", object ({ { "code", code }, { "message", text } }) } });
}

juce::var McpServer::success (const juce::var& id, const juce::var& result)
{
    return object ({ { "jsonrpc", "2.0" }, { "id", id }, { "result", result } });
}

juce::String McpServer::describeClient (const juce::String& client)
{
    if (client.containsIgnoreCase ("claude"))
        return "Claude";

    return client.isNotEmpty() ? client : juce::String ("AI");
}

juce::var McpServer::callTool (const juce::String& name, const juce::var& arguments)
{
    auto args = arguments.isObject() ? arguments : object ({});
    juce::var request;

    if (name == "undo" || name == "redo")
    {
        request = object ({ { "source", source } });
        request.getDynamicObject()->setProperty (juce::Identifier (name), true);
    }
    else if (name == "batch")
    {
        request = object ({ { "source", source },
                            { "request", object ({ { "batch", args["label"].toString().isNotEmpty() ? args["label"]
                                                                                                     : juce::var ("AI changes") },
                                                   { "commands", args["commands"] } }) } });
    }
    else if (registry.find (name) != nullptr)
    {
        request = object ({ { "source", source },
                            { "request", object ({ { "command", name }, { "params", args } }) } });
    }
    else
    {
        return textResult ("Unknown tool: " + name, true);
    }

    auto reply = client.send (request);

    if (! static_cast<bool> (reply["ok"]))
        return textResult (reply["error"].toString(), true);

    auto value = reply["result"];
    return textResult (value.isVoid() ? juce::String ("Done.") : juce::JSON::toString (value, true), false);
}

} // namespace ng

