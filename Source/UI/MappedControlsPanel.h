#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../Core/MidiLearn.h"

namespace wt
{
    /** Quick-access strip with the plug-in parameters that were assigned with MIDI Learn
        (or added by hand). Each knob follows the parameter in both directions. */
    class MappedControlsPanel : public juce::Component,
                                private juce::ChangeListener,
                                private juce::Timer
    {
    public:
        explicit MappedControlsPanel (MidiLearn& midiLearn);
        ~MappedControlsPanel() override;

        void refresh();
        void resized() override;
        void paint (juce::Graphics&) override;

        std::function<void()> onMappingsChanged;

    private:
        class Knob;

        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void timerCallback() override;
        void updateParameterBox();

        MidiLearn& midiLearn;
        juce::TextButton learnButton { "MIDI Learn" };
        juce::ComboBox parameterBox;
        juce::StringArray cachedParameterNames;
        juce::TextButton addButton;
        juce::TextButton clearButton;
        juce::Label statusLabel;
        juce::Viewport viewport;
        juce::Component knobHolder;
        std::vector<std::unique_ptr<Knob>> knobs;
    };
}
