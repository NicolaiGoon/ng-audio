#include "CommandDispatcher.h"

namespace ng
{
namespace
{
    /** Looks up "$<n>.<field>.<field>..." in the results so far; an error says why it can't. */
    juce::var lookUpReference (const juce::String& text, const juce::Array<juce::var>& results, juce::String& error)
    {
        auto parts = juce::StringArray::fromTokens (text.substring (1), ".", {});
        auto index = parts[0];

        if (index.isEmpty() || ! index.containsOnly ("0123456789"))
        {
            error = "'" + text + "' isn't a reference like \"$0.channel\" (start a plain string with \"$$\" for a literal $)";
            return {};
        }

        auto n = index.getIntValue();

        if (n >= results.size())
        {
            error = "'" + text + "' refers to command " + index + ", which hasn't run yet (commands count from 0)";
            return {};
        }

        auto value = results[n];

        for (int i = 1; i < parts.size(); ++i)
        {
            auto& field = parts[i];

            if (value.isArray() && field.containsOnly ("0123456789") && field.isNotEmpty() && field.getIntValue() < value.size())
                value = value[field.getIntValue()];
            else if (value.isObject() && value.hasProperty (juce::Identifier (field.isEmpty() ? "_" : field)))
                value = value[juce::Identifier (field)];
            else
            {
                error = "'" + text + "': command " + index + "'s result has no '" + parts.joinIntoString (".", 1, i) + "'";
                return {};
            }
        }

        return value;
    }

    /** A copy of a command's params with the "$<n>.<field>" references filled in. */
    juce::var resolveReferences (const juce::var& v, const juce::Array<juce::var>& results, juce::String& error)
    {
        if (v.isString())
        {
            auto text = v.toString();

            if (text.startsWith ("$$"))
                return text.substring (1);

            if (text.startsWith ("$"))
                return lookUpReference (text, results, error);

            return v;
        }

        if (auto* array = v.getArray())
        {
            juce::Array<juce::var> copy;

            for (auto& item : *array)
                copy.add (resolveReferences (item, results, error));

            return copy;
        }

        if (auto* obj = v.getDynamicObject())
        {
            auto copy = new juce::DynamicObject();

            for (auto& prop : obj->getProperties())
                copy->setProperty (prop.name, resolveReferences (prop.value, results, error));

            return juce::var (copy);
        }

        return v;
    }

    bool isQuery (const juce::String& name)
    {
        return name.startsWith ("get_") || name.startsWith ("list_") || name.startsWith ("find_");
    }
}

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
    auto madeUndoStep = ownsTransaction && result.ok && um.getNumActionsInCurrentTransaction() > 0;

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

    if (! isQuery (name))
        log (name, { name }, result, madeUndoStep);

    return result;
}

CommandResult CommandDispatcher::executeBatch (const juce::String& undoLabel, const juce::var& commands)
{
    if (! commands.isArray())
        return CommandResult::failure ("Batch must be an array of commands");

    auto& um = session.getUndoManager();
    um.beginNewTransaction (undoLabel);

    juce::Array<juce::var> results;
    juce::StringArray names;
    CommandResult outcome;

    for (auto& c : *commands.getArray())
    {
        auto name = c["command"].toString();
        names.add (name);

        juce::String referenceError;
        auto params = resolveReferences (c["params"], results, referenceError);

        auto r = referenceError.isEmpty() ? run (name, params)
                                          : CommandResult::failure (name + ": " + referenceError);

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

    auto madeUndoStep = outcome.ok && um.getNumActionsInCurrentTransaction() > 0;
    um.beginNewTransaction();
    notify (undoLabel, outcome);
    log (undoLabel, names, outcome, madeUndoStep);
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
    CommandResult result { done, {}, done ? juce::String() : "There's nothing to undo" };
    notify ("undo", result);
    log ("Undo", { "undo" }, result, false);
    return done;
}

bool CommandDispatcher::redo()
{
    auto done = session.getUndoManager().redo();
    session.refreshAfterHistoryChange();
    CommandResult result { done, {}, done ? juce::String() : "There's nothing to redo" };
    notify ("redo", result);
    log ("Redo", { "redo" }, result, false);
    return done;
}

bool CommandDispatcher::canUndo() const   { return session.getUndoManager().canUndo(); }
bool CommandDispatcher::canRedo() const   { return session.getUndoManager().canRedo(); }

bool CommandDispatcher::isLatestUndoStep (const LogEntry& entry) const
{
    auto& um = session.getUndoManager();
    return entry.madeUndoStep && um.canUndo()
            && um.getUndoDescriptions().size() == entry.undoDepth
            && um.getUndoDescription() == entry.undoDescription;
}

void CommandDispatcher::log (const juce::String& label, juce::StringArray commands, const CommandResult& result, bool madeUndoStep)
{
    if (source.isEmpty() || onLog == nullptr)
        return;

    auto& um = session.getUndoManager();

    LogEntry entry;
    entry.source = source;
    entry.label = label;
    entry.commands = std::move (commands);
    entry.ok = result.ok;
    entry.error = result.error;
    entry.madeUndoStep = madeUndoStep;
    entry.undoDepth = um.getUndoDescriptions().size();
    entry.undoDescription = um.getUndoDescription();
    entry.time = juce::Time::getCurrentTime();
    onLog (entry);
}

void CommandDispatcher::notify (const juce::String& name, const CommandResult& result)
{
    session.updateLoopRange();

    if (onCommandExecuted != nullptr)
        onCommandExecuted (name, result);
}

} // namespace ng
