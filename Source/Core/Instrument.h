#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace wt
{
    /** Loop points in sample frames. 'end' is the LAST sample that belongs to the loop (inclusive). */
    struct LoopInfo
    {
        bool enabled = false;
        juce::int64 start = 0;
        juce::int64 end = 0;

        juce::int64 getLength() const { return end - start + 1; }
    };

    /** One recorded sample mapped onto a key and velocity range. */
    struct SampleZone
    {
        juce::String fileName;          // file name inside the instrument's sample folder (with .wav)
        int rootNote = 60;
        int lowKey = 60, highKey = 60;
        int lowVelocity = 1, highVelocity = 127;
        int layerIndex = 0;             // velocity layer (0 = softest)
        int playedVelocity = 127;

        juce::int64 numFrames = 0;
        double sampleRate = 48000.0;
        int numChannels = 2;
        int bitsPerSample = 24;
        bool isFloat = false;

        LoopInfo loop;
        bool oneShot = false;
        double gainDb = 0.0;

        juce::String getNameWithoutExtension() const { return fileName.upToLastOccurrenceOf (".", false, false); }
    };

    /** The result of a sampling run: everything an exporter needs. */
    struct Instrument
    {
        juce::String name;
        juce::String creator;
        juce::String category;
        juce::String description;

        juce::File folder;              // <output>/<name>/
        juce::File sampleFolder;        // <output>/<name>/Samples/

        std::vector<SampleZone> zones;
        int numVelocityLayers = 1;

        bool oneShot = false;
        bool looped = false;
        double attackSeconds = 0.0;
        double releaseSeconds = 0.3;    // measured from the release tails of the plug-in
        double sampleRate = 48000.0;
        int bitsPerSample = 24;

        juce::File getSampleFile (const SampleZone& zone) const { return sampleFolder.getChildFile (zone.fileName); }

        juce::int64 getTotalSampleBytes() const
        {
            juce::int64 total = 0;
            for (auto& z : zones)
                total += getSampleFile (z).getSize();
            return total;
        }

        /** Zones grouped by velocity layer, softest layer first. */
        std::vector<std::vector<const SampleZone*>> getZonesByLayer() const
        {
            std::vector<std::vector<const SampleZone*>> layers ((size_t) juce::jmax (1, numVelocityLayers));
            for (auto& z : zones)
            {
                const auto index = (size_t) juce::jlimit (0, (int) layers.size() - 1, z.layerIndex);
                layers[index].push_back (&z);
            }
            return layers;
        }
    };
}
