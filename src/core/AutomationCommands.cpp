#include "CommandRegistry.h"

namespace ng
{
namespace
{
    using params::object;
    using params::isNumber;
    using params::prop;
    using params::schema;

    double round (double v)
    {
        return std::round (v * 1.0e4) / 1.0e4;
    }

    // A strip's fader range, in dB, as set_mixer_strip takes it.
    constexpr double minDb = -100.0, maxDb = 6.0;

    /** A value as the curve stores it: a fader position for volume, else the value itself. */
    float toCurveValue (const AutomationTarget& t, double value)
    {
        return t.isVolume ? te::decibelsToVolumeFaderPosition (static_cast<float> (value)) : static_cast<float> (value);
    }

    double fromCurveValue (const AutomationTarget& t, float value)
    {
        return t.isVolume ? std::max (minDb, static_cast<double> (te::volumeFaderPositionToDB (value))) : static_cast<double> (value);
    }

    CommandResult setAutomation (Session& session, const juce::var& p)
    {
        auto t = findAutomationTarget (session, "set_automation", p);

        if (t.param == nullptr)
            return CommandResult::failure (t.error);

        auto points = p["points"];

        if (! points.isArray())
            return CommandResult::failure ("set_automation: points is a list of {beat, value} ([] removes the automation)");

        // Check every point before changing anything.
        auto& tempo = session.getEdit().tempoSequence;
        std::vector<std::pair<double, double>> checked;

        for (auto& point : *points.getArray())
        {
            auto beat = point["beat"], value = point["value"];

            if (! isNumber (beat) || ! isNumber (value))
                return CommandResult::failure ("set_automation: each point is {\"beat\": number, \"value\": number}");

            if (static_cast<double> (beat) < 0.0)
                return CommandResult::failure ("set_automation: beats start at 0");

            auto v = static_cast<double> (value);

            if (v < t.min || v > t.max)
                return CommandResult::failure ("set_automation: " + t.name + " is " + juce::String (t.min) + " to " + juce::String (t.max)
                                                 + (t.isVolume ? " dB" : ""));

            checked.emplace_back (static_cast<double> (beat), v);
        }

        std::stable_sort (checked.begin(), checked.end(), [] (auto& a, auto& b) { return a.first < b.first; });

        auto& curve = t.param->getCurve();
        curve.clear();

        for (auto& [beat, value] : checked)
            curve.addPoint (tempo.toTime (te::BeatPosition::fromBeats (beat)), toCurveValue (t, value), 0.0f);

        return CommandResult::success (describeAutomation (session, t));
    }

    CommandResult getAutomation (Session& session, const juce::var& p)
    {
        auto t = findAutomationTarget (session, "get_automation", p);

        if (t.param == nullptr)
            return CommandResult::failure (t.error);

        return CommandResult::success (describeAutomation (session, t));
    }
}

//==============================================================================
AutomationTarget findAutomationTarget (Session& session, const juce::String& command, const juce::var& p)
{
    AutomationTarget t;
    auto stripRef = params::getString (p, "strip");
    auto parameter = params::getString (p, "parameter");

    if (parameter.isEmpty())
    {
        t.error = command + ": give a parameter: \"volume\", \"pan\", or with an effect, one of its parameters (get_effect lists them)";
        return t;
    }

    if (! p.hasProperty ("effect"))
    {
        te::VolumeAndPanPlugin* fader = nullptr;

        if (stripRef == "master")
        {
            fader = session.getEdit().getMasterVolumePlugin().get();
        }
        else if (auto strip = session.findTrack (stripRef, Session::Role::strip))
        {
            fader = strip->getVolumePlugin();
            t.strip = strip;
        }
        else
        {
            t.error = command + ": no mixer strip called '" + stripRef + "' (use a strip id or name, or \"master\")";
            return t;
        }

        if (fader == nullptr || (parameter != "volume" && parameter != "pan"))
        {
            t.error = command + ": a strip's own parameters are \"volume\" and \"pan\"; for an effect's, give the effect too";
            return t;
        }

        t.isVolume = parameter == "volume";
        t.param = t.isVolume ? fader->volParam.get() : fader->panParam.get();
        t.parameterId = parameter;
        t.name = t.isVolume ? "Volume" : "Pan";
        t.min = t.isVolume ? minDb : -1.0;
        t.max = t.isVolume ? maxDb : 1.0;
        return t;
    }

    auto effect = findEffect (session, command, p);

    if (effect.effect == nullptr)
    {
        t.error = effect.error;
        return t;
    }

    if (stripRef != "master")
        t.strip = session.findTrack (stripRef, Session::Role::strip);

    t.effect = effect.effect;
    t.param = effect.effect->getAutomatableParameterByID (parameter).get();

    if (t.param == nullptr)
        for (auto candidate : effect.effect->getAutomatableParameters())
            if (candidate->getParameterName().equalsIgnoreCase (parameter))
                t.param = candidate;

    if (t.param == nullptr)
    {
        t.error = command + ": '" + effect.effect->getName() + "' has no parameter '" + parameter + "' (get_effect lists them)";
        return t;
    }

    t.parameterId = t.param->paramID;
    t.name = effect.effect->getName() + " " + t.param->getParameterName();
    t.min = t.param->getValueRange().getStart();
    t.max = t.param->getValueRange().getEnd();
    return t;
}

juce::var describeAutomation (Session& session, const AutomationTarget& t)
{
    auto& tempo = session.getEdit().tempoSequence;
    auto& curve = t.param->getCurve();
    juce::Array<juce::var> points;

    for (int i = 0; i < curve.getNumPoints(); ++i)
        points.add (object ({ { "beat", round (tempo.toBeats (curve.getPointTime (i)).inBeats()) },
                              { "value", round (fromCurveValue (t, curve.getPointValue (i))) } }));

    auto result = object ({ { "strip", t.strip != nullptr ? t.strip->itemID.toString() : juce::String ("master") },
                            { "parameter", t.parameterId },
                            { "name", t.name },
                            { "min", t.min },
                            { "max", t.max },
                            { "value", round (fromCurveValue (t, t.param->getCurrentExplicitValue())) },
                            { "points", points } });

    if (t.effect != nullptr)
        result.getDynamicObject()->setProperty ("effect", t.effect->itemID.toString());

    return result;
}

void registerAutomationCommands (CommandRegistry& registry)
{
    auto stripProp = prop ("string", "Mixer strip id or name, or \"master\"");
    auto effectProp = prop ("string", "To automate an effect's parameter: the effect's id, name or position on the strip");
    auto parameterProp = prop ("string", "\"volume\" (in dB) or \"pan\" (-1 to 1), or with effect, one of its parameter ids or "
                                         "names from get_effect (in that parameter's range)");

    auto points = prop ("array", "The points, as {beat, value}; the value moves in a straight line from each to the next. "
                                 "[] removes the automation, so the parameter stays where its control is set");

    registry.add ({ "set_automation",
                    "Automates a mixer strip's volume or pan, or an effect parameter, so it changes over the song: a fade-out, "
                    "a filter sweep, a louder chorus. Replaces the parameter's automation with the points given.",
                    schema ({ { "strip", stripProp }, { "effect", effectProp }, { "parameter", parameterProp }, { "points", points } },
                            { "strip", "parameter", "points" }),
                    true, setAutomation });

    registry.add ({ "get_automation",
                    "Returns a parameter's automation points and its range (get_state lists which parameters have automation).",
                    schema ({ { "strip", stripProp }, { "effect", effectProp }, { "parameter", parameterProp } },
                            { "strip", "parameter" }),
                    false, getAutomation });
}

} // namespace ng
