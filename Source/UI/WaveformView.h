#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Core/SampleProcessing.h"

namespace wt
{
    /** Shows the last recorded sample with its loop region. */
    class WaveformView : public juce::Component
    {
    public:
        void setSample (std::shared_ptr<ProcessedSample> sample, const juce::String& label);
        void clear();
        void paint (juce::Graphics&) override;

    private:
        std::shared_ptr<ProcessedSample> sample;
        juce::String label;
        std::vector<std::pair<float, float>> peaks;
        void computePeaks();
        void resized() override { computePeaks(); }
    };
}
