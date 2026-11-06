#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace wt
{
    namespace Colours
    {
        const juce::Colour background  { 0xff111317 };
        const juce::Colour panel       { 0xff1a1d22 };
        const juce::Colour panelLight  { 0xff23272e };
        const juce::Colour outline     { 0xff2e333b };
        const juce::Colour text        { 0xffe9e9ea };
        const juce::Colour textDim     { 0xff8c929c };
        const juce::Colour accent      { 0xffff7a1a };   // Wavethings orange
        const juce::Colour accent2     { 0xff2ec4b6 };   // teal
        const juce::Colour danger      { 0xffe5484d };
        const juce::Colour ok          { 0xff46c46e };
    }

    class WavethingsLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        WavethingsLookAndFeel();

        void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                               float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                                   bool isMouseOverButton, bool isButtonDown) override;
        void drawGroupComponentOutline (juce::Graphics&, int width, int height, const juce::String& text,
                                        const juce::Justification&, juce::GroupComponent&) override;
        juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    };

    /** Rounded panel with a title, used for every section of the main window. */
    class Section : public juce::Component
    {
    public:
        explicit Section (const juce::String& sectionTitle) : title (sectionTitle) {}
        void paint (juce::Graphics& g) override;
        juce::Rectangle<int> getContentArea() const { return getLocalBounds().reduced (12).withTrimmedTop (22); }

    private:
        juce::String title;
    };
}
