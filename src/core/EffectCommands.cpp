#include "CommandRegistry.h"

namespace ng
{
namespace
{
    using params::prop;
    using params::schema;

    juce::var object (std::initializer_list<std::pair<const char*, juce::var>> properties)
    {
        auto obj = new juce::DynamicObject();

        for (auto& [name, value] : properties)
            obj->setProperty (name, value);

        return juce::var (obj);
    }

    bool isNumber (const juce::var& v)
    {
        return v.isDouble() || v.isInt() || v.isInt64();
    }

    // The delay's time isn't a Tracktion parameter, so it's offered as one here.
    const juce::String delayLengthId ("length");
    constexpr int minDelayMs = 1, maxDelayMs = 2000;

    //==============================================================================
    /** Creates the effect add_effect names: a built-in one, or a scanned plugin effect by id or name. */
    std::variant<te::Plugin::Ptr, juce::String> createEffect (Session& session, const juce::String& name)
    {
        auto& edit = session.getEdit();

        for (auto& builtIn : Session::getBuiltInEffects())
            if (builtIn.id.equalsIgnoreCase (name) || builtIn.name.equalsIgnoreCase (name))
                return edit.getPluginCache().createNewPlugin (builtIn.pluginType, {});

        auto& known = edit.engine.getPluginManager().knownPluginList;
        std::unique_ptr<juce::PluginDescription> found = known.getTypeForIdentifierString (name);

        if (found == nullptr)
        {
            juce::Array<juce::PluginDescription> byName;

            for (auto& d : known.getTypes())
                if (d.name.equalsIgnoreCase (name))
                    byName.add (d);

            if (byName.size() > 1)
                return "there are " + juce::String (byName.size()) + " plugins called '" + name
                         + "'; use its id from list_plugins";

            if (byName.size() == 1)
                found = std::make_unique<juce::PluginDescription> (byName.getFirst());
        }

        if (found == nullptr)
            return "unknown effect '" + name + "' (use eq, compressor, reverb, delay, or a plugin from list_plugins)";

        if (found->isInstrument)
            return "'" + found->name + "' is an instrument; load it on a channel with set_instrument";

        te::Plugin::Ptr plugin = edit.getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, *found);

        if (plugin != nullptr && plugin->isMissing())
        {
            auto* external = dynamic_cast<te::ExternalPlugin*> (plugin.get());
            auto reason = external != nullptr ? external->getLoadError() : juce::String();
            return "couldn't load '" + found->name + "'" + (reason.isNotEmpty() ? ": " + reason : juce::String());
        }

        return plugin;
    }

    //==============================================================================
    /** Sets a plugin parameter as an undo step. Tracktion's own effects keep their values in
        the project, so their changes are undoable already; a plugin keeps its values inside
        itself, so this remembers the old value to put back. */
    class SetPluginParameterAction  : public juce::UndoableAction
    {
    public:
        SetPluginParameterAction (te::Edit& e, te::EditItemID plugin, juce::String param, float from, float to)
            : edit (e), pluginID (plugin), paramID (std::move (param)), oldValue (from), newValue (to)
        {
        }

        bool perform() override    { return apply (newValue); }
        bool undo() override       { return apply (oldValue); }

    private:
        bool apply (float value)
        {
            if (auto plugin = edit.getPluginCache().getPluginFor (pluginID))
            {
                if (auto param = plugin->getAutomatableParameterByID (paramID))
                {
                    param->setParameter (value, juce::sendNotificationSync);
                    return true;
                }
            }

            return false;
        }

        te::Edit& edit;
        te::EditItemID pluginID;
        juce::String paramID;
        float oldValue, newValue;
    };

    /** A parameter's value. A plugin's parameters learn of changes the plugin makes itself
        (such as when a project restores its settings) a moment later, so ask the plugin. */
    float getValue (te::Plugin& effect, te::AutomatableParameter& param)
    {
        // Tracktion names a plugin's parameter by its id, or its index if it has none.
        if (auto plugin = dynamic_cast<te::ExternalPlugin*> (&effect))
        {
            if (auto instance = plugin->getAudioPluginInstance())
            {
                auto& all = instance->getParameters();

                for (int i = 0; i < all.size(); ++i)
                {
                    auto withID = dynamic_cast<juce::AudioProcessorParameterWithID*> (all[i]);

                    if ((withID != nullptr ? withID->paramID : juce::String (i)) == param.paramID)
                        return all[i]->getValue();
                }
            }
        }

        return param.getCurrentValue();
    }

    juce::var describeParameters (te::Plugin& effect)
    {
        juce::Array<juce::var> list;

        if (auto delay = dynamic_cast<te::DelayPlugin*> (&effect))
            list.add (object ({ { "id", delayLengthId }, { "name", "Length" }, { "value", delay->lengthMs.get() },
                                { "min", minDelayMs }, { "max", maxDelayMs },
                                { "text", juce::String (delay->lengthMs.get()) + " ms" } }));

        for (auto param : effect.getAutomatableParameters())
            list.add (object ({ { "id", param->paramID }, { "name", param->getParameterName() },
                                { "value", getValue (effect, *param) },
                                { "min", param->getValueRange().getStart() }, { "max", param->getValueRange().getEnd() },
                                { "text", param->getCurrentValueAsStringWithLabel() } }));

        return list;
    }

    juce::var describeEffectWithParameters (te::Plugin& effect)
    {
        auto description = Session::describeEffect (effect);
        description.getDynamicObject()->setProperty ("params", describeParameters (effect));
        return description;
    }

    //==============================================================================
    CommandResult addEffect (Session& session, const juce::var& p)
    {
        juce::String error;
        auto list = findList (session, "add_effect", p, error);

        if (list == nullptr)
            return CommandResult::failure (error);

        auto effects = Session::getEffects (*list);
        auto index = effects.size();

        if (p.hasProperty ("index"))
        {
            if (! isNumber (p["index"]) || static_cast<int> (p["index"]) < 0)
                return CommandResult::failure ("add_effect: index is a position from 0");

            index = juce::jmin (effects.size(), static_cast<int> (p["index"]));
        }

        auto created = createEffect (session, params::getString (p, "effect"));

        if (auto message = std::get_if<juce::String> (&created))
            return CommandResult::failure ("add_effect: " + *message);

        auto effect = std::get<te::Plugin::Ptr> (created);

        if (effect == nullptr)
            return CommandResult::failure ("add_effect: couldn't create the effect");

        Session::storeEffectSettings (*effect);
        list->insertPlugin (effect, Session::getEffectInsertIndex (*list, index), nullptr);

        if (! list->contains (effect.get()))
            return CommandResult::failure ("add_effect: the strip can't take more effects");

        return CommandResult::success (object ({ { "strip", p["strip"] },
                                                 { "index", Session::getEffects (*list).indexOf (effect.get()) },
                                                 { "effect", Session::describeEffect (*effect) } }));
    }

    CommandResult removeEffect (Session& session, const juce::var& p)
    {
        auto r = findEffect (session, "remove_effect", p);

        if (r.effect == nullptr)
            return CommandResult::failure (r.error);

        auto id = r.effect->itemID.toString();
        r.effect->deleteFromParent();
        return CommandResult::success (object ({ { "strip", p["strip"] }, { "removed", id } }));
    }

    CommandResult moveEffect (Session& session, const juce::var& p)
    {
        auto r = findEffect (session, "move_effect", p);

        if (r.effect == nullptr)
            return CommandResult::failure (r.error);

        if (! isNumber (p["index"]) || static_cast<int> (p["index"]) < 0)
            return CommandResult::failure ("move_effect: index is a position from 0");

        auto effects = Session::getEffects (*r.list);
        auto to = juce::jmin (effects.size() - 1, static_cast<int> (p["index"]));
        auto from = effects.indexOf (r.effect);

        if (to != from)
        {
            // Move the effect's state to where the effect now in that position is.
            auto parent = r.effect->state.getParent();
            parent.moveChild (parent.indexOf (r.effect->state), parent.indexOf (effects[to]->state),
                              &session.getUndoManager());
        }

        return CommandResult::success (object ({ { "strip", p["strip"] }, { "effect", r.effect->itemID.toString() },
                                                 { "index", Session::getEffects (*r.list).indexOf (r.effect) } }));
    }

    CommandResult setEffect (Session& session, const juce::var& p)
    {
        auto r = findEffect (session, "set_effect", p);

        if (r.effect == nullptr)
            return CommandResult::failure (r.error);

        auto effect = r.effect;
        auto enabled = p["enabled"];
        auto values = p["params"];

        if (! enabled.isVoid() && ! enabled.isBool())
            return CommandResult::failure ("set_effect: enabled is true or false");

        if (! values.isVoid() && ! values.isObject())
            return CommandResult::failure ("set_effect: params is an object of parameter id (or name) to value");

        // Check every value before changing anything, so a bad one leaves the effect as it was.
        std::vector<std::pair<te::AutomatableParameter::Ptr, float>> changes;
        std::optional<int> delayLength;

        if (auto obj = values.getDynamicObject())
        {
            for (auto& [key, value] : obj->getProperties())
            {
                auto name = key.toString();

                if (! isNumber (value))
                    return CommandResult::failure ("set_effect: the value for '" + name + "' must be a number");

                auto number = static_cast<double> (value);

                if (auto delay = dynamic_cast<te::DelayPlugin*> (effect); delay != nullptr && name == delayLengthId)
                {
                    if (number < minDelayMs || number > maxDelayMs)
                        return CommandResult::failure ("set_effect: length is " + juce::String (minDelayMs) + " to "
                                                         + juce::String (maxDelayMs) + " ms");

                    delayLength = juce::roundToInt (number);
                    continue;
                }

                te::AutomatableParameter::Ptr param = effect->getAutomatableParameterByID (name);

                if (param == nullptr)
                    for (auto candidate : effect->getAutomatableParameters())
                        if (candidate->getParameterName().equalsIgnoreCase (name))
                            param = candidate;

                if (param == nullptr)
                    return CommandResult::failure ("set_effect: '" + effect->getName() + "' has no parameter '" + name
                                                     + "' (get_effect lists them)");

                auto range = param->getValueRange();

                if (number < range.getStart() || number > range.getEnd())
                    return CommandResult::failure ("set_effect: '" + param->getParameterName() + "' is "
                                                     + juce::String (range.getStart()) + " to " + juce::String (range.getEnd()));

                changes.emplace_back (param, static_cast<float> (number));
            }
        }

        auto& um = session.getUndoManager();

        if (! enabled.isVoid())
            effect->setEnabled (static_cast<bool> (enabled));

        if (delayLength)
            if (auto delay = dynamic_cast<te::DelayPlugin*> (effect))
                delay->lengthMs = *delayLength;

        for (auto& [param, value] : changes)
        {
            if (dynamic_cast<te::ExternalPlugin*> (effect) != nullptr)
                um.perform (new SetPluginParameterAction (session.getEdit(), effect->itemID, param->paramID,
                                                          getValue (*effect, *param), value));
            else
                param->setParameter (value, juce::sendNotificationSync);
        }

        auto result = describeEffectWithParameters (*effect);
        result.getDynamicObject()->setProperty ("strip", p["strip"]);
        return CommandResult::success (result);
    }

    CommandResult getEffect (Session& session, const juce::var& p)
    {
        auto r = findEffect (session, "get_effect", p);

        if (r.effect == nullptr)
            return CommandResult::failure (r.error);

        return CommandResult::success (describeEffectWithParameters (*r.effect));
    }

    CommandResult showEffectWindow (Session& session, const juce::var& p)
    {
        auto r = findEffect (session, "show_effect_window", p);

        if (r.effect == nullptr)
            return CommandResult::failure (r.error);

        if (r.effect->isMissing())
            return CommandResult::failure ("show_effect_window: '" + r.effect->getName() + "' isn't loaded");

        r.effect->showWindowExplicitly();
        return CommandResult::success (object ({ { "effect", r.effect->itemID.toString() },
                                                 { "shown", r.effect->windowState->isWindowShowing() } }));
    }
}

te::PluginList* findList (Session& session, const juce::String& command, const juce::var& p, juce::String& error)
{
    auto ref = params::getString (p, "strip");
    auto list = session.findEffectList (ref);

    if (list == nullptr)
        error = command + ": no mixer strip called '" + ref + "' (use a strip id or name, or \"master\")";

    return list;
}

EffectRef findEffect (Session& session, const juce::String& command, const juce::var& p)
{
    EffectRef r;
    r.list = findList (session, command, p, r.error);

    if (r.list == nullptr)
        return r;

    auto effects = Session::getEffects (*r.list);
    auto ref = p["effect"];

    if (isNumber (ref))
    {
        auto index = static_cast<int> (ref);

        if (! juce::isPositiveAndBelow (index, effects.size()))
            r.error = command + ": there's no effect " + juce::String (index) + " (the strip has "
                        + juce::String (effects.size()) + ", counting from 0)";
        else
            r.effect = effects[index];

        return r;
    }

    auto name = ref.toString();
    juce::Array<te::Plugin*> byName;

    for (auto e : effects)
    {
        if (e->itemID.toString() == name)
        {
            r.effect = e;
            return r;
        }

        if (e->getName().equalsIgnoreCase (name) || Session::getPluginId (*e).equalsIgnoreCase (name))
            byName.add (e);
    }

    if (byName.size() == 1)
        r.effect = byName.getFirst();
    else if (byName.size() > 1)
        r.error = command + ": the strip has " + juce::String (byName.size()) + " effects called '" + name
                    + "'; use its id or position";
    else
        r.error = command + ": the strip has no effect '" + name + "'";

    return r;
}

void registerEffectCommands (CommandRegistry& registry)
{
    auto stripProp = prop ("string", "Mixer strip id or name, or \"master\"");
    auto effectProp = prop ("string", "The effect's id from get_state, its position on the strip (0 is the first), "
                                      "or its name if no other effect on the strip shares it");

    registry.add ({ "add_effect",
                    "Adds an insert effect to a mixer strip (or the master), before the fader. Built in: eq, compressor, "
                    "reverb and delay; or a scanned plugin effect by its id or name from list_plugins. "
                    "Fails if the plugin won't load.",
                    schema ({ { "strip", stripProp },
                              { "effect", prop ("string", "eq, compressor, reverb, delay, or a plugin's id or name") },
                              { "index", prop ("integer", "Position among the strip's effects, from 0 (default: last)") } },
                            { "strip", "effect" }),
                    true, addEffect });

    registry.add ({ "remove_effect", "Removes an effect from a mixer strip.",
                    schema ({ { "strip", stripProp }, { "effect", effectProp } }, { "strip", "effect" }),
                    true, removeEffect });

    registry.add ({ "move_effect", "Moves an effect to another position on its strip (effects run first to last).",
                    schema ({ { "strip", stripProp }, { "effect", effectProp },
                              { "index", prop ("integer", "New position, from 0") } },
                            { "strip", "effect", "index" }),
                    true, moveEffect });

    registry.add ({ "set_effect",
                    "Turns an effect on or off (bypass) and sets its parameters, by the ids or names get_effect lists. "
                    "Values are in each parameter's own range. Changes made in a plugin's own window aren't undo steps.",
                    schema ({ { "strip", stripProp }, { "effect", effectProp },
                              { "enabled", prop ("boolean", "False bypasses the effect") },
                              { "params", prop ("object", "Parameter id or name to value, e.g. {\"wet level\": 0.5}") } },
                            { "strip", "effect" }),
                    true, setEffect });

    registry.add ({ "get_effect", "Returns an effect with its parameters: id, name, value, range and the value as text.",
                    schema ({ { "strip", stripProp }, { "effect", effectProp } }, { "strip", "effect" }),
                    false, getEffect });

    registry.add ({ "show_effect_window",
                    "Opens an effect's window: the plugin's own editor, or sliders for a built-in effect. "
                    "Not an undo step; shown is false when there's no screen to show it on.",
                    schema ({ { "strip", stripProp }, { "effect", effectProp } }, { "strip", "effect" }),
                    false, showEffectWindow });
}

} // namespace ng
