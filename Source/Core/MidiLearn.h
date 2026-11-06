#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

namespace wt
{
    /** Maps MIDI CCs to parameters of the hosted plug-in.
        Learning works in either order: move a control on the plug-in's editor (or pick a parameter
        from the list) and move a knob on the MIDI controller. Mapped parameters are shown as
        quick-access controls in the main window. */
    class MidiLearn : public juce::ChangeBroadcaster,
                      private juce::AudioProcessorListener,
                      private juce::AsyncUpdater
    {
    public:
        struct Mapping
        {
            int parameterIndex = -1;
            int ccNumber = -1;          // -1 = no CC, only shown in the panel
            int midiChannel = 0;        // 0 = any channel
            juce::String parameterName;
        };

        MidiLearn() = default;
        ~MidiLearn() override;

        void setPlugin (juce::AudioPluginInstance* plugin);
        juce::AudioPluginInstance* getPlugin() const { return plugin; }

        /** Called from the MIDI thread. Returns true if the message was used by a mapping. */
        bool handleIncomingMidi (const juce::MidiMessage& message);

        // Learning -----------------------------------------------------------------
        void setLearning (bool shouldLearn);
        bool isLearning() const { return learning.load(); }
        void setLearnParameter (int parameterIndex);   // choose the parameter from a list
        int getLearnParameter() const { return learnParameter.load(); }
        int getLearnCC() const { return learnCC.load(); }
        juce::String getLearnStatusText() const;

        // Mappings -------------------------------------------------------------------
        std::vector<Mapping> getMappings() const;
        void addMapping (int parameterIndex, int ccNumber, int midiChannel = 0);
        void removeMapping (int parameterIndex);
        void clearMappings();

        juce::AudioProcessorParameter* getParameter (int index) const;
        juce::StringArray getParameterNames() const;

        std::unique_ptr<juce::XmlElement> toXml() const;
        void fromXml (const juce::XmlElement& xml);

    private:
        void audioProcessorParameterChanged (juce::AudioProcessor*, int parameterIndex, float newValue) override;
        void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}
        void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int parameterIndex) override;
        void handleAsyncUpdate() override;
        void completeLearnIfReady();

        juce::AudioPluginInstance* plugin = nullptr;
        mutable juce::CriticalSection lock;
        std::vector<Mapping> mappings;

        std::atomic<bool> learning { false };
        std::atomic<int> learnParameter { -1 };
        std::atomic<int> learnCC { -1 };
        std::atomic<int> learnChannel { 0 };
        std::atomic<bool> ignoreParameterCallbacks { false };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiLearn)
    };
}
