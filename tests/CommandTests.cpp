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

    /** Renders the whole edit offline, as it would play (mute and solo apply), and returns
        the RMS of the left channel. Renderer::renderToFile (Edit&, ...) isn't used
        here because it unmutes and solo-isolates every track it renders. */
    float renderRms (Fixture& f)
    {
        auto& edit = f.session.getEdit();
        juce::TemporaryFile out (".wav");

        tracktion::Renderer::Parameters r (edit);
        r.destFile = out.getFile();
        r.audioFormat = edit.engine.getAudioFileFormatManager().getWavFormat();
        r.bitDepth = 24;
        r.sampleRateForAudio = 44100.0;
        r.blockSizeForAudio = 512;
        r.time = { tracktion::TimePosition(), edit.getLength() };
        r.usePlugins = true;
        r.useMasterPlugins = true;
        r.tracksToDo = tracktion::toBitSet (tracktion::getAllTracks (edit));

        tracktion::TransportControl::stopAllTransports (edit.engine, false, true);

        if (auto task = tracktion::render_utils::createRenderTask (r, "test render", nullptr, nullptr))
            while (task->runJob() == juce::ThreadPoolJob::jobNeedsRunningAgain)
            {}

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (out.getFile()));

        if (reader == nullptr || reader->lengthInSamples == 0)
            return -1.0f;

        juce::AudioBuffer<float> buffer ((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read (&buffer, 0, buffer.getNumSamples(), 0, true, true);
        return buffer.getRMSLevel (0, 0, buffer.getNumSamples());
    }

    juce::var note (int pitch, double start, double length)
    {
        return obj ({ { "pitch", pitch }, { "start", start }, { "length", length } });
    }

    bool near (const juce::var& v, double expected)
    {
        return std::abs (static_cast<double> (v) - expected) < 0.01;
    }

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

    void testMixerStripSettings (tracktion::Engine& engine)
    {
        std::cout << "set_mixer_strip sets fader, pan, mute and solo\n";
        Fixture f (engine);

        f.dispatcher.execute ("create_track", obj ({ { "name", "Bass" } }));

        auto r = f.dispatcher.execute ("set_mixer_strip", obj ({ { "strip", "Bass" }, { "volume_db", -6.0 }, { "pan", -0.5 } }));
        CHECK (r.ok);
        auto strip = f.state()["strips"][0];
        CHECK (near (strip["volume_db"], -6.0));
        CHECK (near (strip["pan"], -0.5));
        CHECK (! static_cast<bool> (strip["mute"]));

        // Only the values given change.
        CHECK (f.dispatcher.execute ("set_mixer_strip", obj ({ { "strip", "Bass" }, { "mute", true } })).ok);
        strip = f.state()["strips"][0];
        CHECK (static_cast<bool> (strip["mute"]));
        CHECK (near (strip["volume_db"], -6.0));

        // Like most DAWs, mute and solo aren't undo steps; the fader and pan are.
        CHECK (f.dispatcher.undo());
        strip = f.state()["strips"][0];
        CHECK (near (strip["volume_db"], 0.0));
        CHECK (near (strip["pan"], 0.0));
        CHECK (static_cast<bool> (strip["mute"]));
        CHECK (f.dispatcher.execute ("set_mixer_strip", obj ({ { "strip", "Bass" }, { "mute", false } })).ok);

        // The master strip has a fader and pan.
        CHECK (f.dispatcher.execute ("set_mixer_strip", obj ({ { "strip", "master" }, { "volume_db", -3.0 } })).ok);
        CHECK (near (f.state()["master"]["volume_db"], -3.0));
        CHECK (! f.dispatcher.execute ("set_mixer_strip", obj ({ { "strip", "master" }, { "mute", true } })).ok);

        // Bad values are rejected without changing anything.
        CHECK (! f.dispatcher.execute ("set_mixer_strip", obj ({ { "strip", "Bass" }, { "volume_db", -6.0 }, { "pan", 3 } })).ok);
        CHECK (! f.dispatcher.execute ("set_mixer_strip", obj ({ { "strip", "Bass" }, { "volume_db", 20 } })).ok);
        CHECK (! f.dispatcher.execute ("set_mixer_strip", obj ({ { "strip", "Bass" }, { "volume_db", "loud" } })).ok);
        CHECK (! f.dispatcher.execute ("set_mixer_strip", obj ({ { "strip", "Nobody" }, { "mute", true } })).ok);
        CHECK (near (f.state()["strips"][0]["volume_db"], 0.0));
    }

    void testGestureIsOneUndoStep (tracktion::Engine& engine)
    {
        std::cout << "a fader drag (gesture) is one undo step\n";
        Fixture f (engine);

        f.dispatcher.execute ("create_track", obj ({ { "name", "Pad" } }));

        f.dispatcher.beginGesture ("Fader");

        for (auto db : { -1.0, -2.0, -4.0, -8.0 })
            f.dispatcher.execute ("set_mixer_strip", obj ({ { "strip", "Pad" }, { "volume_db", db } }));

        f.dispatcher.endGesture();
        CHECK (near (f.state()["strips"][0]["volume_db"], -8.0));

        CHECK (f.dispatcher.undo());
        CHECK (near (f.state()["strips"][0]["volume_db"], 0.0));
        CHECK (f.state()["channels"].size() == 1);   // the create_track is still there

        CHECK (f.dispatcher.canRedo());
        CHECK (f.dispatcher.redo());
        CHECK (near (f.state()["strips"][0]["volume_db"], -8.0));
    }

    void testMuteAndSoloSilence (tracktion::Engine& engine)
    {
        std::cout << "mute and solo change what renders\n";
        Fixture f (engine);

        f.dispatcher.execute ("create_track", obj ({ { "name", "Lead" } }));
        f.dispatcher.execute ("create_track", obj ({ { "name", "Other" } }));
        f.dispatcher.execute ("add_midi_clip", obj ({ { "channel", "Lead" }, { "length_beats", 4 },
                                                      { "notes", juce::Array<juce::var> { note (60, 0, 4) } } }));

        auto open = renderRms (f);
        CHECK (open > 0.001f);

        f.dispatcher.execute ("set_mixer_strip", obj ({ { "strip", "Lead" }, { "mute", true } }));
        auto muted = renderRms (f);
        std::cout << "       open RMS " << open << ", Lead muted RMS " << muted << "\n";
        CHECK (muted < 0.0001f);
        f.dispatcher.execute ("set_mixer_strip", obj ({ { "strip", "Lead" }, { "mute", false } }));

        // Soloing another strip silences Lead; soloing Lead keeps it playing.
        f.dispatcher.execute ("set_mixer_strip", obj ({ { "strip", "Other" }, { "solo", true } }));
        auto otherSolo = renderRms (f);
        f.dispatcher.execute ("set_mixer_strip", obj ({ { "strip", "Other" }, { "solo", false } }));
        f.dispatcher.execute ("set_mixer_strip", obj ({ { "strip", "Lead" }, { "solo", true } }));
        auto leadSolo = renderRms (f);
        std::cout << "       Other soloed RMS " << otherSolo << ", Lead soloed RMS " << leadSolo << "\n";
        CHECK (otherSolo < 0.0001f);
        CHECK (leadSolo > 0.001f);

        // Master fader down silences everything.
        f.dispatcher.execute ("set_mixer_strip", obj ({ { "strip", "master" }, { "volume_db", -100 } }));
        CHECK (renderRms (f) < 0.0001f);

        // Undo brings the sound back, so the live fader follows undo, not just the saved value.
        CHECK (f.dispatcher.undo());
        CHECK (renderRms (f) > 0.001f);
    }

    double loopSeconds (Fixture& f, bool end)
    {
        auto range = f.session.getEdit().getTransport().getLoopRange();
        return (end ? range.getEnd() : range.getStart()).inSeconds();
    }

    void testTempoAndTimeSignature (tracktion::Engine& engine)
    {
        std::cout << "set_tempo and set_time_signature\n";
        Fixture f (engine);

        f.dispatcher.execute ("create_track", obj ({ { "name", "Keys" } }));
        f.dispatcher.execute ("add_midi_clip", obj ({ { "channel", "Keys" }, { "length_beats", 4 } }));

        auto s = f.state();
        CHECK (near (s["tempo_bpm"], 120.0));
        CHECK (static_cast<int> (s["time_signature"]["numerator"]) == 4);
        CHECK (near (f.session.getEdit().getLength().inSeconds(), 2.0));

        // At half the tempo the 4-beat clip lasts twice as long.
        CHECK (f.dispatcher.execute ("set_tempo", obj ({ { "bpm", 60 } })).ok);
        CHECK (near (f.state()["tempo_bpm"], 60.0));
        CHECK (near (f.session.getEdit().getLength().inSeconds(), 4.0));

        CHECK (f.dispatcher.undo());
        CHECK (near (f.state()["tempo_bpm"], 120.0));
        CHECK (near (f.session.getEdit().getLength().inSeconds(), 2.0));

        CHECK (f.dispatcher.execute ("set_time_signature", obj ({ { "numerator", 3 }, { "denominator", 4 } })).ok);
        s = f.state();
        CHECK (static_cast<int> (s["time_signature"]["numerator"]) == 3);
        CHECK (static_cast<int> (s["time_signature"]["denominator"]) == 4);

        // The whole-project loop rounds the 4-beat clip up to two bars of 3/4.
        CHECK (near (s["transport"]["loop_end_beat"], 6.0));
        CHECK (f.dispatcher.undo());
        CHECK (near (f.state()["transport"]["loop_end_beat"], 4.0));

        CHECK (! f.dispatcher.execute ("set_tempo", obj ({ { "bpm", 0 } })).ok);
        CHECK (! f.dispatcher.execute ("set_tempo", obj ({ { "bpm", 500 } })).ok);
        CHECK (! f.dispatcher.execute ("set_tempo", obj ({ { "bpm", "fast" } })).ok);
        CHECK (! f.dispatcher.execute ("set_time_signature", obj ({ { "numerator", 0 }, { "denominator", 4 } })).ok);
        CHECK (! f.dispatcher.execute ("set_time_signature", obj ({ { "numerator", 4 }, { "denominator", 5 } })).ok);
        CHECK (! f.dispatcher.execute ("set_time_signature", obj ({ { "numerator", 4.5 }, { "denominator", 4 } })).ok);
        CHECK (near (f.state()["tempo_bpm"], 120.0));
    }

    void testLoopRegion (tracktion::Engine& engine)
    {
        std::cout << "set_loop sets the loop region in beats\n";
        Fixture f (engine);

        f.dispatcher.execute ("create_track", obj ({ { "name", "Keys" } }));
        f.dispatcher.execute ("add_midi_clip", obj ({ { "channel", "Keys" }, { "length_beats", 4 } }));

        // New projects loop the whole project.
        auto t = f.state()["transport"];
        CHECK (static_cast<bool> (t["looping"]));
        CHECK (static_cast<bool> (t["loop_is_whole_project"]));
        CHECK (near (loopSeconds (f, true), 2.0));

        // The whole-project loop grows with the project.
        f.dispatcher.execute ("add_midi_clip", obj ({ { "channel", "Keys" }, { "start_beat", 4 }, { "length_beats", 4 } }));
        CHECK (near (f.state()["transport"]["loop_end_beat"], 8.0));

        CHECK (f.dispatcher.execute ("set_loop", obj ({ { "start_beat", 2 }, { "end_beat", 6 } })).ok);
        CHECK (near (loopSeconds (f, false), 1.0));
        CHECK (near (loopSeconds (f, true), 3.0));

        // The loop stays on the same beats when the tempo changes.
        f.dispatcher.execute ("set_tempo", obj ({ { "bpm", 60 } }));
        CHECK (near (loopSeconds (f, false), 2.0));
        CHECK (near (loopSeconds (f, true), 6.0));

        // set_loop isn't an undo step: undo reverts the tempo, and the loop follows it.
        CHECK (f.dispatcher.undo());
        CHECK (near (f.state()["tempo_bpm"], 120.0));
        CHECK (near (loopSeconds (f, true), 3.0));
        CHECK (! static_cast<bool> (f.state()["transport"]["loop_is_whole_project"]));

        CHECK (f.dispatcher.execute ("set_loop", obj ({ { "enabled", false } })).ok);
        CHECK (! static_cast<bool> (f.state()["transport"]["looping"]));
        CHECK (f.dispatcher.execute ("set_loop", obj ({ { "enabled", true }, { "whole_project", true } })).ok);
        t = f.state()["transport"];
        CHECK (static_cast<bool> (t["looping"]) && static_cast<bool> (t["loop_is_whole_project"]));
        CHECK (near (t["loop_end_beat"], 8.0));

        CHECK (! f.dispatcher.execute ("set_loop", obj ({ { "start_beat", 2 } })).ok);
        CHECK (! f.dispatcher.execute ("set_loop", obj ({ { "start_beat", 4 }, { "end_beat", 2 } })).ok);
        CHECK (! f.dispatcher.execute ("set_loop", obj ({ { "start_beat", -1 }, { "end_beat", 2 } })).ok);
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
        testMixerStripSettings (engine);
        testGestureIsOneUndoStep (engine);
        testMuteAndSoloSilence (engine);
        testTempoAndTimeSignature (engine);
        testLoopRegion (engine);
        testRendersAudio (engine);
    }

    std::cout << (failures == 0 ? "\nAll checks passed\n" : "\nSome checks failed\n");
    return failures == 0 ? 0 : 1;
}
