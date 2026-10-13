#include "CommandRegistry.h"

namespace ng
{
namespace
{
    using params::prop;
    using params::schema;

    juce::var describeOutputs (Session& session, te::AudioTrack& channel)
    {
        auto obj = new juce::DynamicObject();
        obj->setProperty ("channel", channel.itemID.toString());
        obj->setProperty ("multi_output", Session::getMultiOutputRack (channel) != nullptr);

        juce::Array<juce::var> strips;

        for (auto s : session.getOutputStrips (channel))
            strips.add (s->itemID.toString());

        obj->setProperty ("output_strips", strips);
        return juce::var (obj);
    }

    /** A new, empty rack. It's added outside the undo history: Tracktion's rack instances keep
        the rack object they were made with, and a rack removed and brought back by undo would
        be a new object they don't know. Racks nothing uses are tidied up when a project opens. */
    te::RackType::Ptr createRack (te::Edit& edit, const juce::String& name)
    {
        auto id = edit.createNewItemID();
        juce::ValueTree v (te::IDs::RACK);
        id.writeID (v, nullptr);
        v.setProperty (te::IDs::name, name, nullptr);
        edit.state.getOrCreateChildWithName (te::IDs::RACKS, nullptr).addChild (v, -1, nullptr);
        return edit.getRackList().getRackTypeForID (id);
    }

    /** Moves the instrument into a rack and gives each stereo output a strip that goes to the
        channel's own strip. */
    juce::String turnOn (Session& session, te::AudioTrack& channel, te::AudioTrack& sum)
    {
        auto& edit = session.getEdit();
        te::Plugin::Ptr instrument = Session::getInstrument (channel);

        if (dynamic_cast<te::ExternalPlugin*> (instrument.get()) == nullptr)
            return "'" + channel.getName() + "' needs a plugin instrument for multiple outputs";

        if (instrument->isMissing())
            return "'" + instrument->getName() + "' isn't loaded";

        auto numOutputs = Session::getNumOutputs (*instrument);

        if (numOutputs <= 2)
            return "'" + instrument->getName() + "' has only one stereo output";

        auto rack = createRack (edit, channel.getName() + " outputs");

        if (rack == nullptr)
            return "couldn't create a rack";

        // Auto-connecting gives the rack one output per plugin output and wires MIDI in.
        if (! rack->addPlugin (instrument, { 0.5f, 0.5f }, true))
            return "couldn't put '" + instrument->getName() + "' in a rack";

        // The channel's instance feeds the rack MIDI and plays nothing itself (output 0 is none).
        auto channelInstance = edit.getPluginCache().createNewPlugin (te::RackInstance::create (*rack));

        if (auto ri = dynamic_cast<te::RackInstance*> (channelInstance.get()))
        {
            ri->leftOutputComesFrom = 0;
            ri->rightOutputComesFrom = 0;
        }

        channel.pluginList.insertPlugin (channelInstance, 0, nullptr);

        if (Session::getMultiOutputRack (channel) == nullptr)
            return "couldn't connect the channel to the rack";

        for (int pair = 0; pair * 2 < numOutputs; ++pair)
        {
            auto left = pair * 2 + 1, right = juce::jmin (pair * 2 + 2, numOutputs);
            auto strip = edit.insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (edit), nullptr);

            if (strip == nullptr)
                return "couldn't create a mixer strip";

            session.setRole (*strip, Session::Role::strip);
            strip->setName (channel.getName() + " " + juce::String (left) + "-" + juce::String (right));
            strip->setColour (channel.getColour());
            Session::setOutputOf (*strip, channel, pair);

            auto instance = edit.getPluginCache().createNewPlugin (te::RackInstance::create (*rack));

            if (auto ri = dynamic_cast<te::RackInstance*> (instance.get()))
            {
                // Nothing goes into the rack from here; this output comes out.
                ri->leftInputGoesTo = 0;
                ri->rightInputGoesTo = 0;
                ri->leftOutputComesFrom = left;
                ri->rightOutputComesFrom = right;
            }

            strip->pluginList.insertPlugin (instance, 0, nullptr);
            strip->getOutput().setOutputToTrack (&sum);
        }

        return {};
    }

    /** Puts the instrument back on the channel and removes the output strips and the channel's
        rack instance. The (now empty) rack stays until the project is next opened. */
    void turnOff (Session& session, te::AudioTrack& channel)
    {
        auto& edit = session.getEdit();
        auto rackInstance = Session::getMultiOutputRack (channel);

        if (rackInstance == nullptr)
            return;

        te::Plugin::Ptr instrument = Session::getInstrument (channel);

        if (instrument != nullptr)
        {
            instrument->removeFromParent();
            channel.pluginList.insertPlugin (instrument, 0, nullptr);
        }

        for (auto s : session.getOutputStrips (channel))
            edit.deleteTrack (s);

        rackInstance->deleteFromParent();

        session.writeTrackOutputs();
    }

    CommandResult setMultiOutput (Session& session, const juce::var& p)
    {
        auto ref = params::getString (p, "channel");
        auto channel = session.findTrack (ref, Session::Role::channel);

        if (channel == nullptr)
            return CommandResult::failure ("set_multi_output: no channel called '" + ref + "'");

        if (! p["enabled"].isBool())
            return CommandResult::failure ("set_multi_output: enabled is true or false");

        auto enable = static_cast<bool> (p["enabled"]);
        auto isOn = Session::getMultiOutputRack (*channel) != nullptr;

        if (enable && ! isOn)
        {
            auto sum = session.getOwnStrip (*channel);

            if (sum == nullptr)
                return CommandResult::failure ("set_multi_output: '" + channel->getName() + "' has no strip of its own to sum the outputs");

            if (auto error = turnOn (session, *channel, *sum); error.isNotEmpty())
                return CommandResult::failure ("set_multi_output: " + error);
        }
        else if (! enable && isOn)
        {
            turnOff (session, *channel);
        }

        return CommandResult::success (describeOutputs (session, *channel));
    }
}

void registerOutputCommands (CommandRegistry& registry)
{
    registry.add ({ "set_multi_output",
                    "Gives each stereo output of a channel's plugin instrument (e.g. a drum plugin's kick, snare and "
                    "hats outputs) a mixer strip of its own, all going to the channel's strip, which sums them. "
                    "Turning it off puts the instrument back to one stereo output and removes those strips.",
                    schema ({ { "channel", prop ("string", "Channel id or name") },
                              { "enabled", prop ("boolean", "True for a strip per output, false for one") } },
                            { "channel", "enabled" }),
                    true, setMultiOutput });
}

} // namespace ng
