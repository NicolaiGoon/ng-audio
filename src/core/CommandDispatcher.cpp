#include "CommandDispatcher.h"

namespace ng
{

CommandDispatcher::CommandDispatcher (Session& s, const CommandRegistry& r)
    : session (s), registry (r)
{
}

CommandResult CommandDispatcher::run (const juce::String& name, const juce::var& params)
{
    auto spec = registry.find (name);

    if (spec == nullptr)
        return CommandResult::failure ("Unknown command: " + name);

    if (! (params.isVoid() || params.isObject()))
        return CommandResult::failure (name + ": params must be an object");

    // An export handles messages while it renders; nothing may change the project under it.
    if (session.isExporting() && name != "get_state")
        return CommandResult::failure (name + ": wait until the export is finished");

    auto result = spec->run (session, params);

    if (result.ok && spec->undoable)
        session.sortTrackContents();

    return result;
}

CommandResult CommandDispatcher::execute (const juce::String& name, const juce::var& params)
{
    auto spec = registry.find (name);
    auto& um = session.getUndoManager();

    // Inside a gesture, every command joins the gesture's one undo step.
    auto ownsTransaction = spec != nullptr && spec->undoable && ! gestureOpen;

    if (ownsTransaction)
        um.beginNewTransaction (name);

    auto result = run (name, params);

    if (ownsTransaction)
    {
        if (! result.ok)
        {
            um.undoCurrentTransactionOnly();
            session.refreshAfterHistoryChange();
        }

        um.beginNewTransaction();
    }

    notify (name, result);
    return result;
}

CommandResult CommandDispatcher::executeBatch (const juce::String& undoLabel, const juce::var& commands)
{
    if (! commands.isArray())
        return CommandResult::failure ("Batch must be an array of commands");

    auto& um = session.getUndoManager();
    um.beginNewTransaction (undoLabel);

    juce::Array<juce::var> results;
    CommandResult outcome;

    for (auto& c : *commands.getArray())
    {
        auto name = c["command"].toString();
        auto r = run (name, c["params"]);

        if (! r.ok)
        {
            um.undoCurrentTransactionOnly();
            session.refreshAfterHistoryChange();
            outcome = CommandResult::failure ("Batch '" + undoLabel + "' rolled back. " + r.error);
            break;
        }

        results.add (r.value);
    }

    if (outcome.error.isEmpty())
        outcome = CommandResult::success (results);

    um.beginNewTransaction();
    notify (undoLabel, outcome);
    return outcome;
}

juce::String CommandDispatcher::executeJson (const juce::String& json)
{
    juce::var request;
    auto parse = juce::JSON::parse (json, request);

    CommandResult result;

    if (parse.failed() || ! request.isObject())
        result = CommandResult::failure ("Invalid JSON request: " + parse.getErrorMessage());
    else if (request.hasProperty ("batch"))
        result = executeBatch (request["batch"].toString(), request["commands"]);
    else
        result = execute (request["command"].toString(), request["params"]);

    return juce::JSON::toString (result.toVar(), true);
}

void CommandDispatcher::beginGesture (const juce::String& undoLabel)
{
    session.getUndoManager().beginNewTransaction (undoLabel);
    gestureOpen = true;
}

void CommandDispatcher::endGesture()
{
    if (! gestureOpen)
        return;

    gestureOpen = false;
    session.getUndoManager().beginNewTransaction();
}

//==============================================================================
bool CommandDispatcher::undo()
{
    auto done = session.getUndoManager().undo();
    session.refreshAfterHistoryChange();
    notify ("undo", CommandResult { done, {}, {} });
    return done;
}

bool CommandDispatcher::redo()
{
    auto done = session.getUndoManager().redo();
    session.refreshAfterHistoryChange();
    notify ("redo", CommandResult { done, {}, {} });
    return done;
}

bool CommandDispatcher::canUndo() const   { return session.getUndoManager().canUndo(); }
bool CommandDispatcher::canRedo() const   { return session.getUndoManager().canRedo(); }

void CommandDispatcher::notify (const juce::String& name, const CommandResult& result)
{
    session.updateLoopRange();

    if (onCommandExecuted != nullptr)
        onCommandExecuted (name, result);
}

} // namespace ng
