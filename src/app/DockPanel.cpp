#include "DockPanel.h"

namespace ng
{

//==============================================================================
/** The window a detached panel lives in. Closing it attaches the panel again. */
class DockPanel::Window  : public juce::DocumentWindow
{
public:
    Window (DockPanel& p, juce::Rectangle<int> initialBounds)
        : DocumentWindow (p.title + " - Ng DAW",
                          juce::Desktop::getInstance().getDefaultLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId),
                          DocumentWindow::allButtons),
          panel (p)
    {
        setUsingNativeTitleBar (true);
        setContentNonOwned (&panel, false);
        setResizable (true, false);
        setResizeLimits (320, 160, 10000, 10000);
        getConstrainer()->setMinimumOnscreenAmounts (0x10000, 60, 40, 60);

        auto saved = panel.properties.getValue (getStateKey());

        if (saved.isEmpty() || ! restoreWindowStateFromString (saved))
            setBounds (initialBounds);

        if (panel.keyListener != nullptr)
            addKeyListener (panel.keyListener);

        setVisible (panel.active);
    }

    ~Window() override
    {
        panel.properties.setValue (getStateKey(), getWindowStateAsString());

        if (panel.keyListener != nullptr)
            removeKeyListener (panel.keyListener);

        clearContentComponent();
    }

    void closeButtonPressed() override
    {
        // Attaching deletes this window, so not from inside its own callback.
        juce::MessageManager::callAsync ([safePanel = juce::Component::SafePointer<DockPanel> (&panel)]
        {
            if (safePanel != nullptr)
                safePanel->setDetached (false);
        });
    }

private:
    juce::String getStateKey() const     { return "ngPanelWindow_" + panel.id; }

    DockPanel& panel;
};

//==============================================================================
DockPanel::DockPanel (const juce::String& panelId, const juce::String& panelTitle, icons::Icon panelIcon,
                      juce::Component& panelContent, juce::PropertiesFile& props, juce::KeyListener* keys)
    : id (panelId), title (panelTitle), icon (panelIcon), content (panelContent), properties (props), keyListener (keys)
{
    detachButton.onClick = [this] { setDetached (! isDetached()); };
    addAndMakeVisible (detachButton);
    addAndMakeVisible (content);
}

DockPanel::~DockPanel()
{
    if (window != nullptr)
    {
        // Remembered so the panel opens in its own window again next time.
        properties.setValue ("ngPanelDetached_" + id, true);
        window = nullptr;
    }
}

void DockPanel::setTitle (const juce::String& newTitle)
{
    if (title == newTitle)
        return;

    title = newTitle;

    if (window != nullptr)
        window->setName (title + " - Ng DAW");

    repaint();
}

void DockPanel::addHeaderItem (juce::Component& c, int width, bool atRight)
{
    headerItems.add ({ &c, width, atRight });
    addAndMakeVisible (c);
    resized();
}

void DockPanel::setDetached (bool shouldDetach)
{
    if (shouldDetach == isDetached())
        return;

    if (shouldDetach)
    {
        // The window opens over where the panel was, at least a sensible size.
        auto screenBounds = isShowing() ? getScreenBounds() : juce::Rectangle<int>();

        if (screenBounds.getWidth() < 480 || screenBounds.getHeight() < 240)
            screenBounds = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea
                               .withSizeKeepingCentre (900, 520);

        home = getParentComponent();
        window = std::make_unique<Window> (*this, screenBounds);
        detachButton.setIcon (icons::Icon::attach);
        detachButton.setTooltip ("Put back into the main window");
    }
    else
    {
        window = nullptr;

        if (home != nullptr)
            home->addChildComponent (this);

        setVisible (active);
        detachButton.setIcon (icons::Icon::detach);
        detachButton.setTooltip ("Detach into its own window");
    }

    properties.setValue ("ngPanelDetached_" + id, shouldDetach);
    resized();

    if (onDetachChanged != nullptr)
        onDetachChanged();
}

void DockPanel::setActive (bool shouldBeActive)
{
    if (active == shouldBeActive)
        return;

    active = shouldBeActive;

    if (window != nullptr)
    {
        window->setVisible (active);

        if (active)
            window->toFront (false);
    }
}

void DockPanel::toFront()
{
    if (window != nullptr)
        window->toFront (true);
}

void DockPanel::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    auto corner = isDetached() ? 0.0f : 6.0f;

    g.setColour (theme::panel);
    g.fillRoundedRectangle (bounds, corner);

    auto header = bounds.removeFromTop ((float) headerHeight);
    g.setColour (theme::panelHeader);
    g.fillRoundedRectangle (header, corner);
    g.fillRect (header.removeFromBottom (corner));

    auto r = getLocalBounds().removeFromTop (headerHeight).reduced (8, 0);
    icons::draw (g, icon, r.removeFromLeft (16).toFloat().withSizeKeepingCentre (16.0f, 16.0f), theme::accent.brighter (0.3f));
    r.removeFromLeft (8);
    g.setColour (juce::Colours::white.withAlpha (0.92f));
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText (title, r, juce::Justification::centredLeft, true);
}

void DockPanel::resized()
{
    auto r = getLocalBounds();
    auto header = r.removeFromTop (headerHeight).reduced (6, 4);

    detachButton.setBounds (header.removeFromRight (header.getHeight()));
    header.removeFromRight (6);

    // The title gets what it needs; the items fill in after it, or from the right.
    auto titleWidth = juce::roundToInt (juce::GlyphArrangement::getStringWidth (juce::FontOptions (14.0f, juce::Font::bold), title));
    header.removeFromLeft (std::min (header.getWidth(), 34 + titleWidth));

    for (auto& item : headerItems)
    {
        if (! item.component->isVisible())
            continue;

        if (item.atRight)
        {
            item.component->setBounds (header.removeFromRight (item.width));
            header.removeFromRight (4);
        }
    }

    for (auto& item : headerItems)
    {
        if (! item.component->isVisible() || item.atRight)
            continue;

        item.component->setBounds (header.removeFromLeft (std::min (item.width, header.getWidth())));
        header.removeFromLeft (4);
    }

    content.setBounds (r.reduced (isDetached() ? 0 : 1, 0).withTrimmedBottom (isDetached() ? 0 : 1));
}

void DockPanel::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.y < headerHeight)
        setDetached (! isDetached());
}

//==============================================================================
SplitterBar::SplitterBar (bool isVertical)
    : vertical (isVertical)
{
    setMouseCursor (vertical ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::UpDownResizeCursor);
    setRepaintsOnMouseActivity (true);
}

void SplitterBar::paint (juce::Graphics& g)
{
    auto hot = dragging || isMouseOver();
    auto r = getLocalBounds().toFloat();
    auto line = vertical ? r.withSizeKeepingCentre (hot ? 3.0f : 0.0f, std::min (r.getHeight(), 60.0f))
                         : r.withSizeKeepingCentre (std::min (r.getWidth(), 60.0f), hot ? 3.0f : 0.0f);

    if (hot)
    {
        g.setColour (theme::accent.withAlpha (dragging ? 0.9f : 0.6f));
        g.fillRoundedRectangle (line, 1.5f);
    }
    else
    {
        // Three dots, so the bar can be found.
        g.setColour (juce::Colours::white.withAlpha (0.18f));

        for (int i = -1; i <= 1; ++i)
        {
            auto centre = r.getCentre() + (vertical ? juce::Point<float> (0.0f, (float) i * 6.0f) : juce::Point<float> ((float) i * 6.0f, 0.0f));
            g.fillEllipse (juce::Rectangle<float> (2.5f, 2.5f).withCentre (centre));
        }
    }
}

void SplitterBar::mouseDown (const juce::MouseEvent&)
{
    dragging = true;

    if (onDragStart != nullptr)
        onDragStart();

    repaint();
}

void SplitterBar::mouseDrag (const juce::MouseEvent& e)
{
    if (onDrag != nullptr)
    {
        // In screen terms, since the bar itself moves as the panels resize.
        auto delta = e.getScreenPosition() - e.getMouseDownScreenPosition();
        onDrag (vertical ? delta.x : delta.y);
    }
}

void SplitterBar::mouseUp (const juce::MouseEvent&)
{
    dragging = false;

    if (onDragEnd != nullptr)
        onDragEnd();

    repaint();
}

void SplitterBar::mouseDoubleClick (const juce::MouseEvent&)
{
    if (onReset != nullptr)
        onReset();
}

} // namespace ng
