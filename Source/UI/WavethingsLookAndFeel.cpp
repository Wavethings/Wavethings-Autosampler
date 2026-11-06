#include "WavethingsLookAndFeel.h"
#include <juce_audio_utils/juce_audio_utils.h>

namespace wt
{
    WavethingsLookAndFeel::WavethingsLookAndFeel()
    {
        auto scheme = getDarkColourScheme();
        scheme.setUIColour (ColourScheme::windowBackground, Colours::background);
        scheme.setUIColour (ColourScheme::widgetBackground, Colours::panelLight);
        scheme.setUIColour (ColourScheme::menuBackground, Colours::panel);
        scheme.setUIColour (ColourScheme::outline, Colours::outline);
        scheme.setUIColour (ColourScheme::defaultText, Colours::text);
        scheme.setUIColour (ColourScheme::defaultFill, Colours::accent);
        scheme.setUIColour (ColourScheme::highlightedText, juce::Colours::black);
        scheme.setUIColour (ColourScheme::highlightedFill, Colours::accent);
        scheme.setUIColour (ColourScheme::menuText, Colours::text);
        setColourScheme (scheme);

        setColour (juce::Slider::thumbColourId, Colours::accent);
        setColour (juce::Slider::trackColourId, Colours::accent);
        setColour (juce::Slider::rotarySliderFillColourId, Colours::accent);
        setColour (juce::Slider::rotarySliderOutlineColourId, Colours::outline);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, Colours::panelLight);
        setColour (juce::TextButton::buttonColourId, Colours::panelLight);
        setColour (juce::TextButton::buttonOnColourId, Colours::accent);
        setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        setColour (juce::ToggleButton::tickColourId, Colours::accent);
        setColour (juce::ComboBox::backgroundColourId, Colours::panelLight);
        setColour (juce::ComboBox::outlineColourId, Colours::outline);
        setColour (juce::TextEditor::backgroundColourId, Colours::panelLight);
        setColour (juce::TextEditor::outlineColourId, Colours::outline);
        setColour (juce::TextEditor::focusedOutlineColourId, Colours::accent);
        setColour (juce::ProgressBar::foregroundColourId, Colours::accent);
        setColour (juce::ProgressBar::backgroundColourId, Colours::panelLight);
        setColour (juce::Label::textColourId, Colours::text);
        setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, Colours::accent);
        setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, Colours::accent.withAlpha (0.3f));
    }

    juce::Font WavethingsLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
    {
        return juce::Font (juce::FontOptions (juce::jmin (14.0f, (float) buttonHeight * 0.55f)));
    }

    void WavethingsLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                                                  float startAngle, float endAngle, juce::Slider& slider)
    {
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
        const auto centre = bounds.getCentre();
        const float angle = startAngle + pos * (endAngle - startAngle);
        const float lineW = juce::jmax (2.5f, radius * 0.16f);
        const float arcR = radius - lineW * 0.5f;

        juce::Path track;
        track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (Colours::outline);
        g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        if (slider.isEnabled())
        {
            juce::Path value;
            value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, angle, true);
            g.setColour (slider.findColour (juce::Slider::rotarySliderFillColourId));
            g.strokePath (value, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        g.setColour (Colours::panelLight);
        g.fillEllipse (juce::Rectangle<float> (radius * 1.2f, radius * 1.2f).withCentre (centre));
        const auto tip = centre.getPointOnCircumference (radius * 0.5f, angle);
        g.setColour (Colours::text);
        g.drawLine ({ centre.getPointOnCircumference (radius * 0.15f, angle), tip }, 2.0f);
    }

    void WavethingsLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& background,
                                                      bool over, bool down)
    {
        auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
        auto colour = background;
        if (! button.isEnabled()) colour = colour.withMultipliedAlpha (0.4f);
        if (down) colour = colour.brighter (0.15f);
        else if (over) colour = colour.brighter (0.07f);

        g.setColour (colour);
        g.fillRoundedRectangle (bounds, 5.0f);
        g.setColour (button.getToggleState() ? Colours::accent : Colours::outline);
        g.drawRoundedRectangle (bounds, 5.0f, 1.0f);
    }

    void WavethingsLookAndFeel::drawGroupComponentOutline (juce::Graphics& g, int width, int height, const juce::String& text,
                                                           const juce::Justification&, juce::GroupComponent&)
    {
        g.setColour (Colours::panel);
        g.fillRoundedRectangle (0.0f, 0.0f, (float) width, (float) height, 8.0f);
        g.setColour (Colours::accent);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (text.toUpperCase(), 12, 6, width - 24, 16, juce::Justification::centredLeft);
    }

    void Section::paint (juce::Graphics& g)
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (Colours::panel);
        g.fillRoundedRectangle (b, 8.0f);
        g.setColour (Colours::outline);
        g.drawRoundedRectangle (b.reduced (0.5f), 8.0f, 1.0f);
        g.setColour (Colours::accent);
        g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        g.drawText (title.toUpperCase(), 12, 8, getWidth() - 24, 16, juce::Justification::centredLeft);
    }
}
