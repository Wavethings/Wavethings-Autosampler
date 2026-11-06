#include "PluginWindow.h"

namespace wt
{
    PluginWindow::PluginWindow (juce::AudioPluginInstance& p, std::function<void()> closeCallback)
        : juce::DocumentWindow (p.getName(), juce::Colour (0xff1a1d22), juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
          plugin (p), onClose (std::move (closeCallback))
    {
        setUsingNativeTitleBar (true);

        juce::AudioProcessorEditor* editor = plugin.hasEditor() ? plugin.createEditorAndMakeActive() : nullptr;
        if (editor == nullptr)
            editor = new juce::GenericAudioProcessorEditor (plugin);

        setContentOwned (editor, true);
        setResizable (editor->isResizable(), false);
        centreWithSize (juce::jmax (200, getWidth()), juce::jmax (100, getHeight()));
        setVisible (true);
        toFront (true);
    }

    PluginWindow::~PluginWindow()
    {
        clearContentComponent();
    }

    void PluginWindow::closeButtonPressed()
    {
        if (onClose)
            onClose();
    }
}
