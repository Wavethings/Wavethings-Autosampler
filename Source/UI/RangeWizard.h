#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>

namespace wt
{
    /** Pop-up that asks first for the lowest and then for the highest note, played on the MIDI keyboard. */
    class RangeWizard : public juce::Component
    {
    public:
        RangeWizard (int octaveForMiddleC, std::function<void (int low, int high)> onComplete);

        /** Forwarded by the main window for every incoming MIDI message. */
        void handleMidi (const juce::MidiMessage& message);

        static void show (juce::Component* parent, int octaveForMiddleC,
                          std::function<void (int, int)> onComplete,
                          juce::Component::SafePointer<RangeWizard>& handle);

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        void close();

        int octave;
        std::function<void (int, int)> onComplete;
        int step = 0;
        int lowNote = -1;
        juce::TextButton cancelButton, skipButton;
    };
}
