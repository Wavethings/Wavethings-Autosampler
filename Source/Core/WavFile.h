#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "Instrument.h"

namespace wt
{
    /** A small, predictable WAV writer/reader.
        We write the files ourselves (instead of using juce::WavAudioFormat) so that the layout is
        always the same: RIFF/WAVE, 'fmt ', optional 'smpl' (root key + loop), 'data'.
        Several sampler formats (EXS24, SF2) need to know exactly where the audio data starts. */
    struct WavFile
    {
        struct WriteOptions
        {
            int bitsPerSample = 24;         // 16, 24 or 32 (float)
            double sampleRate = 48000.0;
            int rootNote = -1;              // writes a 'smpl' chunk if >= 0
            LoopInfo loop;
        };

        static bool write (const juce::File& file, const juce::AudioBuffer<float>& audio, const WriteOptions& options);

        struct Info
        {
            int numChannels = 0;
            int bitsPerSample = 0;
            bool isFloat = false;
            double sampleRate = 0;
            juce::int64 numFrames = 0;
            juce::int64 dataOffset = 0;     // file position of the first audio byte
            juce::int64 dataSize = 0;
        };

        static bool readInfo (const juce::File& file, Info& info);
        static bool read (const juce::File& file, juce::AudioBuffer<float>& audio, Info& info);
    };
}
