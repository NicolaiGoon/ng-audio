#include "Theme.h"

namespace ng
{
namespace icons
{
    namespace
    {
        juce::Path arrowHead (juce::Point<float> tip, float angle, float size)
        {
            juce::Path p;
            p.addTriangle (0.0f, -size, size * 0.9f, size * 0.6f, -size * 0.9f, size * 0.6f);
            p.applyTransform (juce::AffineTransform::rotation (angle).translated (tip));
            return p;
        }

        juce::Path gear()
        {
            juce::Path p;
            constexpr int teeth = 8;
            auto centre = juce::Point<float> (12.0f, 12.0f);

            for (int i = 0; i < teeth * 2; ++i)
            {
                auto angle = juce::MathConstants<float>::twoPi * (float) i / (float) (teeth * 2);
                auto radius = i % 2 == 0 ? 10.0f : 7.6f;
                auto a1 = angle - 0.17f, a2 = angle + 0.17f;
                auto p1 = centre.getPointOnCircumference (radius, a1), p2 = centre.getPointOnCircumference (radius, a2);

                if (i == 0)
                    p.startNewSubPath (p1);
                else
                    p.lineTo (p1);

                p.lineTo (p2);
            }

            p.closeSubPath();
            p.addEllipse (8.5f, 8.5f, 7.0f, 7.0f);
            p.setUsingNonZeroWinding (false);
            return p;
        }
    }

    bool isFilled (Icon icon)
    {
        return icon == Icon::play || icon == Icon::stop || icon == Icon::record || icon == Icon::toStart || icon == Icon::settings;
    }

    juce::Path get (Icon icon)
    {
        juce::Path p;

        switch (icon)
        {
            case Icon::play:        p.addTriangle (7.0f, 4.5f, 7.0f, 19.5f, 19.5f, 12.0f); break;
            case Icon::stop:        p.addRoundedRectangle (6.0f, 6.0f, 12.0f, 12.0f, 1.5f); break;
            case Icon::record:      p.addEllipse (5.5f, 5.5f, 13.0f, 13.0f); break;
            case Icon::toStart:
                p.addRectangle (5.0f, 5.5f, 2.5f, 13.0f);
                p.addTriangle (19.0f, 5.5f, 19.0f, 18.5f, 8.5f, 12.0f);
                break;

            case Icon::loop:
                p.startNewSubPath (5.0f, 13.0f);
                p.lineTo (5.0f, 10.0f);
                p.quadraticTo (5.0f, 7.0f, 8.0f, 7.0f);
                p.lineTo (17.0f, 7.0f);
                p.startNewSubPath (19.0f, 11.0f);
                p.lineTo (19.0f, 14.0f);
                p.quadraticTo (19.0f, 17.0f, 16.0f, 17.0f);
                p.lineTo (7.0f, 17.0f);
                p.addPath (arrowHead ({ 18.5f, 7.0f }, juce::MathConstants<float>::halfPi, 2.6f));
                p.addPath (arrowHead ({ 5.5f, 17.0f }, -juce::MathConstants<float>::halfPi, 2.6f));
                break;

            case Icon::metronome:
                p.startNewSubPath (9.0f, 4.0f);
                p.lineTo (15.0f, 4.0f);
                p.lineTo (19.0f, 20.0f);
                p.lineTo (5.0f, 20.0f);
                p.closeSubPath();
                p.startNewSubPath (12.0f, 16.0f);
                p.lineTo (17.5f, 7.0f);
                break;

            case Icon::undo:
            case Icon::redo:
                p.startNewSubPath (6.0f, 10.0f);
                p.lineTo (14.0f, 10.0f);
                p.cubicTo (21.0f, 10.0f, 21.0f, 19.0f, 14.0f, 19.0f);
                p.lineTo (9.0f, 19.0f);
                p.addPath (arrowHead ({ 5.0f, 10.0f }, -juce::MathConstants<float>::halfPi, 3.0f));

                if (icon == Icon::redo)
                    p.applyTransform (juce::AffineTransform::scale (-1.0f, 1.0f, 12.0f, 12.0f));
                break;

            case Icon::settings:    p = gear(); break;

            case Icon::typingKeys:
                p.addRoundedRectangle (3.0f, 6.0f, 18.0f, 12.0f, 2.0f);

                for (int i = 0; i < 4; ++i)
                {
                    p.addRectangle (6.0f + (float) i * 3.6f, 9.0f, 0.8f, 0.8f);
                    p.addRectangle (7.8f + (float) i * 3.0f, 12.0f, 0.8f, 0.8f);
                }

                p.startNewSubPath (8.0f, 15.0f);
                p.lineTo (16.0f, 15.0f);
                break;

            case Icon::piano:
                p.addRoundedRectangle (3.0f, 5.0f, 18.0f, 14.0f, 1.5f);

                for (int i = 1; i < 4; ++i)
                {
                    p.startNewSubPath (3.0f + 4.5f * (float) i, 13.0f);
                    p.lineTo (3.0f + 4.5f * (float) i, 19.0f);
                    p.addRectangle (1.8f + 4.5f * (float) i, 5.0f, 2.4f, 8.0f);
                }
                break;

            case Icon::newFile:
                p.startNewSubPath (6.0f, 3.5f);
                p.lineTo (14.0f, 3.5f);
                p.lineTo (18.5f, 8.0f);
                p.lineTo (18.5f, 20.5f);
                p.lineTo (6.0f, 20.5f);
                p.closeSubPath();
                p.startNewSubPath (14.0f, 3.5f);
                p.lineTo (14.0f, 8.0f);
                p.lineTo (18.5f, 8.0f);
                break;

            case Icon::open:
                p.startNewSubPath (3.5f, 18.5f);
                p.lineTo (3.5f, 6.0f);
                p.lineTo (9.0f, 6.0f);
                p.lineTo (11.0f, 8.0f);
                p.lineTo (18.0f, 8.0f);
                p.lineTo (18.0f, 10.5f);
                p.startNewSubPath (3.5f, 18.5f);
                p.lineTo (6.5f, 10.5f);
                p.lineTo (21.0f, 10.5f);
                p.lineTo (18.0f, 18.5f);
                p.closeSubPath();
                break;

            case Icon::save:
                p.addRoundedRectangle (4.0f, 4.0f, 16.0f, 16.0f, 2.0f);
                p.addRectangle (8.0f, 4.0f, 8.0f, 5.0f);
                p.addRectangle (7.0f, 13.0f, 10.0f, 7.0f);
                break;

            case Icon::detach:
            case Icon::attach:
                p.startNewSubPath (11.0f, 4.5f);
                p.lineTo (4.5f, 4.5f);
                p.lineTo (4.5f, 19.5f);
                p.lineTo (19.5f, 19.5f);
                p.lineTo (19.5f, 13.0f);

                if (icon == Icon::detach)
                {
                    p.startNewSubPath (11.0f, 13.0f);
                    p.lineTo (19.5f, 4.5f);
                    p.startNewSubPath (14.0f, 4.5f);
                    p.lineTo (19.5f, 4.5f);
                    p.lineTo (19.5f, 10.0f);
                }
                else
                {
                    p.startNewSubPath (19.5f, 4.5f);
                    p.lineTo (11.0f, 13.0f);
                    p.startNewSubPath (11.0f, 7.5f);
                    p.lineTo (11.0f, 13.0f);
                    p.lineTo (16.5f, 13.0f);
                }
                break;

            case Icon::zoomIn:
            case Icon::zoomOut:
            case Icon::zoomInVertical:
            case Icon::zoomOutVertical:
            {
                auto vertical = icon == Icon::zoomInVertical || icon == Icon::zoomOutVertical;
                auto in = icon == Icon::zoomIn || icon == Icon::zoomInVertical;

                if (vertical)
                {
                    // Arrows apart (in) or together (out), up and down.
                    p.startNewSubPath (12.0f, 4.0f);
                    p.lineTo (12.0f, 20.0f);
                    p.addPath (arrowHead ({ 12.0f, in ? 3.5f : 10.0f }, in ? 0.0f : juce::MathConstants<float>::pi, 3.0f));
                    p.addPath (arrowHead ({ 12.0f, in ? 20.5f : 14.0f }, in ? juce::MathConstants<float>::pi : 0.0f, 3.0f));
                    p.startNewSubPath (6.0f, 12.0f);
                    p.lineTo (18.0f, 12.0f);
                }
                else
                {
                    p.startNewSubPath (4.0f, 12.0f);
                    p.lineTo (20.0f, 12.0f);
                    p.addPath (arrowHead ({ in ? 3.5f : 10.0f, 12.0f }, in ? -juce::MathConstants<float>::halfPi : juce::MathConstants<float>::halfPi, 3.0f));
                    p.addPath (arrowHead ({ in ? 20.5f : 14.0f, 12.0f }, in ? juce::MathConstants<float>::halfPi : -juce::MathConstants<float>::halfPi, 3.0f));
                    p.startNewSubPath (12.0f, 6.0f);
                    p.lineTo (12.0f, 18.0f);
                }
                break;
            }

            case Icon::plus:
                p.startNewSubPath (12.0f, 5.0f);
                p.lineTo (12.0f, 19.0f);
                p.startNewSubPath (5.0f, 12.0f);
                p.lineTo (19.0f, 12.0f);
                break;

            case Icon::browser:
                p.addRoundedRectangle (3.5f, 4.5f, 17.0f, 15.0f, 2.0f);
                p.startNewSubPath (9.5f, 4.5f);
                p.lineTo (9.5f, 19.5f);
                break;

            case Icon::mixer:
                for (int i = 0; i < 3; ++i)
                {
                    auto x = 6.0f + 6.0f * (float) i;
                    auto knob = i == 1 ? 9.0f : (i == 0 ? 15.0f : 12.0f);
                    p.startNewSubPath (x, 4.0f);
                    p.lineTo (x, 20.0f);
                    p.addRoundedRectangle (x - 2.2f, knob - 1.5f, 4.4f, 3.0f, 1.0f);
                }
                break;

            case Icon::rack:
                for (int i = 0; i < 3; ++i)
                {
                    p.addRoundedRectangle (4.0f, 5.0f + 5.0f * (float) i, 4.0f, 3.5f, 1.0f);
                    p.startNewSubPath (10.5f, 6.7f + 5.0f * (float) i);
                    p.lineTo (20.0f, 6.7f + 5.0f * (float) i);
                }
                break;

            case Icon::timeline:
                p.addRoundedRectangle (3.0f, 6.0f, 9.0f, 4.0f, 1.0f);
                p.addRoundedRectangle (9.0f, 14.0f, 12.0f, 4.0f, 1.0f);
                p.startNewSubPath (3.0f, 12.0f);
                p.lineTo (21.0f, 12.0f);
                break;

            case Icon::exportAudio:
                p.startNewSubPath (12.0f, 15.0f);
                p.lineTo (12.0f, 4.0f);
                p.addPath (arrowHead ({ 12.0f, 3.5f }, 0.0f, 3.2f));
                p.startNewSubPath (5.0f, 13.0f);
                p.lineTo (5.0f, 20.0f);
                p.lineTo (19.0f, 20.0f);
                p.lineTo (19.0f, 13.0f);
                break;

            case Icon::history:
                p.addEllipse (4.0f, 4.0f, 16.0f, 16.0f);
                p.startNewSubPath (12.0f, 7.5f);
                p.lineTo (12.0f, 12.0f);
                p.lineTo (15.5f, 14.0f);
                break;

            case Icon::console:
                p.addRoundedRectangle (3.0f, 4.5f, 18.0f, 15.0f, 2.0f);
                p.startNewSubPath (7.0f, 9.0f);
                p.lineTo (10.0f, 12.0f);
                p.lineTo (7.0f, 15.0f);
                p.startNewSubPath (12.0f, 15.0f);
                p.lineTo (16.5f, 15.0f);
                break;

            case Icon::close:
                p.startNewSubPath (6.5f, 6.5f);
                p.lineTo (17.5f, 17.5f);
                p.startNewSubPath (17.5f, 6.5f);
                p.lineTo (6.5f, 17.5f);
                break;

            case Icon::help:
                p.addEllipse (4.0f, 4.0f, 16.0f, 16.0f);
                p.startNewSubPath (9.5f, 10.0f);
                p.quadraticTo (9.5f, 7.5f, 12.0f, 7.5f);
                p.quadraticTo (14.5f, 7.5f, 14.5f, 10.0f);
                p.quadraticTo (14.5f, 11.5f, 12.0f, 12.5f);
                p.lineTo (12.0f, 14.0f);
                p.addRectangle (11.5f, 16.0f, 1.0f, 1.0f);
                break;

            case Icon::plugins:
                p.startNewSubPath (9.0f, 3.5f);
                p.lineTo (9.0f, 8.0f);
                p.startNewSubPath (15.0f, 3.5f);
                p.lineTo (15.0f, 8.0f);
                p.addRoundedRectangle (6.0f, 8.0f, 12.0f, 7.0f, 2.0f);
                p.startNewSubPath (12.0f, 15.0f);
                p.lineTo (12.0f, 20.5f);
                break;

            case Icon::audio:
                for (int i = 0; i < 7; ++i)
                {
                    static const float heights[] = { 4.0f, 9.0f, 14.0f, 7.0f, 12.0f, 6.0f, 3.0f };
                    auto x = 4.5f + 2.5f * (float) i;
                    p.startNewSubPath (x, 12.0f - heights[i] / 2.0f);
                    p.lineTo (x, 12.0f + heights[i] / 2.0f);
                }
                break;
        }

        return p;
    }

    void draw (juce::Graphics& g, Icon icon, juce::Rectangle<float> area, juce::Colour colour)
    {
        auto size = std::min (area.getWidth(), area.getHeight());
        auto box = area.withSizeKeepingCentre (size, size);
        auto transform = juce::AffineTransform::scale (size / 24.0f).translated (box.getX(), box.getY());
        auto path = get (icon);
        g.setColour (colour);

        if (isFilled (icon))
            g.fillPath (path, transform);
        else
            // The stroke's thickness is in pixels, after the transform.
            g.strokePath (path, juce::PathStrokeType (std::max (1.3f, size * 0.075f), juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded),
                          transform);
    }
}

//==============================================================================
NgLookAndFeel::NgLookAndFeel()
{
    applyAccents();
}

void NgLookAndFeel::applyAccents()
{
    setColour (juce::TextButton::buttonOnColourId, theme::accent);
    setColour (juce::Slider::thumbColourId, theme::accent);
    setColour (juce::Slider::rotarySliderFillColourId, theme::accent);
    setColour (juce::Slider::trackColourId, theme::accent.withAlpha (0.8f));
}

void NgLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                                          bool isMouseOverButton, bool isButtonDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    auto corner = std::min (5.0f, bounds.getHeight() / 2.0f);
    auto colour = backgroundColour.withMultipliedSaturation (button.hasKeyboardFocus (true) ? 1.2f : 1.0f)
                                  .withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f);

    if (isButtonDown)
        colour = colour.darker (0.15f);
    else if (isMouseOverButton)
        colour = colour.brighter (0.12f);

    g.setColour (colour);
    g.fillRoundedRectangle (bounds, corner);

    // A faint light edge at the top, so a button reads as raised without a gradient.
    g.setColour (juce::Colours::white.withAlpha (isButtonDown ? 0.02f : 0.07f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), corner, 1.0f);
}

juce::Font NgLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return juce::FontOptions (std::min (14.0f, (float) buttonHeight * 0.6f));
}

void NgLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool isMouseOverButton, bool)
{
    // A small switch, then the text.
    auto bounds = button.getLocalBounds().toFloat();
    auto height = std::min (16.0f, bounds.getHeight() - 4.0f);
    auto track = juce::Rectangle<float> (bounds.getX() + 2.0f, bounds.getCentreY() - height / 2.0f, height * 1.8f, height);
    auto on = button.getToggleState();
    auto alpha = button.isEnabled() ? 1.0f : 0.4f;

    g.setColour ((on ? button.findColour (juce::ToggleButton::tickColourId).interpolatedWith (theme::accent, 0.85f)
                     : juce::Colour (0xff3a3f46)).withMultipliedAlpha (alpha).brighter (isMouseOverButton ? 0.1f : 0.0f));
    g.fillRoundedRectangle (track, height / 2.0f);

    auto knob = juce::Rectangle<float> (height - 4.0f, height - 4.0f)
                    .withCentre ({ on ? track.getRight() - height / 2.0f : track.getX() + height / 2.0f, track.getCentreY() });
    g.setColour (juce::Colours::white.withAlpha (0.92f * alpha));
    g.fillEllipse (knob);

    g.setColour (button.findColour (juce::ToggleButton::textColourId).withMultipliedAlpha (alpha));
    g.setFont (juce::FontOptions (std::min (14.0f, bounds.getHeight() * 0.6f)));
    g.drawFittedText (button.getButtonText(), bounds.withTrimmedLeft (track.getRight() + 4.0f).toNearestInt(),
                      juce::Justification::centredLeft, 1);
}

void NgLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId).brighter (box.isMouseOver (true) ? 0.08f : 0.0f));
    g.fillRoundedRectangle (bounds, 5.0f);
    g.setColour (box.findColour (juce::ComboBox::outlineColourId).withMultipliedAlpha (box.hasKeyboardFocus (true) ? 1.0f : 0.6f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 5.0f, 1.0f);

    // A chevron.
    auto arrow = juce::Rectangle<float> ((float) width - 18.0f, 0.0f, 12.0f, (float) height).withSizeKeepingCentre (8.0f, 5.0f);
    juce::Path p;
    p.startNewSubPath (arrow.getX(), arrow.getY());
    p.lineTo (arrow.getCentreX(), arrow.getBottom());
    p.lineTo (arrow.getRight(), arrow.getY());
    g.setColour (box.findColour (juce::ComboBox::arrowColourId).withAlpha (box.isEnabled() ? 0.8f : 0.3f));
    g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Font NgLookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    return juce::FontOptions (std::min (14.0f, (float) box.getHeight() * 0.6f));
}

void NgLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float proportion,
                                      float startAngle, float endAngle, juce::Slider& slider)
{
    auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (3.0f);
    auto radius = std::min (bounds.getWidth(), bounds.getHeight()) / 2.0f;
    auto centre = bounds.getCentre();
    auto lineWidth = std::max (2.0f, radius * 0.18f);
    auto arcRadius = radius - lineWidth / 2.0f;
    auto angle = startAngle + proportion * (endAngle - startAngle);
    auto fill = slider.findColour (juce::Slider::rotarySliderFillColourId);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.strokePath (track, juce::PathStrokeType (lineWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // A knob whose range is either side of zero (like pan) fills from the middle.
    auto range = slider.getRange();
    auto fromAngle = range.getStart() < 0.0 && range.getEnd() > 0.0
                         ? startAngle + (float) slider.valueToProportionOfLength (0.0) * (endAngle - startAngle)
                         : startAngle;

    if (slider.isEnabled() && std::abs (angle - fromAngle) > 0.001f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, std::min (fromAngle, angle), std::max (fromAngle, angle), true);
        g.setColour (fill);
        g.strokePath (value, juce::PathStrokeType (lineWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    auto knobRadius = arcRadius - lineWidth * 1.2f;
    g.setColour (juce::Colour (0xff3a4048).brighter (slider.isMouseOverOrDragging() ? 0.15f : 0.0f));
    g.fillEllipse (juce::Rectangle<float> (knobRadius * 2.0f, knobRadius * 2.0f).withCentre (centre));

    auto tip = centre.getPointOnCircumference (knobRadius * 0.85f, angle);
    g.setColour (juce::Colours::white.withAlpha (0.9f));
    g.drawLine ({ centre.getPointOnCircumference (knobRadius * 0.25f, angle), tip }, std::max (1.5f, lineWidth * 0.7f));
}

void NgLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                      float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::LinearVertical && style != juce::Slider::LinearHorizontal)
        return LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);

    auto vertical = style == juce::Slider::LinearVertical;
    auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
    auto trackWidth = 4.0f;
    auto fill = slider.findColour (juce::Slider::trackColourId);

    auto track = vertical ? juce::Rectangle<float> (trackWidth, bounds.getHeight()).withCentre (bounds.getCentre())
                          : juce::Rectangle<float> (bounds.getWidth(), trackWidth).withCentre (bounds.getCentre());
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillRoundedRectangle (track, trackWidth / 2.0f);

    auto filled = vertical ? track.withTop (sliderPos) : track.withRight (sliderPos);
    g.setColour (fill.withMultipliedAlpha (slider.isEnabled() ? 1.0f : 0.4f));
    g.fillRoundedRectangle (filled, trackWidth / 2.0f);

    // A fader cap for vertical faders, a round thumb for horizontal ones.
    auto hot = slider.isMouseOverOrDragging();

    if (vertical)
    {
        auto cap = juce::Rectangle<float> (std::min (bounds.getWidth() - 4.0f, 30.0f), 14.0f).withCentre ({ bounds.getCentreX(), sliderPos });
        g.setColour (juce::Colour (0xff4a515a).brighter (hot ? 0.2f : 0.0f));
        g.fillRoundedRectangle (cap, 3.0f);
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.fillRect (cap.withSizeKeepingCentre (cap.getWidth() - 8.0f, 1.5f));
    }
    else
    {
        g.setColour (juce::Colours::white.withAlpha (hot ? 1.0f : 0.9f));
        g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ sliderPos, bounds.getCentreY() }));
    }
}

void NgLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar& bar, int x, int y, int width, int height, bool vertical,
                                   int thumbStart, int thumbSize, bool isMouseOver, bool isMouseDown)
{
    juce::ignoreUnused (bar);
    auto thumb = vertical ? juce::Rectangle<int> (x, thumbStart, width, thumbSize)
                          : juce::Rectangle<int> (thumbStart, y, thumbSize, height);
    auto r = thumb.toFloat().reduced (vertical ? 2.5f : 1.0f, vertical ? 1.0f : 2.5f);

    if (r.isEmpty())
        return;

    g.setColour (juce::Colours::white.withAlpha (isMouseDown ? 0.5f : isMouseOver ? 0.38f : 0.22f));
    g.fillRoundedRectangle (r, std::min (r.getWidth(), r.getHeight()) / 2.0f);
}

void NgLookAndFeel::drawMenuBarBackground (juce::Graphics& g, int width, int height, bool, juce::MenuBarComponent&)
{
    g.setColour (theme::panelHeader);
    g.fillRect (0, 0, width, height);
}

void NgLookAndFeel::drawMenuBarItem (juce::Graphics& g, int width, int height, int, const juce::String& text,
                                     bool isMouseOverItem, bool isMenuOpen, bool, juce::MenuBarComponent& bar)
{
    auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (1.0f, 3.0f);

    if (isMenuOpen || isMouseOverItem)
    {
        g.setColour (juce::Colours::white.withAlpha (isMenuOpen ? 0.14f : 0.08f));
        g.fillRoundedRectangle (r, 4.0f);
    }

    g.setColour (juce::Colours::white.withAlpha (isMenuOpen || isMouseOverItem ? 1.0f : 0.8f));
    g.setFont (getMenuBarFont (bar, 0, text));
    g.drawFittedText (text, 0, 0, width, height, juce::Justification::centred, 1);
}

juce::Font NgLookAndFeel::getMenuBarFont (juce::MenuBarComponent&, int, const juce::String&)
{
    return juce::FontOptions (14.0f);
}

juce::Font NgLookAndFeel::getPopupMenuFont()
{
    return juce::FontOptions (14.5f);
}

void NgLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    auto bounds = juce::Rectangle<float> ((float) width, (float) height);
    g.setColour (juce::Colour (0xf01b1e22));
    g.fillRoundedRectangle (bounds, 5.0f);
    g.setColour (juce::Colours::white.withAlpha (0.15f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 5.0f, 1.0f);

    juce::AttributedString s;
    s.setJustification (juce::Justification::centredLeft);
    s.append (text, juce::FontOptions (13.0f), juce::Colours::white.withAlpha (0.92f));

    juce::TextLayout layout;
    layout.createLayoutWithBalancedLineLengths (s, (float) width - 16.0f);
    layout.draw (g, bounds.reduced (8.0f, 4.0f));
}

//==============================================================================
IconButton::IconButton (const juce::String& name, icons::Icon i, const juce::String& labelText)
    : juce::Button (name), icon (i), label (labelText)
{
    setTooltip (name);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void IconButton::setIcon (icons::Icon i)
{
    icon = i;
    repaint();
}

void IconButton::setLabel (const juce::String& newLabel)
{
    label = newLabel;
    repaint();
}

void IconButton::setOnColour (juce::Colour c, bool tintIconOnly)
{
    onColour = c;
    tintOnly = tintIconOnly;
    repaint();
}

int IconButton::getIdealWidth (int height) const
{
    auto width = height;

    if (label.isNotEmpty())
        width += juce::roundToInt (juce::GlyphArrangement::getStringWidth (juce::FontOptions (13.5f), label)) + 10;

    return width;
}

void IconButton::buttonStateChanged()
{
    startTimerHz (60);
}

void IconButton::timerCallback()
{
    auto target = isOver() || isDown() ? 1.0f : 0.0f;
    hover += (target - hover) * 0.35f;

    if (std::abs (target - hover) < 0.02f)
    {
        hover = target;
        stopTimer();
    }

    repaint();
}

void IconButton::paintButton (juce::Graphics& g, bool, bool isButtonDown)
{
    auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    auto on = getToggleState();
    auto enabled = isEnabled();

    // Background: the "on" colour when toggled on, otherwise a highlight that fades in on hover.
    if (on && ! tintOnly)
    {
        g.setColour (onColour.withMultipliedAlpha (enabled ? 0.9f : 0.4f).brighter (hover * 0.15f));
        g.fillRoundedRectangle (bounds, 5.0f);
    }
    else
    {
        g.setColour (juce::Colours::white.withAlpha (0.045f + 0.075f * hover + (isButtonDown ? 0.05f : 0.0f)));
        g.fillRoundedRectangle (bounds, 5.0f);
    }

    auto colour = iconColour != juce::Colour() ? iconColour : juce::Colours::white.withAlpha (0.85f);

    if (on)
        colour = tintOnly ? onColour : juce::Colours::white;

    colour = colour.withMultipliedAlpha (enabled ? 1.0f : 0.35f);

    auto iconArea = bounds.removeFromLeft (bounds.getHeight()).reduced (bounds.getHeight() * 0.2f);

    if (isButtonDown)
        iconArea = iconArea.reduced (0.5f);

    icons::draw (g, icon, label.isEmpty() ? getLocalBounds().toFloat().reduced (getHeight() * 0.22f) : iconArea, colour);

    if (label.isNotEmpty())
    {
        g.setColour (colour);
        g.setFont (juce::FontOptions (13.5f));
        g.drawText (label, bounds.withTrimmedRight (6.0f), juce::Justification::centredLeft, true);
    }
}

} // namespace ng
