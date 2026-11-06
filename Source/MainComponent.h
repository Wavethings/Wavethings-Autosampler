#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <set>
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
        void setRange (int low, int high, std::vector<int> roots);
        void setRecordingNote (int note) { recordingNote = note; repaint(); }

    protected:
        void drawWhiteNote (int note, juce::Graphics&, juce::Rectangle<float> area, bool isDown, bool isOver,
                            juce::Colour lineColour, juce::Colour textColour) override;
        void drawBlackNote (int note, juce::Graphics&, juce::Rectangle<float> area, bool isDown, bool isOver,
                            juce::Colour noteFillColour) override;

    private:
        bool isRoot (int note) const { return std::find (roots.begin(), roots.end(), note) != roots.end(); }
        int low = 36, high = 96, recordingNote = -1;
        std::vector<int> roots;
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
        juce::ProgressBar progressBar { progressValue };
        juce::Label statusLabel;
        juce::TextEditor logEditor;
        juce::File lastInstrumentFolder;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
    };
}
