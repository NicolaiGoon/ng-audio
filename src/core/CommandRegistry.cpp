#include "CommandRegistry.h"

namespace ng
{

juce::var CommandResult::toVar() const
{
    auto obj = new juce::DynamicObject();
    obj->setProperty ("ok", ok);

    if (ok)
        obj->setProperty ("result", value);
    else
        obj->setProperty ("error", error);

    return juce::var (obj);
}

//==============================================================================
void CommandRegistry::add (CommandSpec spec)
{
    jassert (spec.name.isNotEmpty() && spec.run != nullptr);
    jassert (find (spec.name) == nullptr);
    specs.push_back (std::move (spec));
}

const CommandSpec* CommandRegistry::find (const juce::String& name) const
{
    for (auto& s : specs)
        if (s.name == name)
            return &s;

    return nullptr;
}

juce::var CommandRegistry::describe() const
{
    juce::Array<juce::var> tools;

    for (auto& s : specs)
    {
        auto obj = new juce::DynamicObject();
        obj->setProperty ("name", s.name);
        obj->setProperty ("description", s.description);
        obj->setProperty ("inputSchema", s.paramsSchema.isObject() ? s.paramsSchema : params::schema ({}));
        tools.add (juce::var (obj));
    }

    return tools;
}

//==============================================================================
namespace params
{
    juce::var schema (std::initializer_list<std::pair<juce::String, juce::var>> properties,
                      juce::StringArray required)
    {
        auto props = new juce::DynamicObject();

        for (auto& [name, p] : properties)
            props->setProperty (name, p);

        auto obj = new juce::DynamicObject();
        obj->setProperty ("type", "object");
        obj->setProperty ("properties", juce::var (props));

        juce::Array<juce::var> req;

        for (auto& r : required)
            req.add (r);

        obj->setProperty ("required", req);
        return juce::var (obj);
    }

    juce::var prop (const juce::String& type, const juce::String& description)
    {
        auto obj = new juce::DynamicObject();
        obj->setProperty ("type", type);
        obj->setProperty ("description", description);
        return juce::var (obj);
    }

    juce::String getString (const juce::var& p, const juce::Identifier& key, const juce::String& fallback)
    {
        return p.hasProperty (key) ? p[key].toString() : fallback;
    }

    double getDouble (const juce::var& p, const juce::Identifier& key, double fallback)
    {
        return p.hasProperty (key) ? static_cast<double> (p[key]) : fallback;
    }

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

    double roundBeats (double beats)
    {
        return std::round (beats * 1.0e6) / 1.0e6;
    }
}

} // namespace ng
