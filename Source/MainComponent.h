#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <set>
#include <array>
#include "Core/AudioEngine.h"
#include "Core/SamplingJob.h"
#include "Core/Settings.h"
#include "UI/WavethingsLookAndFeel.h"
#include "UI/LevelMeter.h"
#include "UI/WaveformView.h"
#include "UI/MappedControlsPanel.h"
#include "UI/PluginWindow.h"
#include "UI/RangeWizard.h"

namespace wt
{
    /** Keyboard that shows the sampling range and the notes that will be recorded. */
    class RangeKeyboard : public juce::MidiKeyboardComponent
    {
    public:
        RangeKeyboard (juce::MidiKeyboardState& s) : juce::MidiKeyboardComponent (s, horizontalKeyboard) {}
        /** Keys drawn as "active" and the notes marked as recorded (orange bar). */
        void setHighlight (std::vector<int> activeKeys, std::vector<int> roots);
        void setRecordingNote (int note) { recordingNote = note; repaint(); }

        /** When set, a click on a key calls this (the key still plays). */
        std::function<void (int note)> onKeyClicked;

    protected:
        bool mouseDownOnKey (int note, const juce::MouseEvent& e) override
        {
            if (onKeyClicked != nullptr)
                onKeyClicked (note);
            return juce::MidiKeyboardComponent::mouseDownOnKey (note, e);
        }

        void drawWhiteNote (int note, juce::Graphics&, juce::Rectangle<float> area, bool isDown, bool isOver,
                            juce::Colour lineColour, juce::Colour textColour) override;
        void drawBlackNote (int note, juce::Graphics&, juce::Rectangle<float> area, bool isDown, bool isOver,
                            juce::Colour noteFillColour) override;

    private:
        bool isRoot (int note) const { return std::find (roots.begin(), roots.end(), note) != roots.end(); }
        bool isActive (int note) const { return note >= 0 && note < 128 && active[(size_t) note]; }
        int recordingNote = -1;
        std::vector<int> roots;
        std::array<bool, 128> active {};
    };

    //==============================================================================
    class MainComponent : public juce::Component,
                          private juce::ChangeListener,
                          private juce::Timer
    {
    public:
        explicit MainComponent (juce::ApplicationProperties& properties);
        ~MainComponent() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        bool isSampling() const { return job != nullptr; }
        void saveState();

        // Interface size (100 % = 1.0). The window applies it as a transform.
        float getUiScale() const { return uiScale; }
        void setUiScale (float newScale);
        std::function<void (float)> onScaleChanged;
        bool keyPressed (const juce::KeyPress& key) override;
        static constexpr int baseWidth = 1320, baseHeight = 920;

    private:
        enum class LearnMode { none, chord, low, high };

        // Setup ------------------------------------------------------------------------
        void createPluginSection();
        void createRangeSection();
        void createThresholdSection();
        void createLoopSection();
        void createOutputSection();
        void createTransport();

        // Plug-ins -----------------------------------------------------------------------
        void updatePluginList();
        void loadSelectedPlugin();
        void loadPluginDescription (const juce::PluginDescription& description);
        void pluginLoaded();
        void updateProgramList();
        void showPluginEditor();
        void showPluginScanner();
        void showAudioSettings();
        juce::String mappingKeyForCurrentPlugin() const;
        void saveMappings();

        // MIDI ---------------------------------------------------------------------------
        void handleMidi (const juce::MidiMessage& message);
        void setLearnMode (LearnMode mode);

        // Settings ------------------------------------------------------------------------
        SamplingSettings collectSettings() const;
        void applySettings (const SamplingSettings& s);
        void updateDerivedDisplays();
        void updateLoopControls();

        // Sampling ------------------------------------------------------------------------
        void startOrStopSampling();
        void samplingFinished (bool success, const Instrument& instrument);
        void setControlsEnabled (bool enabled);
        void appendLog (const juce::String& line);
        void chooseOutputFolder();
        void chooseConvertWithMoss();

        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void timerCallback() override;

        // Members ---------------------------------------------------------------------------
        juce::ApplicationProperties& properties;
        WavethingsLookAndFeel lookAndFeel;
        AudioEngine engine;
        juce::Array<juce::PluginDescription> pluginTypes;
        std::unique_ptr<PluginWindow> pluginWindow;
        std::unique_ptr<juce::DocumentWindow> scannerWindow;
        std::unique_ptr<SamplingJob> job;
        juce::Component::SafePointer<RangeWizard> rangeWizard;
        std::unique_ptr<juce::FileChooser> fileChooser;
        juce::TooltipWindow tooltips { this, 600 };

        LearnMode learnMode = LearnMode::none;
        std::set<int> heldNotes;
        double progressValue = 0.0;
        juce::File outputFolder, mossExecutable;
        bool settingsDirty = false;

        // Plug-in section
        Section pluginSection { "Instrumento (VST3 / AU)" };
        juce::ComboBox pluginBox, programBox;
        juce::TextButton scanButton, editorButton, audioButton, unloadButton;
        juce::Label pluginLabel, programLabel;

        // Range section
        Section rangeSection { "Rango y capas" };
        juce::Label lowLabel, highLabel, stepLabel, layersLabel, playPointLabel, layersInfo, summaryLabel, learnStatus;
        juce::Slider lowSlider, highSlider, stepSlider, layersSlider;
        juce::TextButton learnChordButton, learnLowButton, learnHighButton, wizardButton;
        juce::ToggleButton extendToggle;
        juce::ComboBox playPointBox;
        juce::Label holdLabel, releaseLabel, gapLabel;

        // One Shot: individual notes instead of a range
        juce::Label notesModeLabel;
        juce::TextButton rangeModeButton, listModeButton, pickNotesButton, clearNotesButton;
        std::vector<int> noteList;
        bool useNoteList = false;
        bool pickingNotes = false;
        void toggleListNote (int note);
        void updateNoteModeControls();
        juce::Slider holdSlider, releaseSlider, gapSlider;

        // Threshold section
        Section thresholdSection { "Umbral de silencio" };
        LevelMeter meter;
        juce::Label thresholdLabel, silenceLabel;
        juce::Slider thresholdSlider, silenceSlider;

        // Loop section
        Section loopSection { "Loop" };
        juce::ToggleButton oneShotToggle;
        juce::ComboBox loopModeBox;
        juce::Label loopModeLabel, loopStartLabel, loopEndLabel, crossfadeLabel, loopHelp;
        juce::Slider loopStartSlider, loopEndSlider, crossfadeSlider;
        juce::Label crossfadeWhereLabel;
        juce::ComboBox crossfadeWhereBox;
        juce::ToggleButton equaliseToggle;
        WaveformView waveform;

        // Output section
        Section outputSection { "Exportar" };
        juce::Label nameLabel, creatorLabel, categoryLabel, folderLabel, bitDepthLabel, formatsLabel, mossLabel;
        juce::TextEditor nameEditor, creatorEditor, categoryEditor;
        juce::TextButton folderButton, mossButton, openFolderButton;
        juce::ComboBox bitDepthBox;
        juce::ToggleButton stereoToggle, normalizeToggle, trimToggle;
        juce::Viewport formatsViewport;
        juce::Component formatsHolder;
        juce::OwnedArray<juce::ToggleButton> formatToggles;
        juce::StringArray formatIds;

        // Mapped controls
        Section controlsSection { "Controles del plugin (MIDI Learn)" };
        std::unique_ptr<MappedControlsPanel> mappedControls;

        // Keyboard + transport
        RangeKeyboard keyboard { engine.getKeyboardState() };
        juce::TextButton startButton;
        juce::ToggleButton silentToggle, fastToggle;
        juce::ComboBox scaleBox;
        float uiScale = 1.0f;
        juce::ProgressBar progressBar { progressValue };
        juce::Label statusLabel;
        juce::TextEditor logEditor;
        juce::File lastInstrumentFolder;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
    };
}
