// Headless checks for the command layer and audio rendering. Runs with no audio
// device and no window, so it works in CI: `ctest` or `./NgDawTests`.

#include "core/CommandDispatcher.h"

#include <iostream>

namespace
{
    int failures = 0;

    void check (bool condition, const char* what, int line)
    {
        std::cout << (condition ? "  ok   " : "  FAIL ") << what << "  (line " << line << ")\n";

        if (! condition)
            ++failures;
    }

    #define CHECK(x) check ((x), #x, __LINE__)

    juce::var obj (std::initializer_list<std::pair<const char*, juce::var>> props)
    {
        auto o = new juce::DynamicObject();

        for (auto& [k, v] : props)
            o->setProperty (k, v);

        return juce::var (o);
    }

    struct HeadlessBehaviour  : public tracktion::EngineBehaviour
    {
        bool autoInitialiseDeviceManager() override    { return false; }
    };

    struct Fixture
    {
        tracktion::Engine& engine;
        ng::Session session { engine };
        ng::CommandRegistry registry;
        ng::CommandDispatcher dispatcher { session, registry };

        explicit Fixture (tracktion::Engine& e) : engine (e)    { ng::registerProjectCommands (registry); }

        juce::var state()    { return dispatcher.execute ("get_state").value; }
    };

    //==============================================================================
    void testCreateTrackLinksAStrip (tracktion::Engine& engine)
    {
        std::cout << "create_track makes a linked channel and strip\n";
        Fixture f (engine);

        auto r = f.dispatcher.execute ("create_track", obj ({ { "name", "Bass" }, { "colour", "#3c8ce0" } }));
        CHECK (r.ok);

        auto s = f.state();
        CHECK (s["channels"].size() == 1);
        CHECK (s["strips"].size() == 1);

        auto channel = s["channels"][0];
        auto strip = s["strips"][0];
        CHECK (channel["name"] == "Bass");
        CHECK (strip["name"] == "Bass");
        CHECK (channel["colour"] == strip["colour"]);
        CHECK (channel["output"] == strip["id"]);
        CHECK (static_cast<bool> (channel["linked"]));
        CHECK (strip["output"] == "master");
        CHECK (channel["plugins"].size() == 1 && channel["plugins"][0] == "4osc");
        CHECK (r.value["channel"] == channel["id"]);
    }

    void testRouteDetachesAndShares (tracktion::Engine& engine)
    {
        std::cout << "route detaches a channel and lets strips be shared\n";
        Fixture f (engine);

        f.dispatcher.execute ("create_track", obj ({ { "name", "Kick" } }));
        f.dispatcher.execute ("create_track", obj ({ { "name", "Snare" } }));

        auto r = f.dispatcher.execute ("route", obj ({ { "channel", "Snare" }, { "strip", "Kick" } }));
        CHECK (r.ok);
        CHECK (! static_cast<bool> (r.value["linked"]));

        auto s = f.state();
        auto kickStrip = s["strips"][0];
        CHECK (kickStrip["name"] == "Kick");
        CHECK (kickStrip["inputs"].size() == 2);
        CHECK (s["strips"][1]["inputs"].size() == 0);

        // Routing back to its own strip re-links it.
        r = f.dispatcher.execute ("route", obj ({ { "channel", "Snare" }, { "strip", "Snare" } }));
        CHECK (static_cast<bool> (r.value["linked"]));

        r = f.dispatcher.execute ("route", obj ({ { "channel", "Snare" }, { "strip", "master" } }));
        CHECK (r.ok && r.value["output"] == "master");

        CHECK (! f.dispatcher.execute ("route", obj ({ { "channel", "Nope" }, { "strip", "Kick" } })).ok);
        CHECK (! f.dispatcher.execute ("route", obj ({ { "channel", "Kick" }, { "strip", "Nope" } })).ok);
    }

    void testUndoIsOneStepPerCommand (tracktion::Engine& engine)
    {
        std::cout << "each command and each batch is one undo step\n";
        Fixture f (engine);

        f.dispatcher.execute ("create_track", obj ({ { "name", "Keys" } }));
        f.dispatcher.execute ("create_track", obj ({ { "name", "Pad" } }));
        f.dispatcher.execute ("route", obj ({ { "channel", "Pad" }, { "strip", "Keys" } }));

        CHECK (f.dispatcher.undo());   // the route
        CHECK (static_cast<bool> (f.state()["channels"][1]["linked"]));

        CHECK (f.dispatcher.undo());   // the second create_track, channel and strip together
        CHECK (f.state()["channels"].size() == 1);
        CHECK (f.state()["strips"].size() == 1);

        CHECK (f.dispatcher.redo());
        CHECK (f.state()["channels"].size() == 2);
        CHECK (f.state()["strips"].size() == 2);

        juce::Array<juce::var> batch {
            obj ({ { "command", "create_track" }, { "params", obj ({ { "name", "Lead" } }) } }),
            obj ({ { "command", "add_midi_clip" }, { "params", obj ({ { "channel", "Lead" } }) } }),
            obj ({ { "command", "route" }, { "params", obj ({ { "channel", "Lead" }, { "strip", "Keys" } }) } })
        };

        CHECK (f.dispatcher.executeBatch ("Add lead", batch).ok);
        CHECK (f.state()["channels"].size() == 3);
        CHECK (f.dispatcher.undo());
        CHECK (f.state()["channels"].size() == 2);
    }

    void testFailuresRollBack (tracktion::Engine& engine)
    {
        std::cout << "failed commands and batches leave the project unchanged\n";
        Fixture f (engine);

        f.dispatcher.execute ("create_track", obj ({ { "name", "Drums" } }));

        juce::Array<juce::var> batch {
            obj ({ { "command", "create_track" }, { "params", obj ({ { "name", "Vox" } }) } }),
            obj ({ { "command", "route" }, { "params", obj ({ { "channel", "Vox" }, { "strip", "Missing" } }) } })
        };

        auto r = f.dispatcher.executeBatch ("Broken batch", batch);
        CHECK (! r.ok);
        CHECK (r.error.contains ("Missing"));
        CHECK (f.state()["channels"].size() == 1);
        CHECK (f.state()["strips"].size() == 1);

        CHECK (! f.dispatcher.execute ("create_track", obj ({ { "instrument", "kazoo" } })).ok);
        CHECK (! f.dispatcher.execute ("no_such_command").ok);
        CHECK (f.state()["channels"].size() == 1);

        // The failed attempts don't leave empty undo steps behind.
        CHECK (f.dispatcher.undo());
        CHECK (f.state()["channels"].size() == 0);
    }

    void testJsonBridgeAndToolList (tracktion::Engine& engine)
    {
        std::cout << "JSON requests and the MCP tool list\n";
        Fixture f (engine);

        auto reply = juce::JSON::parse (f.dispatcher.executeJson (R"({"command": "create_track", "params": {"name": "Choir"}})"));
        CHECK (static_cast<bool> (reply["ok"]));
        CHECK (reply["result"]["channel"].toString().isNotEmpty());

        reply = juce::JSON::parse (f.dispatcher.executeJson ("not json"));
        CHECK (! static_cast<bool> (reply["ok"]));

        reply = juce::JSON::parse (f.dispatcher.executeJson (R"({"batch": "Two synths", "commands": [
            {"command": "create_track", "params": {"name": "A"}},
            {"command": "create_track", "params": {"name": "B"}}]})"));
        CHECK (static_cast<bool> (reply["ok"]) && reply["result"].size() == 2);

        auto tools = f.registry.describe();
        juce::StringArray names;

        for (auto& t : *tools.getArray())
            names.add (t["name"].toString());

        CHECK (names.contains ("create_track") && names.contains ("route") && names.contains ("get_state"));
        CHECK (tools[0]["inputSchema"]["type"] == "object");
    }

    void testRenameAndRecolourStayInSyncWhileLinked (tracktion::Engine& engine)
    {
        std::cout << "rename_track and set_track_colour keep linked channels and strips in sync\n";
        Fixture f (engine);

        f.dispatcher.execute ("create_track", obj ({ { "name", "Kick" } }));
        f.dispatcher.execute ("create_track", obj ({ { "name", "Snare" } }));

        CHECK (f.dispatcher.execute ("rename_track", obj ({ { "track", "Kick" }, { "name", "Big kick" } })).ok);
        auto s = f.state();
        CHECK (s["channels"][0]["name"] == "Big kick");
        CHECK (s["strips"][0]["name"] == "Big kick");

        // Renaming from the strip side works too.
        CHECK (f.dispatcher.execute ("rename_track", obj ({ { "track", s["strips"][0]["id"] }, { "name", "Kick" } })).ok);
        CHECK (f.state()["channels"][0]["name"] == "Kick");

        CHECK (f.dispatcher.execute ("set_track_colour", obj ({ { "track", "Kick" }, { "colour", "#112233" } })).ok);
        s = f.state();
        CHECK (s["channels"][0]["colour"] == "#112233");
        CHECK (s["strips"][0]["colour"] == "#112233");

        // Once detached, the channel changes on its own.
        f.dispatcher.execute ("route", obj ({ { "channel", "Snare" }, { "strip", "Kick" } }));
        CHECK (f.dispatcher.execute ("rename_track", obj ({ { "track", "Snare" }, { "name", "Clap" } })).ok);
        CHECK (f.dispatcher.execute ("set_track_colour", obj ({ { "track", "Clap" }, { "colour", "445566" } })).ok);
        s = f.state();
        CHECK (s["channels"][1]["name"] == "Clap");
        CHECK (s["strips"][1]["name"] == "Snare");
        CHECK (s["channels"][1]["colour"] == "#445566");
        CHECK (s["strips"][1]["colour"] != "#445566");

        // One undo step each.
        CHECK (f.dispatcher.undo());
        CHECK (f.state()["channels"][1]["colour"] != "#445566");
        CHECK (f.dispatcher.undo());
        CHECK (f.state()["channels"][1]["name"] == "Snare");

        CHECK (! f.dispatcher.execute ("rename_track", obj ({ { "track", "Kick" }, { "name", "  " } })).ok);
        CHECK (! f.dispatcher.execute ("rename_track", obj ({ { "track", "Nobody" }, { "name", "X" } })).ok);
        CHECK (! f.dispatcher.execute ("set_track_colour", obj ({ { "track", "Kick" }, { "colour", "blue" } })).ok);
        CHECK (! f.dispatcher.execute ("create_track", obj ({ { "colour", "#12" } })).ok);
    }

    void testDeleteTrack (tracktion::Engine& engine)
    {
        std::cout << "delete_track removes channels and strips and fixes up routing\n";
        Fixture f (engine);

        f.dispatcher.execute ("create_track", obj ({ { "name", "Kick" } }));
        f.dispatcher.execute ("create_track", obj ({ { "name", "Snare" } }));
        f.dispatcher.execute ("create_track", obj ({ { "name", "Hat" } }));

        // A channel takes its own strip with it.
        CHECK (f.dispatcher.execute ("delete_track", obj ({ { "track", "Hat" } })).ok);
        auto s = f.state();
        CHECK (s["channels"].size() == 2);
        CHECK (s["strips"].size() == 2);

        // ...but not while another channel still feeds that strip.
        f.dispatcher.execute ("route", obj ({ { "channel", "Snare" }, { "strip", "Kick" } }));
        CHECK (f.dispatcher.execute ("delete_track", obj ({ { "track", "Kick" } })).ok);
        s = f.state();
        CHECK (s["channels"].size() == 1);
        CHECK (s["strips"].size() == 2);
        CHECK (s["channels"][0]["output"] == s["strips"][0]["id"]);

        // Deleting a strip sends what fed it to master.
        auto r = f.dispatcher.execute ("delete_track", obj ({ { "track", s["strips"][0]["id"] } }));
        CHECK (r.ok && r.value["rerouted_to_master"].size() == 1);
        s = f.state();
        CHECK (s["strips"].size() == 1);
        CHECK (s["channels"][0]["output"] == "master");

        // Undo brings back the strip and its routing.
        CHECK (f.dispatcher.undo());
        s = f.state();
        CHECK (s["strips"].size() == 2);
        CHECK (s["channels"][0]["output"] == s["strips"][0]["id"]);

        // Track numbers shift as tracks go and come back; routing still follows the right strips.
        CHECK (f.dispatcher.undo());   // the Kick channel comes back, feeding its own strip
        s = f.state();
        CHECK (s["channels"].size() == 2);
        CHECK (s["channels"][0]["output"] == s["strips"][0]["id"]);
        CHECK (s["channels"][1]["output"] == s["strips"][0]["id"]);

        CHECK (f.dispatcher.redo());
        CHECK (f.dispatcher.redo());
        s = f.state();
        CHECK (s["channels"].size() == 1 && s["strips"].size() == 1);
        CHECK (s["channels"][0]["output"] == "master");
        CHECK (s["strips"][0]["name"] == "Snare");

        CHECK (! f.dispatcher.execute ("delete_track", obj ({ { "track", "Nobody" } })).ok);
    }

    void testSetInstrument (tracktion::Engine& engine)
    {
        std::cout << "set_instrument swaps a channel's instrument\n";
        Fixture f (engine);

        f.dispatcher.execute ("create_track", obj ({ { "name", "Lead" } }));
        CHECK (f.state()["channels"][0]["instrument"] == "4osc");

        CHECK (f.dispatcher.execute ("set_instrument", obj ({ { "channel", "Lead" }, { "instrument", "none" } })).ok);
        CHECK (f.state()["channels"][0]["instrument"] == "none");
        CHECK (f.state()["channels"][0]["plugins"].size() == 0);

        CHECK (f.dispatcher.execute ("set_instrument", obj ({ { "channel", "Lead" }, { "instrument", "4osc" } })).ok);
        CHECK (f.dispatcher.execute ("set_instrument", obj ({ { "channel", "Lead" }, { "instrument", "4osc" } })).ok);
        CHECK (f.state()["channels"][0]["plugins"].size() == 1);

        CHECK (f.dispatcher.undo());
        CHECK (f.dispatcher.undo());
        CHECK (f.state()["channels"][0]["instrument"] == "none");

        CHECK (! f.dispatcher.execute ("set_instrument", obj ({ { "channel", "Lead" }, { "instrument", "kazoo" } })).ok);
    }

    void testRendersAudio (tracktion::Engine& engine)
    {
        std::cout << "a synth channel renders sound through its strip\n";
        Fixture f (engine);

        f.dispatcher.execute ("create_track", obj ({ { "name", "Synth" } }));
        auto r = f.dispatcher.execute ("add_midi_clip", obj ({ { "channel", "Synth" }, { "length_beats", 4 },
                                                               { "notes", juce::Array<juce::var> {
                                                                   obj ({ { "pitch", 60 }, { "start", 0 }, { "length", 2 } }),
                                                                   obj ({ { "pitch", 67 }, { "start", 2 }, { "length", 2 } }) } } }));
        CHECK (r.ok && static_cast<int> (r.value["notes"]) == 2);

        juce::TemporaryFile out (".wav");
        CHECK (tracktion::Renderer::renderToFile (f.session.getEdit(), out.getFile(), false));

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (out.getFile()));
        CHECK (reader != nullptr);

        if (reader != nullptr)
        {
            juce::AudioBuffer<float> buffer ((int) reader->numChannels, (int) reader->lengthInSamples);
            reader->read (&buffer, 0, buffer.getNumSamples(), 0, true, true);

            auto rms = buffer.getRMSLevel (0, 0, buffer.getNumSamples());
            std::cout << "       rendered " << reader->lengthInSamples << " samples, RMS " << rms << "\n";
            CHECK (reader->lengthInSamples > 0);
            CHECK (rms > 0.001f);
        }

        // Pulling the strip's fader down silences the channel, so the audio goes through it.
        f.session.getStrips()[0]->getVolumePlugin()->setVolumeDb (-100.0f);
        juce::TemporaryFile muted (".wav");
        tracktion::Renderer::renderToFile (f.session.getEdit(), muted.getFile(), false);
        std::unique_ptr<juce::AudioFormatReader> mutedReader (formats.createReaderFor (muted.getFile()));
        auto mutedRms = 0.0f;

        if (mutedReader != nullptr)
        {
            juce::AudioBuffer<float> buffer ((int) mutedReader->numChannels, (int) mutedReader->lengthInSamples);
            mutedReader->read (&buffer, 0, buffer.getNumSamples(), 0, true, true);
            mutedRms = buffer.getRMSLevel (0, 0, buffer.getNumSamples());
        }

        std::cout << "       with the strip fader down, RMS " << mutedRms << "\n";
        CHECK (mutedRms < 0.0001f);
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    {
        tracktion::Engine engine ("NgDawTests", nullptr, std::make_unique<HeadlessBehaviour>());

        testCreateTrackLinksAStrip (engine);
        testRouteDetachesAndShares (engine);
        testUndoIsOneStepPerCommand (engine);
        testFailuresRollBack (engine);
        testJsonBridgeAndToolList (engine);
        testRenameAndRecolourStayInSyncWhileLinked (engine);
        testDeleteTrack (engine);
        testSetInstrument (engine);
        testRendersAudio (engine);
    }

    std::cout << (failures == 0 ? "\nAll checks passed\n" : "\nSome checks failed\n");
    return failures == 0 ? 0 : 1;
}
