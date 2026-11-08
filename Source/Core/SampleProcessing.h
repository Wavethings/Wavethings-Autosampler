#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "Instrument.h"
#include "Settings.h"

namespace wt
{
    /** Result of turning a raw capture into a playable sample. */
    struct ProcessedSample
    {
        juce::AudioBuffer<float> audio;
        LoopInfo loop;
        juce::int64 heldFrames = 0;         // frames between note-on and note-off after trimming
        double releaseSeconds = 0.0;        // measured release tail of the instrument
        float peak = 0.0f;
        bool isSilent = false;
        juce::StringArray warnings;
    };

    /** All the DSP that happens after a note has been recorded:
        - removes the plug-in latency and the leading silence (cut at a zero crossing)
        - finds the end of the tail and applies a short fade-out at a zero crossing
        - finds loop points on rising zero crossings with the best waveform match
        - renders the selected loop mode into the audio so that every sampler plays it the same way
          (forward, backward, ping-pong, crossfades and the Penrose-style resynthesis) */
    namespace SampleProcessing
    {
        ProcessedSample process (const juce::AudioBuffer<float>& raw, int numRecordedFrames, int noteOffFrame,
                                 int latencyFrames, double sampleRate, const SamplingSettings& settings,
                                 juce::uint32 randomSeed = 1, int rootNote = -1);

        float getPeak (const juce::AudioBuffer<float>& buffer, int start = 0, int num = -1);

        /** Index i with mono[i-1] < 0 <= mono[i], searching from 'from' towards 'to' (either direction). -1 if none. */
        int findRisingZeroCrossing (const float* mono, int numFrames, int from, int to);

        /** Mono sum of all channels. */
        std::vector<float> makeMono (const juce::AudioBuffer<float>& buffer, int numFrames = -1);

        /** Best loop [start, end] (end inclusive) between regionStart and regionEnd, both points on rising zero crossings. */
        bool findLoopPoints (const juce::AudioBuffer<float>& audio, int regionStart, int regionEnd, int minLoopLength,
                             int& loopStart, int& loopEndInclusive);

        /** Keeps the loop at the level of its start when the sound decays during the loop. */
        void equaliseLoopLevel (juce::AudioBuffer<float>& audio, int loopStart, int loopEndInclusive, double sampleRate);

        /** Pre-loop crossfade: blends the end of the loop with the audio that precedes the loop start. */
        void applyLoopCrossfade (juce::AudioBuffer<float>& audio, int loopStart, int loopEndInclusive, int crossfadeFrames,
                                 bool linear = true);

        /** Penrose-style loop: the spectrum of a snapshot is resynthesised (new random phases) into a perfectly
            periodic block which is inserted at a zero crossing with a crossfade. 'periodFrames' is the period of the
            fundamental (0 = unknown); with it the snapshot spans a whole number of periods and stays in tune.
            Returns false if the region is too short. */
        bool createPenroseLoop (const juce::AudioBuffer<float>& source, int regionStart, int regionEnd, int crossfadeFrames,
                                juce::uint32 randomSeed, juce::AudioBuffer<float>& result, LoopInfo& loop,
                                double periodFrames = 0.0);
    }
}
