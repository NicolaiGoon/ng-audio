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
        Returns the array of individual results, or the first error.

        A string param of the form "$<n>.<field>" is replaced by that field of the result of
        command n (counting from 0) earlier in the batch, so one batch can create a track and
        then use its ids: {"command": "add_midi_clip", "params": {"channel": "$0.channel"}}.
        Fields can go deeper ("$2.clips.0.id"), and "$<n>" alone is the whole result. A string
        that starts with "$$" stands for itself with one "$" removed. */
    CommandResult executeBatch (const juce::String& undoLabel, const juce::var& commands);

    /** Runs a request given as JSON:
          {"command": "route", "params": {...}}
          {"batch": "Tidy session", "commands": [...]}
        and returns the result as JSON. This is what an MCP or scripting bridge calls. */
    juce::String executeJson (const juce::String& json);

    /** Groups everything executed until endGesture() into one undo step, e.g. all the
        set_mixer_strip calls made while a fader is dragged. Commands still apply one by one. */
    void beginGesture (const juce::String& undoLabel);
    void endGesture();

    bool undo();
    bool redo();
    bool canUndo() const;
    bool canRedo() const;

    //==============================================================================
    /** What a command, batch, undo or redo sent from outside the UI (an AI client or the
        console) did, for the command log. Queries such as get_state aren't logged. */
    struct LogEntry
    {
        juce::String source;            // who sent it, e.g. "Claude" or "Console"
        juce::String label;             // the command's name, or the batch's label
        juce::StringArray commands;     // the commands a batch ran, in order
        bool ok = false;
        juce::String error;
        bool madeUndoStep = false;      // true if it changed the project, so undo can take it back
        int undoDepth = 0;              // how many undo steps there were just after it
        juce::String undoDescription;   // and the name of the newest one
        juce::Time time;
    };

    /** Called for every logged command (on the thread that ran it, the message thread). */
    std::function<void (const LogEntry&)> onLog;

    /** Logs everything executed while it exists as coming from source. */
    class ScopedSource
    {
    public:
        ScopedSource (CommandDispatcher& d, const juce::String& source) : dispatcher (d), previous (d.source)   { d.source = source; }
        ~ScopedSource()     { dispatcher.source = previous; }

    private:
        CommandDispatcher& dispatcher;
        juce::String previous;
    };

    /** True while a logged entry's undo step is still the newest one, so undo() would take back
        exactly that entry (and nothing done after it). */
    bool isLatestUndoStep (const LogEntry&) const;

    /** Called after every command or undo/redo, e.g. to refresh the UI. */
    std::function<void (const juce::String& commandName, const CommandResult&)> onCommandExecuted;

    const CommandRegistry& getRegistry() const noexcept    { return registry; }

private:
    CommandResult run (const juce::String& name, const juce::var& params);
    void notify (const juce::String&, const CommandResult&);
    void log (const juce::String& label, juce::StringArray commands, const CommandResult&, bool madeUndoStep);

    Session& session;
    const CommandRegistry& registry;
    bool gestureOpen = false;
    juce::String source;
};

} // namespace ng
