#include "SampleEditor.h"
#include "Theme.h"
#include "Widgets.h"

namespace ng
{
namespace
{
    void setUpKnob (juce::Slider& knob, double min, double max, double step, double reset, const juce::String& suffix)
    {
        knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        knob.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 18);
        knob.setRange (min, max, step);
        knob.setDoubleClickReturnValue (true, reset);
        knob.setTextValueSuffix (suffix);
    }

    void setUpCaption (juce::Label& label)
    {
        label.setFont (juce::FontOptions (11.0f));
        label.setColour (juce::Label::textColourId, juce::Colours::grey);
    }

    void setUpValueField (juce::Label& label, const juce::String& tooltip)
    {
        label.setEditable (false, true, false);
        label.setTooltip (tooltip);
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::outlineColourId, juce::Colours::white.withAlpha (0.15f));
        label.setColour (juce::Label::backgroundColourId, juce::Colours::black.withAlpha (0.2f));
    }

    juce::String formatNumber (double v, int decimals)
    {
        auto s = juce::String (v, decimals);
        return s.containsChar ('.') ? s.trimCharactersAtEnd ("0").trimCharactersAtEnd (".") : s;
    }
}

SampleEditor::SampleEditor (Session& s, CommandDispatcher& d)
    : session (s), dispatcher (d)
{
    modeBox.addItem ("Stretch", 1);
    modeBox.addItem ("Beats", 2);
    modeBox.addItem ("Resample", 3);
    modeBox.setTooltip ("How speed and pitch change. Stretch: Rubber Band's highest quality engine, for most sounds. "
                        "Beats: keeps drum hits crisp. Resample: speed and pitch change together, like a record played faster");
    modeBox.onChange = [this]
    {
        static const char* modes[] = { "stretch", "beats", "resample" };
        auto id = modeBox.getSelectedId();

        if (id >= 1 && id <= 3)
            apply ("stretch_mode", modes[id - 1]);
    };

    setUpKnob (pitchKnob, -24.0, 24.0, 1.0, 0.0, " st");
    pitchKnob.setTooltip ("Pitch in semitones (double-click for 0). In Resample mode it changes the speed too");
    setUpKnob (fineKnob, -100.0, 100.0, 1.0, 0.0, " ct");
    fineKnob.setTooltip ("Fine pitch in cents (double-click for 0)");
    setUpKnob (gainKnob, -24.0, 24.0, 0.1, 0.0, " dB");
    gainKnob.setTooltip ("Clip gain (double-click for 0 dB)");

    // A knob changes the clip when it's let go (or typed into), not on every step of a drag:
    // each change re-renders the stretched audio.
    auto pitchChanged = [this] { apply ("pitch_semitones", pitchKnob.getValue() + fineKnob.getValue() / 100.0); };

    for (auto* knob : { &pitchKnob, &fineKnob })
    {
        knob->onDragEnd = pitchChanged;
        knob->onValueChange = [knob, pitchChanged] { if (! knob->isMouseButtonDown()) pitchChanged(); };
    }

    gainKnob.onDragEnd = [this] { apply ("gain_db", gainKnob.getValue()); };
    gainKnob.onValueChange = [this] { if (! gainKnob.isMouseButtonDown()) apply ("gain_db", gainKnob.getValue()); };

    followButton.setTooltip ("Stretch the clip to follow the song's tempo, so it keeps its length in beats when the tempo changes");
    followButton.onClick = [this] { apply ("follow_tempo", followButton.getToggleState()); };

    setUpValueField (bpmValue, "The file's own tempo. Double-click to type it; the clip then follows the song's tempo");
    bpmValue.onTextChange = [this]
    {
        auto bpm = bpmValue.getText().retainCharacters ("0123456789.").getDoubleValue();

        if (bpm > 0.0)
        {
            auto params = makeObject ({ { "clip", clipId }, { "follow_tempo", true }, { "source_bpm", bpm } });
            auto result = dispatcher.execute ("set_audio_clip", params);

            if (! result.ok)
                info.setText (result.error, juce::dontSendNotification);
        }

        update();
    };

    detectButton.setTooltip ("Detect the file's tempo, and make the clip follow the song's tempo");
    detectButton.onClick = [this] { detectTempo(); };

    songTempoButton.setTooltip ("Set the song's tempo to this file's tempo, so the clip plays at its own speed");
    songTempoButton.onClick = [this]
    {
        if (sourceBpm > 0.0)
            dispatcher.execute ("set_tempo", makeObject ({ { "bpm", std::round (sourceBpm * 100.0) / 100.0 } }));
    };

    setUpValueField (lengthValue, "The clip's length in beats. Double-click to type a new length: the clip is stretched to fit");
    lengthValue.onTextChange = [this]
    {
        auto beats = lengthValue.getText().retainCharacters ("0123456789.").getDoubleValue();

        if (beats > 0.0)
            apply ("stretch_to_beats", beats);
        else
            update();
    };

    halveButton.setTooltip ("Squeeze the clip into half as many beats (twice as fast)");
    halveButton.onClick = [this] { apply ("stretch_to_beats", lengthBeats / 2.0); };
    doubleButton.setTooltip ("Stretch the clip over twice as many beats (half as fast)");
    doubleButton.onClick = [this] { apply ("stretch_to_beats", lengthBeats * 2.0); };

    reverseButton.setClickingTogglesState (true);
    reverseButton.setTooltip ("Play the clip backwards");
    reverseButton.onClick = [this] { apply ("reverse", reverseButton.getToggleState()); };

    normalizeButton.setTooltip ("Set the gain so the file's loudest peak reaches 0 dB");
    normalizeButton.onClick = [this] { apply ("normalize", true); };

    samplerButton.setTooltip ("Make a new sampler channel that plays this file across the keys, at its own pitch on C3");
    samplerButton.onClick = [this] { makeSamplerChannel(); };

    for (auto* caption : { &pitchLabel, &fineLabel, &gainLabel, &modeLabel, &bpmLabel, &lengthLabel })
        setUpCaption (*caption);

    info.setFont (juce::FontOptions (12.0f));
    info.setColour (juce::Label::textColourId, juce::Colours::grey);
    info.setJustificationType (juce::Justification::centredRight);

    for (auto* c : std::initializer_list<juce::Component*> { &modeBox, &pitchKnob, &fineKnob, &gainKnob, &pitchLabel, &fineLabel,
                                                             &gainLabel, &modeLabel, &bpmLabel, &lengthLabel, &bpmValue, &lengthValue,
                                                             &followButton, &detectButton, &songTempoButton, &halveButton, &doubleButton,
                                                             &reverseButton, &normalizeButton, &samplerButton, &info })
        addAndMakeVisible (c);
}

SampleEditor::~SampleEditor() = default;

//==============================================================================
void SampleEditor::setClip (const juce::String& newClipId)
{
    clipId = newClipId;
    filePath = {};
    thumbnail.reset();
    dragEdge = Edge::none;
    info.setText ({}, juce::dontSendNotification);
    update();
}

void SampleEditor::clear()
{
    clipId = {};
    thumbnail.reset();
    dragEdge = Edge::none;
    repaint();
}

te::WaveAudioClip* SampleEditor::getWaveClip() const
{
    return clipId.isEmpty() ? nullptr : dynamic_cast<te::WaveAudioClip*> (session.findClip (clipId));
}

bool SampleEditor::update()
{
    auto clip = getWaveClip();

    if (clip == nullptr)
    {
        clear();
        return false;
    }

    // Mid-drag the preview is the truth; the drag's own command re-reads everything after.
    if (dragEdge != Edge::none)
        return true;

    auto& tempo = session.getEdit().tempoSequence;
    auto pos = clip->getPosition();
    auto file = clip->getOriginalFile();
    startBeat = tempo.toBeats (pos.getStart()).inBeats();
    lengthBeats = tempo.toBeats (pos.getEnd()).inBeats() - startBeat;
    sourceStartBeat = session.getSourceStartBeat (*clip);
    followTempo = clip->getAutoTempo();
    sourceBpm = followTempo ? clip->getLoopInfo().getBpm (clip->getAudioFile().getInfo()) : 0.0;
    speed = session.getPlaybackSpeed (*clip);
    reversed = clip->getIsReversed();
    missing = ! file.existsAsFile();

    if (auto track = clip->getTrack())
        colour = track->getColour();

    if (file.getFullPathName() != filePath)
    {
        filePath = file.getFullPathName();
        fileName = file.getFileName();
        thumbnail.reset();

        te::AudioFile audio (session.getEngine(), file);
        auto audioInfo = audio.getInfo();
        fileSeconds = audio.getLength();
        sampleRate = audioInfo.sampleRate;
        numChannels = audioInfo.numChannels;

        if (! missing)
            thumbnail = std::make_unique<te::SmartThumbnail> (session.getEngine(), audio, *this, nullptr);
    }

    // The controls show the clip, without running commands.
    auto mode = Session::getStretchMode (*clip);
    modeBox.setSelectedId (mode == "beats" ? 2 : mode == "resample" ? 3 : 1, juce::dontSendNotification);
    modeBox.setItemEnabled (3, ! followTempo);

    auto pitch = Session::getPitchChange (*clip);
    auto semitones = std::round (pitch);
    pitchKnob.setValue (semitones, juce::dontSendNotification);
    fineKnob.setValue (std::round ((pitch - semitones) * 100.0), juce::dontSendNotification);
    gainKnob.setValue (clip->getGainDB(), juce::dontSendNotification);

    followButton.setToggleState (followTempo, juce::dontSendNotification);
    followButton.setEnabled (mode != "resample");
    detectButton.setEnabled (mode != "resample" && ! missing);
    songTempoButton.setEnabled (followTempo && sourceBpm > 0.0);
    bpmValue.setEnabled (mode != "resample");
    bpmValue.setText (followTempo ? formatNumber (sourceBpm, 2) : juce::String::fromUTF8 ("\xe2\x80\x93"), juce::dontSendNotification);
    lengthValue.setText (formatNumber (lengthBeats, 3), juce::dontSendNotification);
    reverseButton.setToggleState (reversed, juce::dontSendNotification);

    for (auto* c : std::initializer_list<juce::Component*> { &normalizeButton, &samplerButton, &reverseButton })
        c->setEnabled (! missing);

    repaint();
    return true;
}

void SampleEditor::apply (const juce::String& property, const juce::var& value)
{
    if (clipId.isEmpty())
        return;

    auto params = makeObject ({ { "clip", clipId } });
    params.getDynamicObject()->setProperty (property, value);
    auto result = dispatcher.execute ("set_audio_clip", params);
    info.setText (result.ok ? juce::String() : result.error.fromFirstOccurrenceOf ("set_audio_clip: ", false, false),
                  juce::dontSendNotification);

    // A refused change puts the controls back.
    if (! result.ok)
        update();
}

void SampleEditor::detectTempo()
{
    auto result = dispatcher.execute ("analyze_audio", makeObject ({ { "clip", clipId } }));

    if (! result.ok || ! result.value.hasProperty ("bpm"))
    {
        info.setText ("No steady tempo found; type the file's tempo instead", juce::dontSendNotification);
        return;
    }

    auto bpm = static_cast<double> (result.value["bpm"]);
    auto set = dispatcher.execute ("set_audio_clip", makeObject ({ { "clip", clipId }, { "follow_tempo", true }, { "source_bpm", bpm } }));
    info.setText (set.ok ? "Detected " + formatNumber (bpm, 2) + " bpm (" + result.value["beats"].toString() + " beats)"
                         : set.error.fromFirstOccurrenceOf ("set_audio_clip: ", false, false),
                  juce::dontSendNotification);
}

void SampleEditor::makeSamplerChannel()
{
    juce::Array<juce::var> commands;
    commands.add (makeObject ({ { "command", "create_track" },
                                { "params", makeObject ({ { "name", juce::File (filePath).getFileNameWithoutExtension() },
                                                          { "instrument", "sampler" } }) } }));
    commands.add (makeObject ({ { "command", "add_sample" },
                                { "params", makeObject ({ { "channel", "$0.channel" }, { "file", filePath }, { "note", 60 },
                                                          { "min_note", 0 }, { "max_note", 127 }, { "one_shot", false } }) } }));
    auto result = dispatcher.executeBatch ("Sampler from " + fileName, commands);
    info.setText (result.ok ? "Made a sampler channel: play it from the keys" : result.error, juce::dontSendNotification);
}

//==============================================================================
double SampleEditor::fileSecondsToBeat (double seconds) const
{
    if (followTempo && sourceBpm > 0.0)
        return sourceStartBeat + seconds * sourceBpm / 60.0;

    auto& tempo = session.getEdit().tempoSequence;
    auto sourceStart = tempo.toTime (te::BeatPosition::fromBeats (sourceStartBeat));
    return tempo.toBeats (sourceStart + te::TimeDuration::fromSeconds (seconds / std::max (0.01, speed))).inBeats();
}

double SampleEditor::beatToFileSeconds (double beat) const
{
    if (followTempo && sourceBpm > 0.0)
        return (beat - sourceStartBeat) * 60.0 / sourceBpm;

    auto& tempo = session.getEdit().tempoSequence;
    auto sourceStart = tempo.toTime (te::BeatPosition::fromBeats (sourceStartBeat));
    return (tempo.toTime (te::BeatPosition::fromBeats (beat)) - sourceStart).inSeconds() * speed;
}

juce::Rectangle<int> SampleEditor::getWaveArea() const
{
    return getLocalBounds().withTrimmedTop (controlsHeight).reduced (10, 8);
}

float SampleEditor::secondsToX (double seconds) const
{
    auto area = getWaveArea().toFloat();
    return area.getX() + (float) (seconds / std::max (0.001, fileSeconds)) * area.getWidth();
}

double SampleEditor::xToSeconds (float x) const
{
    auto area = getWaveArea().toFloat();
    return (x - area.getX()) / std::max (1.0f, area.getWidth()) * fileSeconds;
}

//==============================================================================
void SampleEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    g.setColour (theme::panelHeader);
    g.fillRect (getLocalBounds().removeFromTop (controlsHeight));
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillRect (0, controlsHeight - 1, getWidth(), 1);

    auto area = getWaveArea();
    g.setColour (juce::Colours::black.withAlpha (0.3f));
    g.fillRoundedRectangle (area.toFloat(), 4.0f);

    if (clipId.isEmpty())
        return;

    if (missing || thumbnail == nullptr)
    {
        g.setColour (juce::Colour (0xffff8a80));
        g.setFont (juce::FontOptions (14.0f));
        g.drawText ("File missing: " + filePath, area, juce::Justification::centred, true);
        return;
    }

    // The part the clip plays, lit; dragging its edges trims the clip.
    auto& tempo = session.getEdit().tempoSequence;
    auto playStart = dragEdge == Edge::start ? dragBeat : startBeat;
    auto playEnd = dragEdge == Edge::end ? dragBeat : startBeat + lengthBeats;
    auto x1 = juce::jlimit ((float) area.getX(), (float) area.getRight(), secondsToX (beatToFileSeconds (playStart)));
    auto x2 = juce::jlimit (x1, (float) area.getRight(), secondsToX (beatToFileSeconds (playEnd)));
    auto lit = juce::Rectangle<float> (x1, (float) area.getY(), x2 - x1, (float) area.getHeight());
    g.setColour (colour.withAlpha (0.25f));
    g.fillRect (lit);

    // The song's beats over the file, with the bars brighter, so the file can be lined up.
    auto firstBeat = std::ceil (fileSecondsToBeat (0.0));
    auto lastBeat = fileSecondsToBeat (fileSeconds);
    auto beatsPerBar = std::max (1, tempo.getTimeSigAt (tempo.toTime (te::BeatPosition::fromBeats (startBeat))).numerator.get());
    auto pixelsPerBeat = area.getWidth() / std::max (0.001, lastBeat - fileSecondsToBeat (0.0));

    for (auto beat = firstBeat; beat <= lastBeat && pixelsPerBeat > 1.0; beat += 1.0)
    {
        auto isBar = (juce::int64) beat % beatsPerBar == 0;

        if (! isBar && pixelsPerBeat < 8.0)
            continue;

        auto x = secondsToX (beatToFileSeconds (beat));
        g.setColour (juce::Colours::white.withAlpha (isBar ? 0.22f : 0.08f));
        g.fillRect (x, (float) area.getY(), 1.0f, (float) area.getHeight());

        if (isBar && pixelsPerBeat * beatsPerBar > 30.0)
        {
            g.setFont (juce::FontOptions (10.0f));
            g.drawText (juce::String ((int) (beat / beatsPerBar) + 1), juce::Rectangle<float> (x + 3.0f, (float) area.getY() + 2.0f, 40.0f, 12.0f),
                        juce::Justification::centredLeft, false);
        }
    }

    // The waveform: the whole file, flipped when the clip plays it backwards, sized by the gain.
    {
        juce::Graphics::ScopedSaveState saved (g);

        if (reversed)
            g.addTransform (juce::AffineTransform::scale (-1.0f, 1.0f, area.toFloat().getCentreX(), 0.0f));

        auto zoom = juce::jlimit (0.05f, 4.0f, juce::Decibels::decibelsToGain ((float) gainKnob.getValue()));
        te::TimeRange range { te::TimePosition(), te::TimePosition::fromSeconds (fileSeconds) };
        g.setColour (colour.brighter (1.0f).interpolatedWith (juce::Colours::white, 0.3f));

        if (thumbnail->getNumChannels() < 2 || area.getHeight() < 90)
            for (int ch = 0; ch < std::max (1, thumbnail->getNumChannels()); ++ch)
                thumbnail->drawChannel (g, area, range, ch, zoom);
        else
            thumbnail->drawChannels (g, area, range, zoom);
    }

    // Outside the clip is dimmed; its edges are handles.
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRect (juce::Rectangle<float> ((float) area.getX(), (float) area.getY(), x1 - (float) area.getX(), (float) area.getHeight()));
    g.fillRect (juce::Rectangle<float> (x2, (float) area.getY(), (float) area.getRight() - x2, (float) area.getHeight()));
    g.setColour (colour.brighter (0.6f));
    g.fillRect (x1 - 1.0f, (float) area.getY(), 2.0f, (float) area.getHeight());
    g.fillRect (x2 - 1.0f, (float) area.getY(), 2.0f, (float) area.getHeight());

    if (playheadX >= (float) area.getX() && playheadX <= (float) area.getRight())
    {
        g.setColour (theme::play);
        g.fillRect (playheadX, (float) area.getY(), 1.5f, (float) area.getHeight());
    }

    // What the file is.
    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.setFont (juce::FontOptions (11.0f));
    auto details = fileName + "   " + formatNumber (fileSeconds, 2) + " s   " + formatNumber (sampleRate / 1000.0, 1) + " kHz   "
                     + (numChannels == 1 ? "mono" : numChannels == 2 ? "stereo" : juce::String (numChannels) + " channels")
                     + (std::abs (speed - 1.0) > 0.0001 ? "   plays at " + formatNumber (speed, 3) + juce::String::fromUTF8 ("\xc3\x97") : juce::String());
    g.drawText (details, area.reduced (6, 4).removeFromBottom (14), juce::Justification::bottomLeft, true);
}

void SampleEditor::resized()
{
    auto r = getLocalBounds().removeFromTop (controlsHeight).reduced (10, 6);
    auto captions = r.removeFromTop (14);
    auto controls = r.withTrimmedTop (4);

    auto place = [&] (juce::Label& caption, int width) -> juce::Rectangle<int>
    {
        caption.setBounds (captions.removeFromLeft (width));
        captions.removeFromLeft (10);
        auto area = controls.removeFromLeft (width);
        controls.removeFromLeft (10);
        return area;
    };

    modeBox.setBounds (place (modeLabel, 104));
    pitchKnob.setBounds (place (pitchLabel, 92));
    fineKnob.setBounds (place (fineLabel, 92));
    gainKnob.setBounds (place (gainLabel, 98));

    {
        auto area = place (bpmLabel, 272);
        followButton.setBounds (area.removeFromLeft (104));
        bpmValue.setBounds (area.removeFromLeft (46).reduced (0, 2));
        area.removeFromLeft (4);
        detectButton.setBounds (area.removeFromLeft (56));
        area.removeFromLeft (4);
        songTempoButton.setBounds (area);
    }

    {
        auto area = place (lengthLabel, 130);
        lengthValue.setBounds (area.removeFromLeft (54).reduced (0, 2));
        area.removeFromLeft (4);
        halveButton.setBounds (area.removeFromLeft (34));
        area.removeFromLeft (4);
        doubleButton.setBounds (area.removeFromLeft (34));
    }

    reverseButton.setBounds (controls.removeFromLeft (70));
    controls.removeFromLeft (6);
    normalizeButton.setBounds (controls.removeFromLeft (80));
    controls.removeFromLeft (6);
    samplerButton.setBounds (controls.removeFromLeft (70));
    info.setBounds (captions.withLeft (std::max (captions.getX(), getWidth() / 2)));
}

void SampleEditor::updatePlayhead()
{
    auto x = -1.0f;

    if (getWaveClip() != nullptr && fileSeconds > 0.0)
    {
        auto& edit = session.getEdit();
        auto beat = edit.tempoSequence.toBeats (edit.getTransport().getPosition()).inBeats();

        if (edit.getTransport().isPlaying() && beat >= startBeat && beat <= startBeat + lengthBeats)
            x = secondsToX (beatToFileSeconds (beat));
    }

    if (std::abs (x - playheadX) > 0.25f)
    {
        playheadX = x;
        repaint (getWaveArea());
    }
}

//==============================================================================
SampleEditor::Edge SampleEditor::edgeAt (juce::Point<float> p) const
{
    if (clipId.isEmpty() || missing || ! getWaveArea().toFloat().contains (p))
        return Edge::none;

    auto x1 = secondsToX (beatToFileSeconds (startBeat));
    auto x2 = secondsToX (beatToFileSeconds (startBeat + lengthBeats));

    if (std::abs (p.x - x1) < 6.0f)  return Edge::start;
    if (std::abs (p.x - x2) < 6.0f)  return Edge::end;
    return Edge::none;
}

void SampleEditor::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (edgeAt (e.position) != Edge::none ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}

void SampleEditor::mouseDown (const juce::MouseEvent& e)
{
    dragEdge = edgeAt (e.position);
    dragBeat = dragEdge == Edge::start ? startBeat : startBeat + lengthBeats;
}

void SampleEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (dragEdge == Edge::none)
        return;

    // Within the file, at least a 16th long, snapped to 16ths unless Alt is held.
    auto beat = fileSecondsToBeat (juce::jlimit (0.0, fileSeconds, xToSeconds (e.position.x)));

    if (! e.mods.isAltDown())
        beat = std::round (beat * 4.0) / 4.0;

    auto fileStart = fileSecondsToBeat (0.0), fileEnd = fileSecondsToBeat (fileSeconds);
    dragBeat = dragEdge == Edge::start ? juce::jlimit (std::max (0.0, fileStart), startBeat + lengthBeats - 0.25, beat)
                                       : juce::jlimit (startBeat + 0.25, fileEnd, beat);
    repaint();
}

void SampleEditor::mouseUp (const juce::MouseEvent&)
{
    if (dragEdge == Edge::none)
        return;

    auto edge = dragEdge;
    dragEdge = Edge::none;
    auto moved = std::abs (dragBeat - (edge == Edge::start ? startBeat : startBeat + lengthBeats)) > 1.0e-6;

    if (moved)
        dispatcher.execute ("resize_clip", makeObject ({ { "clip", clipId },
                                                         { edge == Edge::start ? "start_beat" : "end_beat", dragBeat } }));
    update();
}

} // namespace ng
