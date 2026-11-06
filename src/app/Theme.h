#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

// The app's look: its colours, a set of icons drawn as paths, a look-and-feel that gives
// buttons, knobs, faders, menus and scroll bars a flat, rounded style, and an icon button
// that fades its hover highlight in and out.

namespace ng
{

namespace theme
{
    /** Accent colours shared across the app. */
    const juce::Colour accent (0xff4a90e2), play (0xff47c46b), record (0xffe0533f),
                       panel (0xff23272c), panelHeader (0xff1d2025), outline (0xff3a4048);
}

//==============================================================================
/** Icons, drawn in a 24 x 24 box. Outline icons are stroked, the rest filled. */
namespace icons
{
    enum class Icon
    {
        play, stop, record, toStart, loop, metronome, undo, redo, settings, typingKeys, piano,
        newFile, open, save, detach, attach, zoomIn, zoomOut, zoomInVertical, zoomOutVertical,
        plus, browser, mixer, rack, timeline, exportAudio, history, console, close, help, plugins, audio
    };

    juce::Path get (Icon);
    bool isFilled (Icon);

    /** Draws the icon fitted into the area. */
    void draw (juce::Graphics&, Icon, juce::Rectangle<float> area, juce::Colour);
}

//==============================================================================
/** Flat, rounded controls on top of JUCE's dark colour schemes (which the theme setting picks). */
class NgLookAndFeel  : public juce::LookAndFeel_V4
{
public:
    NgLookAndFeel();

    /** Sets the accent colours again, e.g. after a colour scheme replaced them. */
    void applyAccents();

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool isMouseOverButton, bool isButtonDown) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool isMouseOverButton, bool isButtonDown) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPosProportional,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;

    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height, bool isScrollbarVertical,
                        int thumbStartPosition, int thumbSize, bool isMouseOver, bool isMouseDown) override;
    int getDefaultScrollbarWidth() override     { return 10; }

    void drawMenuBarBackground (juce::Graphics&, int width, int height, bool isMouseOverBar, juce::MenuBarComponent&) override;
    void drawMenuBarItem (juce::Graphics&, int width, int height, int itemIndex, const juce::String& itemText,
                          bool isMouseOverItem, bool isMenuOpen, bool isMouseOverBar, juce::MenuBarComponent&) override;
    juce::Font getMenuBarFont (juce::MenuBarComponent&, int itemIndex, const juce::String& itemText) override;
    juce::Font getPopupMenuFont() override;

    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
};

//==============================================================================
/** A button showing an icon (and optionally a label beside it). When it's a toggle, the
    "on" colour fills it. The hover highlight fades rather than snapping on and off. */
class IconButton  : public juce::Button,
                    private juce::Timer
{
public:
    IconButton (const juce::String& name, icons::Icon, const juce::String& label = {});

    void setIcon (icons::Icon);
    void setLabel (const juce::String&);

    /** The colour the button fills with while it's toggled on (or the icon's colour when
        tintIconOnly is set, e.g. a red record dot). */
    void setOnColour (juce::Colour c, bool tintIconOnly = false);
    void setIconColour (juce::Colour c)             { iconColour = c; repaint(); }

    /** The width the button wants for its icon and label at this height. */
    int getIdealWidth (int height) const;

    void paintButton (juce::Graphics&, bool isMouseOver, bool isButtonDown) override;

private:
    void buttonStateChanged() override;
    void timerCallback() override;

    icons::Icon icon;
    juce::String label;
    juce::Colour onColour { theme::accent }, iconColour;
    bool tintOnly = false;
    float hover = 0.0f;
};

} // namespace ng
