#pragma once

#include <juce_core/juce_core.h>
#include "NoteUtils.h"

namespace wt
{
    enum class LoopMode
    {
        off = 0,
        forward,             // Hacia delante
        backward,            // Hacia atrás
        pingPong,            // Ida y vuelta
        forwardCrossfade,    // Hacia delante + crossfade
        backwardCrossfade,   // Hacia atrás + crossfade
        penrose              // Resíntesis estilo "Penrose Machine"
    };

    /** Loop modes shown in the interface (Forward, Backward, Ping-Pong, Penrose) and their enum values. */
    juce::StringArray getLoopModeNames();
    LoopMode loopModeFromMenuIndex (int index);
    int menuIndexFromLoopMode (LoopMode mode);
    bool loopModeUsesCrossfade (LoopMode mode);

    enum class SampleBitDepth { pcm16 = 16, pcm24 = 24, float32 = 32 };

    /** Everything the user can configure for one sampling run. */
    struct SamplingSettings
    {
        // Range ---------------------------------------------------------------
        int lowNote = 36;              // C1
        int highNote = 96;             // C6
        int stepSemitones = 3;
        bool extendToFullKeyboard = true;

        // One Shot: free choice of notes instead of a range (e.g. C1, D1, E1, G1 for a drum kit).
        // Every chosen note is recorded and mapped only to its own key.
        bool useNoteList = false;
        std::vector<int> noteList;

        // Velocity layers -------------------------------------------------------
        int numVelocityLayers = 1;
        VelocityPlayPoint velocityPlayPoint = VelocityPlayPoint::top;

        // Timing --------------------------------------------------------------
        double noteHoldSeconds = 3.0;        // how long the note is held
        double maxReleaseSeconds = 8.0;      // safety limit for the release tail
        double thresholdDb = -60.0;          // below this the instrument counts as silent
        double silenceHoldMs = 150.0;        // must stay below the threshold this long
        double gapMs = 200.0;                // pause between notes
        double maxWaitForSilenceSeconds = 10.0;

        // Loop ----------------------------------------------------------------
        bool oneShot = false;                // derived: "One Shot" notes mode without loop -> the sample plays to the end
        bool loopEnabled = true;             // Loop on/off
        LoopMode loopMode = LoopMode::forward;   // forward, backward, pingPong or penrose
        bool crossfadeEnabled = true;        // crossfade on/off (Penrose always crossfades)
        bool crossfadeLinear = true;         // linear or curved (equal power), like the two MPC icons
        double loopStartPercent = 30.0;      // loop start / end, percent of the held part (searched around these points)
        double loopEndPercent = 95.0;
        double crossfadeMs = 150.0;
        bool equaliseLoopLevel = true;       // compensate the decay inside the loop
        bool renderCrossfade = true;         // false: the sampler applies the crossfade (MPC, Kontakt, EXS, SFZ...)

        // Audio processing ----------------------------------------------------
        bool trimLeadingSilence = true;
        bool normalize = false;              // one common gain for the whole instrument
        double normalizeTargetDb = -1.0;
        double fadeOutMs = 10.0;
        SampleBitDepth bitDepth = SampleBitDepth::pcm24;
        bool stereo = true;

        // Background sampling ------------------------------------------------
        bool silentMode = false;             // record without sound on the speakers
        bool fastRender = false;             // silent mode only: render faster than real time

        // Naming / metadata ---------------------------------------------------
        juce::String instrumentName { "Wavethings Instrument" };
        juce::String creator;
        juce::String category;
        juce::String description;
        int octaveForMiddleC = 3;           // C3 = 60

        // Output --------------------------------------------------------------
        juce::File outputFolder;
        juce::StringArray exportFormats { "sfz", "decentsampler", "bitwig" };
        juce::File convertWithMossExecutable;

        bool isLooping() const { return ! oneShot && loopEnabled && loopMode != LoopMode::off; }
        bool isUsingNoteList() const { return useNoteList; }
        bool usesCrossfade() const { return isLooping() && (crossfadeEnabled || loopMode == LoopMode::penrose); }

        /** The notes that will be recorded and the keys each one covers. */
        std::vector<int> getRootNotes() const;
        std::vector<KeyRange> getKeyRanges() const;

        std::unique_ptr<juce::XmlElement> toXml() const;
        void fromXml (const juce::XmlElement& xml);

        /** Copies only what a sampling preset contains: notes, layers, times and loop. */
        void copyPresetFieldsFrom (const SamplingSettings& preset);

        /** Rough size of the result, before anything is recorded. */
        struct SizeEstimate
        {
            juce::int64 sampleBytes = 0;    // the WAV files in Samples/
            juce::int64 totalBytes = 0;     // everything, including the copies made by the formats
            double secondsPerSample = 0.0;
        };
        SizeEstimate estimateSize (double sampleRate) const;
    };

    /** Built-in sampling presets ("Piano", "Pad"...) and their settings. */
    struct SamplingPreset
    {
        juce::String name;
        SamplingSettings settings;
    };
    std::vector<SamplingPreset> getBuiltInSamplingPresets();
}
