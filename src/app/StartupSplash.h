#pragma once

#include "Theme.h"

namespace ng
{

/**
    The window shown while the app starts: the name, a moving set of level bars, what's being
    done and a progress bar that glides to each step. It fades out once the main window is up.
*/
class StartupSplash  : public juce::Component
{
public:
    StartupSplash()
    {
        setOpaque (false);
        setSize (560, 320);
        centreWithSize (getWidth(), getHeight());
        addToDesktop (juce::ComponentPeer::windowIsTemporary | juce::ComponentPeer::windowHasDropShadow);
        setVisible (true);
        toFront (false);
    }

    /** Shows what's happening and how far along it is (0 to 1). */
    void setProgress (const juce::String& text, float proportion)
    {
        status = text;
        targetProgress = juce::jlimit (0.0f, 1.0f, proportion);
        repaint();
    }

    /** Fades the window out, then deletes it. */
    void fadeOutAndDelete (std::unique_ptr<StartupSplash> self)
    {
        jassert (self.get() == this);
        owned = std::move (self);
        fading = true;
        targetProgress = 1.0f;
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (1.0f);

        juce::ColourGradient background (juce::Colour (0xff1c2129), bounds.getX(), bounds.getY(),
                                         juce::Colour (0xff111418), bounds.getRight(), bounds.getBottom(), false);
        g.setGradientFill (background);
        g.fillRoundedRectangle (bounds, 12.0f);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawRoundedRectangle (bounds, 12.0f, 1.0f);

        // Level bars, rising and falling like a meter playing a beat.
        auto bars = juce::Rectangle<float> (bounds.getX() + 40.0f, 60.0f, bounds.getWidth() - 80.0f, 90.0f);
        constexpr int numBars = 28;
        auto barWidth = bars.getWidth() / (float) numBars;

        for (int i = 0; i < numBars; ++i)
        {
            auto t = (float) phase;
            auto level = 0.5f + 0.25f * std::sin (t * 3.1f + (float) i * 0.45f) + 0.2f * std::sin (t * 5.3f - (float) i * 0.8f);
            level = juce::jlimit (0.08f, 1.0f, level);
            auto height = bars.getHeight() * level;
            auto bar = juce::Rectangle<float> (bars.getX() + (float) i * barWidth + 2.0f, bars.getBottom() - height, barWidth - 4.0f, height);
            g.setColour (theme::accent.interpolatedWith (theme::play, (float) i / (float) numBars).withAlpha (0.35f + 0.55f * level));
            g.fillRoundedRectangle (bar, 2.0f);
        }

        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (40.0f, juce::Font::bold));
        g.drawText ("Ng DAW", bounds.withTop (166.0f).withHeight (48.0f), juce::Justification::centred);

        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.setFont (juce::FontOptions (13.0f));
        g.drawText ("Version " + juce::String (NG_VERSION), bounds.withTop (212.0f).withHeight (20.0f), juce::Justification::centred);

        auto progressArea = juce::Rectangle<float> (bounds.getX() + 60.0f, bounds.getBottom() - 52.0f, bounds.getWidth() - 120.0f, 4.0f);
        g.setColour (juce::Colours::white.withAlpha (0.1f));
        g.fillRoundedRectangle (progressArea, 2.0f);
        g.setColour (theme::accent);
        g.fillRoundedRectangle (progressArea.withWidth (progressArea.getWidth() * progress), 2.0f);

        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (status, progressArea.translated (0.0f, 10.0f).withHeight (20.0f), juce::Justification::centred);
    }

private:
    void onFrame (double timestamp)
    {
        auto delta = lastTimestamp > 0.0 ? juce::jlimit (0.0, 0.1, timestamp - lastTimestamp) : 1.0 / 60.0;
        lastTimestamp = timestamp;
        phase += delta;
        progress += (targetProgress - progress) * 0.12f;

        if (fading)
        {
            auto alpha = getAlpha() - (float) delta * 4.0f;

            if (alpha <= 0.0f)
            {
                // Deleting this from inside its own callback isn't safe; do it next.
                juce::MessageManager::callAsync ([doomed = std::shared_ptr<StartupSplash> (std::move (owned))] {});
                fading = false;
                setVisible (false);
                return;
            }

            setAlpha (alpha);
        }

        repaint();
    }

    juce::String status { "Starting" };
    float progress = 0.0f, targetProgress = 0.0f;
    double phase = 0.0, lastTimestamp = 0.0;
    bool fading = false;
    std::unique_ptr<StartupSplash> owned;
    juce::VBlankAttachment vblank { this, [this] (double timestamp) { onFrame (timestamp); } };

    JUCE_DECLARE_NON_COPYABLE (StartupSplash)
};

} // namespace ng
