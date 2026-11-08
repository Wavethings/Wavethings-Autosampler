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

    juce::StringArray getLoopModeNames();
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
        bool oneShot = false;                // overrides every loop option
        LoopMode loopMode = LoopMode::forward;
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

        bool isLooping() const { return ! oneShot && loopMode != LoopMode::off; }
        bool isUsingNoteList() const { return oneShot && useNoteList; }

        /** The notes that will be recorded and the keys each one covers. */
        std::vector<int> getRootNotes() const;
        std::vector<KeyRange> getKeyRanges() const;

        std::unique_ptr<juce::XmlElement> toXml() const;
        void fromXml (const juce::XmlElement& xml);
    };
}
