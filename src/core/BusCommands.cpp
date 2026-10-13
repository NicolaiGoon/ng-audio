#include "CommandRegistry.h"

namespace ng
{
namespace
{
    using params::prop;
    using params::schema;

    // Tracktion numbers its aux buses; this many is plenty for one project.
    constexpr int maxBuses = 32;
    constexpr double minSendDb = -100.0, maxSendDb = 6.0;

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

    /** Sets a send's level as part of the undo step: the stored value first, then the live one. */
    void setSendLevel (te::AuxSendPlugin& send, double db)
    {
        send.gainLevel = te::decibelsToVolumeFaderPosition (static_cast<float> (db));
        send.gain->updateFromAttachedValue();
    }

    te::AuxSendPlugin* findSend (Session& session, const te::AudioTrack& strip, const te::AudioTrack& bus)
    {
        for (auto send : Session::getSends (strip))
            if (session.findBus (send->getBusNumber()) == &bus)
                return send;

        return nullptr;
    }

    /** Puts a send just before the fader (pre-fader) or just after it (post-fader). */
    void placeSend (te::AudioTrack& strip, te::AuxSendPlugin& send, bool preFader, juce::UndoManager& um)
    {
        auto parent = strip.pluginList.state;
        auto volume = strip.getVolumePlugin();

        if (volume == nullptr)
            return;

        auto from = parent.indexOf (send.state);
        auto volumeIndex = parent.indexOf (volume->state);
        auto to = preFader ? volumeIndex : volumeIndex + 1;

        // moveChild takes the index the child should end up at, counted without it.
        if (from < to)
            --to;

        if (from >= 0 && to != from)
            parent.moveChild (from, to, &um);
    }

    juce::var describeSend (Session& session, const te::AudioTrack& strip, te::AuxSendPlugin& send)
    {
        auto bus = session.findBus (send.getBusNumber());
        return object ({ { "strip", strip.itemID.toString() },
                         { "bus", bus != nullptr ? bus->itemID.toString() : juce::String() },
                         { "level_db", std::round (send.getGainDb() * 100.0) / 100.0 },
                         { "pre_fader", Session::isPreFader (send) } });
    }

    //==============================================================================
    CommandResult createBus (Session& session, const juce::var& p)
    {
        auto& edit = session.getEdit();
        juce::Array<int> used;

        for (auto s : session.getStrips())
            used.add (Session::getBusNumber (*s));

        auto busNumber = 0;

        while (used.contains (busNumber))
            ++busNumber;

        if (busNumber >= maxBuses)
            return CommandResult::failure ("create_bus: a project can have " + juce::String (maxBuses) + " buses");

        auto palette = Session::getPalette();
        auto colour = palette[(session.getStrips().size() + 3) % palette.size()];

        if (p.hasProperty ("colour"))
        {
            auto parsed = parseColour (p["colour"].toString());

            if (! parsed)
                return CommandResult::failure ("create_bus: '" + p["colour"].toString() + "' isn't a hex colour like #3c8ce0");

            colour = *parsed;
        }

        auto busCount = 0;

        for (auto s : session.getStrips())
            if (Session::getBusNumber (*s) >= 0)
                ++busCount;

        auto name = params::getString (p, "name", "Bus " + juce::String (busCount + 1));
        auto strip = edit.insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (edit), nullptr);

        if (strip == nullptr)
            return CommandResult::failure ("create_bus: couldn't create a mixer strip");

        session.setRole (*strip, Session::Role::strip);
        strip->setName (name);
        strip->setColour (colour);
        strip->getOutput().setOutputToDefaultDevice (false);

        // The aux return goes first, so the bus's effects and fader come after it.
        auto ret = edit.getPluginCache().createNewPlugin (te::AuxReturnPlugin::xmlTypeName, {});

        if (auto auxReturn = dynamic_cast<te::AuxReturnPlugin*> (ret.get()))
            auxReturn->busNumber = busNumber;

        strip->pluginList.insertPlugin (ret, 0, nullptr);

        if (Session::getBusNumber (*strip) != busNumber)
            return CommandResult::failure ("create_bus: couldn't set up the bus");

        return CommandResult::success (object ({ { "strip", strip->itemID.toString() }, { "name", name } }));
    }

    struct SendTarget
    {
        te::AudioTrack* strip = nullptr;
        te::AudioTrack* bus = nullptr;
        juce::String error;
    };

    SendTarget findStripAndBus (Session& session, const juce::String& command, const juce::var& p)
    {
        SendTarget t;
        auto stripRef = params::getString (p, "strip");
        auto busRef = params::getString (p, "bus");

        t.strip = session.findTrack (stripRef, Session::Role::strip);
        t.bus = session.findTrack (busRef, Session::Role::strip);

        if (t.strip == nullptr)
            t.error = command + ": no mixer strip called '" + stripRef + "'";
        else if (t.bus == nullptr || Session::getBusNumber (*t.bus) < 0)
            t.error = command + ": no bus called '" + busRef + "' (create one with create_bus)";

        return t;
    }

    CommandResult setSend (Session& session, const juce::var& p)
    {
        auto t = findStripAndBus (session, "set_send", p);

        if (t.error.isNotEmpty())
            return CommandResult::failure (t.error);

        auto level = p["level_db"];
        auto preFader = p["pre_fader"];

        if (! level.isVoid() && ! isNumber (level))
            return CommandResult::failure ("set_send: level_db must be a number");

        if (! level.isVoid() && (static_cast<double> (level) < minSendDb || static_cast<double> (level) > maxSendDb))
            return CommandResult::failure ("set_send: level_db must be between -100 and 6");

        if (! preFader.isVoid() && ! preFader.isBool())
            return CommandResult::failure ("set_send: pre_fader is true or false");

        auto& um = session.getUndoManager();
        auto send = findSend (session, *t.strip, *t.bus);

        if (send == nullptr)
        {
            // A bus sending to itself, or to a strip that already feeds it, would loop.
            if (t.strip == t.bus || session.feeds (*t.bus, *t.strip))
                return CommandResult::failure ("set_send: '" + t.bus->getName() + "' already feeds '" + t.strip->getName()
                                                 + "', so a send would loop back into itself");

            auto created = session.getEdit().getPluginCache().createNewPlugin (te::AuxSendPlugin::create());
            send = dynamic_cast<te::AuxSendPlugin*> (created.get());

            if (send == nullptr)
                return CommandResult::failure ("set_send: couldn't create the send");

            send->busNumber = Session::getBusNumber (*t.bus);
            setSendLevel (*send, 0.0);
            t.strip->pluginList.insertPlugin (created, -1, nullptr);

            if (! t.strip->pluginList.contains (send))
                return CommandResult::failure ("set_send: the strip can't take more sends");

            placeSend (*t.strip, *send, false, um);
        }

        if (! level.isVoid())
            setSendLevel (*send, static_cast<double> (level));

        if (! preFader.isVoid())
            placeSend (*t.strip, *send, static_cast<bool> (preFader), um);

        return CommandResult::success (describeSend (session, *t.strip, *send));
    }

    CommandResult removeSend (Session& session, const juce::var& p)
    {
        auto t = findStripAndBus (session, "remove_send", p);

        if (t.error.isNotEmpty())
            return CommandResult::failure (t.error);

        auto send = findSend (session, *t.strip, *t.bus);

        if (send == nullptr)
            return CommandResult::failure ("remove_send: '" + t.strip->getName() + "' has no send to '" + t.bus->getName() + "'");

        send->deleteFromParent();
        return CommandResult::success (object ({ { "strip", t.strip->itemID.toString() }, { "bus", t.bus->itemID.toString() } }));
    }
}

void registerBusCommands (CommandRegistry& registry)
{
    registry.add ({ "create_bus",
                    "Adds a bus: a mixer strip that other strips send to with set_send (e.g. a shared reverb). "
                    "It goes to master and can hold effects like any strip. Channels can also be routed to it.",
                    schema ({ { "name", prop ("string", "Bus name (default \"Bus 1\", \"Bus 2\", ...)") },
                              { "colour", prop ("string", "Hex colour like #3c8ce0 (default: next in the palette)") } }),
                    true, createBus });

    registry.add ({ "set_send",
                    "Sends a mixer strip to a bus, or changes that send. A new send is post-fader at 0 dB unless "
                    "level_db or pre_fader says otherwise. Fails if the bus already feeds the strip.",
                    schema ({ { "strip", prop ("string", "Mixer strip id or name") },
                              { "bus", prop ("string", "Bus strip id or name") },
                              { "level_db", prop ("number", "Send level in dB, -100 to 6") },
                              { "pre_fader", prop ("boolean", "True to send before the strip's fader, so the fader doesn't change it") } },
                            { "strip", "bus" }),
                    true, setSend });

    registry.add ({ "remove_send", "Removes a mixer strip's send to a bus.",
                    schema ({ { "strip", prop ("string", "Mixer strip id or name") },
                              { "bus", prop ("string", "Bus strip id or name") } },
                            { "strip", "bus" }),
                    true, removeSend });
}

} // namespace ng
