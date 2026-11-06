#include "CommandRegistry.h"
#include "AudioAnalysis.h"

#include <thread>

namespace ng
{
namespace
{
    using params::isNumber;
    using params::prop;
    using params::schema;

    /** How long a song export runs on past the last clip, so reverb and release tails can ring out.
        A loop export stops exactly at the loop's end, so it loops cleanly. */
    constexpr double songTailSeconds = 2.0;

    /** Below this, a loudness reading means silence. */
    constexpr double silenceThreshold = -99.0;

    /** Sets the export flag for as long as it lives. */
    struct ScopedExport
    {
        explicit ScopedExport (Session& s) : session (s)    { session.setExporting (true); }
        ~ScopedExport()                                     { session.setExporting (false); }
        Session& session;
    };

    /** While it lives, the senders into a stem's track play only through their sends: the stem's
        track is soloed, the senders are neither soloed nor isolated (so they're silent, but still
        processed and sending), and every other track is solo-isolated so the senders' channels
        keep playing into them. Solo isn't part of the undo history, and everything is put back. */
    struct ScopedSendsOnly
    {
        ScopedSendsOnly (te::Edit& e, const te::AudioTrack* target, const juce::Array<te::AudioTrack*>& senders)
            : edit (e)
        {
            if (target == nullptr || senders.isEmpty())
                return;

            EngineBehaviour::processMutedTracks = true;

            for (auto t : te::getAudioTracks (edit))
            {
                restore.add ({ t, t->isSolo (false), t->isSoloIsolate (false) });
                t->setSolo (t == target);
                t->setSoloIsolate (t != target && ! senders.contains (t));
            }

            edit.updateMuteSoloStatuses();
        }

        ~ScopedSendsOnly()
        {
            if (restore.isEmpty())
                return;

            for (auto& r : restore)
            {
                r.track->setSolo (r.solo);
                r.track->setSoloIsolate (r.isolate);
            }

            edit.updateMuteSoloStatuses();
            EngineBehaviour::processMutedTracks = false;
        }

        struct Saved
        {
            te::AudioTrack* track;
            bool solo, isolate;
        };

        te::Edit& edit;
        juce::Array<Saved> restore;
    };

    /** Renders part of the project to a WAV file. Rendering runs on its own thread while this
        one keeps handling messages, which audio that is stretched or still being read needs.
        Returns an error, or an empty string. */
    juce::String renderWav (Session& session, const juce::File& dest, te::TimeRange range,
                            const juce::Array<te::Track*>& tracks, bool masterEffects, int bitDepth, double sampleRate)
    {
        auto& edit = session.getEdit();

        te::Renderer::Parameters r (edit);
        r.destFile = dest;
        r.audioFormat = edit.engine.getAudioFileFormatManager().getWavFormat();
        r.bitDepth = bitDepth;
        r.sampleRateForAudio = sampleRate;
        r.blockSizeForAudio = 512;
        r.time = range;
        r.usePlugins = true;
        r.useMasterPlugins = masterEffects;
        r.canRenderInMono = false;
        r.checkNodesForAudio = false;   // a silent part still gets its file, so stems line up

        // Tracktion's toBitSet() sets every track's bit, so the tracks are marked here.
        auto allTracks = te::getAllTracks (edit);

        for (auto t : tracks)
            if (auto index = allTracks.indexOf (t); index >= 0)
                r.tracksToDo.setBit (index);

        if (! dest.getParentDirectory().createDirectory())
            return "can't create the folder " + dest.getParentDirectory().getFullPathName();

        dest.deleteFile();
        auto task = te::render_utils::createRenderTask (r, "Export", nullptr, nullptr);

        if (task == nullptr)
            return "couldn't start rendering";

        std::atomic<bool> finished { false };
        std::thread worker ([&]
        {
            while (task->runJob() == juce::ThreadPoolJob::jobNeedsRunningAgain)
            {}

            finished = true;
        });

        while (! finished)
            if (juce::MessageManager::getInstance()->isThisTheMessageThread())
                juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
            else
                juce::Thread::sleep (5);

        worker.join();

        if (! dest.existsAsFile() || dest.getSize() == 0)
            return "rendering didn't write " + dest.getFullPathName();

        return {};
    }

    /** Encodes a WAV file as an MP3 with LAME or FFmpeg. Returns an error, or an empty string. */
    juce::String encodeMp3 (const juce::File& wav, const juce::File& mp3, int kbps)
    {
        auto encoder = findMp3Encoder();

        if (! encoder.existsAsFile())
            return "MP3 export needs LAME or FFmpeg. Install one (it has to be on the PATH, or next to Ng DAW), "
                   "or export as WAV";

        juce::StringArray args { encoder.getFullPathName() };

        if (encoder.getFileNameWithoutExtension().equalsIgnoreCase ("lame"))
            args.addArray ({ "--quiet", "-b", juce::String (kbps), wav.getFullPathName(), mp3.getFullPathName() });
        else
            args.addArray ({ "-y", "-hide_banner", "-loglevel", "error", "-i", wav.getFullPathName(),
                             "-codec:a", "libmp3lame", "-b:a", juce::String (kbps) + "k", mp3.getFullPathName() });

        mp3.deleteFile();
        juce::ChildProcess process;

        if (! process.start (args, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
            return "couldn't run " + encoder.getFullPathName();

        auto output = process.readAllProcessOutput().trim();
        process.waitForProcessToFinish (60000);

        if (process.getExitCode() != 0 || ! mp3.existsAsFile() || mp3.getSize() == 0)
            return encoder.getFileName() + " couldn't encode the MP3" + (output.isEmpty() ? juce::String() : ": " + output);

        return {};
    }

    /** The mixer strips (and channels going straight to master) that make sound of their own,
        each with a stem: a strip with channels routed into it, or a bus that strips send to. */
    juce::Array<te::AudioTrack*> getStemSources (Session& session)
    {
        juce::Array<te::AudioTrack*> sources;
        auto allTracks = session.getStrips();
        allTracks.addArray (session.getChannels());

        for (auto strip : session.getStrips())
        {
            // A strip routed into another one is part of that one's stem.
            if (strip->getOutput().getDestinationTrack() != nullptr)
                continue;

            for (auto other : allTracks)
                if (other != strip && session.feeds (*other, *strip))
                {
                    sources.add (strip);
                    break;
                }
        }

        for (auto channel : session.getChannels())
            if (session.getDestinationStrip (*channel) == nullptr)
                sources.add (channel);

        return sources;
    }

    /** The strips that send into a stem's track and play to master themselves. While the stem
        renders they're muted, so it holds only what they send, not their own sound. Strips the
        user has muted are left out: they send nothing in the mix either. */
    juce::Array<te::AudioTrack*> getSendersInto (Session& session, te::AudioTrack& target)
    {
        juce::Array<te::AudioTrack*> senders;

        for (auto strip : session.getStrips())
            if (strip != &target && strip->getOutput().getDestinationTrack() == nullptr
                 && ! strip->isMuted (false) && session.feeds (*strip, target))
                senders.add (strip);

        return senders;
    }

    CommandResult exportAudio (Session& session, const juce::var& p)
    {
        auto& edit = session.getEdit();
        auto path = params::getString (p, "path");

        if (path.isEmpty() || ! juce::File::isAbsolutePath (path))
            return CommandResult::failure ("export_audio: give the full path of the file (or, for stems, the folder) to write");

        if (session.isExporting())
            return CommandResult::failure ("export_audio: an export is already running");

        if (edit.getTransport().isRecording())
            return CommandResult::failure ("export_audio: stop recording first");

        for (auto key : { "bit_depth", "sample_rate", "mp3_kbps" })
            if (p.hasProperty (key) && ! isNumber (p[key]))
                return CommandResult::failure (juce::String ("export_audio: ") + key + " must be a number");

        auto stems = static_cast<bool> (p.getProperty ("stems", false));
        auto destination = juce::File (path);
        auto format = params::getString (p, "format", destination.hasFileExtension ("mp3") ? "mp3" : "wav").toLowerCase();

        if (format != "wav" && format != "mp3")
            return CommandResult::failure ("export_audio: format must be \"wav\" or \"mp3\"");

        auto bitDepth = juce::roundToInt (params::getDouble (p, "bit_depth", 24));
        auto sampleRate = params::getDouble (p, "sample_rate", 44100.0);
        auto kbps = juce::roundToInt (params::getDouble (p, "mp3_kbps", 320));

        if (bitDepth != 16 && bitDepth != 24 && bitDepth != 32)
            return CommandResult::failure ("export_audio: bit_depth must be 16, 24 or 32 (float)");

        if (sampleRate != 44100.0 && sampleRate != 48000.0 && sampleRate != 88200.0 && sampleRate != 96000.0)
            return CommandResult::failure ("export_audio: sample_rate must be 44100, 48000, 88200 or 96000");

        if (kbps < 96 || kbps > 320)
            return CommandResult::failure ("export_audio: mp3_kbps must be from 96 to 320");

        if (format == "mp3" && ! findMp3Encoder().existsAsFile())
            return CommandResult::failure ("export_audio: MP3 export needs LAME or FFmpeg. Install one (it has to be on "
                                           "the PATH, or next to Ng DAW), or export as WAV");

        // What to export: the song (to the end of its last clip, plus a tail) or the loop region.
        auto rangeName = params::getString (p, "range", "song");
        te::TimeRange range;

        if (rangeName == "song")
        {
            if (edit.getLength() <= te::TimeDuration())
                return CommandResult::failure ("export_audio: the project is empty, so there's nothing to export");

            range = { te::TimePosition(), edit.getLength() + te::TimeDuration::fromSeconds (songTailSeconds) };
        }
        else if (rangeName == "loop")
        {
            range = edit.getTransport().getLoopRange();

            if (range.getLength() <= te::TimeDuration())
                return CommandResult::failure ("export_audio: there's no loop region; set one with set_loop");
        }
        else
        {
            return CommandResult::failure ("export_audio: range must be \"song\" or \"loop\"");
        }

        // Each job: the file it ends up as, the tracks it renders, whether it goes through the
        // master's effects and fader, and which tracks only feed it through their sends.
        struct Job
        {
            juce::File file;
            juce::Array<te::Track*> tracks;
            bool master = true;
            juce::Array<te::AudioTrack*> sendersOnly;
            te::AudioTrack* source = nullptr;
        };

        std::vector<Job> jobs;
        auto extension = "." + format;

        if (stems)
        {
            if (destination.existsAsFile())
                return CommandResult::failure ("export_audio: for stems, path is a folder, but " + path + " is a file");

            juce::StringArray used;

            for (auto source : getStemSources (session))
            {
                auto name = juce::File::createLegalFileName (session.getProjectName() + " - " + source->getName());

                for (int n = 2; used.contains (name, true); ++n)
                    name = juce::File::createLegalFileName (session.getProjectName() + " - " + source->getName() + " " + juce::String (n));

                used.add (name);

                Job job { destination.getChildFile (name + extension), { source }, false, getSendersInto (session, *source), source };

                for (auto sender : job.sendersOnly)
                    job.tracks.add (sender);

                jobs.push_back (std::move (job));
            }

            if (jobs.empty())
                return CommandResult::failure ("export_audio: no mixer strip plays anything, so there are no stems");
        }
        else
        {
            if (destination.isDirectory())
                return CommandResult::failure ("export_audio: " + path + " is a folder; give a file name");

            jobs.push_back ({ destination.withFileExtension (extension), {}, true, {}, nullptr });
        }

        ScopedExport exporting (session);
        te::TransportControl::stopAllTransports (edit.engine, false, true);

        juce::Array<juce::var> files;

        for (auto& job : jobs)
        {
            ScopedSendsOnly sendsOnly (edit, job.source, job.sendersOnly);
            juce::String error;

            if (format == "wav")
            {
                error = renderWav (session, job.file, range, job.tracks, job.master, bitDepth, sampleRate);
            }
            else
            {
                juce::TemporaryFile wav (job.file.withFileExtension (".wav"), juce::TemporaryFile::useHiddenFile);
                error = renderWav (session, wav.getFile(), range, job.tracks, job.master, 24, sampleRate);

                if (error.isEmpty())
                    error = encodeMp3 (wav.getFile(), job.file, kbps);
            }

            if (error.isNotEmpty())
                return CommandResult::failure ("export_audio: " + error);

            files.add (job.file.getFullPathName());
        }

        auto& tempo = edit.tempoSequence;
        auto result = new juce::DynamicObject();
        result->setProperty ("files", files);
        result->setProperty ("format", format);
        result->setProperty ("start_beat", std::round (tempo.toBeats (range.getStart()).inBeats() * 1.0e6) / 1.0e6);
        result->setProperty ("end_beat", std::round (tempo.toBeats (range.getEnd()).inBeats() * 1.0e6) / 1.0e6);
        result->setProperty ("seconds", std::round (range.getLength().inSeconds() * 1000.0) / 1000.0);
        return CommandResult::success (juce::var (result));
    }
}

namespace
{
    /** Renders the loop (or the song) and measures it: the master, and each strip's stem as
        export_audio would write it, so levels can be checked and balanced without listening.
        The files go in a temporary folder that is deleted afterwards. */
    CommandResult analyzeMix (Session& session, const juce::var& p)
    {
        auto& edit = session.getEdit();

        if (session.isExporting())
            return CommandResult::failure ("analyze_mix: an export is already running");

        if (edit.getTransport().isRecording())
            return CommandResult::failure ("analyze_mix: stop recording first");

        auto& transport = edit.getTransport();
        auto loopRange = transport.getLoopRange();
        auto hasLoop = loopRange.getLength() > te::TimeDuration();
        auto rangeName = params::getString (p, "range", hasLoop && transport.looping ? "loop" : "song");
        te::TimeRange range;

        if (rangeName == "loop")
        {
            if (! hasLoop)
                return CommandResult::failure ("analyze_mix: there's no loop region; set one with set_loop, or use range \"song\"");

            range = loopRange;
        }
        else if (rangeName == "song")
        {
            if (edit.getLength() <= te::TimeDuration())
                return CommandResult::failure ("analyze_mix: the project is empty, so there's nothing to measure");

            range = { te::TimePosition(), edit.getLength() + te::TimeDuration::fromSeconds (songTailSeconds) };
        }
        else
        {
            return CommandResult::failure ("analyze_mix: range must be \"loop\" or \"song\"");
        }

        auto stems = static_cast<bool> (p.getProperty ("stems", true));
        auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                        .getChildFile ("NgDawAnalyze-" + juce::Uuid().toString());

        struct DeleteAfterwards
        {
            juce::File folder;
            ~DeleteAfterwards()     { folder.deleteRecursively(); }
        } cleanup { folder };

        ScopedExport exporting (session);
        te::TransportControl::stopAllTransports (edit.engine, false, true);

        constexpr double sampleRate = 44100.0;
        auto mixFile = folder.getChildFile ("mix.wav");

        if (auto error = renderWav (session, mixFile, range, {}, true, 32, sampleRate); error.isNotEmpty())
            return CommandResult::failure ("analyze_mix: " + error);

        auto master = measureAudioFile (edit.engine, mixFile);
        auto masterObj = new juce::DynamicObject();
        master.addTo (*masterObj);

        juce::StringArray warnings;

        if (master.clippedSamples > 0 || master.peak >= 1.0f)
            warnings.add ("the master clips (" + juce::String (master.clippedSamples) + " samples at full scale); "
                          "turn faders or the master down");
        else if (master.peak > juce::Decibels::decibelsToGain (-1.0f))
            warnings.add ("the master peaks above -1 dBFS, which can clip once encoded to MP3 or AAC");

        if (master.loudnessLufs > silenceThreshold && master.loudnessLufs < -24.0)
            warnings.add ("the mix is quiet (" + juce::String (master.loudnessLufs, 1) + " LUFS); most music sits around -14");

        auto result = new juce::DynamicObject();
        auto& tempo = edit.tempoSequence;
        result->setProperty ("range", rangeName);
        result->setProperty ("start_beat", std::round (tempo.toBeats (range.getStart()).inBeats() * 1.0e6) / 1.0e6);
        result->setProperty ("end_beat", std::round (tempo.toBeats (range.getEnd()).inBeats() * 1.0e6) / 1.0e6);
        result->setProperty ("master", juce::var (masterObj));

        if (stems)
        {
            juce::Array<juce::var> strips;
            int index = 0;

            for (auto source : getStemSources (session))
            {
                auto senders = getSendersInto (session, *source);
                juce::Array<te::Track*> tracks { source };

                for (auto sender : senders)
                    tracks.add (sender);

                auto file = folder.getChildFile ("stem" + juce::String (index++) + ".wav");
                ScopedSendsOnly sendsOnly (edit, source, senders);

                if (auto error = renderWav (session, file, range, tracks, false, 32, sampleRate); error.isNotEmpty())
                    return CommandResult::failure ("analyze_mix: " + error);

                auto levels = measureAudioFile (edit.engine, file);
                auto obj = new juce::DynamicObject();
                obj->setProperty ("id", source->itemID.toString());
                obj->setProperty ("name", source->getName());
                obj->setProperty ("role", Session::getRole (*source) == Session::Role::channel ? "channel" : "strip");

                if (auto fader = source->getVolumePlugin())
                    obj->setProperty ("volume_db", std::round (fader->getVolumeDb() * 100.0f) / 100.0f);

                levels.addTo (*obj);

                if (source->isMuted (false))
                    obj->setProperty ("muted", true);
                else if (levels.clippedSamples > 0)
                    warnings.add ("'" + source->getName() + "' clips on its own; turn its instrument or effects down");

                strips.add (juce::var (obj));
            }

            // Which strip is loudest, and which are buried, relative to it.
            double loudest = silenceThreshold;

            for (auto& s : strips)
                if (! s.hasProperty ("muted"))
                    loudest = std::max (loudest, static_cast<double> (s["loudness_lufs"]));

            for (auto& s : strips)
            {
                auto lufs = static_cast<double> (s["loudness_lufs"]);

                if (! s.hasProperty ("muted") && lufs > silenceThreshold && loudest > silenceThreshold)
                    s.getDynamicObject()->setProperty ("relative_to_loudest_db", std::round ((lufs - loudest) * 10.0) / 10.0);
                else if (! s.hasProperty ("muted") && lufs <= silenceThreshold)
                    warnings.add ("'" + s["name"].toString() + "' is silent in this range");
            }

            result->setProperty ("strips", strips);
        }

        result->setProperty ("warnings", warnings);
        return CommandResult::success (juce::var (result));
    }
}

juce::File findMp3Encoder()
{
   #if JUCE_WINDOWS
    juce::StringArray names { "lame.exe", "ffmpeg.exe" };
    auto separator = ";";
   #else
    juce::StringArray names { "lame", "ffmpeg" };
    auto separator = ":";
   #endif

    // Next to the app first, then the PATH, then where package managers put them (an app
    // started from the Dock or Finder doesn't get the shell's PATH).
    auto app = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
    juce::Array<juce::File> folders { app };

   #if JUCE_MAC
    folders.add (app.getSiblingFile ("Resources"));
   #endif

    for (auto& folder : juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable ("PATH", {}), separator, "\""))
        if (juce::File::isAbsolutePath (folder.trim()))
            folders.add (juce::File (folder.trim()));

   #if ! JUCE_WINDOWS
    for (auto folder : { "/opt/homebrew/bin", "/usr/local/bin", "/usr/bin" })
        folders.add (juce::File (folder));
   #endif

    for (auto& name : names)
        for (auto& folder : folders)
            if (auto file = folder.getChildFile (name); file.existsAsFile())
                return file;

    return {};
}

void registerExportCommands (CommandRegistry& registry)
{
    registry.add ({ "export_audio",
                    "Exports the song (from the start to the end of the last clip, plus two seconds for tails) or the "
                    "loop region to a WAV or MP3 file, as heard through the master. With stems: true, writes one file "
                    "per mixer strip into the folder at path instead, named after the project and the strip, all the "
                    "same length so they line up. A strip's stem has its channels through its own effects and fader; "
                    "what it sends to a bus is in that bus's stem; stems leave out the master's effects and fader. "
                    "Existing files are replaced. MP3 needs LAME or FFmpeg installed.",
                    schema ({ { "path", prop ("string", "Full path of the file to write or, for stems, the folder") },
                              { "format", prop ("string", "\"wav\" (default, or from the path's extension) or \"mp3\"") },
                              { "range", prop ("string", "\"song\" (default) or \"loop\"") },
                              { "stems", prop ("boolean", "One file per mixer strip instead of the mix") },
                              { "bit_depth", prop ("number", "WAV bit depth: 16, 24 (default) or 32 (float)") },
                              { "sample_rate", prop ("number", "44100 (default), 48000, 88200 or 96000") },
                              { "mp3_kbps", prop ("number", "MP3 bit rate, 96 to 320 (default 320)") } },
                            { "path" }),
                    false, exportAudio });

    registry.add ({ "analyze_mix",
                    "Renders the loop region (or the song) offline and measures it, so a mix can be checked without "
                    "listening: the master's peak, RMS, integrated loudness (LUFS) and clipping, and the same for each "
                    "mixer strip's stem with its fader level and how far below the loudest strip it sits. Warnings point "
                    "out clipping, a quiet master and silent strips. Changes nothing; it takes about as long as an export.",
                    schema ({ { "range", prop ("string", "\"loop\" (default while looping) or \"song\"") },
                              { "stems", prop ("boolean", "Also measure each strip (default true); false measures only the master, faster") } }),
                    false, analyzeMix });
}

} // namespace ng
