#include "MixerView.h"
#include "Widgets.h"

namespace ng
{
namespace
{
    constexpr int columnWidth = 84;
    constexpr double minDb = -100.0, maxDb = 6.0;

    /** The project state with the values the columns update in place taken out. If this
        stays the same between updates, the columns can stay too. */
    juce::String getStructureKey (const juce::var& state)
    {
        juce::StringArray parts;

        for (auto& s : *state["strips"].getArray())
            parts.add (s["id"].toString() + "|" + s["name"].toString() + "|" + s["colour"].toString()
                         + "|" + juce::String (s["inputs"].size()));

        return parts.joinIntoString (";");
    }
}

//==============================================================================
/** A stereo peak meter fed by a Tracktion level meter plugin. */
class MixerView::Meter  : public juce::Component,
                          private juce::Timer
{
public:
    explicit Meter (te::LevelMeterPlugin* plugin)
        : meterPlugin (plugin), keepAlive (plugin)
    {
        if (meterPlugin != nullptr)
            meterPlugin->measurer.addClient (client);

        startTimerHz (30);
    }

    ~Meter() override
    {
        if (meterPlugin != nullptr)
            meterPlugin->measurer.removeClient (client);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillRoundedRectangle (r, 2.0f);

        auto barWidth = (r.getWidth() - 3.0f) / 2.0f;

        for (int ch = 0; ch < 2; ++ch)
        {
            auto proportion = juce::jlimit (0.0f, 1.0f, (levels[ch] + 60.0f) / 66.0f);   // -60 dB to +6 dB
            auto bar = juce::Rectangle<float> (r.getX() + 1.0f + ch * (barWidth + 1.0f), r.getY() + 1.0f,
                                               barWidth, r.getHeight() - 2.0f);
            auto lit = bar.withTop (bar.getBottom() - bar.getHeight() * proportion);

            g.setColour (levels[ch] > 0.0f ? juce::Colours::red
                                           : levels[ch] > -6.0f ? juce::Colours::orange
                                                                : juce::Colour (0xff4caf50));
            g.fillRect (lit);
        }
    }

private:
    void timerCallback() override
    {
        bool changed = false;

        for (int ch = 0; ch < 2; ++ch)
        {
            auto newLevel = client.getAndClearAudioLevel (ch).dB;
            auto decayed = std::max (-100.0f, levels[ch] - 1.5f);
            auto level = std::max (newLevel, decayed);

            if (std::abs (level - levels[ch]) > 0.01f)
            {
                levels[ch] = level;
                changed = true;
            }
        }

        if (changed)
            repaint();
    }

    te::LevelMeterPlugin* meterPlugin;
    te::Plugin::Ptr keepAlive;   // the strip can be deleted before this meter is
    te::LevelMeasurer::Client client;
    float levels[2] = { -100.0f, -100.0f };
};

//==============================================================================
/** One mixer strip, or the master strip when id is "master". */
class MixerView::StripColumn  : public juce::Component
{
public:
    StripColumn (CommandDispatcher& d, const juce::var& strip, te::LevelMeterPlugin* meterPlugin)
        : dispatcher (d), id (strip["id"].toString()), isMaster (id == "master"),
          colour (isMaster ? juce::Colours::grey : colourFrom (strip["colour"])),
          swatch (d, id, colour), meter (meterPlugin)
    {
        if (isMaster)
        {
            name.setText ("Master", juce::dontSendNotification);
            name.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        }
        else
        {
            setUpNameLabel (name, dispatcher, id, strip["name"].toString());
            setUpDeleteButton (deleteButton, dispatcher, id, "mixer strip");

            auto numInputs = strip["inputs"].size();
            inputs.setText (juce::String (numInputs) + (numInputs == 1 ? " input" : " inputs"), juce::dontSendNotification);
            inputs.setColour (juce::Label::textColourId, juce::Colours::grey);
            inputs.setJustificationType (juce::Justification::centred);
            inputs.setFont (juce::FontOptions (12.0f));

            mute.setClickingTogglesState (false);
            solo.setClickingTogglesState (false);
            mute.setColour (juce::TextButton::buttonOnColourId, juce::Colours::orange.darker());
            solo.setColour (juce::TextButton::buttonOnColourId, juce::Colours::gold.darker());
            mute.setTooltip ("Mute");
            solo.setTooltip ("Solo");
            mute.onClick = [this] { send ({ { "mute", ! mute.getToggleState() } }); };
            solo.onClick = [this] { send ({ { "solo", ! solo.getToggleState() } }); };

            for (auto* c : std::initializer_list<juce::Component*> { &swatch, &inputs, &mute, &solo, &deleteButton })
                addAndMakeVisible (c);
        }

        name.setJustificationType (juce::Justification::centred);

        pan.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        pan.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        pan.setRange (-1.0, 1.0, 0.01);
        pan.setDoubleClickReturnValue (true, 0.0);
        pan.setTooltip ("Pan (double-click to centre)");
        pan.setPopupDisplayEnabled (true, true, nullptr);
        connect (pan, "Pan", "pan");

        fader.setSliderStyle (juce::Slider::LinearVertical);
        fader.setTextBoxStyle (juce::Slider::TextBoxBelow, false, columnWidth - 8, 18);
        fader.setRange (minDb, maxDb, 0.1);
        fader.setSkewFactorFromMidPoint (-12.0);
        fader.setDoubleClickReturnValue (true, 0.0);
        fader.setTextValueSuffix (" dB");
        fader.setTooltip ("Volume (double-click for 0 dB)");
        connect (fader, "Fader", "volume_db");

        for (auto* c : std::initializer_list<juce::Component*> { &name, &pan, &fader, &meter })
            addAndMakeVisible (c);

        setValues (strip);
    }

    /** Shows new values without sending them back as commands. */
    void setValues (const juce::var& strip)
    {
        if (! fader.isMouseButtonDown())
            fader.setValue (strip["volume_db"], juce::dontSendNotification);

        if (! pan.isMouseButtonDown())
            pan.setValue (strip["pan"], juce::dontSendNotification);

        mute.setToggleState (strip["mute"], juce::dontSendNotification);
        solo.setToggleState (strip["solo"], juce::dontSendNotification);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().reduced (2, 0).toFloat();
        g.setColour (colour.withAlpha (0.18f));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (colour);
        g.fillRoundedRectangle (r.removeFromTop (4.0f), 2.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6, 8);

        auto top = r.removeFromTop (22);

        if (! isMaster)
        {
            swatch.setBounds (top.removeFromLeft (12).reduced (0, 4));
            top.removeFromLeft (2);
        }

        name.setBounds (top);
        r.removeFromTop (4);

        pan.setBounds (r.removeFromTop (40).withSizeKeepingCentre (40, 40));
        r.removeFromTop (4);

        if (! isMaster)
        {
            auto buttons = r.removeFromTop (22);
            mute.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).reduced (1, 0));
            solo.setBounds (buttons.reduced (1, 0));
            r.removeFromTop (4);

            auto bottom = r.removeFromBottom (22);
            deleteButton.setBounds (bottom.removeFromRight (22));
            inputs.setBounds (bottom);
            r.removeFromBottom (4);
        }

        auto meterArea = r.removeFromRight (12).withTrimmedBottom (22);
        meter.setBounds (meterArea);
        fader.setBounds (r);
    }

private:
    void send (std::initializer_list<std::pair<const char*, juce::var>> values)
    {
        auto params = makeObject ({ { "strip", id } });

        for (auto& [k, v] : values)
            params.getDynamicObject()->setProperty (k, v);

        dispatcher.execute ("set_mixer_strip", params);
    }

    /** A drag is one undo step; clicks, double-clicks and the text box are one each. */
    void connect (juce::Slider& slider, const juce::String& undoLabel, const char* param)
    {
        slider.onDragStart = [this, undoLabel] { dispatcher.beginGesture (undoLabel); };
        slider.onDragEnd = [this] { dispatcher.endGesture(); };
        slider.onValueChange = [this, &slider, param] { send ({ { param, slider.getValue() } }); };
    }

    CommandDispatcher& dispatcher;
    juce::String id;
    bool isMaster;
    juce::Colour colour;

    ColourSwatch swatch;
    juce::Label name, inputs;
    juce::Slider pan, fader;
    juce::TextButton mute { "M" }, solo { "S" }, deleteButton;
    Meter meter;
};

//==============================================================================
MixerView::MixerView (Session& s, CommandDispatcher& d)
    : session (s), dispatcher (d)
{
    viewport.setViewedComponent (&columnHolder, false);
    viewport.setScrollBarsShown (false, true);
    addAndMakeVisible (viewport);
}

MixerView::~MixerView()
{
    dispatcher.endGesture();
}

void MixerView::update (const juce::var& state)
{
    auto key = getStructureKey (state);
    auto strips = state["strips"];

    if (key == structureKey && columns.size() == strips.size() && masterColumn != nullptr)
    {
        for (int i = 0; i < columns.size(); ++i)
            columns[i]->setValues (strips[i]);

        masterColumn->setValues (state["master"]);
        return;
    }

    structureKey = key;
    columns.clear();

    for (auto& strip : *strips.getArray())
    {
        auto track = session.findTrack (strip["id"].toString(), Session::Role::strip);
        auto meterPlugin = track != nullptr ? track->getLevelMeterPlugin() : nullptr;
        columnHolder.addAndMakeVisible (columns.add (new StripColumn (dispatcher, strip, meterPlugin)));
    }

    auto master = state["master"].clone();

    if (auto obj = master.getDynamicObject())
        obj->setProperty ("id", "master");

    masterColumn = std::make_unique<StripColumn> (dispatcher, master, session.getMasterMeter());
    addAndMakeVisible (*masterColumn);

    resized();
}

void MixerView::resized()
{
    auto r = getLocalBounds();

    if (masterColumn != nullptr)
        masterColumn->setBounds (r.removeFromRight (columnWidth));

    r.removeFromRight (6);
    viewport.setBounds (r);

    auto height = r.getHeight() - (columns.size() * columnWidth > r.getWidth() ? viewport.getScrollBarThickness() : 0);
    columnHolder.setSize (columns.size() * columnWidth, height);

    for (int i = 0; i < columns.size(); ++i)
        columns[i]->setBounds (i * columnWidth, 0, columnWidth, height);
}

} // namespace ng
