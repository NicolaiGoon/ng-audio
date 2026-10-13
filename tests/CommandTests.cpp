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

        if (edit.getLength() <= tracktion::TimeDuration())
            return 0.0f;   // nothing to render

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

    void testUndoAfterTracktionTidiesUp (tracktion::Engine& engine)
    {
        std::cout << "undo still undoes the last command after Tracktion's delayed tidy-up\n";
        Fixture f (engine);

        // Tracktion re-sorts a track's clips a moment after they change, with the message
        // loop running (as in the app). That must not become an undo step of its own.
        auto settle = [] { juce::MessageManager::getInstance()->runDispatchLoopUntil (200); };

        f.dispatcher.execute ("create_track", obj ({ { "name", "Synth" } }));
        settle();
        f.dispatcher.execute ("set_steps", obj ({ { "channel", "Synth" }, { "step", 2 } }));
        settle();

        auto history = f.session.getUndoManager().getUndoDescriptions();
        std::cout << "       undo history: " << history.joinIntoString (", ") << "\n";
        CHECK (history == juce::StringArray ({ "set_steps", "create_track" }));

        CHECK (f.dispatcher.undo());
        CHECK (f.state()["channels"][0]["pattern"]["steps"] == juce::String::repeatedString (".", 16));
        settle();
        CHECK (f.dispatcher.canRedo());

        // Moving one clip past another reorders the track's clips, which Tracktion also tidies later.
        auto first = f.dispatcher.execute ("add_midi_clip", obj ({ { "channel", "Synth" }, { "start_beat", 0 } })).value["clip"];
        f.dispatcher.execute ("add_midi_clip", obj ({ { "channel", "Synth" }, { "start_beat", 8 } }));
        settle();
        f.dispatcher.execute ("move_clip", obj ({ { "clip", first }, { "start_beat", 16 } }));
        settle();
        CHECK (f.session.getUndoManager().getUndoDescriptions()[0] == "move_clip");
        CHECK (f.dispatcher.undo());
        CHECK (near (f.state()["channels"][0]["clips"][0]["start_beat"], 0.0));
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

        // The metronome toggles, and like the loop it isn't an undo step.
        CHECK (! static_cast<bool> (f.state()["transport"]["metronome"]));
        CHECK (f.dispatcher.execute ("set_metronome", obj ({ { "enabled", true } })).ok);
        CHECK (static_cast<bool> (f.state()["transport"]["metronome"]));
        CHECK (! f.session.getEdit().clickTrackRecordingOnly.get());
        CHECK (f.dispatcher.execute ("set_metronome").ok);
        CHECK (! static_cast<bool> (f.state()["transport"]["metronome"]));

        // The position reads as bar.beat.sixteenth.
        CHECK (f.state()["transport"]["position"].toString() == "1.1.1");
        f.dispatcher.execute ("set_tempo", obj ({ { "bpm", 120 } }));
        f.dispatcher.execute ("set_position", obj ({ { "beat", 5.75 } }));
        CHECK (f.state()["transport"]["position"].toString() == "2.2.4");
        f.dispatcher.execute ("set_position", obj ({ { "beat", 0 } }));
        CHECK (! f.dispatcher.execute ("set_loop", obj ({ { "start_beat", 4 }, { "end_beat", 2 } })).ok);
        CHECK (! f.dispatcher.execute ("set_loop", obj ({ { "start_beat", -1 }, { "end_beat", 2 } })).ok);
    }

    int patternNotes (Fixture& f, const juce::String& channel)
    {
        auto t = f.session.findTrack (channel, ng::Session::Role::channel);
        int notes = 0;

        for (auto c : t->getClips())
            if (auto mc = dynamic_cast<tracktion::MidiClip*> (c))
                notes += mc->getSequence().getNotes().size();

        return notes;
    }

    void testStepSequencer (tracktion::Engine& engine)
    {
        std::cout << "set_steps builds a channel's step pattern\n";
        Fixture f (engine);

        f.dispatcher.execute ("create_track", obj ({ { "name", "Kick" } }));
        auto s = f.state();
        CHECK (s["channels"][0]["pattern"]["steps"] == "................");
        CHECK (s["channels"][0]["clips"].size() == 0);

        CHECK (f.dispatcher.execute ("set_steps", obj ({ { "channel", "Kick" }, { "steps", "x...x...x...x..." } })).ok);
        s = f.state();
        CHECK (s["channels"][0]["pattern"]["steps"] == "x...x...x...x...");
        CHECK (s["channels"][0]["clips"].size() == 1);
        CHECK (patternNotes (f, "Kick") == 4);
        CHECK (near (s["transport"]["loop_end_beat"], 4.0));   // one bar of 16ths

        // One step at a time, as the rack's buttons do; each change rebuilds the one clip.
        CHECK (f.dispatcher.execute ("set_steps", obj ({ { "channel", "Kick" }, { "step", 2 }, { "on", true } })).ok);
        CHECK (f.dispatcher.execute ("set_steps", obj ({ { "channel", "Kick" }, { "step", 4 } })).ok);   // toggles off
        s = f.state();
        CHECK (s["channels"][0]["pattern"]["steps"] == "x.x.....x...x...");
        CHECK (s["channels"][0]["clips"].size() == 1);
        CHECK (patternNotes (f, "Kick") == 4);

        // Each change is one undo step.
        CHECK (f.dispatcher.undo());
        CHECK (f.state()["channels"][0]["pattern"]["steps"] == "x.x.x...x...x...");
        CHECK (patternNotes (f, "Kick") == 5);

        // Arrays, length and pitch.
        CHECK (f.dispatcher.execute ("set_steps", obj ({ { "channel", "Kick" }, { "pitch", 36 },
                                                          { "steps", juce::Array<juce::var> { true, false, true, false } } })).ok);
        s = f.state();
        CHECK (s["channels"][0]["pattern"]["steps"] == "x.x.");
        CHECK (static_cast<int> (s["channels"][0]["pattern"]["pitch"]) == 36);
        CHECK (f.dispatcher.execute ("set_steps", obj ({ { "channel", "Kick" }, { "length", 8 } })).ok);
        CHECK (f.state()["channels"][0]["pattern"]["steps"] == "x.x.....");

        // All steps off removes the clip.
        CHECK (f.dispatcher.execute ("set_steps", obj ({ { "channel", "Kick" }, { "steps", "........" } })).ok);
        CHECK (f.state()["channels"][0]["clips"].size() == 0);

        CHECK (! f.dispatcher.execute ("set_steps", obj ({ { "channel", "Nobody" }, { "steps", "x..." } })).ok);
        CHECK (! f.dispatcher.execute ("set_steps", obj ({ { "channel", "Kick" }, { "steps", "x?x?" } })).ok);
        CHECK (! f.dispatcher.execute ("set_steps", obj ({ { "channel", "Kick" }, { "step", 99 } })).ok);
        CHECK (! f.dispatcher.execute ("set_steps", obj ({ { "channel", "Kick" }, { "length", 0 } })).ok);
        CHECK (! f.dispatcher.execute ("set_steps", obj ({ { "channel", "Kick" }, { "pitch", 200 } })).ok);
        CHECK (! f.dispatcher.execute ("set_steps", obj ({ { "channel", "Kick" }, { "steps", juce::String::repeatedString ("x", 65) } })).ok);
        CHECK (f.state()["channels"][0]["pattern"]["steps"] == "........");
    }

    void testPatternRenders (tracktion::Engine& engine)
    {
        std::cout << "a step pattern makes sound\n";
        Fixture f (engine);

        f.dispatcher.execute ("create_track", obj ({ { "name", "Synth" } }));
        CHECK (renderRms (f) < 0.0001f);

        f.dispatcher.execute ("set_steps", obj ({ { "channel", "Synth" }, { "steps", "x...x...x...x..." } }));
        auto rms = renderRms (f);
        std::cout << "       pattern RMS " << rms << "\n";
        CHECK (rms > 0.001f);
    }

    /** get_state without the parts that are about the open file rather than the music. */
    juce::String musicState (Fixture& f)
    {
        auto state = f.state();

        if (auto o = state.getDynamicObject())
            o->removeProperty ("project");

        return juce::JSON::toString (state, true);
    }

    void testSaveAndOpen (tracktion::Engine& engine)
    {
        std::cout << "save, open and unsaved changes\n";
        juce::TemporaryFile temp (".ngdaw");
        auto path = juce::var (temp.getFile().getFullPathName());
        juce::String before;
        float rmsBefore = 0.0f;

        {
            Fixture f (engine);
            CHECK (! f.session.hasUnsavedChanges());
            CHECK (f.state()["project"]["name"] == "Untitled");

            // A small drum-and-bass style project touching everything Phase 1 can set.
            f.dispatcher.execute ("create_track", obj ({ { "name", "Kick" } }));
            f.dispatcher.execute ("create_track", obj ({ { "name", "Bass" }, { "colour", "#40c070" } }));
            f.dispatcher.execute ("create_track", obj ({ { "name", "Hat" } }));
            CHECK (f.session.hasUnsavedChanges());
            CHECK (f.state()["project"]["unsaved_changes"] == juce::var (true));

            f.dispatcher.execute ("set_steps", obj ({ { "channel", "Kick" }, { "steps", "x...x...x...x..." } }));
            f.dispatcher.execute ("set_steps", obj ({ { "channel", "Bass" }, { "steps", "x.x...x.x.x...x." }, { "pitch", 36 } }));
            f.dispatcher.execute ("set_steps", obj ({ { "channel", "Hat" }, { "steps", "..x...x...x...x." }, { "pitch", 84 } }));
            f.dispatcher.execute ("route", obj ({ { "channel", "Hat" }, { "strip", "Kick" } }));
            f.dispatcher.execute ("rename_track", obj ({ { "track", "Bass" }, { "name", "Sub" } }));
            f.dispatcher.execute ("set_mixer_strip", obj ({ { "strip", "Sub" }, { "volume_db", -6.0 }, { "pan", -0.25 } }));
            f.dispatcher.execute ("set_mixer_strip", obj ({ { "strip", "master" }, { "volume_db", -1.5 } }));
            f.dispatcher.execute ("set_mixer_strip", obj ({ { "strip", "Hat" }, { "mute", true } }));
            f.dispatcher.execute ("set_tempo", obj ({ { "bpm", 172 } }));
            f.dispatcher.execute ("set_time_signature", obj ({ { "numerator", 7 }, { "denominator", 8 } }));
            f.dispatcher.execute ("set_loop", obj ({ { "start_beat", 0 }, { "end_beat", 3.5 } }));

            CHECK (! f.dispatcher.execute ("save_project").ok);   // no file yet

            auto saved = f.dispatcher.execute ("save_project", obj ({ { "path", path } }));
            CHECK (saved.ok);
            CHECK (saved.value["path"] == path);
            CHECK (! f.session.hasUnsavedChanges());
            CHECK (f.state()["project"]["name"] == temp.getFile().getFileNameWithoutExtension());

            // Undo marks it changed again; Save (no path) goes back to the same file.
            f.dispatcher.undo();
            CHECK (f.session.hasUnsavedChanges());
            f.dispatcher.redo();
            CHECK (f.dispatcher.execute ("save_project").ok);
            CHECK (! f.session.hasUnsavedChanges());

            before = musicState (f);
            rmsBefore = renderRms (f);

            // new_project won't throw away unsaved work unless asked to.
            f.dispatcher.execute ("set_tempo", obj ({ { "bpm", 140 } }));
            CHECK (! f.dispatcher.execute ("new_project").ok);
            CHECK (f.session.getChannels().size() == 3);
            CHECK (f.dispatcher.execute ("new_project", obj ({ { "discard_changes", true } })).ok);
            CHECK (f.session.getChannels().isEmpty());
            CHECK (! f.dispatcher.canUndo());
            CHECK (! f.session.hasUnsavedChanges());
            CHECK (f.state()["project"]["path"] == "");
        }

        // Reopening in a fresh session gives back the same project, with nothing to undo.
        {
            Fixture f (engine);
            auto opened = f.dispatcher.execute ("open_project", obj ({ { "path", path } }));
            CHECK (opened.ok);
            CHECK (! f.session.hasUnsavedChanges());
            CHECK (! f.dispatcher.canUndo());

            auto after = musicState (f);
            CHECK (after == before);

            if (after != before)
                std::cout << "       before:\n" << before << "\n       after:\n" << after << "\n";

            auto rmsAfter = renderRms (f);
            std::cout << "       RMS before saving " << rmsBefore << ", after opening " << rmsAfter << "\n";
            CHECK (rmsBefore > 0.001f);
            CHECK (std::abs (rmsAfter - rmsBefore) < rmsBefore * 0.02f);   // 4OSC phases vary a little per render

            // The reopened project still edits and undoes normally.
            CHECK (f.dispatcher.execute ("set_steps", obj ({ { "channel", "Kick" }, { "step", 2 } })).ok);
            CHECK (f.session.hasUnsavedChanges());
            CHECK (f.dispatcher.undo());
            CHECK (f.state()["channels"][0]["pattern"]["steps"] == "x...x...x...x...");
        }

        // Files that aren't projects are refused and the open project is kept.
        {
            Fixture f (engine);
            f.dispatcher.execute ("create_track", obj ({ { "name", "Keep me" } }));
            f.dispatcher.execute ("save_project", obj ({ { "path", path } }));

            juce::TemporaryFile notAProject (".ngdaw");
            notAProject.getFile().replaceWithText ("hello");

            CHECK (! f.dispatcher.execute ("open_project", obj ({ { "path", notAProject.getFile().getFullPathName() } })).ok);
            CHECK (! f.dispatcher.execute ("open_project", obj ({ { "path", path.toString() + ".missing" } })).ok);
            CHECK (! f.dispatcher.execute ("open_project").ok);
            CHECK (f.session.getChannels().size() == 1);
            CHECK (f.session.getFile().getFullPathName() == path.toString());
        }
    }

    void testClipsAndPlayheadInState (tracktion::Engine& engine)
    {
        std::cout << "get_state lists clips in beats and set_position moves the playhead\n";
        Fixture f (engine);

        f.dispatcher.execute ("create_track", obj ({ { "name", "Lead" } }));
        f.dispatcher.execute ("set_steps", obj ({ { "channel", "Lead" }, { "steps", "x...x..." } }));
        f.dispatcher.execute ("add_midi_clip", obj ({ { "channel", "Lead" }, { "start_beat", 8 }, { "length_beats", 6 },
                                                       { "notes", juce::Array<juce::var> { note (60, 0, 1), note (64, 1, 1) } } }));
        f.dispatcher.execute ("set_tempo", obj ({ { "bpm", 90 } }));   // clips stay put in beats

        auto clips = f.state()["channels"][0]["clips"];
        CHECK (clips.size() == 2);
        CHECK (clips[0]["pattern"] == juce::var (true));
        CHECK (near (clips[0]["start_beat"], 0.0));
        CHECK (near (clips[0]["length_beats"], 2.0));
        CHECK (static_cast<int> (clips[0]["notes"]) == 2);
        CHECK (clips[1]["pattern"] == juce::var (false));
        CHECK (near (clips[1]["start_beat"], 8.0));
        CHECK (near (clips[1]["length_beats"], 6.0));
        CHECK (static_cast<int> (clips[1]["notes"]) == 2);
        CHECK (clips[1]["id"].toString().isNotEmpty());

        CHECK (f.dispatcher.execute ("set_position", obj ({ { "beat", 6 } })).ok);
        auto transport = f.state()["transport"];
        CHECK (near (transport["position_beats"], 6.0));
        CHECK (near (transport["position_seconds"], 4.0));   // 6 beats at 90 BPM
        CHECK (! f.dispatcher.canRedo());
        CHECK (! f.dispatcher.execute ("set_position", obj ({ { "beat", -1 } })).ok);
        CHECK (! f.dispatcher.execute ("set_position").ok);
    }

    /** The clips on a channel, from get_state. */
    juce::var clipsOf (Fixture& f, int channelIndex)
    {
        return f.state()["channels"][channelIndex]["clips"];
    }

    void testClipEditing (tracktion::Engine& engine)
    {
        std::cout << "clips move, resize, split, duplicate and delete\n";
        Fixture f (engine);

        f.dispatcher.execute ("create_track", obj ({ { "name", "Lead" } }));
        f.dispatcher.execute ("create_track", obj ({ { "name", "Bass" } }));
        auto added = f.dispatcher.execute ("add_midi_clip", obj ({ { "channel", "Lead" }, { "start_beat", 4 }, { "length_beats", 4 },
                                                                     { "notes", juce::Array<juce::var> { note (60, 0, 1), note (62, 2, 1) } } }));
        auto id = added.value["clip"].toString();

        // Move in time, then to another channel; the notes go with it.
        auto moved = f.dispatcher.execute ("move_clip", obj ({ { "clip", id }, { "start_beat", 8 } }));
        CHECK (moved.ok);
        CHECK (near (clipsOf (f, 0)[0]["start_beat"], 8.0));
        CHECK (near (clipsOf (f, 0)[0]["length_beats"], 4.0));
        CHECK (f.dispatcher.execute ("move_clip", obj ({ { "clip", id }, { "channel", "Bass" } })).ok);
        CHECK (clipsOf (f, 0).size() == 0);
        CHECK (clipsOf (f, 1).size() == 1);
        CHECK (near (clipsOf (f, 1)[0]["start_beat"], 8.0));
        CHECK (static_cast<int> (clipsOf (f, 1)[0]["notes"]) == 2);
        CHECK (f.dispatcher.undo());
        CHECK (clipsOf (f, 0).size() == 1);
        CHECK (f.dispatcher.redo());

        // Trim the end, then the start: the edges move but the notes stay put in the song.
        CHECK (f.dispatcher.execute ("resize_clip", obj ({ { "clip", id }, { "end_beat", 11 } })).ok);
        CHECK (f.dispatcher.execute ("resize_clip", obj ({ { "clip", id }, { "start_beat", 10 } })).ok);
        auto clip = clipsOf (f, 1)[0];
        CHECK (near (clip["start_beat"], 10.0));
        CHECK (near (clip["length_beats"], 1.0));

        if (auto midi = dynamic_cast<tracktion::MidiClip*> (f.session.findTrack ("Bass", ng::Session::Role::channel)->getClips()[0]))
            CHECK (near (midi->getOffsetInBeats().inBeats(), 2.0));   // the note at beat 10 is still at beat 10

        // Pulling the start back can't go past the content's start (beat 8).
        CHECK (f.dispatcher.execute ("resize_clip", obj ({ { "clip", id }, { "start_beat", 2 } })).ok);
        CHECK (near (clipsOf (f, 1)[0]["start_beat"], 8.0));
        CHECK (! f.dispatcher.execute ("resize_clip", obj ({ { "clip", id }, { "end_beat", 8 } })).ok);
        CHECK (! f.dispatcher.execute ("resize_clip", obj ({ { "clip", id } })).ok);

        // Split, duplicate and delete.
        auto long_ = f.dispatcher.execute ("add_midi_clip", obj ({ { "channel", "Lead" }, { "start_beat", 16 }, { "length_beats", 8 },
                                                                     { "notes", juce::Array<juce::var> { note (60, 0, 1), note (64, 6, 1) } } }));
        auto longId = long_.value["clip"].toString();
        auto halves = f.dispatcher.execute ("split_clip", obj ({ { "clip", longId }, { "beat", 20 } }));
        CHECK (halves.ok);
        CHECK (halves.value.size() == 2);
        auto lead = clipsOf (f, 0);
        CHECK (lead.size() == 2);
        CHECK (near (lead[0]["start_beat"], 16.0) && near (lead[0]["length_beats"], 4.0));
        CHECK (near (lead[1]["start_beat"], 20.0) && near (lead[1]["length_beats"], 4.0));
        CHECK (! f.dispatcher.execute ("split_clip", obj ({ { "clip", longId }, { "beat", 30 } })).ok);

        auto secondHalf = halves.value[1]["clip"].toString();
        auto copy = f.dispatcher.execute ("duplicate_clip", obj ({ { "clip", secondHalf } }));
        CHECK (copy.ok);
        CHECK (copy.value["clip"].toString() != secondHalf);
        CHECK (near (copy.value["start_beat"], 24.0));   // right after the original
        CHECK (clipsOf (f, 0).size() == 3);

        CHECK (f.dispatcher.execute ("delete_clip", obj ({ { "clip", copy.value["clip"] } })).ok);
        CHECK (clipsOf (f, 0).size() == 2);
        CHECK (f.dispatcher.undo());
        CHECK (clipsOf (f, 0).size() == 3);

        CHECK (! f.dispatcher.execute ("delete_clip", obj ({ { "clip", "nope" } })).ok);
        CHECK (! f.dispatcher.execute ("move_clip", obj ({ { "clip", secondHalf }, { "start_beat", -1 } })).ok);
        CHECK (! f.dispatcher.execute ("move_clip", obj ({ { "clip", secondHalf }, { "channel", "Nobody" } })).ok);
    }

    void testPatternCopiesFollowSteps (tracktion::Engine& engine)
    {
        std::cout << "copies of a step pattern follow the channel's steps\n";
        Fixture f (engine);

        f.dispatcher.execute ("create_track", obj ({ { "name", "Kick" } }));
        f.dispatcher.execute ("create_track", obj ({ { "name", "Other" } }));
        f.dispatcher.execute ("set_steps", obj ({ { "channel", "Kick" }, { "steps", "x...x...x...x..." } }));
        auto pattern = clipsOf (f, 0)[0]["id"].toString();

        auto copy = f.dispatcher.execute ("duplicate_clip", obj ({ { "clip", pattern }, { "start_beat", 8 } }));
        CHECK (copy.ok);
        CHECK (f.dispatcher.execute ("move_clip", obj ({ { "clip", pattern }, { "start_beat", 4 } })).ok);

        // One step change refills both copies where they stand.
        CHECK (f.dispatcher.execute ("set_steps", obj ({ { "channel", "Kick" }, { "step", 2 } })).ok);
        auto clips = clipsOf (f, 0);
        CHECK (clips.size() == 2);
        CHECK (near (clips[0]["start_beat"], 4.0));
        CHECK (near (clips[1]["start_beat"], 8.0));

        for (auto& c : *clips.getArray())
        {
            CHECK (c["pattern"] == juce::var (true));
            CHECK (static_cast<int> (c["notes"]) == 5);
            CHECK (near (c["length_beats"], 4.0));
        }

        // A shorter pattern shortens every copy.
        CHECK (f.dispatcher.execute ("set_steps", obj ({ { "channel", "Kick" }, { "length", 8 } })).ok);
        CHECK (near (clipsOf (f, 0)[1]["length_beats"], 2.0));

        // Patterns stay on their channel and keep their own length.
        CHECK (! f.dispatcher.execute ("move_clip", obj ({ { "clip", pattern }, { "channel", "Other" } })).ok);
        CHECK (! f.dispatcher.execute ("resize_clip", obj ({ { "clip", pattern }, { "end_beat", 12 } })).ok);
        CHECK (! f.dispatcher.execute ("split_clip", obj ({ { "clip", pattern }, { "beat", 5 } })).ok);

        // With every copy deleted, the next step change puts the pattern back at the start.
        auto remaining = clipsOf (f, 0);

        for (auto& c : *remaining.getArray())
            f.dispatcher.execute ("delete_clip", obj ({ { "clip", c["id"] } }));

        CHECK (clipsOf (f, 0).size() == 0);
        CHECK (f.dispatcher.execute ("set_steps", obj ({ { "channel", "Kick" }, { "step", 1 } })).ok);
        CHECK (clipsOf (f, 0).size() == 1);
        CHECK (near (clipsOf (f, 0)[0]["start_beat"], 0.0));
    }

    void testEditingNotes (tracktion::Engine& engine)
    {
        std::cout << "notes are read and replaced, timed from the clip's start\n";
        Fixture f (engine);

        f.dispatcher.execute ("create_track", obj ({ { "name", "Keys" } }));
        auto added = f.dispatcher.execute ("add_midi_clip", obj ({ { "channel", "Keys" }, { "start_beat", 4 }, { "length_beats", 4 },
                                                                     { "notes", juce::Array<juce::var> { note (64, 2, 1), note (60, 0, 1) } } }));
        auto id = added.value["clip"].toString();

        auto read = f.dispatcher.execute ("get_notes", obj ({ { "clip", id } }));
        CHECK (read.ok);
        CHECK (read.value["notes"].size() == 2);
        CHECK (static_cast<int> (read.value["notes"][0]["pitch"]) == 60);   // sorted by start
        CHECK (near (read.value["notes"][1]["start"], 2.0));
        CHECK (static_cast<int> (read.value["notes"][1]["velocity"]) == 100);
        CHECK (! f.dispatcher.canRedo());

        // Replacing the notes is one undo step.
        auto set = f.dispatcher.execute ("set_notes", obj ({ { "clip", id }, { "notes", juce::Array<juce::var> {
                                                               note (67, 0.5, 0.25), note (72, 3, 1),
                                                               obj ({ { "pitch", 48 }, { "start", 1 }, { "length", 2 }, { "velocity", 40 } }) } } }));
        CHECK (set.ok);
        CHECK (set.value["notes"].size() == 3);
        CHECK (static_cast<int> (set.value["notes"][1]["velocity"]) == 40);
        CHECK (static_cast<int> (clipsOf (f, 0)[0]["notes"]) == 3);
        CHECK (f.dispatcher.undo());
        CHECK (static_cast<int> (clipsOf (f, 0)[0]["notes"]) == 2);
        CHECK (f.dispatcher.redo());

        // After trimming the clip's front, notes are still timed from where the clip now starts.
        CHECK (f.dispatcher.execute ("resize_clip", obj ({ { "clip", id }, { "start_beat", 5 } })).ok);
        auto trimmed = f.dispatcher.execute ("get_notes", obj ({ { "clip", id } })).value["notes"];
        CHECK (near (trimmed[0]["start"], -0.5));   // the note at beat 4.5 is now before the clip's start
        CHECK (near (trimmed[1]["start"], 0.0));
        CHECK (f.dispatcher.execute ("set_notes", obj ({ { "clip", id }, { "notes", juce::Array<juce::var> { note (60, 1, 1) } } })).ok);

        if (auto midi = dynamic_cast<tracktion::MidiClip*> (f.session.findClip (id)))
            CHECK (near (midi->getSequence().getNotes()[0]->getStartBeat().inBeats(), 2.0));   // beat 6 of the song

        // Bad notes are refused and change nothing.
        CHECK (! f.dispatcher.execute ("set_notes", obj ({ { "clip", id }, { "notes", juce::Array<juce::var> { note (128, 0, 1) } } })).ok);
        CHECK (! f.dispatcher.execute ("set_notes", obj ({ { "clip", id }, { "notes", juce::Array<juce::var> { note (60, 0, 0) } } })).ok);
        CHECK (! f.dispatcher.execute ("set_notes", obj ({ { "clip", id }, { "notes", juce::Array<juce::var> { note (60, -2, 1) } } })).ok);
        CHECK (! f.dispatcher.execute ("set_notes", obj ({ { "clip", id } })).ok);
        CHECK (static_cast<int> (clipsOf (f, 0)[0]["notes"]) == 1);

        // An empty list clears the clip.
        CHECK (f.dispatcher.execute ("set_notes", obj ({ { "clip", id }, { "notes", juce::Array<juce::var>() } })).ok);
        CHECK (static_cast<int> (clipsOf (f, 0)[0]["notes"]) == 0);

        // A step pattern's notes come from its steps.
        f.dispatcher.execute ("set_steps", obj ({ { "channel", "Keys" }, { "steps", "x..." } }));
        juce::String patternId;
        auto clips = clipsOf (f, 0);

        for (auto& c : *clips.getArray())
            if (c["pattern"])
                patternId = c["id"].toString();

        CHECK (f.dispatcher.execute ("get_notes", obj ({ { "clip", patternId } })).ok);
        CHECK (! f.dispatcher.execute ("set_notes", obj ({ { "clip", patternId }, { "notes", juce::Array<juce::var>() } })).ok);
        CHECK (! f.dispatcher.execute ("get_notes", obj ({ { "clip", "nope" } })).ok);
    }

    void testQuantizeAndVelocity (tracktion::Engine& engine)
    {
        std::cout << "notes quantize to a grid and keep their velocities\n";
        Fixture f (engine);

        f.dispatcher.execute ("create_track", obj ({ { "name", "Keys" } }));
        auto added = f.dispatcher.execute ("add_midi_clip", obj ({ { "channel", "Keys" }, { "length_beats", 4 },
                                                                     { "notes", juce::Array<juce::var> {
                                                                         obj ({ { "pitch", 60 }, { "start", 0.1 }, { "length", 0.9 }, { "velocity", 30 } }),
                                                                         note (62, 1.4, 0.3), note (64, 2.9, 0.6) } } }));
        auto id = added.value["clip"].toString();
        auto notesNow = [&] { return f.dispatcher.execute ("get_notes", obj ({ { "clip", id } })).value["notes"]; };

        // Only the chosen notes move, to the nearest half beat; lengths stay.
        auto q = f.dispatcher.execute ("quantize_notes", obj ({ { "clip", id }, { "grid", 0.5 },
                                                                  { "notes", juce::Array<juce::var> { 0, 2 } } }));
        CHECK (q.ok);
        CHECK (static_cast<int> (q.value["moved"]) == 2);
        CHECK (q.value["quantized"].size() == 2);
        CHECK (static_cast<int> (q.value["quantized"][1]) == 2);
        auto notes = notesNow();
        CHECK (near (notes[0]["start"], 0.0) && near (notes[0]["length"], 0.9));
        CHECK (static_cast<int> (notes[0]["velocity"]) == 30);   // velocity survives
        CHECK (near (notes[1]["start"], 1.4));                   // not chosen
        CHECK (near (notes[2]["start"], 3.0));
        CHECK (f.dispatcher.undo());
        CHECK (near (notesNow()[0]["start"], 0.1));
        CHECK (f.dispatcher.redo());

        // Half strength goes half way; ends snaps the note ends too.
        CHECK (f.dispatcher.execute ("quantize_notes", obj ({ { "clip", id }, { "grid", 1 }, { "strength", 0.5 },
                                                              { "notes", juce::Array<juce::var> { 1 } } })).ok);
        CHECK (near (notesNow()[1]["start"], 1.2));
        CHECK (f.dispatcher.execute ("quantize_notes", obj ({ { "clip", id }, { "grid", 0.25 }, { "ends", true } })).ok);
        notes = notesNow();
        CHECK (near (notes[0]["length"], 1.0));   // 0 to 0.9 becomes 0 to 1
        CHECK (near (notes[1]["start"], 1.25) && near (notes[1]["length"], 0.25));

        CHECK (! f.dispatcher.execute ("quantize_notes", obj ({ { "clip", id }, { "grid", 0 } })).ok);
        CHECK (! f.dispatcher.execute ("quantize_notes", obj ({ { "clip", id }, { "strength", 2 } })).ok);
        CHECK (! f.dispatcher.execute ("quantize_notes", obj ({ { "clip", id }, { "notes", juce::Array<juce::var> { 7 } } })).ok);

        // Velocity is set through set_notes like the rest of a note.
        auto edited = notesNow();
        edited[2].getDynamicObject()->setProperty ("velocity", 127);
        CHECK (f.dispatcher.execute ("set_notes", obj ({ { "clip", id }, { "notes", edited } })).ok);
        CHECK (static_cast<int> (notesNow()[2]["velocity"]) == 127);
        edited[2].getDynamicObject()->setProperty ("velocity", 0);
        CHECK (! f.dispatcher.execute ("set_notes", obj ({ { "clip", id }, { "notes", edited } })).ok);
    }

    void testArmingForMidiInput (tracktion::Engine& engine)
    {
        std::cout << "one channel at a time is armed for MIDI input\n";
        Fixture f (engine);

        f.dispatcher.execute ("create_track", obj ({ { "name", "Lead" } }));
        f.dispatcher.execute ("create_track", obj ({ { "name", "Bass" } }));
        auto armedAt = [&] (int i) { return static_cast<bool> (f.state()["channels"][i]["armed"]); };

        CHECK (! armedAt (0) && ! armedAt (1));
        CHECK (! f.dispatcher.execute ("record").ok);   // nowhere to record to yet
        CHECK (f.state()["midi_inputs"].isArray());

        CHECK (f.dispatcher.execute ("arm_channel", obj ({ { "channel", "Lead" } })).ok);
        CHECK (armedAt (0) && ! armedAt (1));
        CHECK (f.dispatcher.execute ("arm_channel", obj ({ { "channel", "Bass" } })).ok);
        CHECK (! armedAt (0) && armedAt (1));

        // Disarming a channel that isn't armed leaves the armed one alone.
        CHECK (f.dispatcher.execute ("arm_channel", obj ({ { "channel", "Lead" }, { "armed", false } })).ok);
        CHECK (armedAt (1));
        CHECK (f.dispatcher.execute ("arm_channel", obj ({ { "channel", "Bass" }, { "armed", false } })).ok);
        CHECK (! armedAt (1));
        CHECK (! f.dispatcher.execute ("arm_channel", obj ({ { "channel", "Nobody" } })).ok);

        // Arming isn't part of the song: no undo step, no unsaved changes.
        f.session.markAsSaved();
        auto couldUndo = f.dispatcher.canUndo();
        CHECK (f.dispatcher.execute ("arm_channel", obj ({ { "channel", "Lead" } })).ok);
        CHECK (f.dispatcher.canUndo() == couldUndo);
        CHECK (! f.session.hasUnsavedChanges());

        // Deleting the armed channel leaves nothing armed.
        CHECK (f.dispatcher.execute ("delete_track", obj ({ { "track", "Lead" } })).ok);
        CHECK (f.session.getArmedChannel() == nullptr);
        CHECK (! f.dispatcher.execute ("record").ok);
        CHECK (f.dispatcher.undo());
        CHECK (f.session.getArmedChannel() != nullptr);
    }

    void testPluginScanning (tracktion::Engine& engine)
    {
        std::cout << "scanning finds a VST3 and lists it\n";
        Fixture f (engine);
        auto& known = engine.getPluginManager().knownPluginList;
        known.clear();

        auto listed = [&] (const char* kind) { return f.dispatcher.execute ("list_plugins", obj ({ { "kind", kind } })).value; };
        CHECK (listed ("all").size() == 0);
        CHECK (! f.dispatcher.execute ("list_plugins", obj ({ { "kind", "drums" } })).ok);

        juce::File folder (NG_TEST_PLUGIN_FOLDER);
        CHECK (folder.getChildFile ("Ng Test Synth.vst3").exists());

        auto scan = [&] (juce::var folders)
        {
            return f.dispatcher.execute ("scan_plugins", folders.isVoid() ? obj ({}) : obj ({ { "folders", folders } }));
        };

        CHECK (! scan (juce::Array<juce::var> { "relative/folder" }).ok);
        CHECK (! scan (juce::Array<juce::var> { folder.getChildFile ("missing").getFullPathName() }).ok);
        CHECK (! f.dispatcher.execute ("scan_plugins", obj ({ { "format", "Nope" } })).ok);

        // Nothing in an empty folder.
        juce::TemporaryFile empty;
        CHECK (empty.getFile().createDirectory());
        auto result = scan (juce::Array<juce::var> { empty.getFile().getFullPathName() });
        CHECK (result.ok && int (result.value["added"]) == 0);
        empty.getFile().deleteRecursively();

        f.session.markAsSaved();
        auto couldUndo = f.dispatcher.canUndo();

        result = scan (juce::Array<juce::var> { folder.getFullPathName() });
        CHECK (result.ok && int (result.value["added"]) == 1 && result.value["failed"].size() == 0);

        auto all = listed ("all");
        CHECK (all.size() == 1);
        CHECK (all[0]["name"] == "Ng Test Synth" && all[0]["format"] == "VST3" && static_cast<bool> (all[0]["instrument"]));
        CHECK (all[0]["id"].toString().isNotEmpty());
        CHECK (listed ("instrument").size() == 1 && listed ("effect").size() == 0);

        // Scanning again finds nothing new, and with no folders it uses the ones given last.
        result = scan ({});
        CHECK (result.ok && int (result.value["added"]) == 0 && int (result.value["total"]) == 1);
        CHECK (result.value["folders"][0] == folder.getFullPathName());

        // The plugin list belongs to the app, not the song.
        CHECK (f.dispatcher.canUndo() == couldUndo && ! f.session.hasUnsavedChanges());
        known.clear();
    }

    void testPluginInstrument (tracktion::Engine& engine)
    {
        std::cout << "a scanned plugin plays as a channel's instrument\n";
        Fixture f (engine);
        auto& known = engine.getPluginManager().knownPluginList;
        known.clear();
        CHECK (f.dispatcher.execute ("scan_plugins", obj ({ { "folders", juce::Array<juce::var> { NG_TEST_PLUGIN_FOLDER } } })).ok);
        auto id = f.dispatcher.execute ("list_plugins").value[0]["id"].toString();
        CHECK (id.isNotEmpty());

        auto channel = [&] { return f.state()["channels"][0]; };

        CHECK (f.dispatcher.execute ("create_track", obj ({ { "name", "Lead" }, { "instrument", id } })).ok);
        CHECK (channel()["instrument"] == id && channel()["instrument_name"] == "Ng Test Synth");
        CHECK (! channel().hasProperty ("instrument_missing"));

        // It makes the sound: the test synth plays a sine for each note.
        f.dispatcher.execute ("add_midi_clip", obj ({ { "channel", "Lead" }, { "length_beats", 4 },
                                                      { "notes", juce::Array<juce::var> { note (69, 0, 2) } } }));
        auto rms = renderRms (f);
        std::cout << "       rendered through the plugin, RMS " << rms << "\n";
        CHECK (rms > 0.01f);

        // Swapping instruments is one undo step each way; a name works as well as an id.
        CHECK (f.dispatcher.execute ("set_instrument", obj ({ { "channel", "Lead" }, { "instrument", "4osc" } })).ok);
        CHECK (channel()["instrument"] == "4osc");
        f.dispatcher.undo();
        CHECK (channel()["instrument"] == id);
        CHECK (f.dispatcher.execute ("set_instrument", obj ({ { "channel", "Lead" }, { "instrument", "ng test synth" } })).ok);
        CHECK (channel()["instrument"] == id);

        CHECK (! f.dispatcher.execute ("set_instrument", obj ({ { "channel", "Lead" }, { "instrument", "Nonexistent" } })).ok);
        CHECK (! f.dispatcher.execute ("create_track", obj ({ { "instrument", "Nonexistent" } })).ok);
        CHECK (f.state()["channels"].size() == 1 && channel()["instrument"] == id);

        // The editor window: headless there's no screen, but the plugin is there to show.
        auto shown = f.dispatcher.execute ("show_plugin_window", obj ({ { "channel", "Lead" } }));
        CHECK (shown.ok && ! static_cast<bool> (shown.value["shown"]));
        f.dispatcher.execute ("create_track", obj ({ { "name", "Built in" } }));
        CHECK (! f.dispatcher.execute ("show_plugin_window", obj ({ { "channel", "Built in" } })).ok);

        // Saved and reopened, the channel still has the plugin, with its settings.
        auto volume = [&]() -> juce::AudioProcessorParameter*
        {
            if (auto plugin = dynamic_cast<tracktion::ExternalPlugin*> (ng::Session::getInstrument (*f.session.getChannels()[0])))
                if (auto instance = plugin->getAudioPluginInstance())
                    return instance->getParameters()[0];

            return nullptr;
        };

        CHECK (volume() != nullptr);

        if (auto p = volume())
            p->setValueNotifyingHost (0.25f);

        juce::TemporaryFile temp (".ngdaw");
        auto path = temp.getFile().getFullPathName();
        CHECK (f.dispatcher.execute ("save_project", obj ({ { "path", path } })).ok);
        CHECK (f.dispatcher.execute ("open_project", obj ({ { "path", path }, { "discard_changes", true } })).ok);
        CHECK (channel()["instrument"] == id && ! channel().hasProperty ("instrument_missing"));
        CHECK (volume() != nullptr && std::abs (volume()->getValue() - 0.25f) < 0.001f);

        // On a computer without the plugin, the project still opens and says it's missing.
        known.clear();
        CHECK (f.dispatcher.execute ("open_project", obj ({ { "path", path }, { "discard_changes", true } })).ok);
        CHECK (channel()["instrument_name"] == "Ng Test Synth" && static_cast<bool> (channel()["instrument_missing"]));
        CHECK (! f.dispatcher.execute ("show_plugin_window", obj ({ { "channel", "Lead" } })).ok);
    }

    void testEffects (tracktion::Engine& engine)
    {
        std::cout << "insert effects on mixer strips\n";
        Fixture f (engine);
        auto& known = engine.getPluginManager().knownPluginList;
        known.clear();
        CHECK (f.dispatcher.execute ("scan_plugins", obj ({ { "folders", juce::Array<juce::var> { NG_TEST_PLUGIN_FOLDER,
                                                                                                    NG_TEST_EFFECT_FOLDER } } })).ok);
        auto effectPlugins = f.dispatcher.execute ("list_plugins", obj ({ { "kind", "effect" } })).value;
        CHECK (effectPlugins.size() == 1 && effectPlugins[0]["name"] == "Ng Test Gain");
        auto gainId = effectPlugins[0]["id"].toString();
        auto synthId = f.dispatcher.execute ("list_plugins", obj ({ { "kind", "instrument" } })).value[0]["id"].toString();

        CHECK (f.dispatcher.execute ("create_track", obj ({ { "name", "Lead" }, { "instrument", synthId } })).ok);
        f.dispatcher.execute ("add_midi_clip", obj ({ { "channel", "Lead" }, { "length_beats", 4 },
                                                      { "notes", juce::Array<juce::var> { note (69, 0, 4) } } }));

        auto effects = [&] { return f.state()["strips"][0]["effects"]; };
        auto types = [&]
        {
            juce::StringArray list;
            auto all = effects();

            for (auto& e : *all.getArray())
                list.add (e["type"].toString());

            return list.joinIntoString (",");
        };
        auto add = [&] (const juce::String& strip, const juce::String& effect, juce::var index = {})
        {
            auto params = obj ({ { "strip", strip }, { "effect", effect } });

            if (! index.isVoid())
                params.getDynamicObject()->setProperty ("index", index);

            return f.dispatcher.execute ("add_effect", params);
        };

        CHECK (effects().size() == 0);

        // Adding, inserting, moving and removing are one undo step each.
        CHECK (add ("Lead", "reverb").ok);
        CHECK (types() == "reverb");
        CHECK (add ("Lead", "EQ", 0).ok);
        CHECK (types() == "eq,reverb");
        CHECK (f.dispatcher.execute ("move_effect", obj ({ { "strip", "Lead" }, { "effect", "reverb" }, { "index", 0 } })).ok);
        CHECK (types() == "reverb,eq");
        f.dispatcher.undo();
        CHECK (types() == "eq,reverb");
        f.dispatcher.redo();
        CHECK (types() == "reverb,eq");
        CHECK (f.dispatcher.execute ("remove_effect", obj ({ { "strip", "Lead" }, { "effect", 1 } })).ok);
        CHECK (types() == "reverb");
        f.dispatcher.undo();
        CHECK (types() == "reverb,eq");
        f.dispatcher.undo();
        f.dispatcher.undo();
        CHECK (types() == "reverb");
        f.dispatcher.undo();
        CHECK (types() == "");
        f.dispatcher.redo();
        f.dispatcher.redo();
        CHECK (types() == "eq,reverb");

        // Effects run before the fader, so the fader still sets the strip's level.
        auto& stripList = f.session.getStrips()[0]->pluginList;
        auto volumeIndex = stripList.indexOf (f.session.getStrips()[0]->getVolumePlugin());
        CHECK (volumeIndex > stripList.indexOf (ng::Session::getEffects (stripList)[1]));

        // Bad requests change nothing.
        CHECK (! add ("Lead", "Flanger").ok);
        CHECK (! add ("Nowhere", "reverb").ok);
        CHECK (! add ("Lead", "Ng Test Synth").ok);   // an instrument
        CHECK (! f.dispatcher.execute ("remove_effect", obj ({ { "strip", "Lead" }, { "effect", 5 } })).ok);
        CHECK (! f.dispatcher.execute ("move_effect", obj ({ { "strip", "Lead" }, { "effect", "chorus" }, { "index", 0 } })).ok);
        CHECK (types() == "eq,reverb");

        // Parameters: listed with their ranges, set by id or name, undoable, checked first.
        auto reverb = [&] { return f.dispatcher.execute ("get_effect", obj ({ { "strip", "Lead" }, { "effect", "reverb" } })).value; };
        auto param = [] (const juce::var& effect, const juce::String& id)
        {
            auto all = effect["params"];

            for (auto& p : *all.getArray())
                if (p["id"] == id || p["name"].toString().equalsIgnoreCase (id))
                    return p;

            return juce::var();
        };

        auto wet = param (reverb(), "wet level");
        CHECK (wet.isObject() && near (wet["min"], 0.0) && near (wet["max"], 1.0));
        auto defaultWet = static_cast<double> (wet["value"]);

        CHECK (f.dispatcher.execute ("set_effect", obj ({ { "strip", "Lead" }, { "effect", "reverb" },
                                                          { "params", obj ({ { "wet level", 0.9 } }) } })).ok);
        CHECK (near (param (reverb(), "wet level")["value"], 0.9));
        f.dispatcher.undo();
        CHECK (near (param (reverb(), "wet level")["value"], defaultWet));
        f.dispatcher.redo();
        CHECK (near (param (reverb(), "wet level")["value"], 0.9));
        CHECK (f.dispatcher.execute ("set_effect", obj ({ { "strip", "Lead" }, { "effect", "reverb" },
                                                          { "params", obj ({ { "Room Size", 0.8 } }) } })).ok);
        CHECK (near (param (reverb(), "room size")["value"], 0.8));

        CHECK (! f.dispatcher.execute ("set_effect", obj ({ { "strip", "Lead" }, { "effect", "reverb" },
                                                            { "params", obj ({ { "wet level", 0.1 }, { "room size", 4 } }) } })).ok);
        CHECK (! f.dispatcher.execute ("set_effect", obj ({ { "strip", "Lead" }, { "effect", "reverb" },
                                                            { "params", obj ({ { "colour", 0.5 } }) } })).ok);
        CHECK (near (param (reverb(), "wet level")["value"], 0.9));

        // Bypass is undoable too.
        CHECK (f.dispatcher.execute ("set_effect", obj ({ { "strip", "Lead" }, { "effect", "reverb" }, { "enabled", false } })).ok);
        CHECK (! static_cast<bool> (effects()[1]["enabled"]));
        f.dispatcher.undo();
        CHECK (static_cast<bool> (effects()[1]["enabled"]));

        // The delay's time, which isn't a Tracktion parameter.
        CHECK (add ("Lead", "delay").ok);
        CHECK (f.dispatcher.execute ("set_effect", obj ({ { "strip", "Lead" }, { "effect", "delay" },
                                                          { "params", obj ({ { "length", 300 } }) } })).ok);
        auto delay = f.dispatcher.execute ("get_effect", obj ({ { "strip", "Lead" }, { "effect", "delay" } })).value;
        CHECK (near (param (delay, "length")["value"], 300));
        CHECK (! f.dispatcher.execute ("set_effect", obj ({ { "strip", "Lead" }, { "effect", "delay" },
                                                            { "params", obj ({ { "length", 0 } }) } })).ok);
        CHECK (f.dispatcher.execute ("remove_effect", obj ({ { "strip", "Lead" }, { "effect", "delay" } })).ok);
        CHECK (f.dispatcher.execute ("remove_effect", obj ({ { "strip", "Lead" }, { "effect", "reverb" } })).ok);
        CHECK (f.dispatcher.execute ("remove_effect", obj ({ { "strip", "Lead" }, { "effect", "eq" } })).ok);

        // A plugin effect: it changes the sound, its parameter is an undo step, and bypass skips it.
        auto dry = renderRms (f);
        CHECK (dry > 0.01f);
        CHECK (add ("Lead", gainId).ok);
        CHECK (types() == gainId && effects()[0]["name"] == "Ng Test Gain");
        CHECK (std::abs (renderRms (f) - dry) < 0.001f);

        auto setGain = [&] (double value)
        {
            return f.dispatcher.execute ("set_effect", obj ({ { "strip", "Lead" }, { "effect", 0 },
                                                              { "params", obj ({ { "Gain", value } }) } }));
        };

        CHECK (setGain (0.0).ok);
        auto silent = renderRms (f);
        std::cout << "       dry RMS " << dry << ", through the gain plugin at 0, RMS " << silent << "\n";
        CHECK (silent < 0.0001f);
        f.dispatcher.undo();
        CHECK (std::abs (renderRms (f) - dry) < 0.001f);
        f.dispatcher.redo();
        CHECK (renderRms (f) < 0.0001f);
        CHECK (f.dispatcher.execute ("set_effect", obj ({ { "strip", "Lead" }, { "effect", 0 }, { "enabled", false } })).ok);
        CHECK (std::abs (renderRms (f) - dry) < 0.001f);
        CHECK (f.dispatcher.execute ("set_effect", obj ({ { "strip", "Lead" }, { "effect", 0 }, { "enabled", true } })).ok);
        CHECK (setGain (0.5).ok);

        // The master strip has effects too, ahead of its meter.
        CHECK (add ("master", "compressor").ok);
        CHECK (f.state()["master"]["effects"].size() == 1 && f.state()["master"]["effects"][0]["type"] == "compressor");
        auto& masterList = f.session.getEdit().getMasterPluginList();
        CHECK (masterList[masterList.size() - 1] == f.session.getMasterMeter());

        // Windows: headless there's no screen, but both kinds have something to show.
        auto shown = f.dispatcher.execute ("show_effect_window", obj ({ { "strip", "Lead" }, { "effect", 0 } }));
        CHECK (shown.ok && ! static_cast<bool> (shown.value["shown"]));
        CHECK (f.dispatcher.execute ("show_effect_window", obj ({ { "strip", "master" }, { "effect", "compressor" } })).ok);

        // Saved and reopened, the effects come back in order with their settings.
        CHECK (add ("Lead", "reverb").ok);
        CHECK (f.dispatcher.execute ("set_effect", obj ({ { "strip", "Lead" }, { "effect", "reverb" },
                                                          { "params", obj ({ { "wet level", 0.7 } }) } })).ok);
        // A VST3 plugin takes in a parameter change when it next processes audio, which the
        // app does all the time; here, render once so the saved settings include it.
        renderRms (f);
        juce::TemporaryFile temp (".ngdaw");
        auto path = temp.getFile().getFullPathName();
        CHECK (f.dispatcher.execute ("save_project", obj ({ { "path", path } })).ok);
        CHECK (f.dispatcher.execute ("open_project", obj ({ { "path", path }, { "discard_changes", true } })).ok);
        CHECK (types() == gainId + ",reverb");
        CHECK (near (param (reverb(), "wet level")["value"], 0.7));
        auto gain = f.dispatcher.execute ("get_effect", obj ({ { "strip", "Lead" }, { "effect", 0 } })).value;
        CHECK (near (param (gain, "gain")["value"], 0.5));
        CHECK (f.state()["master"]["effects"].size() == 1);

        // Without the plugin installed, the project still opens and says it's missing.
        known.clear();
        CHECK (f.dispatcher.execute ("open_project", obj ({ { "path", path }, { "discard_changes", true } })).ok);
        CHECK (static_cast<bool> (effects()[0]["missing"]) && ! effects()[1].hasProperty ("missing"));
        CHECK (! f.dispatcher.execute ("show_effect_window", obj ({ { "strip", "Lead" }, { "effect", 0 } })).ok);
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
        testUndoAfterTracktionTidiesUp (engine);
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
        testStepSequencer (engine);
        testPatternRenders (engine);
        testSaveAndOpen (engine);
        testClipsAndPlayheadInState (engine);
        testClipEditing (engine);
        testPatternCopiesFollowSteps (engine);
        testEditingNotes (engine);
        testQuantizeAndVelocity (engine);
        testArmingForMidiInput (engine);
        testPluginScanning (engine);
        testPluginInstrument (engine);
        testEffects (engine);
        testRendersAudio (engine);
    }

    std::cout << (failures == 0 ? "\nAll checks passed\n" : "\nSome checks failed\n");
    return failures == 0 ? 0 : 1;
}
