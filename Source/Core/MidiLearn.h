#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>
#include <set>

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
        /** How the parameter is shown in the panel. */
        enum class ControlType { knob = 0, button = 1 };

        struct Mapping
        {
            int parameterIndex = -1;
            int ccNumber = -1;          // -1 = no CC, only shown in the panel
            int midiChannel = 0;        // 0 = any channel
            juce::String parameterName;
            ControlType type = ControlType::knob;   // button: on/off (CC >= 64 = on)
            bool lockedForSampling = false;         // set to lockedValue before sampling (e.g. Delay off)
            float lockedValue = 0.0f;               // normalised 0-1
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

        // Learning from the plug-in's editor ---------------------------------------------
        /** While on, every control touched on the plug-in's interface is added to the panel (no CC).
            Parameters that move by themselves (LFOs, meters) are ignored. */
        void setCapturing (bool shouldCapture);
        bool isCapturing() const { return capturing.load(); }
        juce::String getLastCapturedName() const { const juce::ScopedLock sl (lock); return lastCaptured; }

        /** Waits for a CC from the MIDI controller and assigns it to this parameter (keeps its type). */
        void learnCcFor (int parameterIndex);
        /** Assigns a CC by hand (-1 = none) and the MIDI channel (0 = any). */
        void setCc (int parameterIndex, int ccNumber, int midiChannel);

        static bool isHelperParameterName (const juce::String& name)
        {
            // VST3 plug-ins made with JUCE expose 2000+ "MIDI CC x|y" helper parameters
            return name.startsWith ("MIDI CC ") && name.containsChar ('|');
        }

        // Mappings -------------------------------------------------------------------
        std::vector<Mapping> getMappings() const;
        /** type < 0: keep the type of an existing mapping, otherwise guess it from the parameter. */
        void addMapping (int parameterIndex, int ccNumber, int midiChannel = 0, int type = -1);
        void setControlType (int parameterIndex, ControlType type);

        /** Before sampling (and after each program change) this parameter is set to this value. */
        void setLockedForSampling (int parameterIndex, bool locked, float normalisedValue);
        /** (parameter index, value) of the locked controls. */
        std::vector<std::pair<int, float>> getLockedValues() const;

        /** Type used for new mappings made with MIDI Learn (-1 = automatic). */
        void setLearnControlType (int type) { learnType = type; }

        ControlType guessControlType (int parameterIndex) const;
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
        void captureParameter (int parameterIndex, bool fromGesture);

        juce::AudioPluginInstance* plugin = nullptr;
        mutable juce::CriticalSection lock;
        std::vector<Mapping> mappings;

        std::atomic<bool> learning { false };
        std::atomic<int> learnParameter { -1 };
        std::atomic<int> learnCC { -1 };
        std::atomic<int> learnChannel { 0 };
        std::atomic<int> learnType { -1 };
        std::atomic<bool> ignoreParameterCallbacks { false };

        std::atomic<bool> capturing { false };
        std::atomic<double> captureStartMs { 0.0 };
        std::atomic<bool> learnKeepsType { false };
        std::vector<int> pendingCaptures;      // parameter indices waiting for the message thread
        std::set<int> movingByThemselves;      // parameters that changed without anyone touching them
        juce::String lastCaptured;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiLearn)
    };
}
