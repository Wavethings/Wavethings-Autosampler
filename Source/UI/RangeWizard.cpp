#include "RangeWizard.h"
#include "WavethingsLookAndFeel.h"
#include "../Core/NoteUtils.h"
#include "../Core/Text.h"

namespace wt
{
    RangeWizard::RangeWizard (int octaveForMiddleC, std::function<void (int, int)> callback)
        : octave (octaveForMiddleC), onComplete (std::move (callback))
    {
        cancelButton.setButtonText ("Cancelar");
        cancelButton.onClick = [this] { close(); };
        addAndMakeVisible (cancelButton);

        skipButton.setButtonText ("Usar la misma nota");
        skipButton.setTooltip ("Para samplear una sola nota (p. ej. un one-shot)");
        skipButton.onClick = [this]
        {
            if (step == 1 && lowNote >= 0)
            {
                if (onComplete) onComplete (lowNote, lowNote);
                close();
            }
        };
        addChildComponent (skipButton);
        setSize (440, 220);
    }

    void RangeWizard::show (juce::Component* parent, int octaveForMiddleC, std::function<void (int, int)> onComplete,
                            juce::Component::SafePointer<RangeWizard>& handle)
    {
        auto* wizard = new RangeWizard (octaveForMiddleC, std::move (onComplete));
        handle = wizard;

        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned (wizard);
        options.dialogTitle = "Rango de notas";
        options.dialogBackgroundColour = Colours::panel;
        options.componentToCentreAround = parent;
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = true;
        options.resizable = false;
        options.launchAsync();
    }

    void RangeWizard::handleMidi (const juce::MidiMessage& m)
    {
        if (! m.isNoteOn())
            return;

        if (step == 0)
        {
            lowNote = m.getNoteNumber();
            step = 1;
            skipButton.setVisible (true);
            repaint();
        }
        else if (step == 1)
        {
            const int high = m.getNoteNumber();
            if (onComplete)
                onComplete (juce::jmin (lowNote, high), juce::jmax (lowNote, high));
            close();
        }
    }

    void RangeWizard::close()
    {
        step = 2;
        // launchAsync() made the window modal with "delete when dismissed"
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState (0);
    }

    void RangeWizard::paint (juce::Graphics& g)
    {
        g.fillAll (Colours::panel);
        auto b = getLocalBounds().reduced (20).withTrimmedBottom (40);

        g.setColour (Colours::textDim);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (step == 0 ? "Paso 1 de 2"_u8 : "Paso 2 de 2"_u8, b.removeFromTop (20), juce::Justification::centredLeft);

        g.setColour (Colours::text);
        g.setFont (juce::FontOptions (19.0f, juce::Font::bold));
        g.drawFittedText (step == 0 ? "Toca en tu teclado MIDI la nota\nMÁS BAJA que quieres samplear"_u8
                                    : "Ahora toca la nota\nMÁS ALTA"_u8,
                          b.removeFromTop (60), juce::Justification::centredLeft, 2);

        if (lowNote >= 0)
        {
            g.setColour (Colours::accent);
            g.setFont (juce::FontOptions (28.0f, juce::Font::bold));
            g.drawText ("Nota baja: "_u8 + NoteUtils::getNoteName (lowNote, octave), b, juce::Justification::centredLeft);
        }
    }

    void RangeWizard::resized()
    {
        auto b = getLocalBounds().reduced (20).removeFromBottom (30);
        cancelButton.setBounds (b.removeFromRight (100));
        b.removeFromRight (8);
        skipButton.setBounds (b.removeFromRight (150));
    }
}
