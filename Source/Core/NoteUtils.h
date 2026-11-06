#pragma once

#include <juce_core/juce_core.h>

namespace wt
{
    /** Note names. The default convention is "C3 = MIDI 60" (Ableton, Logic, Kontakt, Bitwig). */
    struct NoteUtils
    {
        static juce::String getNoteName (int midiNote, int octaveForMiddleC = 3);
        static juce::String getFileSafeNoteName (int midiNote, int octaveForMiddleC = 3);
        static int clampNote (int note) { return juce::jlimit (0, 127, note); }
    };

    /** A velocity layer: range of velocities and the velocity that is actually played when sampling. */
    struct VelocityLayer
    {
        int low = 1;
        int high = 127;
        int played = 127;
    };

    enum class VelocityPlayPoint
    {
        top = 0,     // play the highest velocity of the layer (like most auto-samplers)
        centre,      // play the middle of the layer
        bottom       // play the lowest velocity of the layer
    };

    /** Splits 1..127 into N layers of (almost) equal size. */
    std::vector<VelocityLayer> computeVelocityLayers (int numLayers, VelocityPlayPoint playPoint);

    /** Root notes from low to high every 'step' semitones. The highest note is always included. */
    std::vector<int> computeRootNotes (int lowNote, int highNote, int stepSemitones);

    /** Key range a root covers: splits halfway between neighbouring roots. */
    struct KeyRange { int low = 0, high = 127; };
    std::vector<KeyRange> computeKeyRanges (const std::vector<int>& roots, int stepSemitones, bool extendToFullKeyboard);
}
