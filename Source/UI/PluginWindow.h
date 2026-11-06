#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace wt
{
    /** Floating window with the plug-in's own editor (or a generic one if it has none). */
    class PluginWindow : public juce::DocumentWindow
    {
    public:
        PluginWindow (juce::AudioPluginInstance& plugin, std::function<void()> onClose);
        ~PluginWindow() override;

        void closeButtonPressed() override;
        juce::AudioPluginInstance& getPlugin() { return plugin; }

    private:
        juce::AudioPluginInstance& plugin;
        std::function<void()> onClose;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginWindow)
    };
}
