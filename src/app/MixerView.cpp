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
    juce::String getEffectsKey (const juce::var& strip)
    {
        juce::String key;
        auto effects = strip["effects"];

        for (auto& e : *effects.getArray())
            key << e["id"].toString() << ":" << e["name"].toString() << ":" << (static_cast<bool> (e["enabled"]) ? "on" : "off")
                << (e.hasProperty ("missing") ? ":missing" : "") << ",";

        return key;
    }

    juce::String getStructureKey (const juce::var& state)
    {
        juce::StringArray parts;
        auto strips = state["strips"];

        for (auto& s : *strips.getArray())
            parts.add (s["id"].toString() + "|" + s["name"].toString() + "|" + s["colour"].toString()
                         + "|" + juce::String (s["inputs"].size()) + "|" + getEffectsKey (s));

        parts.add ("master|" + getEffectsKey (state["master"]));
        return parts.joinIntoString (";");
    }

    void showError (const juce::String& error)
    {
        juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOk (juce::MessageBoxIconType::WarningIcon,
                                                                              "Effect", error),
                                      nullptr);
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
/** A strip's insert effects, top to bottom in the order they run, and a button to add one.
    Clicking an effect opens its window; right-clicking it offers bypass, move and remove. */
class MixerView::EffectSlots  : public juce::Component
{
public:
    EffectSlots (Session& s, CommandDispatcher& d, juce::String strip, const juce::var& effects)
        : session (s), dispatcher (d), stripId (std::move (strip))
    {
        for (int i = 0; i < effects.size(); ++i)
        {
            auto effect = effects[i];
            auto* slot = slots.add (new SlotButton (getShortName (effect)));
            auto enabled = static_cast<bool> (effect["enabled"]);
            auto missing = effect.hasProperty ("missing");

            slot->setColour (juce::TextButton::buttonColourId, enabled ? juce::Colour (0xff35506a) : juce::Colour (0xff2a2a2a));
            slot->setColour (juce::TextButton::textColourOffId, missing ? juce::Colours::indianred
                                                                        : enabled ? juce::Colours::white : juce::Colours::grey);
            slot->setTooltip (effect["name"].toString() + (missing ? " (not installed on this computer)"
                                                                   : enabled ? juce::String() : " (bypassed)")
                                + "\nClick to open, right-click for more");
            slot->onOpen = [this, i] { run ("show_effect_window", i); };
            slot->onMenu = [this, i, effect] { showSlotMenu (i, effect); };
            holder.addAndMakeVisible (slot);
        }

        addButton.setTooltip ("Add an effect");
        addButton.onClick = [this] { showAddMenu(); };

        viewport.setViewedComponent (&holder, false);
        viewport.setScrollBarsShown (true, false);
        viewport.setScrollBarThickness (6);
        addAndMakeVisible (viewport);
        addAndMakeVisible (addButton);
    }

    static constexpr int slotHeight = 18, visibleSlots = 3;
    static constexpr int preferredHeight = slotHeight * (visibleSlots + 1) + 2;

    void resized() override
    {
        auto r = getLocalBounds();
        addButton.setBounds (r.removeFromBottom (slotHeight));
        r.removeFromBottom (2);
        viewport.setBounds (r);

        auto width = r.getWidth() - (slots.size() > visibleSlots ? viewport.getScrollBarThickness() : 0);
        holder.setSize (width, slots.size() * slotHeight);

        for (int i = 0; i < slots.size(); ++i)
            slots[i]->setBounds (0, i * slotHeight, width, slotHeight - 1);
    }

    void paint (juce::Graphics& g) override
    {
        if (slots.isEmpty())
        {
            g.setColour (juce::Colours::grey.withAlpha (0.6f));
            g.setFont (juce::FontOptions (11.0f));
            g.drawText ("No effects", viewport.getBounds(), juce::Justification::centred);
        }
    }

private:
    /** A button that runs onOpen when clicked and onMenu when right-clicked. */
    struct SlotButton  : public juce::TextButton
    {
        using TextButton::TextButton;
        using TextButton::clicked;

        void clicked (const juce::ModifierKeys& mods) override
        {
            auto& fn = mods.isPopupMenu() ? onMenu : onOpen;

            if (fn != nullptr)
                fn();
        }

        std::function<void()> onOpen, onMenu;
    };

    static juce::String getShortName (const juce::var& effect)
    {
        for (auto& builtIn : Session::getBuiltInEffects())
            if (builtIn.id == effect["type"].toString())
                return builtIn.name;

        return effect["name"].toString();
    }

    void run (const juce::String& command, int index, std::initializer_list<std::pair<const char*, juce::var>> extra = {})
    {
        auto params = makeObject ({ { "strip", stripId }, { "effect", index } });

        for (auto& [k, v] : extra)
            params.getDynamicObject()->setProperty (k, v);

        auto result = dispatcher.execute (command, params);

        if (! result.ok)
            showError (result.error);
    }

    void showSlotMenu (int index, const juce::var& effect)
    {
        auto enabled = static_cast<bool> (effect["enabled"]);

        juce::PopupMenu menu;
        menu.addSectionHeader (effect["name"].toString());
        menu.addItem (1, "Open window", ! effect.hasProperty ("missing"));
        menu.addItem (2, enabled ? "Bypass" : "Turn on");
        menu.addItem (3, "Move up", index > 0);
        menu.addItem (4, "Move down", index < slots.size() - 1);
        menu.addSeparator();
        menu.addItem (5, "Remove");

        // The menu's actions rebuild the mixer, and this component with it.
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (slots[index]),
                            [safeThis = juce::Component::SafePointer<EffectSlots> (this), index, enabled] (int result)
                            {
                                if (safeThis == nullptr)
                                    return;

                                switch (result)
                                {
                                    case 1:  safeThis->run ("show_effect_window", index); break;
                                    case 2:  safeThis->run ("set_effect", index, { { "enabled", ! enabled } }); break;
                                    case 3:  safeThis->run ("move_effect", index, { { "index", index - 1 } }); break;
                                    case 4:  safeThis->run ("move_effect", index, { { "index", index + 1 } }); break;
                                    case 5:  safeThis->run ("remove_effect", index); break;
                                    default: break;
                                }
                            });
    }

    void showAddMenu()
    {
        juce::PopupMenu menu;
        juce::StringArray choices;

        menu.addSectionHeader ("Built in");

        for (auto& builtIn : Session::getBuiltInEffects())
        {
            choices.add (builtIn.id);
            menu.addItem (choices.size(), builtIn.name);
        }

        menu.addSectionHeader ("Plugins");

        auto types = session.getEdit().engine.getPluginManager().knownPluginList.getTypes();
        std::sort (types.begin(), types.end(), [] (auto& a, auto& b) { return a.name.compareNatural (b.name) < 0; });
        auto anyPlugins = false;

        for (auto& d : types)
        {
            if (d.isInstrument)
                continue;

            choices.add (d.createIdentifierString());
            menu.addItem (choices.size(), d.name + (d.manufacturerName.isNotEmpty() ? "  (" + d.manufacturerName + ")" : juce::String()));
            anyPlugins = true;
        }

        if (! anyPlugins)
            menu.addItem (-1, "None yet: scan in Plugins", false);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addButton),
                            [&d = dispatcher, strip = stripId, choices] (int result)
                            {
                                if (result <= 0)
                                    return;

                                auto added = d.execute ("add_effect", makeObject ({ { "strip", strip },
                                                                                    { "effect", choices[result - 1] } }));

                                if (! added.ok)
                                    showError (added.error);
                            });
    }

    Session& session;
    CommandDispatcher& dispatcher;
    juce::String stripId;

    juce::Viewport viewport;
    juce::Component holder;
    juce::OwnedArray<SlotButton> slots;
    juce::TextButton addButton { "+ FX" };
};

//==============================================================================
/** One mixer strip, or the master strip when id is "master". */
class MixerView::StripColumn  : public juce::Component
{
public:
    StripColumn (Session& s, CommandDispatcher& d, const juce::var& strip, te::LevelMeterPlugin* meterPlugin)
        : dispatcher (d), id (strip["id"].toString()), isMaster (id == "master"),
          colour (isMaster ? juce::Colours::grey : colourFrom (strip["colour"])),
          swatch (d, id, colour), effects (s, d, id, strip["effects"]), meter (meterPlugin)
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

        for (auto* c : std::initializer_list<juce::Component*> { &name, &pan, &effects, &fader, &meter })
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

        effects.setBounds (r.removeFromTop (EffectSlots::preferredHeight));
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
    EffectSlots effects;
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
        columnHolder.addAndMakeVisible (columns.add (new StripColumn (session, dispatcher, strip, meterPlugin)));
    }

    auto master = state["master"].clone();

    if (auto obj = master.getDynamicObject())
        obj->setProperty ("id", "master");

    masterColumn = std::make_unique<StripColumn> (session, dispatcher, master, session.getMasterMeter());
    addAndMakeVisible (*masterColumn);

    resized();
}

void MixerView::clear()
{
    dispatcher.endGesture();
    columns.clear();
    masterColumn = nullptr;
    structureKey = {};
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
