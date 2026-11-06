#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>

namespace wt
{
    /** Vertical output meter with the silence threshold drawn on top of it.
        The line can be dragged. A LED shows whether the level is below the threshold, i.e. whether the
        sampler would move on to the next note right now. */
    class LevelMeter : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
    {
    public:
        LevelMeter();

        std::function<float()> getLevel;                // linear peak since the last call
        std::function<void (double)> onThresholdChanged;

        void setThresholdDb (double db);
        double getThresholdDb() const { return thresholdDb; }

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;

        static constexpr double minDb = -96.0;
        static constexpr double maxDb = 6.0;

    private:
        void timerCallback() override;
        juce::Rectangle<float> getMeterArea() const;
        float dbToY (double db) const;
        double yToDb (float y) const;

        double thresholdDb = -60.0;
        float levelDb = -120.0f;
        float peakHoldDb = -120.0f;
        int peakHoldCounter = 0;
        bool belowThreshold = true;
        double belowSinceMs = 0.0;
    };
}
