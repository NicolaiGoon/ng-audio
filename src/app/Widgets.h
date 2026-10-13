#pragma once

#include "core/CommandDispatcher.h"

// Small UI pieces shared by the channel rack and the mixer. Like the rest of the UI,
// they change the project only by running commands.

namespace ng
{

inline juce::var makeObject (std::initializer_list<std::pair<const char*, juce::var>> props)
{
    auto obj = new juce::DynamicObject();

    for (auto& [k, v] : props)
        obj->setProperty (k, v);

    return juce::var (obj);
}

inline juce::Colour colourFrom (const juce::var& v)
{
    return juce::Colour::fromString ("ff" + v.toString().removeCharacters ("#"));
}

//==============================================================================
/** A colour chip that opens the track palette; picking a colour runs set_track_colour. */
class ColourSwatch  : public juce::Component,
                      public juce::SettableTooltipClient
{
public:
    ColourSwatch (CommandDispatcher& d, juce::String trackId, juce::Colour c)
        : dispatcher (d), id (std::move (trackId)), colour (c)
    {
        setTooltip ("Change colour");
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (colour);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 3.0f);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        juce::PopupMenu menu;
        auto palette = Session::getPalette();

        for (int i = 0; i < palette.size(); ++i)
            menu.addColouredItem (i + 1, juce::String::fromUTF8 ("\xe2\x96\xa0  ") + "#" + palette[i].toDisplayString (false),
                                  palette[i], true, palette[i] == colour);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                            [&d = dispatcher, trackId = id, palette] (int result)
                            {
                                if (result > 0)
                                    d.execute ("set_track_colour", makeObject ({ { "track", trackId },
                                                                                 { "colour", "#" + palette[result - 1].toDisplayString (false) } }));
                            });
    }

private:
    CommandDispatcher& dispatcher;
    juce::String id;
    juce::Colour colour;
};

/** A name you can double-click to edit; committing the edit runs rename_track. */
inline void setUpNameLabel (juce::Label& label, CommandDispatcher& dispatcher, const juce::String& trackId, const juce::String& name)
{
    label.setText (name, juce::dontSendNotification);
    label.setEditable (false, true, false);
    label.setTooltip ("Double-click to rename");
    label.onTextChange = [&label, &dispatcher, trackId]
    {
        dispatcher.execute ("rename_track", makeObject ({ { "track", trackId }, { "name", label.getText() } }));
    };
}

inline void setUpDeleteButton (juce::TextButton& button, CommandDispatcher& dispatcher, const juce::String& trackId, const juce::String& what)
{
    button.setButtonText (juce::String::fromUTF8 ("\xc3\x97"));
    button.setTooltip ("Delete " + what);
    button.onClick = [&dispatcher, trackId]
    {
        dispatcher.execute ("delete_track", makeObject ({ { "track", trackId } }));
    };
}

} // namespace ng
