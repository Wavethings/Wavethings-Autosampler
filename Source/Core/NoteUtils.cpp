#include "NoteUtils.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace wt
{
    juce::String NoteUtils::getNoteName (int midiNote, int octaveForMiddleC)
    {
        return juce::MidiMessage::getMidiNoteName (midiNote, true, true, octaveForMiddleC);
    }

    juce::String NoteUtils::getFileSafeNoteName (int midiNote, int octaveForMiddleC)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const int octave = midiNote / 12 + (octaveForMiddleC - 5);
        return juce::String (names[midiNote % 12]) + juce::String (octave);
    }

    std::vector<VelocityLayer> computeVelocityLayers (int numLayers, VelocityPlayPoint playPoint)
    {
        numLayers = juce::jlimit (1, 127, numLayers);
        std::vector<VelocityLayer> layers;
        layers.reserve ((size_t) numLayers);

        for (int i = 0; i < numLayers; ++i)
        {
            VelocityLayer layer;
            layer.low  = (int) std::floor ((double) i * 127.0 / numLayers) + 1;
            layer.high = (int) std::floor ((double) (i + 1) * 127.0 / numLayers);
            layer.high = juce::jmax (layer.low, layer.high);

            switch (playPoint)
            {
                case VelocityPlayPoint::top:    layer.played = layer.high; break;
                case VelocityPlayPoint::bottom: layer.played = layer.low; break;
                case VelocityPlayPoint::centre: layer.played = (layer.low + layer.high + 1) / 2; break;
            }

            layers.push_back (layer);
        }

        return layers;
    }

    std::vector<int> computeRootNotes (int lowNote, int highNote, int stepSemitones)
    {
        lowNote = NoteUtils::clampNote (lowNote);
        highNote = NoteUtils::clampNote (highNote);
        if (highNote < lowNote)
            std::swap (lowNote, highNote);

        stepSemitones = juce::jmax (1, stepSemitones);

        std::vector<int> roots;
        for (int n = lowNote; n <= highNote; n += stepSemitones)
            roots.push_back (n);

        if (roots.empty() || roots.back() != highNote)
            roots.push_back (highNote);

        return roots;
    }

    std::vector<KeyRange> computeKeyRanges (const std::vector<int>& roots, int stepSemitones, bool extendToFullKeyboard)
    {
        std::vector<KeyRange> ranges (roots.size());
        if (roots.empty())
            return ranges;

        const int halfDown = (stepSemitones - 1) / 2;
        const int halfUp = stepSemitones - 1 - halfDown;

        for (size_t i = 0; i < roots.size(); ++i)
        {
            const int root = roots[i];

            // Lower boundary: right after the upper boundary of the previous zone
            if (i == 0)
                ranges[i].low = extendToFullKeyboard ? 0 : juce::jmax (0, root - halfDown);
            else
                ranges[i].low = ranges[i - 1].high + 1;

            // Upper boundary: halfway to the next root (the upper half belongs to this root)
            if (i + 1 < roots.size())
            {
                const int next = roots[i + 1];
                ranges[i].high = root + (next - root - 1) / 2 + ((next - root - 1) % 2);
                ranges[i].high = juce::jlimit (root, next - 1, ranges[i].high);
            }
            else
            {
                ranges[i].high = extendToFullKeyboard ? 127 : juce::jmin (127, root + halfUp);
            }

            ranges[i].low = juce::jmin (ranges[i].low, root);
            ranges[i].high = juce::jmax (ranges[i].high, root);
        }

        return ranges;
    }
}
