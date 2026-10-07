#pragma once

#include "CommandRegistry.h"

namespace ng
{

/**
    Runs commands against a Session. This is the single entry point for changing the
    project: each call to execute() is one undo step, and executeBatch() runs several
    commands as one undo step (an AI "tidy the session" applies, or undoes, as a whole).
    A command or batch that fails is rolled back.
*/
class CommandDispatcher
{
public:
    CommandDispatcher (Session&, const CommandRegistry&);

    CommandResult execute (const juce::String& name, const juce::var& params = {});

    /** Runs [{"command": ..., "params": {...}}, ...] as one undo step.
        Returns the array of individual results, or the first error. */
    CommandResult executeBatch (const juce::String& undoLabel, const juce::var& commands);

    /** Runs a request given as JSON:
          {"command": "route", "params": {...}}
          {"batch": "Tidy session", "commands": [...]}
        and returns the result as JSON. This is what an MCP or scripting bridge calls. */
    juce::String executeJson (const juce::String& json);

    bool undo();
    bool redo();
    bool canUndo() const;
    bool canRedo() const;

    /** Called after every command or undo/redo, e.g. to refresh the UI. */
    std::function<void (const juce::String& commandName, const CommandResult&)> onCommandExecuted;

    const CommandRegistry& getRegistry() const noexcept    { return registry; }

private:
    CommandResult run (const juce::String& name, const juce::var& params);
    void notify (const juce::String&, const CommandResult&);

    Session& session;
    const CommandRegistry& registry;
};

} // namespace ng
