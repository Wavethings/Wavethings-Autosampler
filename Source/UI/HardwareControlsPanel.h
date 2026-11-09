#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Core/Settings.h"

namespace wt
{
    /** Panel of MIDI controls for a hardware instrument: each control sends a CC to the MIDI output
        (knob 0-127 or on/off button). Controls can be learned from the MIDI controller, and moving
        that controller updates them. Shown instead of the plug-in controls in Hardware mode. */
    class HardwareControlsPanel : public juce::Component,
                                  private juce::Timer
    {
    public:
        struct Control
        {
            juce::String name;
            int cc = 74;
            int channel = 0;            // 0 = the instrument's channel
            bool isButton = false;
            int value = 64;
        };

        HardwareControlsPanel();
        ~HardwareControlsPanel() override;

        /** Sends a CC to the instrument (channel already resolved: 0 = instrument channel). */
        std::function<void (int channel, int cc, int value)> sendCc;
        std::function<void()> onChanged;

        /** Incoming CC from the MIDI controller (message thread): learns or follows. */
        void handleIncomingCc (int channel, int cc, int value);

        void sendAll();
        bool isSendOnStartEnabled() const { return sendOnStartToggle.getToggleState(); }
        std::vector<ControlValue> getValuesToSend() const;

        const std::vector<Control>& getControls() const { return controls; }
        std::unique_ptr<juce::XmlElement> toXml() const;
        void fromXml (const juce::XmlElement* xml);

        void resized() override;

    private:
        class ControlView;

        void rebuild();
        void addControl (const juce::String& name, int cc, bool isButton);
        void showAddMenu();
        void showControlMenu (int index, juce::Component& target);
        void setValue (int index, int value, bool send);
        void updateStatus();
        void changed();
        void timerCallback() override;

        std::vector<Control> controls;
        juce::TextButton addButton, learnButton, sendAllButton, clearButton;
        juce::ToggleButton sendOnStartToggle;
        juce::Label statusLabel;
        juce::Viewport viewport;
        juce::Component holder;
        std::vector<std::unique_ptr<ControlView>> views;

        bool learning = false;
        int learnTarget = -1;           // -1: learning creates a new control
        juce::String lastLearned;
    };
}
