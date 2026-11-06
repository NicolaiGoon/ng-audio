#pragma once

#include "Theme.h"

namespace ng
{

/**
    A panel of the main window (the timeline, the channel rack, the mixer or the piano roll):
    a header with its title, its own buttons and a detach button, over its content.

    Detaching moves the panel into a window of its own, which can be moved to another screen
    and resized; the attach button (or closing that window) puts it back. Whether each panel is
    detached, and where its window was, is remembered for next time.

    Keys pressed in a detached window go to keyListener, so the app's shortcuts (and the typing
    keyboard) work there too.
*/
class DockPanel  : public juce::Component
{
public:
    DockPanel (const juce::String& id, const juce::String& title, icons::Icon, juce::Component& content,
               juce::PropertiesFile&, juce::KeyListener* keyListener);
    ~DockPanel() override;

    void setTitle (const juce::String&);
    const juce::String& getId() const noexcept  { return id; }

    /** Adds a component to the header, after the title (or at the right-hand end). */
    void addHeaderItem (juce::Component&, int width, bool atRight = false);

    bool isDetached() const noexcept            { return window != nullptr; }
    void setDetached (bool);

    /** Whether the panel has anything to show (the piano roll only does while a clip is open).
        A detached panel's window hides while it doesn't. */
    void setActive (bool);
    bool isActive() const noexcept              { return active; }

    /** Brings a detached panel's window to the front. */
    void toFront();

    /** Called after the panel is detached or attached, so the main window can lay itself out again. */
    std::function<void()> onDetachChanged;

    juce::Component& getContent() noexcept      { return content; }

    static constexpr int headerHeight = 30;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    class Window;

    struct HeaderItem
    {
        juce::Component* component;
        int width;
        bool atRight;
    };

    juce::String id, title;
    icons::Icon icon;
    juce::Component& content;
    juce::PropertiesFile& properties;
    juce::KeyListener* keyListener;
    juce::Array<HeaderItem> headerItems;
    IconButton detachButton { "Detach into its own window", icons::Icon::detach };
    std::unique_ptr<Window> window;
    juce::Component::SafePointer<juce::Component> home;
    bool active = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DockPanel)
};

//==============================================================================
/** A bar between two panels that can be dragged to change how much room each gets.
    Double-click it to go back to the default. */
class SplitterBar  : public juce::Component
{
public:
    explicit SplitterBar (bool isVertical);

    /** Called while dragging, with how far the bar has moved since the drag started. */
    std::function<void (int deltaSinceStart)> onDrag;
    std::function<void()> onDragStart, onDragEnd, onReset;

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override  { repaint(); }
    void mouseExit (const juce::MouseEvent&) override   { repaint(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    bool vertical, dragging = false;
};

} // namespace ng
