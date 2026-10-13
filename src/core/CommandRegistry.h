#pragma once

#include "Session.h"

namespace ng
{

/** What a command returns: a value on success, or an error message. */
struct CommandResult
{
    bool ok = false;
    juce::var value;
    juce::String error;

    static CommandResult success (juce::var v = {})       { return { true, std::move (v), {} }; }
    static CommandResult failure (juce::String message)   { return { false, {}, std::move (message) }; }

    /** {"ok": true, "result": ...} or {"ok": false, "error": "..."} */
    juce::var toVar() const;
};

/**
    A named action with parameters. Everything that changes the project goes through
    one of these: UI buttons, keyboard shortcuts, macros, undo, and the MCP server
    (which is a thin wrapper listing these specs as tools).
*/
struct CommandSpec
{
    juce::String name;
    juce::String description;

    /** JSON Schema for the params object, published as the MCP tool's input schema. */
    juce::var paramsSchema;

    /** False for commands that don't edit the project (transport, queries). */
    bool undoable = true;

    std::function<CommandResult (Session&, const juce::var& params)> run;
};

class CommandRegistry
{
public:
    void add (CommandSpec);

    const CommandSpec* find (const juce::String& name) const;
    const std::vector<CommandSpec>& getAll() const noexcept     { return specs; }

    /** Every command as {name, description, inputSchema}, ready for MCP tools/list. */
    juce::var describe() const;

private:
    std::vector<CommandSpec> specs;
};

/** Registers create_track, route, rename_track, set_track_colour, delete_track, set_instrument,
    set_mixer_strip, set_steps, set_tempo, set_time_signature, set_loop, add_midi_clip, move_clip, resize_clip, split_clip,
    duplicate_clip, delete_clip, play, stop, set_position, new_project,
    open_project, save_project and get_state. */
void registerProjectCommands (CommandRegistry&);

//==============================================================================
/** Small helpers for building schemas and reading params. */
namespace params
{
    juce::var schema (std::initializer_list<std::pair<juce::String, juce::var>> properties,
                      juce::StringArray required = {});
    juce::var prop (const juce::String& type, const juce::String& description);

    juce::String getString (const juce::var& params, const juce::Identifier&, const juce::String& fallback = {});
    double getDouble (const juce::var& params, const juce::Identifier&, double fallback);
}

} // namespace ng
