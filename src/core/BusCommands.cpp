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

    //==============================================================================
    CommandResult routeStrip (Session& session, const juce::var& p)
    {
        auto stripRef = params::getString (p, "strip");
        auto outputRef = params::getString (p, "output");
        auto strip = session.findTrack (stripRef, Session::Role::strip);

        if (strip == nullptr)
            return CommandResult::failure ("route_strip: no mixer strip called '" + stripRef + "'");

        if (session.getOutputOf (*strip) != nullptr)
            return CommandResult::failure ("route_strip: '" + strip->getName() + "' plays an instrument output into its channel's strip; "
                                           "turn multiple outputs off with set_multi_output to change that");

        if (outputRef == "master")
        {
            strip->getOutput().setOutputToDefaultDevice (false);
        }
        else
        {
            auto dest = session.findTrack (outputRef, Session::Role::strip);

            if (dest == nullptr)
                return CommandResult::failure ("route_strip: no mixer strip called '" + outputRef + "'");

            if (dest == strip || session.feeds (*dest, *strip))
                return CommandResult::failure ("route_strip: '" + dest->getName() + "' already plays into '" + strip->getName()
                                               + "', so this would loop back into itself");

            strip->getOutput().setOutputToTrack (dest);
        }

        auto dest = strip->getOutput().getDestinationTrack();
        return CommandResult::success (object ({ { "strip", strip->itemID.toString() },
                                                 { "output", dest != nullptr ? dest->itemID.toString() : juce::String ("master") } }));
    }

    juce::String busNameFor (const juce::String& tag)
    {
        return tag.substring (0, 1).toUpperCase() + tag.substring (1) + " Bus";
    }

    /** Takes a channel off its tag's bus: whichever of it or its strip plays into the bus
        goes back to its own strip or to master. */
    void untag (Session& session, te::AudioTrack& channel)
    {
        auto old = Session::getTag (channel);
        session.setTag (channel, {});

        auto bus = session.findTagBus (old);
        auto dest = session.getDestinationStrip (channel);

        if (bus == nullptr || dest == nullptr)
            return;

        if (dest == bus)
        {
            auto own = session.getOwnStrip (channel);

            if (own != nullptr && own != bus)
                channel.getOutput().setOutputToTrack (own);
            else
                channel.getOutput().setOutputToDefaultDevice (false);

            return;
        }

        if (dest->getOutput().getDestinationTrack() != bus)
            return;

        // The strip stays on the bus while another channel on it still has the tag.
        for (auto c : session.getChannels())
            if (c != &channel && session.getDestinationStrip (*c) == dest && Session::getTag (*c) == old)
                return;

        dest->getOutput().setOutputToDefaultDevice (false);
    }
}

CommandResult tagChannel (Session& session, te::AudioTrack& channel, const juce::String& requestedTag, const juce::String& command)
{
    auto tag = requestedTag.trim().toLowerCase();

    if (tag.length() > 32 || ! tag.containsOnly ("abcdefghijklmnopqrstuvwxyz0123456789 -_"))
        return CommandResult::failure (command + ": a tag is a short word like \"drums\", \"bass\" or \"vox\" (letters, digits, spaces and dashes)");

    if (Session::getTag (channel).isNotEmpty())
        untag (session, channel);

    if (tag.isEmpty())
        return CommandResult::success (object ({ { "channel", channel.itemID.toString() }, { "tag", "" } }));

    auto bus = session.findTagBus (tag);
    auto created = bus == nullptr;

    if (created)
    {
        auto result = createBus (session, object ({ { "name", busNameFor (tag) } }));

        if (! result.ok)
            return CommandResult::failure (command + ": " + result.error.fromFirstOccurrenceOf ("create_bus: ", false, false));

        bus = session.findTrack (result.value["strip"].toString(), Session::Role::strip);

        if (bus == nullptr)
            return CommandResult::failure (command + ": couldn't create the bus");

        session.setTagOfBus (*bus, tag);
    }

    session.setTag (channel, tag);
    auto dest = session.getDestinationStrip (channel);

    // A channel playing straight into master, or into another tag's bus, goes through its own
    // strip if it still has one (keeping its effects and fader), or else straight into the bus.
    if (dest == nullptr || (dest != bus && Session::getTagOfBus (*dest).isNotEmpty()))
    {
        auto own = session.getOwnStrip (channel);
        dest = own != nullptr && Session::getTagOfBus (*own).isEmpty() ? own : bus;
        channel.getOutput().setOutputToTrack (dest);
    }

    if (dest != bus && dest->getOutput().getDestinationTrack() != bus)
    {
        if (session.getOutputOf (*dest) != nullptr || session.feeds (*bus, *dest))
            return CommandResult::failure (command + ": '" + bus->getName() + "' already plays into '" + dest->getName()
                                           + "', so this would loop back into itself");

        dest->getOutput().setOutputToTrack (bus);
    }

    return CommandResult::success (object ({ { "channel", channel.itemID.toString() },
                                             { "tag", tag },
                                             { "bus", bus->itemID.toString() },
                                             { "bus_name", bus->getName() },
                                             { "created_bus", created } }));
}

namespace
{
    CommandResult tagChannelCommand (Session& session, const juce::var& p)
    {
        auto ref = params::getString (p, "channel");
        auto channel = session.findTrack (ref, Session::Role::channel);

        if (channel == nullptr)
            return CommandResult::failure ("tag_channel: no channel called '" + ref + "'");

        if (! p.hasProperty ("tag"))
            return CommandResult::failure ("tag_channel: give a tag, or \"\" to remove it");

        return tagChannel (session, *channel, p["tag"].toString(), "tag_channel");
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

    registry.add ({ "route_strip",
                    "Plays a mixer strip into another strip (a group or bus, which then carries it through its own effects "
                    "and fader) or into \"master\". Fails if that would loop.",
                    schema ({ { "strip", prop ("string", "Mixer strip id or name") },
                              { "output", prop ("string", "The strip to play into (id or name), or \"master\"") } },
                            { "strip", "output" }),
                    true, routeStrip });

    registry.add ({ "tag_channel",
                    "Tags a channel with what it is, such as \"drums\", \"bass\" or \"vox\". Each tag has a bus (\"Drums Bus\"), "
                    "created the first time it's used, and the channel's mixer strip plays into it, so all the drums can be "
                    "mixed and processed together (add effects to the bus). An empty tag takes the channel off its bus. "
                    "Returns the bus's id.",
                    schema ({ { "channel", prop ("string", "Channel id or name") },
                              { "tag", prop ("string", "The tag, a short word like drums, bass or vox, or \"\" for none") } },
                            { "channel", "tag" }),
                    true, tagChannelCommand });

    registry.add ({ "remove_send", "Removes a mixer strip's send to a bus.",
                    schema ({ { "strip", prop ("string", "Mixer strip id or name") },
                              { "bus", prop ("string", "Bus strip id or name") } },
                            { "strip", "bus" }),
                    true, removeSend });
}

} // namespace ng
